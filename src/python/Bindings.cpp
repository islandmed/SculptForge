// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Python API (`import sculptforge`), shared by the module for studio pipelines (module.cpp) and Python
// scripting inside the app (AppHost.cpp): every editor command, the document's mesh data as NumPy arrays, and the
// tool schema.
//
//   import sculptforge as sf
//   doc = sf.Document()
//   sf.run(doc, "new_sphere", subdivisions=5)
//   sf.run(doc, "export_usd", path="/tmp/head.usdz")
//   verts, tris = doc.vertices, doc.triangles      # (N, 3) float32, (M, 3) uint32
//   doc.mask, doc.colors, doc.face groups           # optional attributes, None when absent
//   doc.set_selection_set("Eyes", mask=m)          # push a selection set; doc.selection_set("Eyes") pulls it
//
// Without NumPy (the app's own Python has none until you add it), the arrays are lists of rows instead.
#include "python/Bindings.h"

#include "core/Commands.h"
#include "core/Document.h"
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

std::shared_ptr<const python::Api> g_api; // set by bind(): how this module runs commands
const python::CoreCalls* g_core = nullptr; // the core's functions (CoreCalls.h): g_api->core

const CommandRegistry& registry() { return g_api->registry(); }

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

CommandResult execute(Document& doc, const std::string& name, const Args& args) {
    if (!g_api->releaseGil) return g_api->execute(doc, name, args);
    py::gil_scoped_release release; // long operations (remesh, AI services) don't block other Python threads
    return g_api->execute(doc, name, args);
}

std::string run(Document& doc, const std::string& name, const py::kwargs& kwargs) {
    const Args args = toArgs(kwargs); // touches Python objects: needs the GIL
    const CommandResult r = execute(doc, name, args);
    if (!r.ok) throw CommandError(name + ": " + r.message);
    return r.message;
}

// NumPy is optional: tried once, at the first array. Without it (the app's own Python has none until you add it)
// arrays come and go as lists: a list of values, or of k-tuples for rows of k.
bool numpy() {
    static const int ok = [] {
        try {
            py::module_::import("numpy");
            return 1;
        } catch (const py::error_already_set&) {
            return 0;
        }
    }();
    return ok == 1;
}

// `n` rows of `k` elements (k == 1: a 1-D array) as a NumPy array or a list.
template <class E>
py::object rowsOut(const E* data, size_t n, int k) {
    if (numpy()) {
        py::array_t<E> a = k == 1 ? py::array_t<E>(py::ssize_t(n)) : py::array_t<E>({py::ssize_t(n), py::ssize_t(k)});
        if (n) std::memcpy(a.mutable_data(), data, n * size_t(k) * sizeof(E));
        return std::move(a);
    }
    py::list l(n);
    for (size_t i = 0; i < n; ++i) {
        if (k == 1) {
            l[i] = py::cast(data[i]);
            continue;
        }
        py::tuple t(static_cast<size_t>(k));
        for (int j = 0; j < k; ++j) t[size_t(j)] = py::cast(data[i * size_t(k) + size_t(j)]);
        l[i] = std::move(t);
    }
    return l;
}

// Reads `count` rows of `k` elements from an array or nested sequences into `out` (count * k elements); false when
// the shape is wrong.
template <class E>
bool rowsIn(const py::handle& value, size_t count, int k, E* out) {
    if (numpy() || py::hasattr(value, "__array_interface__")) {
        auto a = py::array_t<E, py::array::c_style | py::array::forcecast>::ensure(value);
        const bool shape = a && ((k == 1 && a.ndim() == 1) || (k > 1 && a.ndim() == 2 && a.shape(1) == k));
        if (!shape || size_t(a.shape(0)) != count) return false;
        if (count) std::memcpy(out, a.data(), count * size_t(k) * sizeof(E));
        return true;
    }
    if (!py::isinstance<py::sequence>(value) || py::isinstance<py::str>(value)) return false;
    const auto seq = py::reinterpret_borrow<py::sequence>(value);
    if (seq.size() != count) return false;
    for (size_t i = 0; i < count; ++i) {
        const py::object item = seq[i];
        if (k == 1) {
            out[i] = item.cast<E>();
            continue;
        }
        if (!py::isinstance<py::sequence>(item)) return false;
        const auto row = py::reinterpret_borrow<py::sequence>(item);
        if (row.size() != size_t(k)) return false;
        for (int j = 0; j < k; ++j) out[i * size_t(k) + size_t(j)] = row[size_t(j)].cast<E>();
    }
    return true;
}

