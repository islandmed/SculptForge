// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands for material texture painting (core/TextureChannels.h): texture_channels, paint_material,
// channel_view and texture_layer_channel.
#include "core/Document.h"
#include "core/Polypaint.h"
#include "core/Texture.h"
#include "core/TextureChannels.h"
#include "core/Undo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace sf {

namespace {

using T = ParamSpec::Type;

const char* kNoTexture = "The Part has no texture: create one first (Tool > Texture Map > New, or texture_create)";

std::string channelList() {
    std::string s;
    for (int c = 0; c < kChannelCount; ++c) s += std::string(c ? ", " : "") + channelFormat(c).id;
    return s;
}

// Splits "a, b c" into words.
std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : text) {
        if (ch == ',' || ch == ' ' || ch == ';') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Parses a channel list ("roughness,metallic", "all"); false + error for an unknown name.
bool parseChannels(const std::string& text, uint32_t& mask, std::string& error) {
    mask = 0;
    for (const std::string& w : words(text)) {
        if (w == "all") {
            mask |= (1u << kChannelCount) - 1;
            continue;
        }
        if (w == "none") continue;
        Channel c;
        if (!parseChannel(w, c)) {
            error = "Unknown channel '" + w + "' (use " + channelList() + ")";
            return false;
        }
        mask |= channelBit(c);
    }
    return true;
}

std::string megabytes(size_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    return buf;
}

std::string describeChannels(const PaintTexture& t) {
    std::string s;
    for (int c = 0; c < kChannelCount; ++c) {
        if (!channelEnabled(t, Channel(c))) continue;
        s += std::string(s.empty() ? "" : ", ") + channelFormat(c).label;
        if (c > 0) {
            const TileImage& img = t.channels[size_t(c)].base;
            if (img.denseTiles()) s += " (" + std::to_string(img.denseTiles()) + " painted tiles)";
        }
    }
    return s;
}

std::string describeMaterial(const Document& doc) {
    const BrushMaterial& m = doc.paintMaterial;
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(2);
    bool first = true;
    auto add = [&](Channel c, const std::string& v) {
        if (!(m.channels & channelBit(c))) return;
        o << (first ? "" : ", ") << channelFormat(c).label << " " << v;
        first = false;
    };
    add(Channel::BaseColor, colorToHex(doc.paintColor));
    auto num = [](float x) {
        char b[16];
        std::snprintf(b, sizeof b, "%.2f", double(x));
        return std::string(b);
    };
    add(Channel::Roughness, num(m.roughness));
    add(Channel::Metallic, num(m.metallic));
    add(Channel::Normal, "(" + num(m.normal.x) + ", " + num(m.normal.y) + ", " + num(m.normal.z) + ")");
    add(Channel::Height, num(m.height));
    add(Channel::AO, num(m.ao));
    add(Channel::Emissive, colorToHex(m.emissive));
    add(Channel::Opacity, num(m.opacity));
    return first ? std::string("nothing (no channels)") : o.str();
}

// The texture layer named by "layer" / "index" (the active one by default); -2 + error if none.
int layerArg(const PaintTexture& t, const Args& a, std::string& error) {
    if (a.contains("layer")) {
        const std::string name = argString(a, "layer");
        const int i = findTextureLayer(t, name);
        if (i == -2) error = "No texture layer called '" + name + "'";
        return i;
    }
    if (a.contains("index")) {
        const int i = int(argNumber(a, "index", -1));
        if (i < -1 || i >= int(t.layers.size())) {
            error = "No texture layer " + std::to_string(i);
            return -2;
        }
        return i;
    }
    return t.activeLayer >= -1 && t.activeLayer < int(t.layers.size()) ? t.activeLayer : -1;
}

} // namespace

