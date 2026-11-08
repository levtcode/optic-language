/**
 * @file args.cpp
 * @brief Compiler argument inspection and typed option parsing.
 *
 * C++ declarations and definitions belong to namespace optic::compiler::driver.
 */

#include "compiler/driver/args.hpp"
#include <iostream>
#include <charconv>

namespace optic::compiler::driver {


/* */
void print_args_files(std::vector<std::string> &files) {
    std::cout << "Files:\n";
    for (const auto &f : files) {
        std::cout << "  " << f << "\n";
    }
}

/* */
void print_args_options(std::unordered_map<std::string, ArgValue> &options) {
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
void print_args_flags(std::unordered_map<std::string, bool> &flags) {
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
void print_args(CompilerConfig &config) {

    std::cout << "----- PRINTING COMPILER ARGUMENTS -----\n\n";
    print_args_files  (config.input_files);
    print_args_options(config.options);
    print_args_flags  (config.flags);
}

int match_compiler_arg(const std::string_view &arg, size_t *index) {
    if (arg.empty() || arg[0] != '-') return -1;

    size_t i = 0;
    while (i < arg.size() && arg[i] != '=') i++;

    *index = i;
    if (i++ != arg.size()) return 1;
    else                   return 0;
}

bool same_arg_type(const ArgValue& a, const ArgValue& b) {
    return std::visit(
        []<typename A, typename B>(const A&, const B&) {
            return std::is_same_v<A, B>;
        },
        a, b
    );
}

ArgValue parse_value(const std::string_view &option_name, size_t *index) {
    if (*index >= option_name.size()) {
        *index = std::string::npos;
        return std::string{};
    }
    std::string_view value = option_name.substr(*index + 1);

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

    double floating_value{};
    const auto [float_ptr, float_ec] = std::from_chars(
        value.data(), value.data() + value.size(), floating_value);
    if (float_ec == std::errc{} && float_ptr == value.data() + value.size()) {
        return floating_value;
    }
    return std::string{value};
}

} // namespace optic::compiler::driver
