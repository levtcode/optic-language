/**
 * @file module.cpp
 * @brief Source modules and their dependency graph.
 *
 * C++ declarations and definitions belong to namespace optic.
 */

#include "module.hpp"

namespace optic {

ModuleID ModuleGraph::add_module(std::unique_ptr<Module> module) {
    modules[next_id++] = std::move(module);
    return next_id - 1;
}

Module* ModuleGraph::get_module(ModuleID id) noexcept {
    if (id == invalid_module_id || id >= next_id) {
        return nullptr;
    }

    return modules[id].get();
}

} // namespace optic
