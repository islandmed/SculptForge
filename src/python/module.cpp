// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Python bindings (`import sculptforge`) for studio pipelines: every editor
// command, the document's mesh data as NumPy arrays, and the tool schema.
//
//   import sculptforge as sf
//   doc = sf.Document()
//   sf.run(doc, "new_sphere", subdivisions=5)
//   sf.run(doc, "export_usd", path="/tmp/head.usdz")
//   verts, tris = doc.vertices, doc.triangles      # (N, 3) float32, (M, 3) uint32
//   doc.mask, doc.colors, doc.polygroups           # optional attributes, None when absent
#include "ai/AiCommands.h"
#include "ai/SplatTrainer.h"
#include "core/BrushLibrary.h"
#include "core/Commands.h"
#include "core/Script.h"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstring>
#include <memory>

namespace py = pybind11;
using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r = [] {
        CommandRegistry reg;
        ai::registerAiCommands(reg, [] { return ai::AiSettings::load(ai::defaultAiSettingsPath()); });
        ai::registerSplatTrainCommands(reg, [] { return ai::SplatTrainSettings::load(ai::defaultSplatTrainSettingsPath()); });
        return reg;
    }();
    return r;
}

struct CommandError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Args toArgs(const py::kwargs& kwargs) {
    Args args;
    for (auto item : kwargs) {
        const std::string key = py::cast<std::string>(item.first);
        const py::handle value = item.second;
        if (py::isinstance<py::bool_>(value)) args[key] = value.cast<bool>();
        else if (py::isinstance<py::int_>(value) || py::isinstance<py::float_>(value)) args[key] = value.cast<double>();
        else args[key] = py::cast<std::string>(py::str(value)); // str, pathlib.Path, ...
    }
    return args;
}

std::string run(Document& doc, const std::string& name, const py::kwargs& kwargs) {
    const Args args = toArgs(kwargs); // touches Python objects: needs the GIL
    CommandResult r;
    {
        py::gil_scoped_release release; // long operations (remesh, AI services) don't block other Python threads
        r = registry().execute(doc, name, args);
    }
    if (!r.ok) throw CommandError(name + ": " + r.message);
    return r.message;
}

py::array_t<float> vec3Array(const std::vector<glm::vec3>& v) {
    py::array_t<float> a({py::ssize_t(v.size()), py::ssize_t(3)});
    if (!v.empty()) std::memcpy(a.mutable_data(), v.data(), v.size() * sizeof(glm::vec3));
    return a;
}

// Optional mesh attributes: None while absent, otherwise a copy.
template <class T>
py::object attributeArray(const std::vector<T>& v) {
    if (v.empty()) return py::none();
    py::array_t<T> a(py::ssize_t(v.size()));
    std::memcpy(a.mutable_data(), v.data(), v.size() * sizeof(T));
    return a;
}

// Replaces an optional attribute from a 1-D array of `count` values (None clears it).
template <class T>
void setAttribute(std::vector<T>& dst, const py::object& value, size_t count, const char* what) {
    if (value.is_none()) {
        std::vector<T>().swap(dst);
        return;
    }
    auto a = py::array_t<T, py::array::c_style | py::array::forcecast>::ensure(value);
    if (!a || a.ndim() != 1 || size_t(a.shape(0)) != count)
        throw py::value_error(std::string(what) + ": expected " + std::to_string(count) + " values or None");
    dst.assign(a.data(), a.data() + count);
}

} // namespace

