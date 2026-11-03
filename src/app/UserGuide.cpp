// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/UserGuide.h"

#include "app/App.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "core/Image.h"
#include "render/GL.h"

#include <IconsLucide.h>
#include <SDL3/SDL.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>

namespace sf {

namespace assets {
extern const unsigned char fontSemiBold[];
extern const unsigned long long fontSemiBold_size;
extern const unsigned char fontRegular[];
extern const unsigned long long fontRegular_size;
} // namespace assets

namespace guide_data {
struct File {
    const char* path;
    const unsigned char* data;
    unsigned long long size;
};
extern const File kFiles[];
extern const int kFileCount;
} // namespace guide_data

namespace {

using guide::Block;
using guide::BlockKind;
using guide::Span;
using guide::Spans;

constexpr const char* kWebsite = "https://sculptforge.app/guide/";

// Colours of the page (on the panel background).
constexpr ImU32 kText = IM_COL32(0xd2, 0xd2, 0xd4, 0xff);
constexpr ImU32 kStrong = IM_COL32(0xf0, 0xf0, 0xf0, 0xff);
constexpr ImU32 kHeading = IM_COL32(0xf4, 0xf4, 0xf4, 0xff);
constexpr ImU32 kDim = IM_COL32(0x8c, 0x8c, 0x90, 0xff);
constexpr ImU32 kLink = IM_COL32(0xff, 0xa2, 0x55, 0xff);
constexpr ImU32 kCode = IM_COL32(0xe9, 0xc0, 0x8f, 0xff);
constexpr ImU32 kCodeBg = IM_COL32(0x1b, 0x1b, 0x1e, 0xff);
constexpr ImU32 kInlineCodeBg = IM_COL32(0x33, 0x33, 0x38, 0xff);
constexpr ImU32 kRule = IM_COL32(0x3a, 0x3a, 0x3f, 0xff);
constexpr ImU32 kHighlight = IM_COL32(0xf0, 0x88, 0x2e, 0x5a);

// --- Where the guide's files come from: embedded, or SCULPTFORGE_GUIDE_DIR while writing it ---

std::filesystem::path fsPath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

std::string guideDir() {
    const char* dir = std::getenv("SCULPTFORGE_GUIDE_DIR");
    return dir && *dir ? std::string(dir) : std::string();
}

bool readGuideFile(const std::string& name, std::string& out) {
    if (name.find("..") != std::string::npos) return false;
    if (const std::string dir = guideDir(); !dir.empty()) {
        std::ifstream f(fsPath(dir) / fsPath(name), std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    }
    for (int i = 0; i < guide_data::kFileCount; ++i)
        if (name == guide_data::kFiles[i].path) {
            out.assign(reinterpret_cast<const char*>(guide_data::kFiles[i].data), size_t(guide_data::kFiles[i].size));
            return true;
        }
    return false;
}

bool loadPage(const std::string& file, std::string& text, void*) { return readGuideFile(file, text); }

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

float textWidth(ImFont* f, const std::string& s) { return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, s.c_str()).x; }

// Draws TEXT and slants it (Inter has no italic in the atlas: shear the glyph quads instead).
void drawText(ImDrawList* dl, ImFont* f, ImVec2 p, ImU32 col, const std::string& s, bool italic) {
    const int start = dl->VtxBuffer.Size;
    dl->AddText(f, f->FontSize, p, col, s.c_str());
    if (!italic) return;
    const float base = p.y + f->FontSize * 0.8f;
    for (int i = start; i < dl->VtxBuffer.Size; ++i) dl->VtxBuffer[i].pos.x += (base - dl->VtxBuffer[i].pos.y) * 0.2f;
}

// ---------------------------------------------------------------------------
// Rich text: spans laid out in lines, wrapped at word boundaries
// ---------------------------------------------------------------------------

struct FlowStyle {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    ImFont* mono = nullptr;
    ImFont* heading = nullptr; // headings: every non-code word in this font
    ImU32 color = kText;
    ImU32 labelColor = 0;      // callouts: colour of the leading bold label
    float lineHeight = 20.0f;
};

struct Piece {
    const Span* span = nullptr;
    std::string text;
    ImFont* font = nullptr;
    float width = 0.0f;
    std::string linkText; // the whole link's text (button label for UI tests)
};

struct Word {
    std::vector<Piece> pieces;
    float width = 0.0f;
    bool spaceBefore = false;
};

struct LinkClick {
    std::string target;
};

class Flow {
public:
    Flow(const FlowStyle& st, float scale) : st_(st), s_(scale) {}

    // Lays SPANS out from ORIGIN in WIDTH; draws when DL is set. Returns the height.
    float run(const Spans& spans, ImVec2 origin, float width, ImDrawList* dl, const std::vector<std::string>* highlight,
              LinkClick* click, int& linkCounter) {
        build(spans);
        const float space = textWidth(st_.heading ? st_.heading : st_.regular, " ");
        float x = 0.0f, y = 0.0f;
        bool any = false;
        for (const Word& w : words_) {
            float sw = w.spaceBefore && x > 0.0f ? space : 0.0f;
            if (x > 0.0f && x + sw + w.width > width) {
                x = 0.0f;
                y += st_.lineHeight;
                sw = 0.0f;
            }
            x += sw;
            any = true;
            if (dl) {
                float px = origin.x + x;
                for (const Piece& p : w.pieces) {
                    drawPiece(dl, p, ImVec2(px, origin.y + y), highlight, click, linkCounter);
                    px += p.width;
                }
            }
            x += w.width;
        }
        return any ? y + st_.lineHeight : 0.0f;
    }

private:
    ImFont* fontFor(const Span& sp) const {
        if (sp.code) return st_.heading ? st_.heading : st_.regular; // inline code: the text font on a chip
        if (st_.heading) return st_.heading;
        return sp.bold ? st_.bold : st_.regular;
    }

    float pieceWidth(const Span& sp, const std::string& text, ImFont* f) const {
        if (sp.kbd) return textWidth(st_.regular, text) + 10.0f * s_ + 4.0f * s_;
        const float w = textWidth(f, text);
        return sp.code ? w + 6.0f * s_ : w;
    }

    void build(const Spans& spans) {
        words_.clear();
        // The text of each link (consecutive spans with the same target).
        std::vector<std::string> linkText(spans.size());
        for (size_t i = 0; i < spans.size();) {
            size_t j = i;
            std::string text;
            while (j < spans.size() && !spans[i].link.empty() && spans[j].link == spans[i].link) text += spans[j++].text;
            if (j == i) {
                ++i;
                continue;
            }
            for (size_t k = i; k < j; ++k) linkText[k] = text;
            i = j;
        }
        Word cur;
        bool pendingSpace = false;
        auto endWord = [&] {
            if (!cur.pieces.empty()) words_.push_back(std::move(cur));
            cur = Word{};
        };
        for (size_t si = 0; si < spans.size(); ++si) {
            const Span& sp = spans[si];
            ImFont* f = fontFor(sp);
            auto addPiece = [&](std::string text) {
                if (text.empty()) return;
                if (cur.pieces.empty()) cur.spaceBefore = pendingSpace;
                pendingSpace = false;
                Piece p;
                p.span = &sp;
                p.font = f;
                p.width = pieceWidth(sp, text, f);
                p.text = std::move(text);
                p.linkText = linkText[si];
                cur.width += p.width;
                cur.pieces.push_back(std::move(p));
            };
            if (sp.kbd) {
                addPiece(sp.text);
                continue;
            }
            std::string run;
            for (char c : sp.text) {
                if (c == ' ' || c == '\n' || c == '\t') {
                    addPiece(std::move(run));
                    run.clear();
                    endWord();
                    pendingSpace = true;
                } else {
                    run += c;
                }
            }
            addPiece(std::move(run));
        }
        endWord();
    }

