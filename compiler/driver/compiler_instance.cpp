/* compiler_instance.cpp */

#include "compiler/driver/compiler_instance.hpp"
#include "compiler/driver/diagnostics_engine.hpp"
#include "compiler/driver/args.hpp"
#include "compiler/preprocessor/preprocessor.hpp"
#include "compiler/lexer/lexer.hpp"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <algorithm>

/* */
bool get_data(const std::string fname, std::string &dest, DiagnosticsEngine &diagnostic_engine) noexcept {
    const SourceLocation srcloc(SourceKind::Stdin, "", "", 0, 0);
    FILE *f = fopen(fname.c_str(), "rb");

    if (!f) {
        diagnostic_engine.report(
            srcloc,
            std::format("Error: Cant open file '{}'. File does not exists.\n", fname),
            "Solution: Check if the file exists on your system path, if doesnt, create the file yourself.\n",

            "More information: This happens because the OS (Operating System) tries to access to the " \
            "specified path, but the resource does not exists in that path. So, the OS returns a null pointer " \
            "to that resource, causing it to be imposible to read or write in that resource.\n",
            DiagnosticsLevel::Error
        );
        fclose(f);
        return false;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    dest.resize(size);
    if ((fread(dest.data(), 1, size, f) != size) && ferror(f)) {
        diagnostic_engine.report(
            srcloc,
            std::format("Error: Failed to read file '{}', IO error.\n", fname),
            "Solution: Verify if your file contains a valid encode format (UTF-8 for example) and is not a binary file.\n",
            "More information: ...",
            DiagnosticsLevel::Error
        );
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
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

inline void CompilerInstance::set_mode(const CompilerMode mode) {
    compiler_args.mode = mode;
}

inline void CompilerInstance::add_file(const std::string file) {
    if (std::find(compiler_args.files.begin(), compiler_args.files.end(), file) == compiler_args.files.end()) {
        compiler_args.files.push_back(file);
    }
}

void CompilerInstance::set_option(const std::string option_name, const ArgValue value) {
    (void) option_name;
    (void) value;
}

void CompilerInstance::set_flag(const std::string flag_name, const bool val) {
    (void) flag_name;
    (void) val;
}

int CompilerInstance::get_flag_value(const std::string flag_name) {
    (void) flag_name;
    return -1;
}

inline CompilerMode CompilerInstance::get_compiler_mode() {
    return compiler_args.mode;
}

ArgValue CompilerInstance::get_option_value(const std::string option_name) {
    (void) option_name;
    return -1;
}

inline std::vector<Module> CompilerInstance::get_files() {
    return modules;
}


// %==========% Compiler Phases %==========%

/* */
void CompilerInstance::cli_tooling(int argc, char **argv) noexcept {
    // TODO: Print args if debug mode, refactor std::visit lambda to allow local use and improve report messages.

    SourceLocation srcloc(SourceKind::Stdin, "", "", 0, 0);
    std::string tmp[2] = {""};

    for (int i = 0; i < argc; i++) {
        size_t index;
        int match = cli_tooling::match_compiler_arg(argv[i], &index);

        if (match == -1) {
            add_file(argv[i]);
        }

        else if (match == 1) {
            ArgValue val = cli_tooling::parse_value(argv[i], &index);

            if (index == std::string::npos) {
                diagnostics_engine.report(
                    srcloc,
                    std::format("Error: Couldnt parse option value, value type is empty or not supported.\n"),
                    "",
                    "More information: The compiler cli tooling expects a valid value type. Valid value types are: int, double, bool, string.\n",
                    DiagnosticsLevel::Error
                );
                stop();
            }

            std::string option_name = std::string(argv[i]).substr(0, index);

            if (!compiler_args.options.contains(option_name)) {
                tmp[0] = "option"; tmp[1] = option_name;
                break;
            }

            if (!cli_tooling::same_arg_type(compiler_args.options[option_name], val)) {
                diagnostics_engine.report(
                    srcloc,
                    std::format("Error: Option value type dont match compiler type. There is a types conflict.\n"),
                    "",
                    "",
                    DiagnosticsLevel::Error
                );
                stop();
            }
        }

        else {
            std::string flag_name = std::string(argv[i]).substr(0, index);
            
            if (!compiler_args.flags.contains(flag_name)) {
                tmp[0] = "flag"; tmp[1] = flag_name;
                break;
            }

            compiler_args.flags[flag_name] = !compiler_args.flags[flag_name];
        }
    }

    if (!tmp[0].empty()) {
        diagnostics_engine.report(srcloc,
            std::format("Error: {} name '{}' does not exists. Couldnt apply any changes. See a list of supported {}s.\n", tmp[0], tmp[1], tmp[0]),
            "",
            "",
            DiagnosticsLevel::Error
        );
        call_usage();
        stop();
    }
}

/* */
void CompilerInstance::lexing() noexcept {
    const SourceLocation srcloc(SourceKind::Stdin, "", "", 0, 0);

    if (compiler_args.mode == CompilerMode::Debug) {
        printf("\n----- PRINTING TOKENS -----\n\n");
    }

    for (size_t i = 0; i < compiler_args.files.size(); i++) {
        std::string fname = compiler_args.files[i];

        if (!fname.ends_with(optic_extension)) {
            diagnostics_engine.report(srcloc,
                std::format("Error: File '{}' is not a Optic file '{}', cant be processed.\n", fname, optic_extension),
                "",
                "",
                DiagnosticsLevel::Error
            );
            continue;
        }

        Module module(fname);
        if (!get_data(fname, module.get_buffer(), diagnostics_engine)) continue;

        Lexer lexer(&module, &diagnostics_engine);
        lexer.tokenize(module.get_tokens());

        if (compiler_args.mode == CompilerMode::Debug) {
            lexer.print_tokens(module);
        }
        modules.push_back(std::move(module));
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
    diagnostics_engine.show_all();
    if (generate_core_dump)
        abort();
    else
        exit(1);
}

/* */
[[nodiscard]]
int CompilerInstance::run(int argc, char *argv[], CompilerMode mode) noexcept {
    set_mode(mode);

    cli_tooling(--argc, ++argv);
    lexing();
    preprocess();
    parsing();
    semantic_analize();
    optimize();
    linking();
    code_generation();

    diagnostics_engine.get_config().guide_engine ? diagnostics_engine.run_guide_engine() : diagnostics_engine.show_all();
    return (diagnostics_engine.has_errors()) ? EXIT_FAILURE : EXIT_SUCCESS;
}