/* module.cpp */

#include "module.hpp"

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