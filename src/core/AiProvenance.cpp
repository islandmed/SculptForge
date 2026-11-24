// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AiProvenance.h"

#include "core/Json.h"

#include <ctime>

namespace sf {

std::string AiProvenance::line() const {
    if (empty()) return {};
    std::string out;
    auto add = [&out](const std::string& s) {
        if (s.empty()) return;
        out += (out.empty() ? "" : ", ") + s;
    };
    add(model);
    add(host);
    add(date);
    if (!role.empty()) out += " (" + role + ")";
    return out;
}

std::string aiProvenanceToday() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char date[16];
    std::strftime(date, sizeof date, "%Y-%m-%d", &local);
    return date;
}

Json aiProvenanceToJson(const AiProvenance& p) {
    Json j = Json::object();
    j.set("host", p.host);
    j.set("model", p.model);
    j.set("date", p.date);
    j.set("role", p.role);
    return j;
}

AiProvenance aiProvenanceFromJson(const Json& j) {
    AiProvenance p;
    if (!j.isObject()) return p;
    p.host = j["host"].asString();
    p.model = j["model"].asString();
    p.date = j["date"].asString();
    p.role = j["role"].asString();
    return p;
}

} // namespace sf