py::object vec3Array(const std::vector<glm::vec3>& v) { return rowsOut(v.empty() ? nullptr : &v[0].x, v.size(), 3); }

// Optional mesh attributes: None while absent, otherwise a copy.
template <class T>
py::object attributeArray(const std::vector<T>& v) {
    if (v.empty()) return py::none();
    return rowsOut(v.data(), v.size(), 1);
}

// Replaces an optional attribute from a 1-D array of `count` values (None clears it).
template <class T>
void setAttribute(std::vector<T>& dst, const py::object& value, size_t count, const char* what) {
    if (value.is_none()) {
        std::vector<T>().swap(dst);
        return;
    }
    std::vector<T> v(count);
    if (!rowsIn(value, count, 1, v.data()))
        throw py::value_error(std::string(what) + ": expected " + std::to_string(count) + " values or None");
    dst.swap(v);
}

// An (n, k) float32 array of `count` rows (k = 1: 1-D), or a ValueError. T is float or a glm vector of k floats.
template <class T>
std::vector<T> rows(const py::object& value, size_t count, int k, const char* what) {
    static_assert(sizeof(T) % sizeof(float) == 0);
    std::vector<T> out(count);
    if (!rowsIn(value, count, k, reinterpret_cast<float*>(out.data())))
        throw py::value_error(std::string(what) + ": expected " + std::to_string(count) + (k > 1 ? " rows of " + std::to_string(k) : " values"));
    return out;
}

template <class T>
py::object floatRows(const std::vector<T>& v, int k) {
    return rowsOut(v.empty() ? nullptr : reinterpret_cast<const float*>(v.data()), v.size(), k);
}

void edit(Document& d, python::Edit what, const std::function<void()>& change) {
    if (g_api->edit) g_api->edit(d, what, change);
    else change();
}

} // namespace

