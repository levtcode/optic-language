/* compiler_instance.cpp */

#include "compiler/driver/compiler_instance.hpp"

#include "compiler/driver/args.hpp"
#include "compiler/preprocessor/preprocessor.hpp"
#include "compiler/lexer/lexer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

/* */
bool get_data(const std::string fname, std::string& dest, DiagnosticsEngine& diagnostic_engine) noexcept {
    const SourceLocation srcloc(
        SourceKind::Stdin,
        "",
        "",
        0,
        0
    );

    try {
        std::ifstream file(fname, std::ios::binary);

        if (!file) {
            diagnostic_engine.report(
                srcloc,
                std::format(
                    "Error: Cannot open file '{}'.\n",
                    fname
                ),
                "Solution: Verify that the file exists and that "
                "the compiler has permission to access it.\n",
                "",
                DiagnosticsLevel::Error
            );

            return false;
        }

        std::string buffer(
            std::istreambuf_iterator<char>{file},
            std::istreambuf_iterator<char>{}
        );

        if (file.bad()) {
            diagnostic_engine.report(
                srcloc,
                std::format(
                    "Error: Failed to read file '{}'.\n",
                    fname
                ),
                "Solution: Verify that the file is accessible "
                "and that no IO error occurred.\n",
                "",
                DiagnosticsLevel::Error
            );

            return false;
        }

        dest = std::move(buffer);

        return true;
    }
    catch (...) {
        diagnostic_engine.report(
            srcloc,
            std::format(
                "Error: An exception occurred while reading '{}'.\n",
                fname
            ),
            "",
            "",
            DiagnosticsLevel::Error
        );

        return false;
    }
}

void CompilerInstance::call_usage(bool help_manual) noexcept {
    printf(
        "Usage:\n" \
        "Main arguments\n"
        "\thelp\t\tShow a help user manual.\n" \
        "\ttest\t\tRun a dedicated module to test the optic's compiler.\n" \
        "\tinstall\t\tInstall an optic package.\n" \
        "\tuninstall\tUninstall an optic package.\n" \
        "\tbuild\t\tCompile and build an executable binary file.\n" \
        "\trun\t\tCompile and run on the runtime engine the optic's source code file.\n"
        "\tdebug\t\tCompile and build the optic's source code file showing the compiler's objects.\n\n" \

        "Compiler flags\n" \
        "...\n\n"
        // TODO
    );

    if (help_manual) {
        // TODO
    }
}


// %==========% Compiler Interface %==========%

void CompilerInstance::set_mode(
    const CompilerMode mode
) {
    compiler_context.config().mode = mode;
}

void CompilerInstance::add_file(
    const std::string file
) {
    auto& files = compiler_context.config().input_files;

    if (std::find(
        files.begin(),
        files.end(),
        file
    ) == files.end()) {
        files.push_back(file);
    }
}

void CompilerInstance::set_option(
    const std::string option_name,
    const ArgValue value
) {
    compiler_context.config().set_option(
        option_name,
        value
    );
}

void CompilerInstance::set_flag(
    const std::string flag_name,
    const bool val
) {
    compiler_context.config().set_flag(
        flag_name,
        val
    );
}

