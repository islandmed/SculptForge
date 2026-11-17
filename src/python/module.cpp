// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Python module for studio pipelines (`import sculptforge`, without the app): the API in Bindings.cpp, running
// commands itself.
#include "python/Bindings.h"

#include "core/AuditLog.h"
#include "core/StudioProfile.h"
#include "ai/AiPolicy.h"

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(sculptforge, m) {
    // Studio mode (SF_STUDIO_PROFILE, SF_OFFLINE=1 or a studio build; core/StudioProfile.h), as in the app.
    sf::ai::setStudioAiAllowed(sf::startStudioMode("", "", false).profile.ai);
    sf::audit::setClient("python"); // (the AI audit log, core/AuditLog.h)
    auto api = std::make_shared<sf::python::Api>();
    api->core = &sf::python::directCoreCalls();
    api->execute = api->core->execute;
    api->registry = api->core->registry;
    sf::python::bind(m, std::move(api), "SculptForge: free, open-source digital sculpting - Python API for pipelines");
}
