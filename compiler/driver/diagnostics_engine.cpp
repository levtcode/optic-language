/**
 * @file diagnostics_engine.cpp
 * @brief Source diagnostics, severity reporting and diagnostic presentation.
 *
 * C++ declarations and definitions belong to namespace optic::compiler::driver.
 */

#include "compiler/driver/diagnostics_engine.hpp"

#include <algorithm>
#include <format>
#include <iostream>
#include <limits>
#include <sstream>

namespace optic::compiler::driver {

const char* Diagnostic::severity_to_string(const DiagnosticsLevel level) {
    switch (level) {
        case DiagnosticsLevel::Hint:
            return "Hint";
        case DiagnosticsLevel::Warning:
            return "Warning";
        case DiagnosticsLevel::Error:
            return "Error";
        case DiagnosticsLevel::FatalError:
            return "FatalError";
        case DiagnosticsLevel::InternalError:
            return "InternalError";
    }

    return "Unknown";
}

namespace {
std::string_view trim_end(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

// Positions in the frontend count bytes. Expand tabs to four-column stops and
// count UTF-8 continuation bytes as part of the preceding character.
std::string expand(std::string_view text) {
    std::string result;
    std::size_t column = 0;
    for (unsigned char c : text) {
        if (c == '\t') {
            const auto count = 4 - column % 4;
            result.append(count, ' ');
            column += count;
        } else if (c < 32 || c == 127) {
            result += ' ';
            ++column;
        } else {
            result += static_cast<char>(c);
            if ((c & 0xc0) != 0x80) {
                ++column;
            }
        }
    }
    return result;
}

std::size_t display_width(std::string_view text) {
    const auto expanded = expand(text);
    return std::count_if(
        expanded.begin(), expanded.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
}

const char* severity_color(DiagnosticsLevel level) {
    if (level == DiagnosticsLevel::Warning) {
        return "\033[33m";
    }
    if (level == DiagnosticsLevel::Hint) {
        return "\033[36m";
    }
    return "\033[1;91m";
}

std::string excerpt(std::string_view source,
                    unsigned line,
                    unsigned column,
                    unsigned end_line,
                    unsigned end_column,
                    bool color,
                    DiagnosticsLevel level) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\n') {
            lines.push_back(trim_end(source.substr(start, i - start)));
            start = i + 1;
        }
    }
    lines.push_back(trim_end(source.substr(start)));
    if (line == 0 || line > lines.size() || column == 0 || column - 1 > lines[line - 1].size()) {
        return {};
    }

    if (end_line < line || end_line > lines.size() || (end_line == line && end_column <= column) ||
        end_column == 0 || end_column - 1 > lines[end_line - 1].size()) {
        end_line = line;
        end_column = column;
    }
    // An exclusive endpoint at the next line's start does not highlight that line.
    if (end_line > line && end_column == 1) {
        --end_line;
        end_column = static_cast<unsigned>(lines[end_line - 1].size()) + 1;
    }
    const auto first = line > 1 ? line - 1 : 1;
    const auto last = std::min<std::size_t>(lines.size(), end_line + 1);
    const auto width = std::to_string(last).size();
    const std::string padding(width, ' ');
    const auto faint = color ? "\033[90m" : "";
    const auto mark_color = color ? severity_color(level) : "";
    const auto reset = color ? "\033[0m" : "";
    std::ostringstream out;
    out << faint << padding << " |" << reset << '\n';
    for (std::size_t n = first; n <= last; ++n) {
        // Bound long multiline diagnostics while preserving both endpoints.
        if (n > line + 1 && n + 1 < end_line) {
            out << faint << padding << " | ..." << reset << '\n';
            n = end_line - 2;
            continue;
        }
        const auto text = lines[n - 1];
        out << faint << std::format("{:>{}} | ", n, width) << reset << expand(text) << '\n';
        if (n < line || n > end_line) {
            continue;
        }
        const auto begin = n == line ? column - 1 : 0;
        const auto end = n == end_line ? end_column - 1 : text.size();
        const auto left = display_width(text.substr(0, begin));
        const auto right = display_width(text.substr(0, end));
        const auto count = std::max<std::size_t>(1, right - left);
        out << faint << padding << " | " << reset << std::string(left, ' ') << mark_color
            << (n == line ? '^' : '~') << std::string(count - 1, '~') << reset << '\n';
    }
    return out.str();
}
} // namespace

SourceLocationRange SourceLocationRange::from_text(const SourceLocation& begin,
                                                   std::string_view text) {
    auto end = begin;
    for (char c : text) {
        if (c == '\n') {
            ++end.line;
            end.column = 1;
        } else {
            ++end.column;
        }
    }
    return {begin, end};
}

