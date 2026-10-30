/* trie_operators.hpp */

#pragma once

#include "compiler/lexer/tokens.hpp"

#include <unordered_map>
#include <string>

/* */
struct TrieNode {
    std::unordered_map<char, TrieNode*> nodes;
    bool is_end_of_op = false;

    void insert(const std::string &op) noexcept;
    static void init(const std::unordered_map<std::string_view, TokenType>&) noexcept;
    static void destroy() noexcept;
    static bool search(const std::string &op) noexcept;
    static bool is_prefix(const std::string &op, const std::unordered_map<std::string_view, TokenType>&) noexcept;
};