void registerTextureChannelCommands(CommandRegistry& reg) {
    reg.add({"texture_channels",
             "Turn material channels of the active Part's texture on or off (PBR texture painting). Base colour is "
             "always there; roughness, metallic, normal, height, ambient occlusion (ao), emissive and opacity can be "
             "added. A new channel starts at its default everywhere and costs no memory until it is painted. Turning "
             "one off discards what was painted in it (undoable). Without arguments, lists the channels.",
             {{"enable", T::String, "Channels to turn on, e.g. \"roughness,metallic,normal\" or \"all\".", false},
              {"disable", T::String, "Channels to turn off (their paint is discarded; undoable).", false},
              {"height_normal_strength", T::Number,
               "How steep the normal made from the height channel is (default 4; 0 = height adds no normal).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!hasTexture(m)) return {false, kNoTexture};
                 uint32_t on = 0, off = 0;
                 std::string err;
                 if (!parseChannels(argString(a, "enable"), on, err) || !parseChannels(argString(a, "disable"), off, err))
                     return {false, err};
                 if ((off & 1u) && a.contains("disable")) return {false, "Base colour can't be turned off"};
                 const bool strength = a.contains("height_normal_strength");
                 if (!on && !off && !strength)
                     return {true, "Texture channels: " + describeChannels(*m.texture) + " (" +
                                       megabytes(channelBytes(*m.texture)) + " of channel memory)"};
                 UndoRecord r = captureTexture(m, "Texture Channels");
                 PaintTexture& t = *m.texture;
                 bool changed = false;
                 for (int c = 1; c < kChannelCount; ++c) {
                     if (on & (1u << c)) changed |= setChannelEnabled(t, Channel(c), true);
                     if (off & (1u << c)) changed |= setChannelEnabled(t, Channel(c), false);
                 }
                 if (strength) {
                     const float s = std::clamp(float(argNumber(a, "height_normal_strength", 4.0)), 0.0f, 100.0f);
                     changed |= s != t.heightNormalStrength;
                     t.heightNormalStrength = s;
                     t.markAllDirty();
                 }
                 if (!changed) return {true, "Texture channels unchanged: " + describeChannels(t)};
                 if (!channelEnabled(t, doc.channelView)) doc.channelView = Channel::BaseColor;
                 finishTextureUndo(r, m);
                 doc.undo.push(std::move(r));
                 doc.modified = true;
                 return {true, "Texture channels: " + describeChannels(t)};
             }});

    reg.add({"paint_material",
             "Set what the Paint brush lays down on a material texture: a value per channel. Giving a channel's value "
             "adds that channel to the brush; `channels` says exactly which ones it paints (\"all\", \"none\", or a "
             "list). Channels the texture hasn't turned on (texture_channels) are skipped when painting. Without "
             "arguments, shows the brush material.",
             {{"channels", T::String, "Exactly the channels the brush paints, e.g. \"base_color,roughness\" or \"all\".", false},
              {"color", T::String, "Base colour (as set_color: #rrggbb, r,g,b or a name).", false},
              {"roughness", T::Number, "0 (glossy) - 1 (rough).", false},
              {"metallic", T::Number, "0 (dielectric) - 1 (metal).", false},
              {"normal", T::String, "Tangent-space direction \"x,y,z\" (0,0,1 = flat).", false},
              {"height", T::Number, "0 - 1 (0.5 = flat).", false},
              {"ao", T::Number, "Ambient occlusion 0 (occluded) - 1 (open).", false},
              {"emissive", T::String, "Emitted colour (#rrggbb...; black = none).", false},
              {"opacity", T::Number, "0 (transparent) - 1 (opaque).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 BrushMaterial m = doc.paintMaterial;
                 glm::vec3 color = doc.paintColor;
                 uint32_t add = 0;
                 auto number = [&](const char* key, float& out, Channel c) {
                     if (!a.contains(key)) return;
                     out = std::clamp(float(argNumber(a, key, out)), 0.0f, 1.0f);
                     add |= channelBit(c);
                 };
                 if (a.contains("color")) {
                     if (!parseColor(argString(a, "color"), color)) return {false, "Unknown colour '" + argString(a, "color") + "'"};
                     add |= channelBit(Channel::BaseColor);
                 }
                 if (a.contains("emissive")) {
                     if (!parseColor(argString(a, "emissive"), m.emissive))
                         return {false, "Unknown colour '" + argString(a, "emissive") + "'"};
                     add |= channelBit(Channel::Emissive);
                 }
                 number("roughness", m.roughness, Channel::Roughness);
                 number("metallic", m.metallic, Channel::Metallic);
                 number("height", m.height, Channel::Height);
                 number("ao", m.ao, Channel::AO);
                 number("opacity", m.opacity, Channel::Opacity);
                 if (a.contains("normal")) {
                     float x = 0, y = 0, z = 1;
                     if (std::sscanf(argString(a, "normal").c_str(), "%f , %f , %f", &x, &y, &z) != 3 ||
                         glm::length(glm::vec3(x, y, z)) < 1e-6f)
                         return {false, "normal must be \"x,y,z\", e.g. 0,0,1"};
                     m.normal = glm::normalize(glm::vec3(x, y, z));
                     add |= channelBit(Channel::Normal);
                 }
                 m.channels |= add;
                 if (a.contains("channels")) {
                     std::string err;
                     if (!parseChannels(argString(a, "channels"), m.channels, err)) return {false, err};
                 }
                 doc.paintMaterial = m;
                 doc.paintColor = color;
                 doc.paintMaterial.color = color;
                 std::string note;
                 if (hasTexture(doc.mesh)) {
                     const uint32_t skipped = m.channels & ~enabledChannelMask(*doc.mesh.texture);
                     for (int c = 1; c < kChannelCount; ++c)
                         if (skipped & (1u << c)) note += std::string(note.empty() ? "" : ", ") + channelFormat(c).id;
                     if (!note.empty()) note = " (not on this texture: " + note + "; texture_channels enable=...)";
                 }
                 return {true, "Brush material: " + describeMaterial(doc) + note};
             }});

    reg.add({"channel_view",
             "Show one material channel of the textures on its own in the viewport (roughness, metallic, ao, height "
             "and opacity as greyscale, normal as its colours, emissive as colour), or go back to the normal view "
             "(off / base_color). Only changes what the view shows.",
             {{"channel", T::String, "A channel name, or off.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string name = argString(a, "channel");
                 Channel c = Channel::BaseColor;
                 if (name != "off" && name != "none" && !parseChannel(name, c))
                     return {false, "Unknown channel '" + name + "' (use " + channelList() + " or off)"};
                 if (c != Channel::BaseColor) {
                     if (!hasTexture(doc.mesh)) return {false, kNoTexture};
                     if (!channelEnabled(*doc.mesh.texture, c))
                         return {false, std::string("The texture has no ") + channelFormat(c).label +
                                            " channel (texture_channels enable=" + channelFormat(c).id + ")"};
                 }
                 doc.channelView = c;
                 for (int i = 0; i < doc.subtoolCount(); ++i)
                     if (const Mesh& m = doc.subtoolMesh(i); m.texture) syncChannelView(*m.texture, c);
                 return {true, c == Channel::BaseColor ? std::string("Showing the textures as they are")
                                                       : std::string("Showing the ") + channelFormat(c).label + " channel"};
             }});

    reg.add({"texture_layer_channel",
             "Per-channel settings of a texture layer: whether the layer affects a material channel, the channel's "
             "blend mode (the paint layer modes, plus normal_combine for normals) and its opacity (multiplied by the "
             "layer's). For base_color, enabled hides or shows the layer's colour only.",
             {{"channel", T::String, "The channel (roughness, normal, base_color, ...).", true},
              {"layer", T::String, "Layer name (default: the active layer).", false},
              {"index", T::Number, "Layer index instead of a name (0 = lowest above Base).", false},
              {"enabled", T::Bool, "The layer affects this channel.", false},
              {"blend", T::String, "Blend mode: normal, multiply, screen, overlay, ..., normal_combine.", false},
              {"opacity", T::Number, "0 - 1.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!hasTexture(m)) return {false, kNoTexture};
                 Channel c;
                 if (!parseChannel(argString(a, "channel"), c)) return {false, "Unknown channel (use " + channelList() + ")"};
                 if (!channelEnabled(*m.texture, c))
                     return {false, std::string("The texture has no ") + channelFormat(c).label + " channel"};
                 std::string err;
                 const int i = layerArg(*m.texture, a, err);
                 if (i == -2) return {false, err};
                 if (i < 0) return {false, "Base has no channel settings; add a layer"};
                 uint8_t blend = layerChannelBlend(*m.texture, i, c);
                 if (a.contains("blend")) {
                     if (!parseChannelBlend(argString(a, "blend"), blend)) return {false, "Unknown blend mode"};
                     if (blend == kBlendNormalCombine && c != Channel::Normal)
                         return {false, "normal_combine is for the normal channel"};
                     if (c == Channel::BaseColor && blend >= kBlendModeCount) return {false, "Unknown blend mode"};
                 }
                 UndoRecord r = captureTexture(m, "Texture Layer Channel");
                 PaintTexture& t = *m.texture;
                 syncLayerChannels(t);
                 TextureLayer& l = t.layers[size_t(i)];
                 LayerChannel& lc = l.channels[size_t(c)];
                 lc.enabled = argBool(a, "enabled", lc.enabled);
                 const float opacity = std::clamp(float(argNumber(a, "opacity", layerChannelOpacity(t, i, c))), 0.0f, 1.0f);
                 if (c == Channel::BaseColor) {
                     l.blend = BlendMode(blend);
                     l.opacity = opacity;
                 } else {
                     lc.blend = blend;
                     lc.opacity = opacity;
                 }
                 compositeTextureAll(t);
                 finishTextureUndo(r, m);
                 doc.undo.push(std::move(r));
                 doc.modified = true;
                 char buf[160];
                 std::snprintf(buf, sizeof buf, "Layer '%s' %s: %s, %s, %d%%", l.name.c_str(), channelFormat(c).label,
                               lc.enabled ? "on" : "off", channelBlendLabel(blend), int(std::lround(opacity * 100.0f)));
                 return {true, buf};
             }});
}

} // namespace sf