void DiagnosticsEngine::register_source(const std::string& file, std::string_view source) {
    auto it = sources.find(file);
    if (it == sources.end() || it->second != source) {
        sources.insert_or_assign(file, std::string(source));
    }
}

std::string DiagnosticsEngine::render(const Diagnostic& diag, bool color) const {
    const std::string severity = Diagnostic::severity_to_string(diag.level);
    auto message = trim_end(diag.msg);
    // Existing producers include their severity. Accept both old and new callers.
    const auto prefix = severity + ": ";
    if (message.starts_with(prefix)) {
        message.remove_prefix(prefix.size());
    }
    std::ostringstream out;
    out << (color ? severity_color(diag.level) : "") << severity << ":" << (color ? "\033[0m" : "")
        << ' ' << message << '\n';
    const auto& loc = diag.loc;
    if (!loc.file.empty() || loc.source_kind == SourceKind::Stdin) {
        out << "  --> " << (loc.file.empty() ? "<stdin>" : loc.file);
        if (loc.line && loc.column) {
            out << ':' << loc.line << ':' << loc.column;
        }
        out << '\n';
    }
    if (const auto it = sources.find(loc.file); it != sources.end()) {
        auto end = loc;
        if (diag.range && diag.range->begin.file == loc.file && diag.range->end.file == loc.file &&
            diag.range->begin.line == loc.line && diag.range->begin.column == loc.column) {
            end = diag.range->end;
        }
        out << excerpt(it->second, loc.line, loc.column, end.line, end.column, color, diag.level);
    }
    if (!loc.function_name.empty()) {
        out << "  = in function: " << loc.function_name << '\n';
    }
    if (!diag.solution.empty()) {
        out << "  = help: " << trim_end(diag.solution) << '\n';
    }
    if (!diag.more_info.empty()) {
        out << "  = note: " << trim_end(diag.more_info) << '\n';
    }
    return out.str();
}

void DiagnosticsEngine::print_info(const Diagnostic& diag, const SourceKind&) noexcept {
    std::cerr << render(diag, config.color_diagnostics) << '\n';
}

std::string DiagnosticsEngine::format_msg(const std::string& msg,
                                          const std::string& buffer_view,
                                          unsigned line,
                                          unsigned column,
                                          const std::string& pointer) const {
    // Compatibility helper: buffer_view is the single source line numbered `line`.
    const auto text = trim_end(buffer_view);
    if (!line || !column || column - 1 > text.size()) {
        return std::string(trim_end(msg)) + "\n";
    }
    const auto width = std::to_string(line).size();
    const std::string padding(width, ' ');
    return std::format("{}\n{} |\n{} | {}\n{} | {}{}\n",
                       trim_end(msg),
                       padding,
                       line,
                       expand(text),
                       padding,
                       std::string(display_width(text.substr(0, column - 1)), ' '),
                       pointer.empty() ? "^" : pointer);
}

void DiagnosticsEngine::report(const SourceLocation& loc,
                               const std::string& message,
                               const std::string& solution,
                               const std::string& more_info,
                               const DiagnosticsLevel lvl,
                               std::optional<SourceLocationRange> range) {
    diagnostics.emplace_back(loc, message, solution, more_info, lvl);
    diagnostics.back().range = std::move(range);

    if (lvl != DiagnosticsLevel::Hint) {
        if (lvl == DiagnosticsLevel::Warning) {
            warnings_num++;
        } else {
            errors_num++;
        }
    }
}

void DiagnosticsEngine::show() noexcept {
    if (__actual_diag >= diagnostics.size()) {
        return;
    }
    auto& diag = diagnostics[__actual_diag];
    print_info(diag, diag.loc.source_kind);
    __actual_diag++;
}

void DiagnosticsEngine::show_all() noexcept {
    for (auto& diag : diagnostics) {
        print_info(diag, diag.loc.source_kind);
    }
}

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
            if (diagnostics[__actual_diag - 1].solution.empty()) {
                std::cout << "There is no solution available to display.\n";
                break;
            }

            std::cout << diagnostics[__actual_diag - 1].solution;
            break;
        }
        case 2: {
            if (diagnostics[__actual_diag - 1].more_info.empty()) {
                std::cout << "There is no more information available to display.\n";
                break;
            }

            std::cout << diagnostics[__actual_diag - 1].more_info;
            break;
        }
        case 3:
            return;
        case 4:
            __actual_diag = diagnostics.size();
            return;
    }
}

void DiagnosticsEngine::run_guide_engine() noexcept {
    // MUCH LATER: Run the guide engine in a dedicated GUI or use specific graphics to show the
    // diagnostics.

    while (__actual_diag < diagnostics.size()) {
        show();
        show_guide_engine_menu();
    }
}

} // namespace optic::compiler::driver