int CompilerInstance::get_flag_value(
    const std::string& flag_name
) const {
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

std::optional<ArgValue> CompilerInstance::get_option_value(
    const std::string& option_name
) const {
    const ArgValue* value =
        compiler_context.config().get_option(option_name);

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

    const SourceLocation srcloc(
        SourceKind::Stdin,
        "",
        "",
        0,
        0
    );

    std::string unknown_type;
    std::string unknown_name;

    for (int i = 0; i < argc; ++i) {
        const std::string_view arg(argv[i]);

        std::size_t index = std::string::npos;

        const int match =
            cli_tooling::match_compiler_arg(arg, &index);

        // Source file.
        if (match == -1) {
            add_file(std::string(arg));
            continue;
        }

        // Option with a value.
        if (match == 1) {
            const ArgValue value =
                cli_tooling::parse_value(arg, &index);

            if (index == std::string::npos) {
                diagnostics.report(
                    srcloc,
                    "Error: Could not parse compiler option value.\n",
                    "",
                    "Supported value types: int, double, bool and string.\n",
                    DiagnosticsLevel::Error
                );

                continue;
            }

            const std::string option_name(
                arg.substr(0, index)
            );

            const auto it = config.options.find(option_name);

            if (it == config.options.end()) {
                unknown_type = "option";
                unknown_name = option_name;
                break;
            }

            if (!cli_tooling::same_arg_type(
                it->second,
                value
            )) {
                diagnostics.report(
                    srcloc,
                    std::format(
                        "Error: Type mismatch for option '{}'.\n",
                        option_name
                    ),
                    "",
                    "The provided value does not match the "
                    "option's declared type.\n",
                    DiagnosticsLevel::Error
                );

                continue;
            }

            config.set_option(option_name, value);

            continue;
        }

        // Compiler flag.
        const std::string flag_name(
            arg.substr(0, index)
        );

        const auto it = config.flags.find(flag_name);

        if (it == config.flags.end()) {
            unknown_type = "flag";
            unknown_name = flag_name;
            break;
        }

        it->second = !it->second;
    }

    if (!unknown_type.empty()) {
        diagnostics.report(
            srcloc,
            std::format(
                "Error: Unknown {} '{}'.\n",
                unknown_type,
                unknown_name
            ),
            "",
            "",
            DiagnosticsLevel::Error
        );

        call_usage();
    }
}

/* */
void CompilerInstance::lexing() noexcept {
    auto& config       = compiler_context.config();
    auto& diagnostics  = compiler_context.diagnostics();
    auto& module_graph = compiler_context.modules();

    const SourceLocation srcloc(
        SourceKind::Stdin,
        "",
        "",
        0,
        0
    );

    if (config.mode == CompilerMode::Debug) {
        std::printf(
            "\n----- PRINTING TOKENS -----\n\n"
        );
    }

    for (const std::string& fname : config.input_files) {

        if (!fname.ends_with(OPTIC_EXTENSION)) {
            diagnostics.report(
                srcloc,
                std::format(
                    "Error: '{}' is not a valid Optic source file.\n",
                    fname
                ),
                std::format(
                    "Solution: Optic source files must use '{}'.\n",
                    OPTIC_EXTENSION
                ),
                "",
                DiagnosticsLevel::Error
            );

            continue;
        }

        try {
            auto module = std::make_unique<Module>(fname);
            std::string buffer;

            if (!get_data(fname, buffer, diagnostics)) continue;

            module->set_buffer(std::move(buffer));

            Lexer lexer(module.get(), &diagnostics);

            lexer.tokenize(module->get_tokens());

            if (config.mode == CompilerMode::Debug) {
                lexer.print_tokens(*module);
            }

            module_graph.add_module(std::move(module));
        }
        catch (...) {
            diagnostics.report(
                srcloc,
                std::format(
                    "Error: Failed to process module '{}'.\n",
                    fname
                ),
                "",
                "",
                DiagnosticsLevel::Error
            );
        }
    }
}

/* */
void CompilerInstance::preprocess() noexcept {
    // TODO
}

/* */
void CompilerInstance::parsing() noexcept {
    // TODO
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
    if (generate_core_dump)
        abort();
    else
        exit(1);
}

/* */
[[nodiscard]]
int CompilerInstance::run(int argc, char *argv[], CompilerMode mode) noexcept {
    auto& diagnostics = compiler_context.diagnostics();

    set_mode(mode);

    cli_tooling(--argc, ++argv);
    lexing();
    preprocess();
    parsing();
    semantic_analize();
    optimize();
    linking();
    code_generation();

    if (diagnostics.get_config().guide_engine) {
        diagnostics.run_guide_engine();
    } else {
        diagnostics.show_all();
    }

    return diagnostics.has_errors() ? EXIT_FAILURE : EXIT_SUCCESS;
}