    void drawPiece(ImDrawList* dl, const Piece& p, ImVec2 at, const std::vector<std::string>* highlight, LinkClick* click,
                   int& linkCounter) {
        const Span& sp = *p.span;
        const float lh = st_.lineHeight;
        ImFont* f = p.font;
        const ImVec2 r0(at.x, at.y), r1(at.x + p.width, at.y + lh);
        bool hovered = false;
        if (!sp.link.empty() && click) {
            ImGui::SetCursorScreenPos(r0);
            const std::string label = (p.linkText.empty() ? p.text : p.linkText) + "##guidelink" + std::to_string(linkCounter++);
            if (ImGui::InvisibleButton(label.c_str(), ImVec2(std::max(1.0f, p.width), lh))) click->target = sp.link;
            hovered = ImGui::IsItemHovered();
            if (hovered) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                const guide::Target t = guide::splitTarget(sp.link);
                if (t.external) ImGui::SetTooltip("%s", sp.link.c_str());
            }
        }
        if (sp.kbd) {
            ImFont* kf = st_.regular;
            const float h = kf->FontSize + 4.0f * s_;
            const ImVec2 a(at.x + 2.0f * s_, at.y + (lh - h) * 0.5f), b(at.x + p.width - 2.0f * s_, a.y + h);
            dl->AddRectFilled(a, b, IM_COL32(0x2c, 0x2c, 0x30, 0xff), 3.0f * s_);
            dl->AddRect(a, b, IM_COL32(0x4a, 0x4a, 0x50, 0xff), 3.0f * s_);
            dl->AddText(kf, kf->FontSize, ImVec2(a.x + 5.0f * s_, a.y + 2.0f * s_), ui::color::accent, p.text.c_str());
            return;
        }
        float tx = at.x;
        const float ty = at.y + (lh - f->FontSize) * 0.5f;
        if (sp.code) {
            dl->AddRectFilled(ImVec2(at.x, ty - 1.0f * s_), ImVec2(at.x + p.width, ty + f->FontSize + 1.0f * s_), kInlineCodeBg,
                              3.0f * s_);
            tx += 3.0f * s_;
        }
        if (highlight && !highlight->empty()) {
            const std::string low = lower(p.text);
            for (const auto& w : *highlight)
                for (size_t at2 = low.find(w); at2 != std::string::npos; at2 = low.find(w, at2 + w.size())) {
                    const float x0 = tx + textWidth(f, p.text.substr(0, at2));
                    const float x1 = tx + textWidth(f, p.text.substr(0, at2 + w.size()));
                    dl->AddRectFilled(ImVec2(x0 - 1.0f * s_, ty - 1.0f * s_), ImVec2(x1 + 1.0f * s_, ty + f->FontSize + 1.0f * s_),
                                      kHighlight, 2.0f * s_);
                }
        }
        ImU32 col = st_.color;
        if (sp.code) col = kCode;
        else if (!sp.link.empty()) col = kLink;
        else if (sp.bold && st_.labelColor && &sp == firstSpan_) col = st_.labelColor;
        else if (sp.bold && !st_.heading) col = kStrong;
        drawText(dl, f, ImVec2(tx, ty), col, p.text, sp.italic);
        if (hovered) dl->AddLine(ImVec2(r0.x, ty + f->FontSize), ImVec2(r1.x, ty + f->FontSize), kLink, 1.0f * s_);
    }

public:
    const Span* firstSpan_ = nullptr; // callouts colour their label (the first bold span)

private:
    FlowStyle st_;
    float s_;
    std::vector<Word> words_;
};

struct CalloutLook {
    const char* label;
    ImU32 color;
};
constexpr CalloutLook kCallouts[] = {
    {"Tip", IM_COL32(0x62, 0xc0, 0x7e, 0xff)},     {"Note", IM_COL32(0x6f, 0xa8, 0xe6, 0xff)},
    {"ZBrush", IM_COL32(0xf0, 0x88, 0x2e, 0xff)},  {"Not yet", IM_COL32(0xa9, 0x90, 0xdc, 0xff)},
    {"Warning", IM_COL32(0xe5, 0x60, 0x4a, 0xff)}, {"Important", IM_COL32(0xe5, 0x60, 0x4a, 0xff)},
};

ImU32 calloutColor(const Block& b) {
    if (b.spans.empty() || !b.spans[0].bold) return kRule;
    std::string label = b.spans[0].text;
    while (!label.empty() && (label.back() == ':' || label.back() == ' ')) label.pop_back();
    for (const auto& c : kCallouts)
        if (label == c.label) return c.color;
    return IM_COL32(0x8c, 0x8c, 0x90, 0xff);
}

} // namespace

// ---------------------------------------------------------------------------
// Set-up and navigation
// ---------------------------------------------------------------------------

void UserGuide::addFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    auto add = [&](float size, bool bold = true) {
        return bold ? io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontSemiBold),
                                                     int(assets::fontSemiBold_size), size * scale, &cfg)
                    : io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontRegular),
                                                     int(assets::fontRegular_size), size * scale, &cfg);
    };
    // Reading text a step above the 15 px UI font (the guide, What's New, the update banner).
    body_ = add(kReadingSize, false);
    bodyBold_ = add(kReadingSize);
    h1_ = add(30.0f);
    h2_ = add(23.0f);
    h3_ = add(19.0f);
}

bool UserGuide::ensureLoaded() {
    if (loaded_) return loadError_.empty();
    loaded_ = true;
    if (!book_.load(loadPage, nullptr, loadError_) && loadError_.empty()) loadError_ = "The user guide could not be loaded.";
    prepareWhatsNew();
    return loadError_.empty();
}

void UserGuide::open(const std::string& target) {
    open_ = true;
    if (!target.empty()) navigate(target);
}

void UserGuide::toggle() { open_ = !open_; }

std::string UserGuide::currentFile() const {
    if (place_.chapter < 0 || place_.chapter >= int(book_.chapters().size())) return "index.md";
    return book_.chapters()[size_t(place_.chapter)].file;
}

std::string UserGuide::chapterTitle(const std::string& file) {
    if (!ensureLoaded()) return {};
    const int i = book_.find(file);
    return i >= 0 ? book_.chapters()[size_t(i)].title : std::string();
}

const guide::Document* UserGuide::document(const std::string& file) {
    if (!ensureLoaded()) return nullptr;
    const int i = book_.find(file);
    return i >= 0 ? &book_.chapters()[size_t(i)].doc : nullptr;
}

ImTextureID UserGuide::image(const std::string& path, ImVec2* size) {
    const Texture* t = texture(path);
    if (!t) return ImTextureID(0);
    if (size) *size = ImVec2(float(t->width), float(t->height));
    return ImTextureID(uintptr_t(t->tex));
}

const guide::Document& UserGuide::page() const {
    if (place_.chapter >= 0 && place_.chapter < int(book_.chapters().size())) return book_.chapters()[size_t(place_.chapter)].doc;
    return book_.index();
}

void UserGuide::go(Place place, const std::string& anchor, bool record) {
    if (record) {
        back_.push_back({place_.chapter, scroll_});
        if (back_.size() > 100) back_.erase(back_.begin());
        forward_.clear();
    }
    const bool samePage = place.chapter == place_.chapter;
    place_ = place;
    pendingAnchor_ = anchor;
    pendingBlock_ = -1;
    pendingScroll_ = -1.0f;
    pendingTop_ = anchor.empty() && !samePage;
    if (!samePage) highlight_.clear();
}

