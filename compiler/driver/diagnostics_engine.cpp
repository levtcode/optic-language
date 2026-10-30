/* diagnostic_engine.cpp */

#include "compiler/driver/diagnostics_engine.hpp"

#include <iostream>
#include <format>

/* */
const char* Diagnostic::severity_to_string(const DiagnosticsLevel level) {
    switch (level) {
        case DiagnosticsLevel::Hint: return "Hint";
        case DiagnosticsLevel::Warning: return "Warning";
        case DiagnosticsLevel::Error: return "Error";
        case DiagnosticsLevel::FatalError: return "FatalError";
        case DiagnosticsLevel::InternalError: return "InternalError";
    }
    return "Unknown";
}

/* */
void DiagnosticsEngine::print_info(const Diagnostic &diag, const SourceKind &src_kind) noexcept {
    if (src_kind == SourceKind::Stdin) {
        std::cerr << std::format(
            "From <stdin>:\n" \
            "{}\n",
            diag.msg
        );
        return;
    }

    else if (src_kind == SourceKind::BufferNoAST) {
        std::cerr << std::format(
            "At line: {}, in column: {}\n" \
            "{}\n" \
            "In file: {}\n",
            diag.loc.line, diag.loc.column,
            diag.msg, diag.loc.file
        );
        return;
    }

    std::cerr << std::format(
        "On function: {}, at line: {}, in column: {}\n" \
        "{}\n" \
        "In file: {}\n",
        diag.loc.function_name, diag.loc.line, diag.loc.column,
        diag.msg, diag.loc.file
    );
}

/* */
std::string DiagnosticsEngine::format_msg(const std::string &msg, const std::string &buffer_view, 
    const unsigned line, const unsigned column, const std::string &pointer
) noexcept {
    
    std::string padding(std::to_string(line).length(), ' ');
    std::string error_mark = std::format("{:>{}}", pointer, column);

    return std::format(
        "{}\n" \
        "{} |\n" \
        "{} |      {}\n" \
        "{} |      {}\n" \
        "{} |\n",
        msg,
        padding,
        line, buffer_view,
        padding, error_mark,
        padding
    );
}

/* */
void DiagnosticsEngine::report(
    const SourceLocation &loc,
    const std::string &message,
    const std::string &solution,
    const std::string &more_info,
    const DiagnosticsLevel lvl
) {
    diagnostics.emplace_back(loc, message, solution, more_info, lvl);

    if (lvl != DiagnosticsLevel::Hint) {
        if (lvl == DiagnosticsLevel::Warning) warnings_num++;
        else { errors_num++; }
    }
}

/* */
void DiagnosticsEngine::show() noexcept {
    auto &diag = diagnostics[__actual_diag];
    print_info(diag, diag.loc.source_kind);
    __actual_diag++;
}

/* */
void DiagnosticsEngine::show_all() noexcept {
    for (auto &diag : diagnostics) {
        print_info(diag, diag.loc.source_kind);
    }
}

/* */
void DiagnosticsEngine::show_guide_engine_menu() noexcept {
    int user_option;

    while (true) {
        std::cout << "\nWhat do you want to do?:\n";
        std::cout << "1. Propose a solution.\n";
        std::cout << "2. More information.\n";
        std::cout << "3. Next diagnostic.\n";
        std::cout << "4. Skip and stop compilation\n\n";

        std::cout << "Select an option (1-4): ";

        if (!(std::cin >> user_option)) {
            std::cerr << "Error: The option must be a integer value.\n";
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }

        if (user_option < 1 || user_option > 4) {
            std::cerr << "Error: The option must be in the range [1-4].\n";
            continue;
        }
        
        break;
    }
    
    switch (user_option) {
        case 1: {
            if (diagnostics[__actual_diag].solution.empty()) {
                std::cout << "There is no solution available to display.\n";
                break;
            }
            std::cout << diagnostics[__actual_diag].solution;
            break;
        }
        case 2: {
            if (diagnostics[__actual_diag].more_info.empty()) {
                std::cout << "There is no more information available to display.\n";
                break;
            }
            std::cout << diagnostics[__actual_diag].more_info;
            break;
        }
        case 3: return;
        case 4:
            __actual_diag = diagnostics.size();
            return;
    }
}

/* */
void DiagnosticsEngine::run_guide_engine() noexcept {
    // MUCH LATER: Run the guide engine in a dedicated GUI or use specific graphics to show the diagnostics.

    while (__actual_diag < diagnostics.size()) {
        show();
        show_guide_engine_menu();
    }
}