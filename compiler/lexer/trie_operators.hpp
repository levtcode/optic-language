/**
 * @file trie_operators.hpp
 * @brief Operator prefix lookup using a trie.
 *
 * C++ declarations and definitions belong to namespace optic::compiler::lexer.
 */

#pragma once

#include "compiler/lexer/tokens.hpp"
#include <memory>
#include <unordered_map>
#include <string>

namespace optic::compiler::lexer {

/* */
struct TrieNode {
    std::unordered_map<char, TrieNode*> nodes;
    bool is_end_of_op = false;

    static TrieNode *init(const std::unordered_map<std::string_view, TokenType>&) noexcept;
    static void insert(const std::string &op, TrieNode *root) noexcept;
    static void destroy(TrieNode *root) noexcept;
    static bool search(const std::string &op, TrieNode *root) noexcept;
    static bool is_prefix(const std::string &op, const std::unordered_map<std::string_view, TokenType>&) noexcept;
};

} // namespace optic::compiler::lexer