void UserGuide::navigate(const std::string& target, bool record) {
    if (!ensureLoaded()) return;
    const guide::Target t = guide::splitTarget(target);
    if (t.external) {
        SDL_OpenURL(t.file.c_str());
        return;
    }
    if (t.file.empty()) { // an anchor on this page
        go(place_, t.anchor, record);
        return;
    }
    std::string file = t.file;
    if (const size_t slash = file.rfind('/'); slash != std::string::npos) file = file.substr(slash + 1);
    // Chapters renamed to SculptForge's own names: old links still find them.
    static const std::pair<const char*, const char*> kRenamed[] = {{"subtools.md", "parts.md"},
                                                                   {"zspheres.md", "forge-spheres.md"},
                                                                   {"polypaint.md", "vertex-paint.md"},
                                                                   {"polygroups.md", "face-groups.md"}};
    for (const auto& [from, to] : kRenamed)
        if (file == from) file = to;
    if (file == "index.md") {
        go({-1, 0.0f}, t.anchor, record);
        return;
    }
    const int chapter = book_.find(file);
    if (chapter < 0) return;
    go({chapter, 0.0f}, t.anchor, record);
}

std::string UserGuide::websiteUrl() const {
    std::string file = currentFile();
    file = file.substr(0, file.size() - 3) + ".html";
    return std::string(kWebsite) + file;
}

std::string UserGuide::chapterFor(const std::string& palette, const std::string& actionId) {
    auto starts = [&](const char* p) { return palette.rfind(p, 0) == 0; };
    auto idStarts = [&](const char* p) { return actionId.rfind(p, 0) == 0; };
    // Palette paths by SculptForge's names, and the older ZBrush-style ones (stored layouts, scripts).
    if (starts("Tool > Geometry > Quad Remesh") || starts("Tool > Geometry > QRemesh") || starts("Tool > Parts > Project") ||
        starts("Tool > SubTool > Project"))
        return "retopology.md";
    if (starts("Tool > Parts") || starts("Tool > SubTool")) return "parts.md";
    if (starts("Tool > Forge Spheres") || starts("Tool > ZSpheres")) return "forge-spheres.md";
    if (starts("Tool > Layers")) return "layers.md";
    if (starts("Tool > Facial Shapes")) return "facial-shapes.md";
    if (starts("Tool > Geometry") || starts("Tool > Remesh")) return "geometry.md";
    if (starts("Tool > Masking")) return "masking.md";
    if (starts("Tool > Face Groups") || starts("Tool > Polygroups") || starts("Tool > Visibility")) return "face-groups.md";
    if (starts("Tool > Selection Sets")) return "selection-sets.md";
    if (starts("Tool > Export")) return "exporting.md";
    if (starts("Tool > Display")) return "interface.md";
    if (starts("Tool")) return "parts.md";
    if (starts("Brush"))
        return actionId == "ui.brush_lab" || actionId == "ui.brush_gallery" || actionId == "brush.paste" ? "brush-lab.md"
                                                                                                         : "sculpting.md";
    if (starts("Draw") || starts("Stroke") || starts("Alpha")) return "sculpting.md";
    if (starts("Color") || starts("Material")) return "vertex-paint.md";
    if (starts("File > Blender")) return "blender.md";
    if (starts("File > Maya")) return "maya.md";
    if (starts("File > Capture from phone") || actionId == "file.capture_phone") return "phone-capture.md";
    if (starts("File > Export") || idStarts("file.export") || actionId == "file.send_unreal" || actionId == "file.save_splats" ||
        starts("File > Send to ZBrush") || starts("File > GoZ"))
        return "exporting.md";
    if (starts("File")) return "projects.md";
    if (starts("Generate")) return "generate-3d.md";
    if (starts("Splats > Make sculptable")) return "scan-to-sculptable.md";
    if (starts("Splats")) return "splats.md";
    if (starts("Script")) return "scripting.md";
    if (starts("Library > Packs") || starts("Macros") || idStarts("pack.") || idStarts("macro.")) return "packs-and-macros.md";
    if (starts("Transform > Gizmo") || idStarts("gizmo.") || actionId == "tool.draw" || actionId == "tool.move" ||
        actionId == "tool.scale" || actionId == "tool.rotate")
        return "gizmo.md";
    if (starts("Transform")) return idStarts("transform.symmetry") ? "sculpting.md" : "navigation.md";
    if (starts("Render") || starts("Edit") || starts("Help") || starts("Palettes") || starts("Preferences") ||
        starts("Library") || starts("LightBox"))
        return "interface.md";
    return {};
}

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------

const UserGuide::Texture* UserGuide::texture(const std::string& path) {
    auto it = textures_.find(path);
    if (it != textures_.end()) return it->second.failed ? nullptr : &it->second;
    Texture& t = textures_[path];
    std::string bytes;
    Image image;
    std::string err;
    if (!readGuideFile(path, bytes) ||
        !Image::decode(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), image, err)) {
        t.failed = true;
        return nullptr;
    }
    gl::GenTextures(1, &t.tex);
    gl::BindTexture(GL_TEXTURE_2D, t.tex);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   image.rgba.data());
    gl::BindTexture(GL_TEXTURE_2D, 0);
    t.width = image.width;
    t.height = image.height;
    return &t;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void UserGuide::draw(App& app) {
    if (!open_) return;
    const float s = app.uiScale_;
    const CanvasRect c = app.canvasRect();
    const ImVec2 size(std::min(1180.0f * s, c.w * 0.94f), std::min(800.0f * s, c.h * 0.94f));
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f * s, 320.0f * s), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s, 8.0f * s));
    const bool visible = ImGui::Begin("User Guide", &open_, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }
    if (!ensureLoaded()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::danger), "%s", loadError_.c_str());
        ImGui::End();
        return;
    }
    // Keys while the guide has focus: Alt+Left / Alt+Right, Ctrl+F; mouse back / forward buttons.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && !back_.empty()) {
            forward_.push_back({place_.chapter, scroll_});
            const Place p = back_.back();
            back_.pop_back();
            go(p, "", false);
            pendingScroll_ = p.scroll;
            pendingTop_ = false;
        } else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && !forward_.empty()) {
            back_.push_back({place_.chapter, scroll_});
            const Place p = forward_.back();
            forward_.pop_back();
            go(p, "", false);
            pendingScroll_ = p.scroll;
            pendingTop_ = false;
        }
        if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_F, false)) focusSearch_ = true;
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        if (ImGui::IsMouseClicked(3) && !back_.empty()) {
            forward_.push_back({place_.chapter, scroll_});
            const Place p = back_.back();
            back_.pop_back();
            go(p, "", false);
            pendingScroll_ = p.scroll;
            pendingTop_ = false;
        } else if (ImGui::IsMouseClicked(4) && !forward_.empty()) {
            back_.push_back({place_.chapter, scroll_});
            const Place p = forward_.back();
            forward_.pop_back();
            go(p, "", false);
            pendingScroll_ = p.scroll;
            pendingTop_ = false;
        }
    }

    drawToolbar(app);
    const float sideW = 262.0f * s;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::shellBg);
    ImGui::BeginChild("##guide_side", ImVec2(sideW, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    if (query_[0]) drawSearchResults(app);
    else drawContents(app);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 1.0f * s);
    drawPage(app);
    ImGui::End();
}

