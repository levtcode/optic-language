/* args.cpp */

#include "compiler/driver/args.hpp"

#include <iostream>
#include <charconv>

const SourceLocation loc(SourceKind::Stdin, "", "", 0, 0);

/* */
void cli_tooling::print_args_files(std::vector<std::string> &files) {
    std::cout << "Files:\n";
    for (const auto &f : files) {
        std::cout << "  " << f << "\n";
    }
}

/* */
void cli_tooling::print_args_options(std::unordered_map<std::string, ArgValue> &options) {
    std::cout << "\nCompiler options:\n";
    for (const auto &[key, value] : options) {
        std::cout << "  " << key << " = ";

        std::visit([](const auto &v) {
            using T = std::decay_t<decltype(v)>;

            if constexpr (std::is_same_v<T, bool>) {
                std::cout << (v ? "true" : "false");
            } else {
                std::cout << v;
            }
        }, value);

        std::cout << "\n";
    }
}

/* */
void cli_tooling::print_args_flags(std::unordered_map<std::string, bool> &flags) {
    std::cout << "\nCompiler flags:\n";
    for (const auto &[key, value] : flags) {
        std::cout << "  " << key << " -> ";

        if (value == true) {
            std::cout << "on";
        } else {
            std::cout << "off";
        }

        std::cout << "\n";
    }
    std::cout << "\n";
}

/* */
void cli_tooling::print_args(CompilerArgs &args) {
    using namespace cli_tooling;

    std::cout << "----- PRINTING COMPILER ARGUMENTS -----\n\n";
    print_args_files(args.files);
    print_args_options(args.options);
    print_args_flags(args.flags);
}

int cli_tooling::match_compiler_arg(const std::string_view &arg, size_t *index) {
    if (arg[0] != '-') return -1;

    size_t i = 0;
    while (i < arg.size() && arg[i] != '=') i++;

    *index = i;
    if (i++ != arg.size()) return 1;
    else                   return 0;
}

bool cli_tooling::same_arg_type(const ArgValue& a, const ArgValue& b) {
    return std::visit(
        []<typename A, typename B>(const A&, const B&) {
            return std::is_same_v<A, B>;
        },
        a, b
    );
}

ArgValue cli_tooling::parse_value(const std::string_view &option_name, size_t *index) {
    std::string_view value = option_name.substr(0, *index);

    if (value.empty()) {
        *index = std::string::npos;
        return std::string{};
    }

    if (value == "true") {
        return true;
    }

    if (value == "false") {
        return false;
    }

    int integer_value{};

    auto [int_ptr, int_ec] = std::from_chars(
        value.data(),
        value.data() + value.size(),
        integer_value
    );

    if (int_ec == std::errc{} &&
        int_ptr == value.data() + value.size()) {
        return integer_value;
    }

    return std::string{value};
}