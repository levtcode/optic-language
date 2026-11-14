/**
 * @file compiler_instance.cpp
 * @brief Compiler configuration and frontend phase coordination.
 *
 * C++ declarations and definitions belong to namespace optic::compiler::driver.
 */

#include "compiler/driver/compiler_instance.hpp"
#include "compiler/ast/dump.hpp"
#include "compiler/driver/args.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/preprocessor/preprocessor.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <utility>

namespace optic::compiler::driver {

void CompilerInstance::call_usage(bool help_manual) noexcept {
    (void)help_manual;
    std::printf("Usage: opticc <build|run|debug> [options] <files.optic>\n\n"
                "  build    Analyze sources; native code generation is not implemented.\n"
                "  run      Analyze sources; bytecode generation/runtime are not implemented.\n"
                "  debug    Print compiler configuration, dependencies and preprocessed tokens.\n\n"
                "  --color-diagnostics=auto|enable|disable\n"
                "  -Wall    Enable all warnings in diagnostic configuration.\n"
                "  -g       Record the debug-information flag (backend pending).\n"
                "  --       Treat remaining arguments as source filenames.\n"
                "  --help, -h    Show this help.\n"
                "  --version     Show compiler version.\n");

    /// if help_manual is enabled, opticc will show extended info. Not implemented yet
}

// %==========% Compiler Interface %==========%

void CompilerInstance::set_mode(const CompilerMode mode) {
    compiler_context.config().mode = mode;
}

void CompilerInstance::add_file(const std::string file) {
    auto& files = compiler_context.config().input_files;

    if (std::find(files.begin(), files.end(), file) == files.end()) {
        files.push_back(file);
    }
}

void CompilerInstance::set_option(const std::string option_name, const ArgValue value) {
    compiler_context.config().set_option(option_name, value);
}

void CompilerInstance::set_flag(const std::string flag_name, const bool val) {
    compiler_context.config().set_flag(flag_name, val);
}

int CompilerInstance::get_flag_value(const std::string& flag_name) const {
    const auto& flags = compiler_context.config().flags;
    const auto it = flags.find(flag_name);

    if (it == flags.end()) {
        return -1;
    }

    return it->second ? 1 : 0;
}

CompilerMode CompilerInstance::get_compiler_mode() const {
    return compiler_context.config().mode;
}

std::optional<ArgValue> CompilerInstance::get_option_value(const std::string& option_name) const {
    const ArgValue* value = compiler_context.config().get_option(option_name);

    if (!value) {
        return std::nullopt;
    }

    return *value;
}

// %==========% Compiler Phases %==========%

/* */
void CompilerInstance::cli_tooling(int argc, char** argv) noexcept {
    auto& config = compiler_context.config();
    auto& diagnostics = compiler_context.diagnostics();

    const SourceLocation srcloc(SourceKind::Stdin, "", "", 0, 0);

    std::string unknown_type;
    std::string unknown_name;

    bool files_only = false;
    for (int i = 0; i < argc; ++i) {
        const std::string_view arg(argv[i]);

        if (!files_only && arg == "--") {
            files_only = true;
            continue;
        }
        if (arg.empty()) {
            diagnostics.report(
                srcloc, "Error: Empty compiler argument.\n", "", "", DiagnosticsLevel::Error);
            continue;
        }
        if (files_only) {
            add_file(std::string(arg));
            continue;
        }
        std::size_t index = std::string::npos;

        const int match = optic::compiler::driver::match_compiler_arg(arg, &index);

        // Source file.
        if (match == -1) {
            add_file(std::string(arg));
            continue;
        }

        // Option with a value.
        if (match == 1) {
            const ArgValue value = optic::compiler::driver::parse_value(arg, &index);

            if (index == std::string::npos) {
                diagnostics.report(srcloc,
                                   "Error: Could not parse compiler option value.\n",
                                   "",
                                   "Supported value types: int, double, bool and string.\n",
                                   DiagnosticsLevel::Error);
                continue;
            }

            const std::string option_name(arg.substr(0, index));

            const auto it = config.options.find(option_name);

            if (it == config.options.end()) {
                unknown_type = "option";
                unknown_name = option_name;
                break;
            }

            if (!optic::compiler::driver::same_arg_type(it->second, value)) {
                diagnostics.report(
                    srcloc,
                    std::format("Error: Type mismatch for option '{}'.\n", option_name),
                    "",
                    "The provided value does not match the "
                    "option's declared type.\n",
                    DiagnosticsLevel::Error);

                continue;
            }

            config.set_option(option_name, value);
            continue;
        }

        // Compiler flag.
        const std::string flag_name(arg.substr(0, index));

        const auto it = config.flags.find(flag_name);

        if (it == config.flags.end()) {
            unknown_type = "flag";
            unknown_name = flag_name;
            break;
        }

        it->second = true;
    }

    if (!unknown_type.empty()) {
        diagnostics.report(srcloc,
                           std::format("Error: Unknown {} '{}'.\n", unknown_type, unknown_name),
                           "",
                           "",
                           DiagnosticsLevel::Error);

        call_usage();
    }
}

void CompilerInstance::preprocess() noexcept {
    auto& config = compiler_context.config();
    try {
        preprocessor::Preprocessor preprocessor(compiler_context);
        const bool success = preprocessor.run(config.input_files);
        if (config.mode == CompilerMode::Debug) {
            auto& modules = compiler_context.modules();
            modules.print(std::cout);
            if (!success) {
                std::cout << "[preprocessor] Token dump skipped: preprocessing failed.\n";
                return;
            }
            std::cout << "\n----- PRINTING PREPROCESSED TOKENS -----\n\n";
            for (const auto id : modules.compilation_order()) {
                const auto& module = *modules.get_module(id);
                lexer::Lexer::print_tokens(module);
            }
        }
    } catch (const std::exception& exception) {
        compiler_context.diagnostics().report(
            {},
            std::string("Error: Frontend preprocessing failed: ") + exception.what(),
            "",
            "",
            DiagnosticsLevel::Error);
    }
}

/* */
void CompilerInstance::parsing() noexcept {
    auto& trees = compiler_context.syntax_trees();
    trees.clear();
    try {
        auto& modules = compiler_context.modules();
        for (const auto id : modules.compilation_order()) {
            const auto& module = *modules.get_module(id);
            if (module.state() != ModuleState::Ready) {
                continue;
            }
            parser::Parser parser(id, module, compiler_context.diagnostics());
            if (auto tree = parser.parse()) {
                if (compiler_context.config().mode == CompilerMode::Debug) {
                    std::cout << "\n----- AST: " << module.get_path() << " -----\n";
                    ast::dump(*tree, std::cout);
                }
                trees.emplace(id, std::move(tree));
            }
        }
    } catch (const std::exception& exception) {
        compiler_context.diagnostics().report({},
                                              std::string("Error: Parsing failed: ") +
                                                  exception.what(),
                                              "",
                                              "",
                                              DiagnosticsLevel::Error);
    }
}

/* */
void CompilerInstance::semantic_analize() noexcept {
    // TODO
}

/* */
void CompilerInstance::optimize() noexcept {
    // TODO
}

/* */
void CompilerInstance::linking() noexcept {
    // TODO
}

/* */
void CompilerInstance::code_generation() noexcept {
    // TODO
}

/* */
[[noreturn]]
void CompilerInstance::stop(bool generate_core_dump) noexcept {
    compiler_context.diagnostics().show_all();
    if (generate_core_dump) {
        abort();
    } else {
        exit(1);
    }
}

/* */
[[nodiscard]]
int CompilerInstance::run(int argc, char* argv[], CompilerMode mode) noexcept {
    auto& diagnostics = compiler_context.diagnostics();

    set_mode(mode);

    cli_tooling(argc, argv);
    if (compiler_context.config().input_files.empty()) {
        diagnostics.report(
            SourceLocation{}, "Error: No input files.\n", "", "", DiagnosticsLevel::Error);
    }
    if (diagnostics.has_errors()) {
        diagnostics.show_all();
        return EXIT_FAILURE;
    }
    auto diagnostic_config = diagnostics.get_config();
    diagnostic_config.all_warnings = compiler_context.config().has_flag("-Wall");
    const auto color =
        std::get<std::string>(compiler_context.config().options.at("--color-diagnostics"));
    if (color != "auto" && color != "enable" && color != "disable") {
        std::fprintf(stderr, "opticc: --color-diagnostics expects auto, enable or disable.\n");
        return EXIT_FAILURE;
    }
    diagnostic_config.color_diagnostics = color != "disable";
    diagnostics.set_config(diagnostic_config);
    if (mode == CompilerMode::Debug) {
        optic::compiler::driver::print_args(compiler_context.config());
    }
    preprocess();
    if (!diagnostics.has_errors()) {
        parsing();
    }
    if (!diagnostics.has_errors()) {
        semantic_analize();
    }
    if (!diagnostics.has_errors()) {
        optimize();
    }
    if (!diagnostics.has_errors()) {
        code_generation();
    }
    if (!diagnostics.has_errors()) {
        linking();
    }

    if (diagnostics.get_config().guide_engine) {
        diagnostics.run_guide_engine();
    } else {
        diagnostics.show_all();
    }

    if (diagnostics.has_errors()) {
        return EXIT_FAILURE;
    }
    if (mode != CompilerMode::Debug) {
        std::fprintf(stderr,
                     "opticc: %s is not implemented; frontend analysis completed, but no artifact "
                     "was generated.\n",
                     mode == CompilerMode::Run ? "bytecode generation and execution"
                                               : "native code generation");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

} // namespace optic::compiler::driver