void UserGuide::drawToolbar(App& app) {
    const float s = app.uiScale_;
    const ImVec2 btn(ImGui::GetFrameHeight() * 1.2f, 0);
    ImGui::BeginDisabled(back_.empty());
    if (ImGui::Button(ICON_LC_ARROW_LEFT "##guide_back", btn)) {
        forward_.push_back({place_.chapter, scroll_});
        const Place p = back_.back();
        back_.pop_back();
        go(p, "", false);
        pendingScroll_ = p.scroll;
        pendingTop_ = false;
    }
    ImGui::EndDisabled();
    ui::HelpText("Back", "Alt+Left", "Go back to the page you were reading.", "Help > User Guide");
    ui::DelayedTooltip("Back (Alt+Left)");
    ImGui::SameLine();
    ImGui::BeginDisabled(forward_.empty());
    if (ImGui::Button(ICON_LC_ARROW_RIGHT "##guide_forward", btn)) {
        back_.push_back({place_.chapter, scroll_});
        const Place p = forward_.back();
        forward_.pop_back();
        go(p, "", false);
        pendingScroll_ = p.scroll;
        pendingTop_ = false;
    }
    ImGui::EndDisabled();
    ui::HelpText("Forward", "Alt+Right", "Go forward again.", "Help > User Guide");
    ui::DelayedTooltip("Forward (Alt+Right)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_LC_HOUSE "##guide_home", btn)) {
        query_[0] = '\0';
        navigate("index.md");
    }
    ui::HelpText("Contents", nullptr, "The guide's first page: every chapter.", "Help > User Guide");
    ui::DelayedTooltip("Contents");
    ImGui::SameLine();
    const float webW = ImGui::CalcTextSize(ICON_LC_GLOBE "  Open in browser").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::min(340.0f * s, ImGui::GetContentRegionAvail().x - webW - 12.0f * s));
    if (focusSearch_) {
        ImGui::SetKeyboardFocusHere();
        focusSearch_ = false;
    }
    ImGui::InputTextWithHint("##guide_search", ICON_LC_SEARCH "  Search the guide (Ctrl+F)", query_, sizeof query_,
                             ImGuiInputTextFlags_EscapeClearsAll);
    ui::HelpText("Search", "Ctrl+F", "Find every place in the guide that mentions all of these words.", "Help > User Guide");
    if (query_[0]) {
        ImGui::SameLine(0, 2.0f * s);
        if (ui::GlyphButton("##guide_clear", ICON_LC_X, ImVec2(ImGui::GetFrameHeight(), 0))) query_[0] = '\0';
        ui::DelayedTooltip("Clear the search");
    }
    if (lastQuery_ != query_) {
        lastQuery_ = query_;
        hits_ = book_.search(query_);
    }
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - webW);
    if (ImGui::Button(ICON_LC_GLOBE "  Open in browser##guide_web")) SDL_OpenURL(websiteUrl().c_str());
    ui::HelpText("Open in browser", nullptr, "Read this page on the SculptForge website.", "Help > User Guide");
    ImGui::Dummy(ImVec2(0, 2.0f * s));
}

void UserGuide::drawContents(App& app) {
    const float s = app.uiScale_;
    ImGui::Dummy(ImVec2(0, 4.0f * s));
    ImGui::Indent(6.0f * s);
    const float w = ImGui::GetContentRegionAvail().x - 6.0f * s;
    if (ImGui::Selectable("Contents##guide.toc.index", place_.chapter < 0, 0, ImVec2(w, 0))) {
        if (place_.chapter >= 0) navigate("index.md");
    }
    std::string group;
    const auto& chapters = book_.chapters();
    for (size_t i = 0; i < chapters.size(); ++i) {
        const auto& ch = chapters[i];
        if (ch.group != group) {
            group = ch.group;
            ImGui::Dummy(ImVec2(0, 6.0f * s));
            if (app.smallFont()) ImGui::PushFont(app.smallFont());
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kDim), "%s", group.c_str());
            if (app.smallFont()) ImGui::PopFont();
        }
        const bool current = int(i) == place_.chapter;
        if (current) ImGui::PushStyleColor(ImGuiCol_Text, theme::accent);
        if (ImGui::Selectable((ch.title + "##guide.toc." + ch.file).c_str(), current, 0, ImVec2(w, 0)) && !current)
            navigate(ch.file);
        if (current) ImGui::PopStyleColor();
        if (!current) continue;
        // The open chapter's sections.
        if (app.smallFont()) ImGui::PushFont(app.smallFont());
        ImGui::Indent(12.0f * s);
        if (ch.file == "whats-new.md") { // the versions in full, then the groups (core/WhatsNew.h)
            for (int v = 0; v < whatsNew_.detailed; ++v) {
                const auto& ver = whatsNew_.versions[size_t(v)];
                if (ImGui::Selectable(("Version " + ver.version + "##guide.sec." + ver.anchor).c_str(), false, 0, ImVec2(w - 12.0f * s, 0)))
                    navigate("#" + ver.anchor);
            }
            for (const auto& g : whatsNew_.groups)
                if (ImGui::Selectable((g.label + "##guide.sec." + g.anchor).c_str(), false, 0, ImVec2(w - 12.0f * s, 0)))
                    navigate("#" + g.anchor);
        } else {
            for (const auto& b : ch.doc.blocks) {
                if (b.kind != BlockKind::Heading || b.level != 2) continue;
                const std::string title = guide::plainText(b.spans);
                if (ImGui::Selectable((title + "##guide.sec." + b.anchor).c_str(), false, 0, ImVec2(w - 12.0f * s, 0)))
                    navigate("#" + b.anchor);
            }
        }
        ImGui::Unindent(12.0f * s);
        if (app.smallFont()) ImGui::PopFont();
    }
    ImGui::Unindent(6.0f * s);
    ImGui::Dummy(ImVec2(0, 6.0f * s));
}

void UserGuide::drawSearchResults(App& app) {
    const float s = app.uiScale_;
    ImGui::Dummy(ImVec2(0, 4.0f * s));
    ImGui::Indent(6.0f * s);
    const float w = ImGui::GetContentRegionAvail().x - 6.0f * s;
    if (hits_.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kDim), "Nothing in the guide matches \"%s\".", query_);
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kDim), "%zu result%s%s", hits_.size(), hits_.size() == 1 ? "" : "s",
                           hits_.size() >= 50 ? " (best 50)" : "");
    }
    ImFont* small = app.smallFont() ? app.smallFont() : ImGui::GetFont();
    ImFont* bold = app.boldFont() ? app.boldFont() : ImGui::GetFont();
    for (size_t i = 0; i < hits_.size(); ++i) {
        const auto& h = hits_[i];
        const auto& ch = book_.chapters()[size_t(h.chapter)];
        // Chapter (small), section (bold, wrapped), then up to three lines of snippet.
        const float textW = w - 12.0f * s;
        const std::string where = h.section.empty() ? ch.title : h.section;
        const ImVec2 whereSize = bold->CalcTextSizeA(bold->FontSize, FLT_MAX, textW, where.c_str());
        const ImVec2 snipSize = small->CalcTextSizeA(small->FontSize, FLT_MAX, textW, h.snippet.c_str());
        const float snipH = std::min(snipSize.y, small->FontSize * 3.0f);
        const float rowH = small->FontSize + 2.0f * s + whereSize.y + 3.0f * s + snipH + 12.0f * s;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool selected = h.chapter == place_.chapter && h.block == pendingBlock_;
        if (ImGui::Selectable(("##guide.hit." + std::to_string(i)).c_str(), selected, 0, ImVec2(w, rowH))) {
            navigate(ch.file + (h.anchor.empty() ? std::string() : "#" + h.anchor));
            pendingAnchor_.clear();
            pendingBlock_ = h.block;
            pendingTop_ = false;
            highlight_.clear();
            std::string q = lower(query_), word;
            for (char c : q + " ") {
                if (c == ' ') {
                    if (!word.empty()) highlight_.push_back(word);
                    word.clear();
                } else {
                    word += c;
                }
            }
        }
        ImGui::LogText("%s %s %s", ch.title.c_str(), where.c_str(), h.snippet.c_str());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(p, ImVec2(p.x + w, p.y + rowH - 4.0f * s), true);
        const float x = p.x + 6.0f * s;
        float ty = p.y + 5.0f * s;
        dl->AddText(small, small->FontSize, ImVec2(x, ty), theme::u32(theme::accent, 0.85f), ch.title.c_str());
        ty += small->FontSize + 2.0f * s;
        dl->AddText(bold, bold->FontSize, ImVec2(x, ty), kStrong, where.c_str(), nullptr, textW);
        ty += whereSize.y + 3.0f * s;
        // Snippet with the match marked: the highlight goes on the wrapped line the match falls on.
        const float sy = ty;
        dl->AddText(small, small->FontSize, ImVec2(x, sy), kDim, h.snippet.c_str(), nullptr, textW);
        if (h.matchLength && h.matchStart + h.matchLength <= h.snippet.size()) {
            // Find the wrapped line and x of the match by measuring the snippet prefix with wrapping.
            const char* begin = h.snippet.c_str();
            const char* lineStart = begin;
            float ly = sy;
            while (lineStart < begin + h.matchStart) {
                const char* wrap = small->CalcWordWrapPositionA(1.0f, lineStart, begin + h.snippet.size(), textW);
                if (wrap == lineStart) wrap = lineStart + 1;
                if (wrap > begin + h.matchStart) break;
                lineStart = wrap;
                while (lineStart < begin + h.snippet.size() && *lineStart == ' ') ++lineStart;
                ly += small->FontSize;
            }
            const float mx0 = x + small->CalcTextSizeA(small->FontSize, FLT_MAX, 0, lineStart, begin + h.matchStart).x;
            const float mx1 = mx0 + small->CalcTextSizeA(small->FontSize, FLT_MAX, 0, begin + h.matchStart,
                                                         begin + h.matchStart + h.matchLength)
                                         .x;
            dl->AddRectFilled(ImVec2(mx0 - 1, ly), ImVec2(mx1 + 1, ly + small->FontSize), kHighlight, 2.0f * s);
            dl->AddText(small, small->FontSize, ImVec2(mx0, ly), kStrong, begin + h.matchStart,
                        begin + h.matchStart + h.matchLength);
        }
        dl->PopClipRect();
    }
    ImGui::Unindent(6.0f * s);
}