void python::bind(py::module_& m, std::shared_ptr<const Api> api, const char* description) {
    g_api = std::move(api);
    g_core = g_api->core;
    m.doc() = description;
    m.attr("__version__") = SCULPTFORGE_VERSION;
    py::register_exception<CommandError>(m, "CommandError", PyExc_RuntimeError);

    py::class_<Document>(m, "Document", "A scene: the sculpt mesh (with layers), Gaussian splats and edit history.")
        .def(py::init([] {
            // With the user's brush library and SCULPTFORGE_BRUSH_PATH studio brushes, like the app.
            std::vector<std::string> warnings;
            auto doc = g_core->newDocument(warnings);
            for (const auto& message : warnings)
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
                std::vector<float> mask = d.mesh.mask;
                setAttribute(mask, v, d.mesh.vertexCount(), "mask");
                edit(d, python::Edit::Mask, [&] {
                    d.mesh.mask.swap(mask);
                    g_core->markAllDirty(d.mesh);
                    d.modified = true;
                });
            },
            "Per-vertex mask, (N,) float32 with 0 = free and 1 = fully masked; None if the mesh has no mask.")
        .def_property(
            "colors",
            [](const Document& d) -> py::object { return d.mesh.colors.empty() ? py::object(py::none()) : vec3Array(d.mesh.colors); },
            [](Document& d, const py::object& v) {
                std::vector<glm::vec3> colors;
                if (!v.is_none()) {
                    colors.resize(d.mesh.vertexCount());
                    if (!rowsIn(v, colors.size(), 3, colors.empty() ? nullptr : &colors[0].x))
                        throw py::value_error("colors: expected (N, 3) linear RGB or None");
                }
                edit(d, python::Edit::Colors, [&] {
                    if (v.is_none()) d.mesh.clearColors();
                    else d.mesh.colors.swap(colors);
                    g_core->markAllDirty(d.mesh);
                    d.modified = true;
                });
            },
            "Per-vertex paint colours, (N, 3) float32 linear RGB in 0..1; None if unpainted.")
        .def_property(
            "facegroups", [](const Document& d) { return attributeArray(d.mesh.facegroups); },
            [](Document& d, const py::object& v) {
                std::vector<uint32_t> groups = d.mesh.facegroups;
                setAttribute(groups, v, d.mesh.triangleCount(), "facegroups");
                edit(d, python::Edit::FaceGroups, [&] {
                    d.mesh.facegroups.swap(groups);
                    d.mesh.facesDirty = true;
                    d.modified = true;
                });
            },
            "Per-triangle face group ids, (M,) uint32; None means every triangle is in group 0.")
        .def_property(
            "hidden", [](const Document& d) { return attributeArray(d.mesh.hidden); },
            [](Document& d, const py::object& v) {
                std::vector<uint8_t> hidden = d.mesh.hidden;
                setAttribute(hidden, v, d.mesh.triangleCount(), "hidden");
                edit(d, python::Edit::Hidden, [&] {
                    d.mesh.hidden.swap(hidden);
                    d.mesh.facesDirty = true;
                    d.modified = true;
                });
            },
            "Per-triangle hidden flags, (M,) uint8 (nonzero = hidden); None means all visible.")
        .def_property_readonly(
            "triangles",
            [](const Document& d) {
                return rowsOut(d.mesh.triangles.empty() ? nullptr : &d.mesh.triangles[0].x, d.mesh.triangleCount(), 3);
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
            [](Document& d, const py::object& vertices, const py::object& triangles) {
                Mesh mesh;
                mesh.positions.resize(size_t(py::len(vertices)));
                mesh.triangles.resize(size_t(py::len(triangles)));
                if (!rowsIn(vertices, mesh.positions.size(), 3, mesh.positions.empty() ? nullptr : &mesh.positions[0].x) ||
                    !rowsIn(triangles, mesh.triangles.size(), 3, mesh.triangles.empty() ? nullptr : &mesh.triangles[0].x))
                    throw py::value_error("expected vertices (N, 3) and triangles (M, 3)");
                for (const auto& t : mesh.triangles)
                    if (t.x >= mesh.vertexCount() || t.y >= mesh.vertexCount() || t.z >= mesh.vertexCount())
                        throw py::value_error("triangle index out of range");
                g_core->finishMesh(mesh);
                if (g_api->replaceMesh) g_api->replaceMesh(d, std::move(mesh));
                else g_core->setMesh(d, std::move(mesh));
            },
            py::arg("vertices"), py::arg("triangles"),
            "Replace the active Part's mesh with your own (in the app one undo step; in a pipeline it clears undo).")
        .def_property_readonly("splat_count", [](const Document& d) { return d.splats.size(); })
        .def_property_readonly("subdivision_levels", [](const Document& d) { return g_core->subdivLevelCount(d.mesh); },
                               "How many subdivision levels the active Part has (0: none, after subdiv_divide 2 or more).")
        .def_property_readonly(
            "subdivision_level",
            [](const Document& d) { return g_core->subdivLevelCount(d.mesh) ? g_core->subdivCurrentLevel(d.mesh) + 1 : 0; },
            "The level shown and sculpted, 1 (lowest) to subdivision_levels; 0 without levels.")
        // Parts: the mesh properties above are the active Part's.
        .def_property_readonly("parts",
                               [](const Document& d) {
                                   py::list out;
                                   for (int i = 0; i < d.partCount(); ++i)
                                       out.append(py::dict(py::arg("name") = d.parts[size_t(i)].name,
                                                           py::arg("visible") = d.parts[size_t(i)].visible,
                                                           py::arg("triangles") = d.partMesh(i).triangleCount()));
                                   return out;
                               },
                               "The Parts (separate meshes) in list order, as dicts with name, visible and triangles.")
        .def_property(
            "active_part", [](const Document& d) { return d.activePart; },
            [](Document& d, const py::object& v) {
                int index = -1;
                if (py::isinstance<py::str>(v)) index = g_core->findPart(d, v.cast<std::string>());
                else index = v.cast<int>();
                if (index < 0 || index >= d.partCount()) throw py::index_error("no such Part");
                if (g_api->selectPart) g_api->selectPart(d, index);
                else g_core->selectPart(d, index);
            },
            "Index of the Part that vertices, mask, layers... and editing commands refer to. Set it to an "
            "index or a Part name to select another one.")
        // Selection Sets (core/SelectionSets.h) of the active Part (Part): push and pull them as arrays.
        .def_property_readonly(
            "selection_sets",
            [](const Document& d) {
                py::list out;
                const SelectionSetList* list = d.mesh.selectionSets.get();
                if (list)
                    for (const auto& s : list->sets) {
                        py::list channels;
                        for (const char* c : {"mask", "facegroups", "paint", "visibility", "uvs", "morph"}) {
                            uint32_t bit = 0;
                            std::string err;
                            if (g_core->parseSetChannels(c, bit, err) && s->has(bit)) channels.append(c);
                        }
                        out.append(py::dict(py::arg("name") = s->name, py::arg("channels") = channels,
                                            py::arg("level") = s->level, py::arg("vertex_count") = s->vertexCount,
                                            py::arg("triangle_count") = s->triangleCount,
                                            py::arg("fits") = g_core->selectionSetFits(d.mesh, *s),
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
                const int i = list ? g_core->findSelectionSet(*list, name) : -1;
                if (i < 0) throw py::key_error("no selection set " + name);
                SelectionSetData data;
                std::string err;
                if (!g_core->selectionSetAt(d.mesh, *list->sets[size_t(i)], kSetAllChannels, data, err)) throw CommandError(err);
                py::dict out;
                if (data.channels & kSetMask) out["mask"] = floatRows(data.mask, 1);
                if (data.channels & kSetFaceGroups) out["facegroups"] = attributeArray(data.facegroups);
                if (data.channels & kSetPaint) out["colors"] = floatRows(data.colors, 3);
                if (data.channels & kSetVisibility) out["hidden"] = attributeArray(data.hidden);
                if (data.channels & kSetUvs) out["uvs"] = floatRows(data.uvs, 2);
                if (data.channels & kSetMorph) out["positions"] = floatRows(data.positions, 3);
                return out;
            },
            py::arg("name"),
            "A selection set's channels at the current subdivision level, as a dict of NumPy arrays (mask, facegroups, "
            "colors, hidden, uvs (per triangle corner), positions), only those the set holds.")
        .def(
            "set_selection_set",
            [](Document& d, const std::string& name, const py::object& mask, const py::object& facegroups,
               const py::object& colors, const py::object& hidden, const py::object& uvs, const py::object& positions) {
                const size_t nv = d.mesh.vertexCount(), nt = d.mesh.triangleCount();
                SelectionSetData data;
                if (!mask.is_none()) data.mask = rows<float>(mask, nv, 1, "mask"), data.channels |= kSetMask;
                if (!facegroups.is_none()) {
                    setAttribute(data.facegroups, facegroups, nt, "facegroups");
                    data.channels |= kSetFaceGroups;
                }
                if (!colors.is_none()) data.colors = rows<glm::vec3>(colors, nv, 3, "colors"), data.channels |= kSetPaint;
                if (!hidden.is_none()) {
                    setAttribute(data.hidden, hidden, nt, "hidden");
                    data.channels |= kSetVisibility;
                }
                if (!uvs.is_none()) data.uvs = rows<glm::vec2>(uvs, nt * 3, 2, "uvs"), data.channels |= kSetUvs;
                if (!positions.is_none()) data.positions = rows<glm::vec3>(positions, nv, 3, "positions"), data.channels |= kSetMorph;
                if (!data.channels) throw py::value_error("give at least one of mask, facegroups, colors, hidden, uvs, positions");
                SelectionSet set;
                std::string msg;
                if (!g_core->makeSelectionSet(name, data, uint32_t(nv), uint32_t(nt), g_core->subdivCurrentLevel(d.mesh), set, msg) ||
                    !(g_api->addSelectionSet ? g_api->addSelectionSet(d, std::move(set), "Store set " + name, msg)
                                             : g_core->addSelectionSet(d, std::move(set), "Store set " + name, msg)))
                    throw CommandError(msg);
                return msg;
            },
            py::arg("name"), py::kw_only(), py::arg("mask") = py::none(), py::arg("facegroups") = py::none(),
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
    if (g_api->appDocument) { // in the app: the open scene, and commands on it without naming it
        m.def("document", [] { return g_api->appDocument; }, py::return_value_policy::reference,
              "The scene open in the app (the same object every time).");
        m.def(
            "run", [](const std::string& name, const py::kwargs& kwargs) { return run(*g_api->appDocument, name, kwargs); },
            py::arg("command"), "Run a command on the open scene, e.g. run('remesh', resolution=256).");
    }

    m.def(
        "run_script",
        [](Document& doc, const std::string& script) {
            std::vector<ScriptStatement> statements;
            std::string err;
            if (!g_core->parseScript(script, statements, err)) throw CommandError(err);
            py::list messages;
            for (const auto& st : statements) {
                const CommandResult r = execute(doc, st.command, st.args);
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

    m.def("tool_schema", [] { return g_core->toolSchemaJson(registry()); },
          "Commands as LLM tool definitions (JSON), for wiring SculptForge into your own agents.");
}
