// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Theme.h"
#include "app/CommandPalette.h"

#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"
#include "app/ZBrushNames.h"

#include <IconsLucide.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <set>

namespace sf {

namespace {

constexpr int kMaxRows = 12;
constexpr int kRecentShown = 8;

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur += char(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

bool wordStart(const std::string& hay, size_t pos) {
    return pos == 0 || !std::isalnum(static_cast<unsigned char>(hay[pos - 1]));
}

// How well WORD matches HAY (both lower case): 0 = not at all. Substrings beat scattered letters,
// and matches at the start of a word beat matches inside one.
int fuzzy(const std::string& hay, const std::string& word, bool allowScattered = true) {
    if (hay.empty() || word.empty()) return 0;
    if (const size_t pos = hay.find(word); pos != std::string::npos) {
        int s = 60;
        if (pos == 0) s += 40;
        else if (wordStart(hay, pos)) s += 25;
        if (word.size() == hay.size()) s += 20;
        return s;
    }
    if (!allowScattered || word.size() < 2) return 0;
    // Letters in order ("sbdv" -> "subdivide"); each jump costs, word starts are cheap.
    int s = 30;
    size_t at = 0;
    for (size_t i = 0; i < word.size(); ++i) {
        const size_t found = hay.find(word[i], at);
        if (found == std::string::npos) return 0;
        if (i > 0 && found != at && !wordStart(hay, found)) s -= 3;
        at = found + 1;
    }
    return std::max(s, 1);
}

} // namespace

std::string CommandPalette::zbrushHint(const Action& a, const std::string& query) {
    const std::string names = zbrushNamesIn(a.label, a.palette);
    if (names.empty()) return {};
    const std::string zbrush = lower(names), label = lower(a.label);
    for (const auto& w : words(query))
        if (w.size() >= 2 && fuzzy(zbrush, w, false) > 0 && fuzzy(label, w, false) == 0)
            return "(ZBrush users: " + names + ")";
    return {};
}

int CommandPalette::score(const Action& a, const std::string& query) {
    const auto ws = words(query);
    if (ws.empty()) return 1;
    const std::string label = lower(a.label), palette = lower(a.palette), id = lower(a.id), command = lower(a.command),
                      description = lower(a.description);
    const std::string shown = lower(trAction(a.id, a.label)); // the label in the interface language finds it too (#44)
    const std::string zbrush = lower(zbrushNamesIn(a.label, a.palette, true)); // and the ZBrush (or earlier) name
    int total = 0;
    for (const auto& w : ws) {
        const int s = 3 * std::max({fuzzy(label, w), shown == label ? 0 : fuzzy(shown, w), fuzzy(zbrush, w, false)}) +
                      2 * fuzzy(palette, w, false) + 2 * std::max(fuzzy(id, w), fuzzy(command, w)) +
                      fuzzy(description, w, false);
        if (s == 0) return 0; // every word must match somewhere
        total += s;
    }
    return total;
}

namespace {

// A command another action already runs is listed once, under that action.
std::set<std::string> coveredCommands(const ActionRegistry& reg) {
    std::set<std::string> covered;
    for (const auto& a : reg.all())
        if (!a.command.empty() && a.id != "cmd." + a.command) covered.insert(a.command);
    return covered;
}

bool listed(const Action& a, const std::set<std::string>& covered) {
    if (!a.run || a.label.empty()) return false;
    return a.id.rfind("cmd.", 0) != 0 || !covered.count(a.command);
}

} // namespace

std::vector<size_t> CommandPalette::find(const App& app, const std::string& query, size_t limit,
                                         const std::function<bool(const Action&)>& accept) {
    const ActionRegistry& reg = app.actions_;
    const auto& all = reg.all();
    const std::set<std::string> covered = coveredCommands(reg);
    auto withDescription = [&](const Action& a) {
        if (!a.description.empty() || a.command.empty()) return a;
        Action copy = a;
        if (const Command* c = app.commands_.find(a.command)) copy.description = c->description;
        return copy;
    };
    std::vector<std::pair<int, size_t>> scored;
    const bool empty = words(query).empty();
    for (size_t i = 0; i < all.size(); ++i) {
        if (!listed(all[i], covered) || (accept && !accept(all[i]))) continue;
        if (empty) scored.emplace_back(1, i);
        else if (const int s = score(withDescription(all[i]), query); s > 0) scored.emplace_back(s, i);
    }
    std::stable_sort(scored.begin(), scored.end(), [&](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return lower(all[a.second].label) < lower(all[b.second].label);
    });
    std::vector<size_t> out;
    for (const auto& [s, i] : scored) {
        if (out.size() >= limit) break;
        out.push_back(i);
    }
    return out;
}

void CommandPalette::search(App& app) {
    const ActionRegistry& reg = app.actions_;
    if (query_ == lastQuery_ && reg.revision() == lastRevision_) return;
    lastQuery_ = query_;
    lastRevision_ = reg.revision();
    results_.clear();
    selected_ = 0;
    scrollToSelected_ = true;
    const auto& all = reg.all();
    if (words(query_).empty()) {
        const std::set<std::string> covered = coveredCommands(reg);
        for (const auto& id : reg.recent()) {
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].id == id && listed(all[i], covered)) results_.push_back(i);
            if (int(results_.size()) >= kRecentShown) break;
        }
        if (results_.empty()) results_ = find(app, "", all.size()); // A-Z
        return;
    }
    results_ = find(app, query_, all.size());
}