float UserGuide::drawBlock(App& app, const Block& b, int index, ImVec2 o, float width, bool draw) {
    const float s = app.uiScale_;
    ImDrawList* dl = draw ? ImGui::GetWindowDrawList() : nullptr;
    ImFont* regular = body_ ? body_ : ImGui::GetFont();
    FlowStyle st;
    st.regular = regular;
    st.bold = bodyBold_ ? bodyBold_ : app.boldFont() ? app.boldFont() : regular;
    st.mono = app.consoleFont();
    st.lineHeight = std::round(regular->FontSize * 1.55f);
    LinkClick click;
    const std::vector<std::string>* hl = highlight_.empty() ? nullptr : &highlight_;
    auto flow = [&](const Spans& spans, ImVec2 at, float w, const FlowStyle& style, const Span* label = nullptr) {
        Flow f(style, s);
        f.firstSpan_ = label;
        return f.run(spans, at, w, dl, hl, draw ? &click : nullptr, linkCounter_);
    };
    float h = 0.0f;
    if (draw) ImGui::LogText("%s\n", guide::plainText(b).c_str());

    switch (b.kind) {
    case BlockKind::Heading: {
        FlowStyle hs = st;
        hs.heading = b.level == 1 ? h1_ : b.level == 2 ? h2_ : demoteHeadings_ ? bodyBold_ : h3_; // (What's New's groups)
        if (!hs.heading) hs.heading = st.bold;
        hs.color = kHeading;
        hs.lineHeight = std::round(hs.heading->FontSize * 1.3f);
        const float top = index == 0 ? 0.0f : (b.level == 1 ? 8.0f : b.level == 2 ? 22.0f : 14.0f) * s;
        h = top + flow(b.spans, ImVec2(o.x, o.y + top), width, hs);
        if (b.level <= 2) {
            if (dl) dl->AddLine(ImVec2(o.x, o.y + h + 3.0f * s), ImVec2(o.x + width, o.y + h + 3.0f * s), kRule, 1.0f);
            h += 8.0f * s;
        }
        break;
    }
    case BlockKind::Paragraph: h = flow(b.spans, o, width, st); break;
    case BlockKind::Bullet:
    case BlockKind::Numbered: {
        const float indent = (22.0f + float(b.level) * 22.0f) * s;
        if (dl) {
            if (b.kind == BlockKind::Bullet) {
                const float r = (b.level ? 2.2f : 2.6f) * s;
                const ImVec2 c(o.x + indent - 11.0f * s, o.y + st.lineHeight * 0.5f + 0.5f * s);
                if (b.level) dl->AddCircle(c, r, kDim, 12, 1.2f * s);
                else dl->AddCircleFilled(c, r, kDim, 12);
            } else {
                const std::string n = std::to_string(b.number) + ".";
                const float nw = textWidth(regular, n);
                dl->AddText(regular, regular->FontSize,
                            ImVec2(o.x + indent - 7.0f * s - nw, o.y + (st.lineHeight - regular->FontSize) * 0.5f), kDim,
                            n.c_str());
            }
        }
        h = flow(b.spans, ImVec2(o.x + indent, o.y), width - indent, st);
        break;
    }
    case BlockKind::Callout: {
        const float pad = 10.0f * s, bar = 3.0f * s;
        const ImU32 col = calloutColor(b);
        FlowStyle cs = st;
        cs.labelColor = col;
        const Span* label = !b.spans.empty() && b.spans[0].bold ? &b.spans[0] : nullptr;
        const float textW = width - pad * 2.0f - bar;
        const float th = Flow(cs, s).run(b.spans, o, textW, nullptr, nullptr, nullptr, linkCounter_);
        h = th + pad * 1.6f;
        if (dl) {
            dl->AddRectFilled(o, ImVec2(o.x + width, o.y + h), IM_COL32(0x2d, 0x2d, 0x31, 0xff), 4.0f * s);
            dl->AddRectFilled(o, ImVec2(o.x + bar, o.y + h), col, 4.0f * s, ImDrawFlags_RoundCornersLeft);
            flow(b.spans, ImVec2(o.x + bar + pad, o.y + pad * 0.8f), textW, cs, label);
        }
        break;
    }
    case BlockKind::Code: {
        ImFont* mono = st.mono;
        const float pad = 10.0f * s;
        const float lh = std::round(mono->FontSize * 1.4f);
        const float cw = std::max(1.0f, textWidth(mono, "M"));
        const size_t cols = size_t(std::max(8.0f, (width - pad * 2.0f) / cw));
        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= b.text.size()) {
            size_t end = b.text.find('\n', start);
            if (end == std::string::npos) end = b.text.size();
            std::string line = b.text.substr(start, end - start);
            do { // long lines wrap at the column limit
                lines.push_back(line.substr(0, cols));
                line = line.size() > cols ? "  " + line.substr(cols) : std::string();
            } while (!line.empty());
            start = end + 1;
        }
        h = float(lines.size()) * lh + pad * 2.0f;
        if (dl) {
            const ImVec2 b1(o.x + width, o.y + h);
            dl->AddRectFilled(o, b1, kCodeBg, 4.0f * s);
            dl->AddRect(o, b1, IM_COL32(0x34, 0x34, 0x38, 0xff), 4.0f * s);
            for (size_t i = 0; i < lines.size(); ++i)
                dl->AddText(mono, mono->FontSize, ImVec2(o.x + pad, o.y + pad + float(i) * lh + (lh - mono->FontSize) * 0.5f),
                            IM_COL32(0xd8, 0xd8, 0xd0, 0xff), lines[i].c_str());
            // A Copy button while the pointer is over the block.
            if (ImGui::IsMouseHoveringRect(o, b1) && ImGui::IsWindowHovered()) {
                const char* label = ICON_LC_COPY " Copy";
                const ImVec2 bs(ImGui::CalcTextSize(label).x + 12.0f * s, ImGui::GetFrameHeight());
                ImGui::SetCursorScreenPos(ImVec2(b1.x - bs.x - 4.0f * s, o.y + 4.0f * s));
                if (ImGui::Button((std::string(label) + "##guidecode" + std::to_string(index)).c_str(), bs)) {
                    SDL_SetClipboardText(b.text.c_str());
                    app.status_ = "Copied to the clipboard";
                }
            }
        }
        break;
    }
    case BlockKind::Table: {
        const size_t cols = b.rows.empty() ? 0 : b.rows[0].size();
        if (!cols) break;
        const float padX = 8.0f * s, padY = 5.0f * s;
        FlowStyle header = st;
        header.regular = st.bold;
        header.color = kStrong;
        // Natural (one line) and minimum (longest word) width of each column, then share the width.
        std::vector<float> natural(cols, 0.0f), minimum(cols, 0.0f);
        for (size_t r = 0; r < b.rows.size(); ++r)
            for (size_t c = 0; c < cols; ++c) {
                const Spans& cell = b.rows[r][c];
                float lineW = 0.0f;
                for (const auto& sp : cell) {
                    ImFont* f = (sp.bold || r == 0) && !sp.code ? st.bold : regular;
                    lineW += textWidth(f, sp.text) + (sp.code ? 6.0f * s : sp.kbd ? 14.0f * s : 0.0f);
                    std::string word;
                    for (char ch : sp.text + " ") {
                        if (ch == ' ') {
                            minimum[c] = std::max(minimum[c], textWidth(f, word) + (sp.code ? 6.0f * s : 0.0f));
                            word.clear();
                        } else {
                            word += ch;
                        }
                    }
                }
                natural[c] = std::max(natural[c], lineW + padX * 2.0f);
                minimum[c] = std::max(minimum[c] + padX * 2.0f, 40.0f * s);
            }
        std::vector<float> colW(cols);
        float sumNat = 0.0f, sumMin = 0.0f;
        for (size_t c = 0; c < cols; ++c) sumNat += natural[c], sumMin += minimum[c];
        if (sumNat <= width) {
            colW = natural;
            const float extra = (width - sumNat) / float(cols);
            for (auto& w : colW) w += sumNat > width * 0.6f ? extra : 0.0f; // short tables stay compact
        } else {
            const float spare = std::max(0.0f, width - sumMin);
            const float want = std::max(1.0f, sumNat - sumMin);
            for (size_t c = 0; c < cols; ++c) colW[c] = minimum[c] + spare * (natural[c] - minimum[c]) / want;
        }
        float tableW = 0.0f;
        for (float w : colW) tableW += w;
        float y = 0.0f;
        for (size_t r = 0; r < b.rows.size(); ++r) {
            const FlowStyle& cs = r == 0 ? header : st;
            float rowH = 0.0f;
            for (size_t c = 0; c < cols; ++c)
                rowH = std::max(rowH, Flow(cs, s).run(b.rows[r][c], o, colW[c] - padX * 2.0f, nullptr, nullptr, nullptr,
                                                      linkCounter_));
            rowH = std::max(rowH, st.lineHeight) + padY * 2.0f;
            if (dl) {
                const ImVec2 a(o.x, o.y + y), z(o.x + tableW, o.y + y + rowH);
                dl->AddRectFilled(a, z, r == 0 ? IM_COL32(0x33, 0x33, 0x37, 0xff)
                                               : r % 2 ? IM_COL32(0x29, 0x29, 0x2c, 0xff) : IM_COL32(0x25, 0x25, 0x28, 0xff));
                float x = o.x;
                for (size_t c = 0; c < cols; ++c) {
                    flow(b.rows[r][c], ImVec2(x + padX, o.y + y + padY), colW[c] - padX * 2.0f, cs);
                    x += colW[c];
                }
            }
            y += rowH;
        }
        if (dl) dl->AddRect(o, ImVec2(o.x + tableW, o.y + y), IM_COL32(0x3a, 0x3a, 0x3f, 0xff), 2.0f * s);
        h = y;
        break;
    }
    case BlockKind::Image: {
        const Texture* t = texture(b.text);
        ImFont* small = app.smallFont() ? app.smallFont() : regular;
        if (!t) {
            h = st.lineHeight;
            if (dl) dl->AddText(o, ui::color::danger, ("[missing image " + b.text + "]").c_str());
            break;
        }
        const float w = std::min(width, float(t->width) * s);
        const float ih = w * float(t->height) / float(std::max(1, t->width));
        const float x = o.x + (width - w) * 0.5f;
        const ImVec2 capSize = small->CalcTextSizeA(small->FontSize, FLT_MAX, width * 0.9f, b.alt.c_str());
        h = ih + (b.alt.empty() ? 0.0f : capSize.y + 8.0f * s);
        if (dl) {
            dl->AddImageRounded(ImTextureID(uintptr_t(t->tex)), ImVec2(x, o.y), ImVec2(x + w, o.y + ih), ImVec2(0, 0),
                                ImVec2(1, 1), IM_COL32_WHITE, 4.0f * s);
            dl->AddRect(ImVec2(x, o.y), ImVec2(x + w, o.y + ih), IM_COL32(0x3a, 0x3a, 0x3f, 0xff), 4.0f * s);
            if (!b.alt.empty())
                drawText(dl, small, ImVec2(o.x + (width - capSize.x) * 0.5f, o.y + ih + 6.0f * s), kDim, b.alt, false);
        }
        break;
    }
    case BlockKind::Rule:
        h = 14.0f * s;
        if (dl) dl->AddLine(ImVec2(o.x, o.y + h * 0.5f), ImVec2(o.x + width, o.y + h * 0.5f), kRule, 1.0f);
        break;
    }
    if (!click.target.empty()) navigate(click.target);
    return h;
}

