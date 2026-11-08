/** @file module.cpp
 * @brief Canonical module identity and deterministic dependency ordering.
 */
#include "module.hpp"
#include <algorithm>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <unordered_set>

namespace optic::compiler::driver {
Module::Module(std::string filename) : path(std::move(filename)) {}

std::string ModuleGraph::path_key(const std::filesystem::path& path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) return path.lexically_normal().generic_string();
    auto canonical = std::filesystem::weakly_canonical(absolute, error);
    return (error ? absolute : canonical).lexically_normal().generic_string();
}

ModuleID ModuleGraph::find_module(const std::filesystem::path& path) const {
    const auto it = path_ids.find(path_key(path));
    return it == path_ids.end() ? invalid_module_id : it->second;
}

ModuleID ModuleGraph::add_module(std::unique_ptr<Module> module) {
    if (!module) return invalid_module_id;
    const auto key = path_key(module->get_path());
    if (const auto it = path_ids.find(key); it != path_ids.end()) return it->second;
    if (next_id == std::numeric_limits<ModuleID>::max()) throw std::length_error("Too many modules");
    const auto id = next_id++;
    modules.emplace(id, std::move(module));
    path_ids.emplace(key, id);
    ordered_modules.clear();
    return id;
}

Module* ModuleGraph::get_module(ModuleID id) noexcept {
    const auto it = modules.find(id);
    return it == modules.end() ? nullptr : it->second.get();
}
const Module* ModuleGraph::get_module(ModuleID id) const noexcept {
    const auto it = modules.find(id);
    return it == modules.end() ? nullptr : it->second.get();
}

bool ModuleGraph::add_dependency(ModuleID module, ModuleID dependency) {
    auto* source = get_module(module);
    if (!source || !get_module(dependency)) return false;
    if (std::ranges::find(source->dependencies, dependency) != source->dependencies.end()) return true;
    std::vector<ModuleID> pending{dependency};
    std::unordered_set<ModuleID> visited;
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (id == module) return false;
        if (!visited.insert(id).second) continue;
        const auto& deps = get_module(id)->dependencies;
        pending.insert(pending.end(), deps.begin(), deps.end());
    }
    source->dependencies.push_back(dependency);
    ordered_modules.clear();
    return true;
}

bool ModuleGraph::topological_sort() {
    ordered_modules.clear();
    // Explicit DFS stack avoids overflowing the C++ stack on long import chains.
    std::unordered_map<ModuleID, unsigned> states;
    for (const auto& [root, _] : modules) {
        if (states[root] == 2) continue;
        std::vector<std::pair<ModuleID, std::size_t>> stack{{root, 0}};
        states[root] = 1;
        while (!stack.empty()) {
            auto& [id, next] = stack.back();
            const auto* module = get_module(id);
            if (!module) { ordered_modules.clear(); return false; }
            if (next == module->dependencies.size()) {
                ordered_modules.push_back(id);
                states[id] = 2;
                stack.pop_back();
                continue;
            }
            const auto dep = module->dependencies[next++];
            if (states[dep] == 1) { ordered_modules.clear(); return false; }
            if (states[dep] == 0) {
                states[dep] = 1;
                stack.emplace_back(dep, 0);
            }
        }
    }
    return true;
}

void ModuleGraph::print(std::ostream& out) const {
    out << "\n----- MODULE DEPENDENCIES -----\n";
    for (const auto id : ordered_modules) {
        const auto* module = get_module(id);
        out << module->get_path() << " -> [";
        bool first = true;
        for (const auto dep : module->dependencies) {
            if (!first) out << ", ";
            out << get_module(dep)->get_path();
            first = false;
        }
        out << "]\n";
    }
}
} // namespace optic::compiler::driver