void CommandPalette::draw(App& app, const CanvasRect& area) {
    constexpr const char* kPopup = "##command_palette";
    const float s = app.uiScale_;
    bool focus = false;
    if (openRequested_) {
        openRequested_ = false;
        ImGui::OpenPopup(kPopup);
        query_[0] = 0;
        lastQuery_ = "\x01";
        focus = true;
    }
    const float width = std::min(560.0f * s, area.w - 24.0f * s);
    ImGui::SetNextWindowPos(ImVec2(area.x + (area.w - width) * 0.5f, area.y + 14.0f * s), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0x25 / 255.0f, 0x25 / 255.0f, 0x28 / 255.0f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(IM_COL32(0xf0, 0x88, 0x2e, 0x70)));
    std::function<void()> pending;
    std::string prefill;
    if (ImGui::BeginPopup(kPopup, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        open_ = true;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Search field with a magnifier inside its frame.
        const float iconW = ImGui::CalcTextSize(ICON_LC_SEARCH).x + 10.0f * s;
        const ImVec2 fieldPos = ImGui::GetCursorScreenPos();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(iconW, 6.0f * s));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(IM_COL32(0x1b, 0x1b, 0x1e, 0xff)));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (focus) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##Command palette", tr("Search commands, brushes and settings"), query_, sizeof query_);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        dl->AddText(ImVec2(fieldPos.x + 5.0f * s, fieldPos.y + 6.0f * s), ui::color::textDim, ICON_LC_SEARCH);
        search(app);

        const auto& all = app.actions_.all();
        const int n = int(results_.size());
        if (n > 0) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) selected_ = (selected_ + 1) % n, scrollToSelected_ = true;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) selected_ = (selected_ + n - 1) % n, scrollToSelected_ = true;
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true)) selected_ = std::min(n - 1, selected_ + kMaxRows), scrollToSelected_ = true;
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true)) selected_ = std::max(0, selected_ - kMaxRows), scrollToSelected_ = true;
            selected_ = std::clamp(selected_, 0, n - 1);
        }
        int run = -1;
        bool toConsole = false;
        if (n > 0 && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
            run = selected_;
            toConsole = ImGui::GetIO().KeyShift;
        }

        ImGui::Dummy(ImVec2(0, 2.0f * s));
        if (words(query_).empty() && n > 0 && !app.actions_.recent().empty())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::textDim), "%s", tr("Recently used"));
        const float rowH = ImGui::GetTextLineHeight() + 10.0f * s;
        if (n == 0) {
            ImGui::Dummy(ImVec2(0, 4.0f * s));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::textDim), "%s", tr("  No matching commands"));
            ImGui::Dummy(ImVec2(0, 4.0f * s));
        } else {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGui::BeginChild("##palette_rows", ImVec2(0, rowH * float(std::min(n, kMaxRows))), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoNav);
            ImDrawList* rows = ImGui::GetWindowDrawList();
            rows->ChannelsSplit(2);
            ImGuiListClipper clipper;
            clipper.Begin(n, rowH);
            if (scrollToSelected_) clipper.IncludeItemByIndex(selected_);
            while (clipper.Step()) {
                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                    const Action& a = all[results_[size_t(r)]];
                    const bool enabled = !a.enabled || a.enabled();
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const float w = ImGui::GetContentRegionAvail().x;
                    rows->ChannelsSetCurrent(1);
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
                    if (ImGui::Selectable((std::string(trAction(a.id, a.label)) + "##pal." + a.id).c_str(), false, ImGuiSelectableFlags_None, ImVec2(w, rowH)))
                        run = r;
                    ImGui::PopStyleColor(4);
                    const bool hovered = ImGui::IsItemHovered();
                    if (hovered && (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f)) selected_ = r;
                    ui::HelpAction(app, a.id.c_str());
                    if (r == selected_ && scrollToSelected_) ImGui::SetScrollHereY(0.5f);
                    // Row: icon, label, dim palette path, shortcut keycaps on the right.
                    const ImU32 fg = enabled ? ui::color::text : ui::color::textDim;
                    const float ty = p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                    float x = p.x + 8.0f * s;
                    rows->AddText(ImVec2(x, ty), r == selected_ ? ui::color::accent : ui::color::textDim,
                                  a.icon ? a.icon : ICON_LC_CHEVRON_RIGHT);
                    x += ImGui::GetFontSize() + 10.0f * s;
                    const char* shownLabel = trAction(a.id, a.label);
                    rows->AddText(ImVec2(x, ty), fg, shownLabel);
                    x += ImGui::CalcTextSize(shownLabel).x + 10.0f * s;
                    // Found by its ZBrush name: "Forge Spheres (ZBrush users: ZSpheres)".
                    if (const std::string hint = zbrushHint(a, query_); !hint.empty()) {
                        rows->AddText(ImVec2(x - 4.0f * s, ty), ui::color::accent, hint.c_str());
                        if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogText("\n%s %s", shownLabel, hint.c_str());
                        x += ImGui::CalcTextSize(hint.c_str()).x + 6.0f * s;
                    }
                    const std::string chord = app.actions_.shortcutText(a.id);
                    const float keysW = chord.empty() ? 0.0f : ui::KeycapsWidth(chord);
                    const float right = p.x + w - 8.0f * s - keysW;
                    if (!a.palette.empty()) {
                        const ImVec4 clip(x, p.y, right - 8.0f * s, p.y + rowH);
                        rows->AddText(nullptr, 0.0f, ImVec2(x, ty), ui::color::textDim, trPath(a.palette).c_str(), nullptr, 0.0f, &clip);
                    }
                    if (!chord.empty()) ui::KeycapsAt(rows, ImVec2(right, ty - 1.0f * s), chord);
                    if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogText("\n%s  %s  %s", trAction(a.id, a.label), trPath(a.palette).c_str(), chord.c_str());
                    rows->ChannelsSetCurrent(0);
                    if (r == selected_)
                        rows->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), IM_COL32(0xf0, 0x88, 0x2e, 0x30), theme::rounding(4.0f * s));
                    else if (hovered)
                        rows->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), IM_COL32(0xff, 0xff, 0xff, 0x0a), theme::rounding(4.0f * s));
                }
            }
            rows->ChannelsMerge();
            scrollToSelected_ = false;
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
        // Footer: keys.
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        {
            const ImVec4 dim = ImGui::ColorConvertU32ToFloat4(ui::color::textDim);
            auto hint = [&](const char* keys, const char* what) {
                ui::Keycaps(keys);
                ImGui::SameLine(0, 4.0f * s);
                ImGui::TextColored(dim, "%s", tr(what));
                ImGui::SameLine(0, 14.0f * s);
            };
            ImGui::SetWindowFontScale(0.86f);
            hint("Up / Down", "select");
            hint("Enter", "run");
            hint("Shift+Enter", "edit in console");
            hint("Esc", "close");
            const std::string count = std::to_string(n) + tr(n == 1 ? " result" : " results");
            // Right-aligned, but never over the hints (they are longer in some languages).
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(count.c_str()).x));
            ImGui::TextColored(dim, "%s", count.c_str());
            ImGui::SetWindowFontScale(1.0f);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        if (run >= 0 && run < n) {
            const Action& a = all[results_[size_t(run)]];
            ImGui::CloseCurrentPopup();
            app.actions_.touch(a.id);
            app.saveRecentActions();
            const Command* c = a.command.empty() ? nullptr : app.commands_.find(a.command);
            const bool required = c && std::any_of(c->params.begin(), c->params.end(), [](const ParamSpec& p) { return p.required; });
            if (a.enabled && !a.enabled()) {
                app.status_ = a.label + " isn't available right now";
            } else if (c && (required || (toConsole && !c->params.empty()))) {
                prefill = c->name + " ";
            } else {
                pending = a.run;
            }
        }
        ImGui::EndPopup();
    } else {
        open_ = false;
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    // Run after the popup is closed: actions may open windows or popups of their own.
    if (!prefill.empty()) app.consolePrefill(prefill);
    if (pending) pending();
}

} // namespace sf