float UserGuide::layoutBlocks(App& app, int first, int end, ImVec2 origin, float width, float y) {
    const float s = app.uiScale_;
    const guide::Document& doc = page();
    const int page = place_.chapter;
    for (int i = first; i < end; ++i) {
        const Block& b = doc.blocks[size_t(i)];
        blockY_[size_t(i)] = y;
        if (b.kind == BlockKind::Heading) anchorY_[b.anchor] = y;
        const ImVec2 at(origin.x, origin.y + y);
        float h = blockH_[size_t(i)];
        const bool visible = h < 0.0f || ImGui::IsRectVisible(at, ImVec2(at.x + width, at.y + h));
        if (visible) h = drawBlock(app, b, i, at, width, true);
        if (place_.chapter != page) return y; // a link was followed: the rest is the next page's
        blockH_[size_t(i)] = h;
        // Spacing after the block: list items sit close together, the last one of a list leaves a gap.
        const bool listItem = b.kind == BlockKind::Bullet || b.kind == BlockKind::Numbered;
        const bool nextList = i + 1 < end && (doc.blocks[size_t(i) + 1].kind == BlockKind::Bullet ||
                                              doc.blocks[size_t(i) + 1].kind == BlockKind::Numbered);
        float gap = 12.0f * s;
        if (b.kind == BlockKind::Heading) gap = b.level == 3 ? 4.0f * s : 8.0f * s;
        else if (listItem && nextList) gap = 3.0f * s;
        else if (b.kind == BlockKind::Image || b.kind == BlockKind::Table || b.kind == BlockKind::Code) gap = 16.0f * s;
        y += h + gap;
    }
    return y;
}

// ---------------------------------------------------------------------------
// What's New (whats-new.md): the newest versions in full (a band, the feature cards, "All changes"), then the older
// ones folded into groups with their highlights (core/WhatsNew.h; the website does the same in build_site.py).
// ---------------------------------------------------------------------------

void UserGuide::prepareWhatsNew() {
    const int i = book_.find("whats-new.md");
    if (i < 0) return;
    // SF_GUIDE_VERSION shows the chapter as of another version (ui_whats_new pins the newest section it checks).
    const char* as = std::getenv("SF_GUIDE_VERSION");
    whatsNew_ = whatsnew::build(book_.chapters()[size_t(i)].doc, as && *as ? as : SCULPTFORGE_VERSION);
    expanded_.clear();
    if (!whatsNew_.versions.empty()) expanded_[whatsNew_.versions[0].anchor] = true; // the newest one is open
}

