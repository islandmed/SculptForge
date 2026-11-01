// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BakeImage.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

// Declarations only: the implementations are compiled in core/Image.cpp.
#define STBI_NO_STDIO
#include "stb_image.h"
// Compiled with stb_image_write's implementation in core/Image.cpp (not declared by its header).
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace sf {

namespace {

std::filesystem::path fsPath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

bool writeFile(const std::string& path, const std::vector<uint8_t>& bytes, std::string& error) {
    std::ofstream f(fsPath(path), std::ios::binary);
    if (f) f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!f) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

bool readFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& error) {
    std::ifstream f(fsPath(path), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// --- PNG ---

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void putBe32(std::vector<uint8_t>& out, uint32_t v) {
    out.insert(out.end(), {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)});
}

void chunk(std::vector<uint8_t>& out, const char* type, const uint8_t* data, size_t n) {
    putBe32(out, uint32_t(n));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    if (n) out.insert(out.end(), data, data + n);
    putBe32(out, crc32(&out[start], n + 4));
}

// --- EXR ---

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.insert(out.end(), {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)});
}
void putF(std::vector<uint8_t>& out, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(out, v);
}
void attribute(std::vector<uint8_t>& out, const char* name, const char* type, const std::vector<uint8_t>& value) {
    out.insert(out.end(), name, name + std::strlen(name) + 1);
    out.insert(out.end(), type, type + std::strlen(type) + 1);
    put32(out, uint32_t(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

float halfToFloat(uint16_t h) {
    const uint32_t s = uint32_t(h >> 15) << 31, e = (h >> 10) & 0x1F, m = h & 0x3FF;
    uint32_t bits;
    if (e == 0) {
        if (m == 0) {
            bits = s;
        } else { // subnormal
            int ee = -1;
            uint32_t mm = m;
            do {
                ++ee;
                mm <<= 1;
            } while (!(mm & 0x400));
            bits = s | (uint32_t(127 - 15 - ee) << 23) | ((mm & 0x3FF) << 13);
        }
    } else if (e == 31) {
        bits = s | 0x7F800000u | (m << 13);
    } else {
        bits = s | ((e + 127 - 15) << 23) | (m << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// The EXR channel names for `channels` values per pixel, in the file's (alphabetical) order, and where each
// comes from in an interleaved pixel.
std::vector<std::pair<const char*, int>> exrChannels(int channels) {
    if (channels == 1) return {{"Y", 0}};
    return {{"B", 2}, {"G", 1}, {"R", 0}};
}

} // namespace

bool savePng16(const std::string& path, int width, int height, int channels, const std::vector<uint16_t>& px,
               std::string& error) {
    std::vector<uint8_t> out;
    if (!encodePng16(width, height, channels, px, out, error)) return false;
    return writeFile(path, out, error);
}

bool decodePng16(const uint8_t* bytes, size_t size, int& width, int& height, int& channels, std::vector<uint16_t>& pixels,
                 std::string& error, int wantChannels) {
    int w = 0, h = 0, n = 0;
    stbi_us* data = size > 0 && size < (size_t(1) << 31) ? stbi_load_16_from_memory(bytes, int(size), &w, &h, &n, wantChannels) : nullptr;
    if (!data) {
        error = "Cannot decode the 16-bit PNG";
        return false;
    }
    width = w, height = h, channels = wantChannels ? wantChannels : n;
    pixels.assign(data, data + size_t(w) * size_t(h) * size_t(channels));
    stbi_image_free(data);
    return true;
}

bool encodePng16(int width, int height, int channels, const std::vector<uint16_t>& px, std::vector<uint8_t>& out,
                 std::string& error) {
    if (width <= 0 || height <= 0 || channels < 1 || channels > 4 ||
        px.size() != size_t(width) * size_t(height) * size_t(channels)) {
        error = "Bad 16-bit image";
        return false;
    }
    // Rows with the "Up" filter (good for smooth maps), samples big-endian.
    const size_t rowBytes = size_t(width) * size_t(channels) * 2;
    std::vector<uint8_t> raw((rowBytes + 1) * size_t(height));
    std::vector<uint8_t> prev(rowBytes, 0), cur(rowBytes);
    for (int y = 0; y < height; ++y) {
        const uint16_t* src = &px[size_t(y) * size_t(width) * size_t(channels)];
        for (size_t i = 0; i < size_t(width) * size_t(channels); ++i) {
            cur[i * 2] = uint8_t(src[i] >> 8);
            cur[i * 2 + 1] = uint8_t(src[i]);
        }
        uint8_t* dst = &raw[size_t(y) * (rowBytes + 1)];
        dst[0] = 2; // Up
        for (size_t i = 0; i < rowBytes; ++i) dst[i + 1] = uint8_t(cur[i] - prev[i]);
        prev.swap(cur);
    }
    int zlen = 0;
    unsigned char* z = stbi_zlib_compress(raw.data(), int(raw.size()), &zlen, 6);
    if (!z) {
        error = "PNG compression failed";
        return false;
    }
    out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    putBe32(ihdr, uint32_t(width));
    putBe32(ihdr, uint32_t(height));
    static const uint8_t kColorType[5] = {0, 0, 4, 2, 6}; // grey, grey + alpha, RGB, RGBA
    ihdr.insert(ihdr.end(), {16, kColorType[channels], 0, 0, 0});
    chunk(out, "IHDR", ihdr.data(), ihdr.size());
    chunk(out, "IDAT", z, size_t(zlen));
    std::free(z);
    chunk(out, "IEND", nullptr, 0);
    return true;
}

bool loadPng16(const std::string& path, int& width, int& height, int& channels, std::vector<uint16_t>& pixels,
               std::string& error) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes, error)) return false;
    int w = 0, h = 0, n = 0;
    stbi_us* data = stbi_load_16_from_memory(bytes.data(), int(bytes.size()), &w, &h, &n, 0);
    if (!data) {
        error = "Cannot decode " + path;
        return false;
    }
    width = w, height = h, channels = n;
    pixels.assign(data, data + size_t(w) * size_t(h) * size_t(n));
    stbi_image_free(data);
    return true;
}

bool saveExr(const std::string& path, int width, int height, int channels, const std::vector<float>& px, std::string& error) {
    if (width <= 0 || height <= 0 || (channels != 1 && channels != 3) ||
        px.size() != size_t(width) * size_t(height) * size_t(channels)) {
        error = "Bad float image";
        return false;
    }
    const auto chans = exrChannels(channels);
    std::vector<uint8_t> out;
    put32(out, 20000630); // magic
    put32(out, 2);        // version 2, single-part scanline
    std::vector<uint8_t> v;
    for (const auto& [name, src] : chans) {
        v.insert(v.end(), name, name + std::strlen(name) + 1);
        put32(v, 2);                        // FLOAT
        v.insert(v.end(), {0, 0, 0, 0});    // pLinear + reserved
        put32(v, 1);                        // x sampling
        put32(v, 1);                        // y sampling
    }
    v.push_back(0);
    attribute(out, "channels", "chlist", v);
    attribute(out, "compression", "compression", {0});
    v.clear();
    for (int i : {0, 0, width - 1, height - 1}) put32(v, uint32_t(i));
    attribute(out, "dataWindow", "box2i", v);
    attribute(out, "displayWindow", "box2i", v);
    attribute(out, "lineOrder", "lineOrder", {0});
    v.clear();
    putF(v, 1.0f);
    attribute(out, "pixelAspectRatio", "float", v);
    v.clear();
    putF(v, 0.0f);
    putF(v, 0.0f);
    attribute(out, "screenWindowCenter", "v2f", v);
    v.clear();
    putF(v, 1.0f);
    attribute(out, "screenWindowWidth", "float", v);
    out.push_back(0); // end of header
    const size_t lineBytes = size_t(width) * size_t(channels) * 4;
    const size_t table = out.size(), first = table + size_t(height) * 8;
    out.resize(first + size_t(height) * (8 + lineBytes));
    for (int y = 0; y < height; ++y) {
        const uint64_t off = first + size_t(y) * (8 + lineBytes);
        for (int b = 0; b < 8; ++b) out[table + size_t(y) * 8 + size_t(b)] = uint8_t(off >> (8 * b));
        uint8_t* p = &out[off];
        const uint32_t hdr[2] = {uint32_t(y), uint32_t(lineBytes)};
        for (int k = 0; k < 2; ++k)
            for (int b = 0; b < 4; ++b) *p++ = uint8_t(hdr[k] >> (8 * b));
        for (const auto& [name, src] : chans)
            for (int x = 0; x < width; ++x) {
                const float f = px[(size_t(y) * size_t(width) + size_t(x)) * size_t(channels) + size_t(src)];
                std::memcpy(p, &f, 4); // EXR is little-endian, as every platform we build for
                p += 4;
            }
    }
    return writeFile(path, out, error);
}

bool loadExr(const std::string& path, int& width, int& height, int& channels, std::vector<float>& pixels,
             std::string& error) {
    std::vector<uint8_t> b;
    if (!readFile(path, b, error)) return false;
    size_t pos = 0;
    auto bad = [&](const char* why) {
        error = std::string("Not a supported EXR (") + why + "): " + path;
        return false;
    };
    auto u32 = [&](size_t at) {
        return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
    };
    if (b.size() < 8 || u32(0) != 20000630) return bad("magic");
    if ((u32(4) & 0xFF) != 2 || (u32(4) & 0x1E00)) return bad("only single-part scanline files");
    pos = 8;
    struct Chan {
        std::string name;
        int type;
    };
    std::vector<Chan> chans;
    int compression = -1, x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    for (;;) {
        if (pos >= b.size()) return bad("truncated header");
        if (b[pos] == 0) {
            ++pos;
            break;
        }
        auto str = [&]() {
            std::string s;
            while (pos < b.size() && b[pos]) s += char(b[pos++]);
            ++pos;
            return s;
        };
        const std::string name = str(), type = str();
        if (pos + 4 > b.size()) return bad("truncated header");
        const uint32_t size = u32(pos);
        pos += 4;
        if (pos + size > b.size()) return bad("truncated header");
        const size_t end = pos + size;
        if (name == "channels") {
            while (pos < end && b[pos]) {
                Chan c;
                c.name = str();
                if (pos + 16 > end) return bad("channel list");
                c.type = int(u32(pos));
                chans.push_back(c);
                pos += 16;
            }
        } else if (name == "compression") {
            compression = b[pos];
        } else if (name == "dataWindow" && size == 16) {
            x0 = int(u32(pos)), y0 = int(u32(pos + 4)), x1 = int(u32(pos + 8)), y1 = int(u32(pos + 12));
        }
        pos = end;
    }
    if (compression != 0) return bad("compressed");
    width = x1 - x0 + 1, height = y1 - y0 + 1;
    if (width <= 0 || height <= 0 || chans.empty() || width > 65536 || height > 65536) return bad("size");
    // Map file channels to output channels: Y alone -> 1, else R, G, B -> 3.
    std::map<std::string, int> slot = {{"Y", 0}, {"R", 0}, {"G", 1}, {"B", 2}};
    const bool rgb = chans.size() >= 3;
    channels = rgb ? 3 : 1;
    size_t pixelBytes = 0;
    for (const Chan& c : chans) {
        if (c.type != 1 && c.type != 2) return bad("unsupported pixel type");
        pixelBytes += c.type == 1 ? 2 : 4;
    }
    pixels.assign(size_t(width) * size_t(height) * size_t(channels), 0.0f);
    const size_t table = pos;
    if (table + size_t(height) * 8 > b.size()) return bad("offset table");
    for (int line = 0; line < height; ++line) {
        uint64_t off = 0;
        for (int k = 0; k < 8; ++k) off |= uint64_t(b[table + size_t(line) * 8 + size_t(k)]) << (8 * k);
        if (off + 8 + pixelBytes * size_t(width) > b.size()) return bad("scanline");
        const int y = int(u32(size_t(off))) - y0;
        if (y < 0 || y >= height) return bad("scanline y");
        size_t p = size_t(off) + 8;
        for (const Chan& c : chans) {
            const auto it = slot.find(c.name);
            const int dst = it == slot.end() || (!rgb && c.name != "Y" && chans.size() > 1) ? -1 : it->second;
            for (int x = 0; x < width; ++x) {
                float f;
                if (c.type == 2) {
                    std::memcpy(&f, &b[p], 4);
                    p += 4;
                } else {
                    f = halfToFloat(uint16_t(b[p] | b[p + 1] << 8));
                    p += 2;
                }
                if (dst >= 0 && dst < channels) pixels[(size_t(y) * size_t(width) + size_t(x)) * size_t(channels) + size_t(dst)] = f;
            }
        }
    }
    return true;
}

} // namespace sf