PYBIND11_MODULE(sculptforge, m) {
    m.doc() = "SculptForge: free, open-source digital sculpting - Python API for pipelines";
    m.attr("__version__") = SCULPTFORGE_VERSION;
    py::register_exception<CommandError>(m, "CommandError", PyExc_RuntimeError);

    py::class_<Document>(m, "Document", "A scene: the sculpt mesh (with layers), Gaussian splats and edit history.")
        .def(py::init([] {
            // With the user's brush library and SCULPTFORGE_BRUSH_PATH studio brushes, like the app.
            auto doc = std::make_unique<Document>();
            for (const auto& message : loadBrushLibrary(*doc))
                if (PyErr_WarnEx(PyExc_RuntimeWarning, message.c_str(), 1) < 0) throw py::error_already_set();
            return doc;
        }))
        .def_property_readonly("vertices", [](const Document& d) { return vec3Array(d.mesh.positions); },
                               "Current vertex positions, (N, 3) float32 (a copy).")
        .def_property_readonly("base_vertices", [](const Document& d) { return vec3Array(d.mesh.basePositions()); },
                               "Vertex positions with every layer at zero, (N, 3) float32.")
        .def_property_readonly("normals", [](const Document& d) { return vec3Array(d.mesh.normals); })
        .def_property(
            "mask", [](const Document& d) { return attributeArray(d.mesh.mask); },
            [](Document& d, const py::object& v) {
                setAttribute(d.mesh.mask, v, d.mesh.vertexCount(), "mask");
                d.mesh.markAllDirty();
                d.modified = true;
            },
            "Per-vertex mask, (N,) float32 with 0 = free and 1 = fully masked; None if the mesh has no mask.")
        .def_property(
            "colors",
            [](const Document& d) -> py::object { return d.mesh.colors.empty() ? py::object(py::none()) : vec3Array(d.mesh.colors); },
            [](Document& d, const py::object& v) {
                if (v.is_none()) {
                    d.mesh.clearColors();
                } else {
                    auto a = py::array_t<float, py::array::c_style | py::array::forcecast>::ensure(v);
                    if (!a || a.ndim() != 2 || a.shape(1) != 3 || size_t(a.shape(0)) != d.mesh.vertexCount())
                        throw py::value_error("colors: expected (N, 3) linear RGB or None");
                    d.mesh.colors.resize(d.mesh.vertexCount());
                    std::memcpy(d.mesh.colors.data(), a.data(), d.mesh.colors.size() * sizeof(glm::vec3));
                }
                d.mesh.markAllDirty();
                d.modified = true;
            },
            "Per-vertex paint colours, (N, 3) float32 linear RGB in 0..1; None if unpainted.")
        .def_property(
            "polygroups", [](const Document& d) { return attributeArray(d.mesh.polygroups); },
            [](Document& d, const py::object& v) {
                setAttribute(d.mesh.polygroups, v, d.mesh.triangleCount(), "polygroups");
                d.mesh.facesDirty = true;
                d.modified = true;
            },
            "Per-triangle face group ids, (M,) uint32; None means every triangle is in group 0.")
        .def_property(
            "hidden", [](const Document& d) { return attributeArray(d.mesh.hidden); },
            [](Document& d, const py::object& v) {
                setAttribute(d.mesh.hidden, v, d.mesh.triangleCount(), "hidden");
                d.mesh.facesDirty = true;
                d.modified = true;
            },
            "Per-triangle hidden flags, (M,) uint8 (nonzero = hidden); None means all visible.")
        .def_property_readonly(
            "triangles",
            [](const Document& d) {
                py::array_t<uint32_t> a({py::ssize_t(d.mesh.triangleCount()), py::ssize_t(3)});
                if (d.mesh.triangleCount())
                    std::memcpy(a.mutable_data(), d.mesh.triangles.data(), d.mesh.triangleCount() * sizeof(glm::uvec3));
                return a;
            },
            "Triangle vertex indices, (M, 3) uint32 (a copy).")
        .def_property_readonly("layers",
                               [](const Document& d) {
                                   py::list out;
                                   for (const auto& l : d.mesh.layers) out.append(py::make_tuple(l.name, l.weight));
                                   return out;
                               },
                               "Sculpt layers (blend shapes) as (name, weight) tuples.")
        .def("layer_delta", [](const Document& d, int index) {
                if (index < 0 || index >= int(d.mesh.layers.size())) throw py::index_error("no such layer");
                return vec3Array(d.mesh.layers[size_t(index)].delta);
            }, py::arg("index"), "Per-vertex offsets of a layer, (N, 3) float32.")
        .def(
            "set_mesh",
            [](Document& d, py::array_t<float, py::array::c_style | py::array::forcecast> vertices,
               py::array_t<uint32_t, py::array::c_style | py::array::forcecast> triangles) {
                if (vertices.ndim() != 2 || vertices.shape(1) != 3 || triangles.ndim() != 2 || triangles.shape(1) != 3)
                    throw py::value_error("expected vertices (N, 3) and triangles (M, 3)");
                Mesh mesh;
                mesh.positions.resize(size_t(vertices.shape(0)));
                std::memcpy(mesh.positions.data(), vertices.data(), mesh.positions.size() * sizeof(glm::vec3));
                mesh.triangles.resize(size_t(triangles.shape(0)));
                std::memcpy(mesh.triangles.data(), triangles.data(), mesh.triangles.size() * sizeof(glm::uvec3));
                for (const auto& t : mesh.triangles)
                    if (t.x >= mesh.vertexCount() || t.y >= mesh.vertexCount() || t.z >= mesh.vertexCount())
                        throw py::value_error("triangle index out of range");
                mesh.rebuildTopology();
                mesh.recomputeAllNormals();
                d.setMesh(std::move(mesh));
            },
            py::arg("vertices"), py::arg("triangles"), "Replace the sculpt with your own mesh (clears undo).")
        .def_property_readonly("splat_count", [](const Document& d) { return d.splats.size(); })
        // SubTools: the mesh properties above are the active SubTool's.
        .def_property_readonly("subtools",
                               [](const Document& d) {
                                   py::list out;
                                   for (int i = 0; i < d.subtoolCount(); ++i)
                                       out.append(py::dict(py::arg("name") = d.subtools[size_t(i)].name,
                                                           py::arg("visible") = d.subtools[size_t(i)].visible,
                                                           py::arg("triangles") = d.subtoolMesh(i).triangleCount()));
                                   return out;
                               },
                               "The Parts (separate meshes) in list order, as dicts with name, visible and triangles.")
        .def_property(
            "active_subtool", [](const Document& d) { return d.activeSubtool; },
            [](Document& d, const py::object& v) {
                int index = -1;
                if (py::isinstance<py::str>(v)) index = d.findSubtool(v.cast<std::string>());
                else index = v.cast<int>();
                if (index < 0 || index >= d.subtoolCount()) throw py::index_error("no such Part");
                d.selectSubtool(index);
            },
            "Index of the Part that vertices, mask, layers... and editing commands refer to. Set it to an "
            "index or a Part name to select another one.")
        .def("__repr__", [](const Document& d) {
            return "<sculptforge.Document " + std::to_string(d.mesh.vertexCount()) + " vertices, " +
                   std::to_string(d.mesh.triangleCount()) + " triangles, " + std::to_string(d.mesh.layers.size()) +
                   " layers, " + std::to_string(d.splats.size()) + " splats>";
        });

    m.def("run", &run, py::arg("document"), py::arg("command"),
          "Run a SculptForge command on a document, e.g. run(doc, 'remesh', resolution=256). "
          "Returns the command's message; raises sculptforge.CommandError on failure.");

    m.def(
        "run_script",
        [](Document& doc, const std::string& script) {
            std::vector<ScriptStatement> statements;
            std::string err;
            if (!parseScript(script, statements, err)) throw CommandError(err);
            py::list messages;
            for (const auto& st : statements) {
                CommandResult r;
                {
                    py::gil_scoped_release release;
                    r = registry().execute(doc, st.command, st.args);
                }
                if (!r.ok) throw CommandError("line " + std::to_string(st.line) + " (" + st.command + "): " + r.message);
                messages.append(r.message);
            }
            return messages;
        },
        py::arg("document"), py::arg("script"),
        "Run a script in sculptforge-cli syntax: 'new_sphere subdivisions=5; export_glb path=out.glb'.");

    m.def(
        "commands",
        [] {
            py::list out;
            for (const auto& c : registry().all()) {
                py::list params;
                for (const auto& p : c.params) {
                    const char* type = p.type == ParamSpec::Type::Bool ? "bool" : p.type == ParamSpec::Type::Number ? "number" : "string";
                    params.append(py::dict(py::arg("name") = p.name, py::arg("type") = type,
                                           py::arg("description") = p.description, py::arg("required") = p.required));
                }
                out.append(py::dict(py::arg("name") = c.name, py::arg("description") = c.description,
                                    py::arg("params") = params));
            }
            return out;
        },
        "Every available command with its parameters.");

    m.def("tool_schema", [] { return registry().toolSchemaJson(); },
          "Commands as LLM tool definitions (JSON), for wiring SculptForge into your own agents.");
}