std::vector<std::string> UserGuide::whatsNewContents() const {
    std::vector<std::string> out;
    for (int v = 0; v < whatsNew_.detailed; ++v) out.push_back("Version " + whatsNew_.versions[size_t(v)].version);
    for (const auto& g : whatsNew_.groups) out.push_back(g.label);
    return out;
}

int UserGuide::whatsNewExpanded() const {
    return int(std::count_if(expanded_.begin(), expanded_.end(), [](const auto& e) { return e.second; }));
}

float UserGuide::drawWhatsNewBand(App& app, ImVec2 at, float width, const std::string& title, const std::string& right,
                                  const std::string& line, bool large) {
    const float s = app.uiScale_;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* head = (large ? h2_ : h3_) ? (large ? h2_ : h3_) : ImGui::GetFont();
    ImFont* small = app.smallFont() ? app.smallFont() : ImGui::GetFont();
    ImFont* regular = body_ ? body_ : ImGui::GetFont();
    // A short accent rule, the title with the date on the right, and one line under it.
    dl->AddRectFilled(at, ImVec2(at.x + 40.0f * s, at.y + 3.0f * s), theme::u32(theme::accent), 1.5f * s);
    float y = at.y + 14.0f * s;
    dl->AddText(head, head->FontSize, ImVec2(at.x, y), theme::u32(theme::textBright), title.c_str());
    ImGui::LogText("%s\n", title.c_str());
    if (!right.empty()) {
        const float rw = small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, right.c_str()).x;
        dl->AddText(small, small->FontSize, ImVec2(at.x + width - rw, y + head->FontSize - small->FontSize - 2.0f * s),
                    theme::u32(theme::textDim), right.c_str());
    }
    y += std::round(head->FontSize * 1.3f);
    if (!line.empty()) {
        const float lw = std::min(width, 680.0f * s);
        const ImVec2 ls = regular->CalcTextSizeA(regular->FontSize, FLT_MAX, lw, line.c_str());
        dl->AddText(regular, regular->FontSize, ImVec2(at.x, y + 2.0f * s), theme::u32(theme::textDim), line.c_str(), nullptr, lw);
        ImGui::LogText("%s\n", line.c_str());
        y += ls.y + 4.0f * s;
    }
    return y - at.y;
}

float UserGuide::drawWhatsNewCards(App& app, const whatsnew::Version& v, ImVec2 at, float width) {
    const float s = app.uiScale_;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* bold = bodyBold_ ? bodyBold_ : ImGui::GetFont();
    ImFont* regular = app.fontCardBody_ ? app.fontCardBody_ : body_ ? body_ : ImGui::GetFont();
    const float lineSize = std::round(15.0f * s);
    const float gap = 14.0f * s, pad = 14.0f * s;
    const int n = int(v.cards.size());
    const int cols = std::clamp(int((width + gap) / (230.0f * s + gap)), 1, std::min(3, n));
    const float cw = (width - gap * float(cols - 1)) / float(cols), ph = std::round(cw * 340.0f / 600.0f);
    float y = 0.0f;
    for (int row = 0; row * cols < n; ++row) {
        float rowH = 0.0f;
        for (int c = 0; c < cols && row * cols + c < n; ++c) { // the row's height: its tallest words
            const auto& card = v.cards[size_t(row * cols + c)];
            rowH = std::max(rowH, ph + pad + bold->CalcTextSizeA(bold->FontSize, FLT_MAX, cw - pad * 2.0f, card.title.c_str()).y +
                                      4.0f * s + regular->CalcTextSizeA(lineSize, FLT_MAX, cw - pad * 2.0f, card.line.c_str()).y +
                                      12.0f * s + ImGui::GetFrameHeight() + pad);
        }
        for (int c = 0; c < cols && row * cols + c < n; ++c) {
            const int i = row * cols + c;
            const auto& card = v.cards[size_t(i)];
            const ImVec2 p0(at.x + float(c) * (cw + gap), at.y + y), p1(p0.x + cw, p0.y + rowH);
            if (!ImGui::IsRectVisible(p0, p1)) continue;
            const float r = 10.0f * s;
            const bool hot = ImGui::IsMouseHoveringRect(p0, p1) && ImGui::IsWindowHovered();
            dl->AddRectFilled(p0, p1, theme::u32(theme::header), r);
            if (const Texture* tex = card.image.empty() ? nullptr : texture(card.image)) {
                // Covers the top of the card (centred crop to 600 x 340).
                const float ia = float(tex->width) / float(std::max(1, tex->height)), ba = cw / ph;
                ImVec2 uv0(0, 0), uv1(1, 1);
                if (ia > ba) uv0.x = 0.5f - ba / ia * 0.5f, uv1.x = 0.5f + ba / ia * 0.5f;
                else uv0.y = 0.5f - ia / ba * 0.5f, uv1.y = 0.5f + ia / ba * 0.5f;
                dl->AddImageRounded(ImTextureID(uintptr_t(tex->tex)), p0, ImVec2(p1.x, p0.y + ph), uv0, uv1, IM_COL32_WHITE, r,
                                    ImDrawFlags_RoundCornersTop);
            }
            dl->AddRect(p0, p1, theme::u32(hot ? theme::accent : theme::text, hot ? 0.55f : 0.10f), r, 0, 1.0f);
            float ty = p0.y + ph + pad;
            const float tw = cw - pad * 2.0f;
            dl->AddText(bold, bold->FontSize, ImVec2(p0.x + pad, ty), theme::u32(theme::textBright), card.title.c_str(), nullptr, tw);
            ImGui::LogText("%s\n", card.title.c_str());
            ty += bold->CalcTextSizeA(bold->FontSize, FLT_MAX, tw, card.title.c_str()).y + 4.0f * s;
            dl->AddText(regular, lineSize, ImVec2(p0.x + pad, ty), theme::u32(theme::textDim), card.line.c_str(), nullptr, tw);
            ImGui::LogText("%s\n", card.line.c_str());
            const bool guideLink = card.action.rfind("guide:", 0) == 0;
            ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, p1.y - pad - ImGui::GetFrameHeight()));
            const std::string label = std::string(guideLink ? "Read more " : "Try it ") + ICON_LC_ARROW_RIGHT + "##wn." + v.anchor + "." +
                                      std::to_string(i);
            if (ImGui::Button(label.c_str())) app.runWhatsNewAction(card.action);
        }
        y += rowH + gap;
    }
    return y - gap;
}

bool UserGuide::drawWhatsNewToggle(App& app, ImVec2 at, float width, const std::string& id, bool open, float& h) {
    const float s = app.uiScale_;
    ImFont* bold = bodyBold_ ? bodyBold_ : ImGui::GetFont();
    h = bold->FontSize + 14.0f * s;
    ImGui::SetCursorScreenPos(at);
    const bool clicked = ImGui::InvisibleButton(("All changes##wn." + id).c_str(), ImVec2(width, h));
    const bool hot = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 c = theme::u32(hot ? theme::accent : theme::text);
    dl->AddLine(ImVec2(at.x, at.y), ImVec2(at.x + width, at.y), theme::u32(theme::sectionRule));
    // A chevron (drawn: the reading font has no icons), right when folded, down when open.
    const ImVec2 m(at.x + 6.0f * s, at.y + 7.0f * s + bold->FontSize * 0.5f);
    const float r = 4.0f * s;
    const ImVec2 pts[3] = {open ? ImVec2(m.x - r, m.y - r * 0.5f) : ImVec2(m.x - r * 0.5f, m.y - r),
                           open ? ImVec2(m.x, m.y + r * 0.5f) : ImVec2(m.x + r * 0.5f, m.y),
                           open ? ImVec2(m.x + r, m.y - r * 0.5f) : ImVec2(m.x - r * 0.5f, m.y + r)};
    dl->AddPolyline(pts, 3, theme::u32(theme::accent), 0, 1.8f * s);
    dl->AddText(bold, bold->FontSize, ImVec2(at.x + 20.0f * s, at.y + 7.0f * s), c, "All changes");
    ImGui::LogText("All changes\n");
    return clicked;
}

