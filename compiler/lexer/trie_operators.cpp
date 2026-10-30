/* trie_operators.cpp */

#include "compiler/lexer/trie_operators.hpp"

TrieNode *root = new TrieNode();

/* */
void TrieNode::insert(const std::string &operator_) noexcept {
    TrieNode *current = root;
    
    for (auto op : operator_) {
        if (!current->nodes.contains(op)) {
            current->nodes[op] = new TrieNode();
        }
        current = current->nodes[op];
    }

    current->is_end_of_op = true;
}

/* */
bool TrieNode::search(const std::string &operator_) noexcept {
    TrieNode *current = root;

    for (auto op : operator_) {
        if (!current->nodes.contains(op)) { return false; }
        current = current->nodes[op];
    }

    return current->is_end_of_op;
}

/* */
bool TrieNode::is_prefix(const std::string &s, const std::unordered_map<std::string_view, TokenType> &operator_table) noexcept {
    for (const auto &[op, _] : operator_table) {
        if (op.starts_with(s)) return true;
    }
    return false;
}

/* */
void TrieNode::init(const std::unordered_map<std::string_view, TokenType> &table) noexcept {
    for (auto it = table.begin(); it != table.end(); it++) {
        root->insert(std::string(it->first));
    }
}

/* */
void recursive_destroy(TrieNode *node) noexcept {
    for (auto &[_, child] : node->nodes) {
        recursive_destroy(child);
    }

    delete node;
}

/* */
void TrieNode::destroy() noexcept {
    recursive_destroy(root);
}