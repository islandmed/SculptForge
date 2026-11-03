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
//   doc.set_selection_set("Eyes", mask=m)          # push a selection set; doc.selection_set("Eyes") pulls it
#include "ai/AiCommands.h"
#include "ai/AiPolicy.h"
#include "ai/SplatTrainer.h"
#include "core/BrushLibrary.h"
#include "core/Commands.h"
#include "core/Multires.h"
#include "core/Script.h"
#include "core/SelectionSets.h"

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
        ai::setAiEnabled(ai::aiFeaturesAllowed(ai::AiSettings::load(ai::defaultAiSettingsPath()).enabled)); // Preferences > AI, SF_AI
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

// An (n, k) float32 array of `count` rows (k = 1: 1-D), or a ValueError.
template <class T>
std::vector<T> rows(const py::object& value, size_t count, int k, const char* what) {
    auto a = py::array_t<float, py::array::c_style | py::array::forcecast>::ensure(value);
    const bool shape = a && ((k == 1 && a.ndim() == 1) || (k > 1 && a.ndim() == 2 && a.shape(1) == k));
    if (!shape || size_t(a.shape(0)) != count)
        throw py::value_error(std::string(what) + ": expected " + std::to_string(count) + (k > 1 ? " rows of " + std::to_string(k) : " values"));
    std::vector<T> out(count);
    std::memcpy(out.data(), a.data(), count * sizeof(T));
    return out;
}

template <class T>
py::array_t<float> floatRows(const std::vector<T>& v, int k) {
    py::array_t<float> a = k == 1 ? py::array_t<float>(py::ssize_t(v.size())) : py::array_t<float>({py::ssize_t(v.size()), py::ssize_t(k)});
    if (!v.empty()) std::memcpy(a.mutable_data(), v.data(), v.size() * sizeof(T));
    return a;
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
        // Selection Sets (core/SelectionSets.h) of the active Part (SubTool): push and pull them as arrays.
        .def_property_readonly(
            "selection_sets",
            [](const Document& d) {
                py::list out;
                const SelectionSetList* list = d.mesh.selectionSets.get();
                if (list)
                    for (const auto& s : list->sets) {
                        py::list channels;
                        for (const char* c : {"mask", "polygroups", "paint", "visibility", "uvs", "morph"}) {
                            uint32_t bit = 0;
                            std::string err;
                            if (parseSetChannels(c, bit, err) && s->has(bit)) channels.append(c);
                        }
                        out.append(py::dict(py::arg("name") = s->name, py::arg("channels") = channels,
                                            py::arg("level") = s->level, py::arg("vertex_count") = s->vertexCount,
                                            py::arg("triangle_count") = s->triangleCount,
                                            py::arg("fits") = selectionSetFits(d.mesh, *s),
                                            py::arg("morph_target") = s->name == list->morphTarget));
                    }
                return out;
            },
            "The active Part's selection sets, as dicts with name, channels, level, vertex_count, triangle_count, "
            "fits and morph_target.")
        .def(
            "selection_set",
            [](const Document& d, const std::string& name) {
                const SelectionSetList* list = d.mesh.selectionSets.get();
                const int i = list ? list->find(name) : -1;
                if (i < 0) throw py::key_error("no selection set " + name);
                SelectionSetData data;
                std::string err;
                if (!selectionSetAt(d.mesh, *list->sets[size_t(i)], kSetAllChannels, data, err)) throw CommandError(err);
                py::dict out;
                if (data.channels & kSetMask) out["mask"] = floatRows(data.mask, 1);
                if (data.channels & kSetPolygroups) out["polygroups"] = attributeArray(data.polygroups);
                if (data.channels & kSetPaint) out["colors"] = floatRows(data.colors, 3);
                if (data.channels & kSetVisibility) out["hidden"] = attributeArray(data.hidden);
                if (data.channels & kSetUvs) out["uvs"] = floatRows(data.uvs, 2);
                if (data.channels & kSetMorph) out["positions"] = floatRows(data.positions, 3);
                return out;
            },
            py::arg("name"),
            "A selection set's channels at the current subdivision level, as a dict of NumPy arrays (mask, polygroups, "
            "colors, hidden, uvs (per triangle corner), positions), only those the set holds.")
        .def(
            "set_selection_set",
            [](Document& d, const std::string& name, const py::object& mask, const py::object& polygroups,
               const py::object& colors, const py::object& hidden, const py::object& uvs, const py::object& positions) {
                const size_t nv = d.mesh.vertexCount(), nt = d.mesh.triangleCount();
                SelectionSetData data;
                if (!mask.is_none()) data.mask = rows<float>(mask, nv, 1, "mask"), data.channels |= kSetMask;
                if (!polygroups.is_none()) {
                    setAttribute(data.polygroups, polygroups, nt, "polygroups");
                    data.channels |= kSetPolygroups;
                }
                if (!colors.is_none()) data.colors = rows<glm::vec3>(colors, nv, 3, "colors"), data.channels |= kSetPaint;
                if (!hidden.is_none()) {
                    setAttribute(data.hidden, hidden, nt, "hidden");
                    data.channels |= kSetVisibility;
                }
                if (!uvs.is_none()) data.uvs = rows<glm::vec2>(uvs, nt * 3, 2, "uvs"), data.channels |= kSetUvs;
                if (!positions.is_none()) data.positions = rows<glm::vec3>(positions, nv, 3, "positions"), data.channels |= kSetMorph;
                if (!data.channels) throw py::value_error("give at least one of mask, polygroups, colors, hidden, uvs, positions");
                SelectionSet set;
                std::string msg;
                if (!makeSelectionSet(name, data, uint32_t(nv), uint32_t(nt), subdivCurrentLevel(d.mesh), set, msg) ||
                    !addSelectionSet(d, std::move(set), "Store set " + name, msg))
                    throw CommandError(msg);
                return msg;
            },
            py::arg("name"), py::kw_only(), py::arg("mask") = py::none(), py::arg("polygroups") = py::none(),
            py::arg("colors") = py::none(), py::arg("hidden") = py::none(), py::arg("uvs") = py::none(),
            py::arg("positions") = py::none(),
            "Store (or replace) a selection set on the active Part from arrays laid out for the current level "
            "(one undo step). Restore it with run(doc, 'selection_set_restore', name=...).")
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