float UserGuide::drawWhatsNew(App& app, ImVec2 origin, float width) {
    const float s = app.uiScale_;
    const guide::Document& doc = page();
    const whatsnew::Page& wn = whatsNew_;
    const float prose = std::min(width, 680.0f * s); // about 70 characters a line
    // Going to a heading or a search hit inside a folded section opens it.
    int target = pendingBlock_ >= 0 ? pendingBlock_ : -1;
    if (!pendingAnchor_.empty())
        for (size_t i = 0; i < doc.blocks.size(); ++i)
            if (doc.blocks[i].kind == BlockKind::Heading && doc.blocks[i].anchor == pendingAnchor_) target = int(i);
    if (target >= 0) {
        for (int v = 0; v < int(wn.versions.size()); ++v) {
            const auto& ver = wn.versions[size_t(v)];
            if (target < ver.first || target >= ver.end) continue;
            if (v < wn.detailed) expanded_[ver.anchor] = true;
            for (const auto& g : wn.groups)
                if (std::find(g.versions.begin(), g.versions.end(), v) != g.versions.end()) expanded_[g.anchor] = true;
        }
    }

    float y = layoutBlocks(app, 0, wn.introEnd, origin, prose, 0.0f) + 18.0f * s;
    const int page = place_.chapter;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int vi = 0; vi < wn.detailed && place_.chapter == page; ++vi) {
        const whatsnew::Version& v = wn.versions[size_t(vi)];
        anchorY_[v.anchor] = y;
        blockY_[size_t(v.heading)] = y;
        const std::string date = v.date == "unreleased" ? std::string("Not released yet") : v.date;
        y += drawWhatsNewBand(app, ImVec2(origin.x, origin.y + y), width, "Version " + v.version, date, v.tagline, true) + 16.0f * s;
        if (!v.cards.empty()) y += drawWhatsNewCards(app, v, ImVec2(origin.x, origin.y + y), width) + 16.0f * s;
        const bool open = expanded_[v.anchor];
        float th = 0.0f;
        if (drawWhatsNewToggle(app, ImVec2(origin.x, origin.y + y), width, v.anchor, open, th)) expanded_[v.anchor] = !open;
        y += th;
        if (open) y = layoutBlocks(app, v.first, v.end, origin, prose, y + 8.0f * s);
        y += 40.0f * s;
    }
    if (!wn.groups.empty() && place_.chapter == page) {
        ImFont* small = app.boldFont() ? app.boldFont() : ImGui::GetFont();
        dl->AddText(small, small->FontSize, ImVec2(origin.x, origin.y + y), theme::u32(theme::textDim), "EARLIER VERSIONS");
        y += small->FontSize + 18.0f * s;
    }
    for (const whatsnew::Group& g : wn.groups) {
        if (place_.chapter != page) break;
        anchorY_[g.anchor] = y;
        y += drawWhatsNewBand(app, ImVec2(origin.x, origin.y + y), width, g.label, "", "", true) + 4.0f * s;
        // The highlights, as a list ("Title: line" with the title in bold).
        for (const std::string& h : g.highlights) {
            const auto [title, rest] = whatsnew::splitHighlight(h);
            Block b;
            b.kind = BlockKind::Bullet;
            if (!title.empty()) b.spans.push_back({title, true});
            b.spans.push_back({title.empty() ? rest : ": " + rest});
            y += drawBlock(app, b, -1, ImVec2(origin.x, origin.y + y), prose, true) + 3.0f * s;
        }
        y += 10.0f * s;
        const bool open = expanded_[g.anchor];
        float th = 0.0f;
        if (drawWhatsNewToggle(app, ImVec2(origin.x, origin.y + y), width, g.anchor, open, th)) expanded_[g.anchor] = !open;
        y += th;
        if (open) {
            for (int vi : g.versions) {
                const whatsnew::Version& v = wn.versions[size_t(vi)];
                y += 14.0f * s;
                anchorY_[v.anchor] = y;
                blockY_[size_t(v.heading)] = y;
                ImFont* head = h3_ ? h3_ : ImGui::GetFont();
                const std::string title = "Version " + v.version;
                dl->AddText(head, head->FontSize, ImVec2(origin.x, origin.y + y), theme::u32(theme::accent), title.c_str());
                ImGui::LogText("%s\n", title.c_str());
                y += std::round(head->FontSize * 1.4f);
                demoteHeadings_ = true; // its "###" sit under the version's title
                y = layoutBlocks(app, v.first, v.end, origin, prose, y);
                demoteHeadings_ = false;
                if (place_.chapter != page) break;
            }
        }
        y += 34.0f * s;
    }
    return y;
}

void UserGuide::drawPage(App& app) {
    const float s = app.uiScale_;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::panelBg);
    ImGui::BeginChild("##guide_page", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    const guide::Document& doc = page();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float pad = 24.0f * s;
    const float width = std::max(200.0f * s, std::min(avail - pad * 2.0f, 820.0f * s));
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 origin(start.x + std::max(pad, (avail - width) * 0.5f), start.y + pad * 0.8f);
    const size_t n = doc.blocks.size();
    const bool cached = layoutPage_ == place_.chapter && std::abs(layoutWidth_ - width) < 0.5f && blockH_.size() == n;
    if (!cached) {
        blockH_.assign(n, -1.0f);
        layoutWidth_ = width;
        layoutPage_ = place_.chapter;
    }
    blockY_.assign(n, 0.0f);
    anchorY_.clear();
    linkCounter_ = 0;
    const int page = place_.chapter;
    const bool whatsNew = currentFile() == "whats-new.md";
    float y = whatsNew ? drawWhatsNew(app, origin, width) : layoutBlocks(app, 0, int(n), origin, width, 0.0f);
    if (place_.chapter == page && whatsNew) {
        // The end of What's New: a quiet line about supporting SculptForge (it only opens the website's page).
        const float y0 = origin.y + y + 8.0f * s;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(origin.x, y0), ImVec2(origin.x + width, y0), theme::u32(theme::sectionRule));
        ImGui::SetCursorScreenPos(ImVec2(origin.x, y0 + 14.0f * s));
        if (bodyFont()) ImGui::PushFont(bodyFont());
        ImGui::PushTextWrapPos(origin.x + width);
        ImGui::TextUnformatted("SculptForge is free and open source, and stays that way. If it helps your work, "
                               "you can support its development.");
        ImGui::PopTextWrapPos();
        if (bodyFont()) ImGui::PopFont();
        ImGui::Spacing();
        ImGui::SetCursorScreenPos(ImVec2(origin.x, ImGui::GetCursorScreenPos().y));
        if (ImGui::Button("Support SculptForge##whats_new")) app.openSupportPage();
        y = ImGui::GetCursorScreenPos().y - origin.y;
    }
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(avail, y + pad * 2.0f));
    // Scroll to where navigation asked (positions are known now).
    const float top = pad * 0.8f;
    if (place_.chapter == page) {
        if (!pendingAnchor_.empty()) {
            if (auto it = anchorY_.find(pendingAnchor_); it != anchorY_.end()) ImGui::SetScrollY(std::max(0.0f, top + it->second - 6.0f * s));
            pendingAnchor_.clear();
        } else if (pendingBlock_ >= 0 && pendingBlock_ < int(n)) {
            ImGui::SetScrollY(std::max(0.0f, top + blockY_[size_t(pendingBlock_)] - 60.0f * s));
            pendingBlock_ = -pendingBlock_ - 2; // done, but remembered (search result stays selected)
        } else if (pendingScroll_ >= 0.0f) {
            ImGui::SetScrollY(pendingScroll_);
            pendingScroll_ = -1.0f;
        } else if (pendingTop_) {
            ImGui::SetScrollY(0.0f);
            pendingTop_ = false;
        }
    }
    scroll_ = ImGui::GetScrollY();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace sf
