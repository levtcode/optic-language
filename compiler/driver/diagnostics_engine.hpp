/**
 * @file diagnostics_engine.hpp
 * @brief Source diagnostics, severity reporting and diagnostic presentation.
 *
 * C++ declarations and definitions belong to namespace optic::compiler::driver.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace optic::compiler::driver {

enum class DiagnosticsLevel {
    Hint,
    Warning,
    Error,
    FatalError,
    InternalError,
};

enum class SourceKind {
    Stdin,
    BufferAST,
    BufferNoAST,
};

struct SourceLocation {
    SourceKind source_kind = SourceKind::BufferNoAST;
    std::string file;
    std::string function_name;
    unsigned line = 1;
    unsigned column = 1;

    SourceLocation() = default;

    SourceLocation(SourceKind src_kind,
                   const std::string& f,
                   const std::string& func_name,
                   const unsigned ln,
                   const unsigned col)
        : source_kind(src_kind),
          file(f),
          function_name(func_name),
          line(ln),
          column(col) {}
};

/// One-based byte positions; end is exclusive and must belong to the same source.
struct SourceLocationRange {
    SourceLocation begin;
    SourceLocation end;

    static SourceLocationRange from_text(const SourceLocation& begin, std::string_view text);
};

struct DiagnosticsConfig {
    bool color_diagnostics = false;
    bool guide_engine = false;
    bool all_warnings = false;
    // more ...
};

struct Diagnostic {
    SourceLocation loc;
    std::string msg;
    std::string solution;
    std::string more_info;
    DiagnosticsLevel level;
    std::optional<SourceLocationRange> range;

    Diagnostic(const SourceLocation& location,
               const std::string& message,
               const std::string& sol,
               const std::string& info,
               const DiagnosticsLevel lvl)
        : loc(location),
          msg(message),
          solution(sol),
          more_info(info),
          level(lvl) {}

    [[nodiscard]] static const char* severity_to_string(const DiagnosticsLevel);
};

class DiagnosticsEngine {
private:
    unsigned warnings_num = 0;
    unsigned errors_num = 0;
    unsigned __actual_diag = 0;

    DiagnosticsConfig config;
    std::vector<Diagnostic> diagnostics;
    std::unordered_map<std::string, std::string> sources;

    void print_info(const Diagnostic&, const SourceKind&) noexcept;
    void show_guide_engine_menu() noexcept;

public:
    void report(const SourceLocation&,
                const std::string& msg,
                const std::string& sol,
                const std::string& info,
                const DiagnosticsLevel,
                std::optional<SourceLocationRange> range = std::nullopt);

    /// Own a source snapshot so diagnostics remain renderable after its module is destroyed.
    void register_source(const std::string& file, std::string_view source);
    /// Deterministic plain text by default; terminal output uses DiagnosticsConfig.
    std::string render(const Diagnostic&, bool color = false) const;
    void show() noexcept;
    void show_all() noexcept;
    void run_guide_engine() noexcept;

    inline int warnings_count() const noexcept {
        return warnings_num;
    }

    inline int error_count() const noexcept {
        return errors_num;
    }

    inline bool has_errors() const noexcept {
        return errors_num > 0;
    }

    const std::vector<Diagnostic>& entries() const noexcept {
        return diagnostics;
    }

    DiagnosticsConfig get_config() const noexcept {
        return config;
    }

    void set_config(DiagnosticsConfig value) noexcept {
        config = value;
    }

    std::string format_msg(const std::string& msg,
                           const std::string& buffer_view,
                           const unsigned ln,
                           const unsigned col,
                           const std::string& pointer = "^") const;
};

} // namespace optic::compiler::driver
