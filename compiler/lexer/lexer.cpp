/**
 * @file lexer.cpp
 * @brief Implementation of Optic's lexical analyzer.
 *
 * The lexer performs the first stage of the frontend, transforming the source
 * code into a stream of tokens while preserving source locations for accurate
 * diagnostics. Token recognition is delegated to specialized readers for each
 * lexical category (identifiers, literals, operators, comments, etc.).
 *
 * @todo Multiline string literals.
 * @todo Unicode escape sequences.
 * @todo Fill the '// Error' lines
 */

#include "compiler/lexer/lexer.hpp"
#include "compiler/lexer/tables.hpp"
#include "compiler/lexer/trie_operators.hpp"

inline bool is_octal(int c) noexcept { return (c >= '0' && c <= '7'); }

inline bool is_numeric(int c) noexcept {
    return (isdigit(c) || (c == '.') || 
            (c == 'e') || (c == 'E'));
}

inline bool Lexer::is_symbol(const int c) const noexcept {
    return ((c == '(') || (c == ')') ||
            (c == '{') || (c == '}') ||
            (c == '[') || (c == ']') ||
            (c == ';') || (c == ':') ||
            (c == ',') || (c == '.'));
}

inline bool Lexer::is_operator(const int c) const noexcept {
    return ((c == '=') || (c == '+') || (c == '-') ||
            (c == '*') || (c == '%') || (c == '&') ||
            (c == '|') || (c == '~') || (c == '^') ||
            (c == '<') || (c == '>') || (c == '!'));
}

char Lexer::read_escape(int c) const noexcept {
    switch (c) {
        case 'a': return '\a';
        case 'b': return '\b';
        case 't': return '\t';
        case 'n': return '\n';
        case 'f': return '\f';
        case 'r': return '\r';
        case '"': return '\"';
        case '\'': return '\'';
        default: return c;
    }
}


/**
 * @brief Decodes an octal escape sequence.
 *
 * Assumes the lexer is positioned at the first octal digit immediately after
 * the backslash. Consumes the complete sequence, converts it to its byte value
 * and appends the decoded character to the destination string.
 *
 * @param str Destination string receiving the decoded character.
 */
void Lexer::read_octal_escape(std::string &str) noexcept {
    std::string tmp_buf;
    int n, c = get();

    do {
        tmp_buf += c;
    } while ((c = get()) != EOF && is_octal(c));
    --pos;

    if ((n = std::stoi(tmp_buf, nullptr, 8)) > 255) {
        // Error
    }

    str += n;
}

/**
 * @brief Decodes a hexadecimal escape sequence.
 *
 * Assumes the lexer has already consumed the '\x' introducer and is positioned
 * at the first hexadecimal digit. The resulting byte is appended to the output
 * string.
 *
 * @param str Destination string receiving the decoded character.
 */
void Lexer::read_hexadecimal_escape(std::string &str) noexcept {
    std::string tmp_buf;
    short int n, c = get();

    do {
        tmp_buf += c;
    } while ((c = get()) != EOF && isxdigit(c));
    --pos;

    if ((n = std::stoi(tmp_buf, nullptr, 16)) > 255) {
        // Error
    }

   str += n;
}


/**
 * @brief Reads an identifier or reserved keyword.
 *
 * Consumes an identifier according to Optic's lexical grammar and performs a
 * keyword lookup before producing the final token. Reserved words are emitted
 * using their dedicated token types; otherwise an Identifier token is returned.
 */
token_t Lexer::read_identifier() noexcept {
    std::string word;
    size_t _start_col = module->loc().column;
    int c = get();

    do {
        word += c;
        
    } while ((c = get()) != EOF && (isalnum(c) || c == '_'));
    if (c != EOF) --pos;

    if (keyword_table.find(word) != keyword_table.end()) return token_t(keyword_table.at(word), word, module->loc().line, _start_col);
    return token_t(TokenType::Identifier, word, module->loc().line, _start_col);
}


/**
 * @brief Reads a quoted string literal.
 *
 * Supports standard escape sequences together with octal and hexadecimal
 * escapes. If the closing quote is not found before reaching EOF, a diagnostic
 * is emitted and tokenization continues.
 *
 * @note Future extensions:
 *       - Unicode escapes.
 *       - Multiline strings.
 */
token_t Lexer::read_string() noexcept {
    std::string str;
    size_t _start_col = module->loc().column;
    int quote = get(), tmp = get();

    str += tmp;
    module->loc().column += 2;

    while ((tmp = get()) != EOF && tmp != quote) {
        if (tmp == '\\') {
            tmp = get();

            if (is_octal(tmp)) {
                --pos;
                read_octal_escape(str);
                continue;
            }

            if (tmp == 'x') {
                read_hexadecimal_escape(str);
                continue;
            }

            if (tmp == 'u' || tmp == 'U') {
                // TODO
                continue;
            }

            else {
                str += read_escape(tmp);
                module->loc().column += 2;
                continue;
            }
        }

        str += tmp;
    }
   
    if (tmp != quote) {
        diagnostic_engine->report(
            module->loc(),
            "Error: String not closed",
            "Solution: Close the string, append '\"' to the end of the string. Example: \"Hello World\" <- here\n",
            "More information: <...>",
            DiagnosticsLevel::Error
        );
    }

    return token_t(TokenType::String, str, module->loc().line, _start_col);
}


/**
 * @brief Reads a numeric literal.
 *
 * Supports integer literals, decimal floating-point literals and scientific
 * notation. Invalid exponent suffixes are rolled back so the numeric token
 * ends before the malformed exponent.
 */
token_t Lexer::read_number() noexcept {
    std::string number;
    size_t _start_col = (module->loc().column > 0) ? (module->loc().column - 1) : 0;
    bool has_dot = false;
    bool has_exponent = false;
    bool is_float = false;

    int c = get();
    if (c == EOF) {
        return token_t(TokenType::Int, "", module->loc().line, _start_col);
    }

    number += c;

    while (true) {
        int next = peek();

        if (isdigit(next)) {
            number += static_cast<char>(get());
            continue;
        }

        if (!has_dot && !has_exponent && next == '.') {
            has_dot = true;
            is_float = true;
            number += static_cast<char>(get());
            continue;
        }

        if (!has_exponent && (next == 'e' || next == 'E')) {
            size_t exponent_pos = pos;
            unsigned consumed_chars = 1;
            int exponent_char = get();
            number += static_cast<char>(exponent_char);

            int exponent_next = peek();
            if (exponent_next == '+' || exponent_next == '-') {
                consumed_chars++;
                exponent_next = get();
                number += static_cast<char>(exponent_next);
            }

            if (!isdigit(peek())) {
                pos = exponent_pos;
                module->loc().column -= consumed_chars;
                break;
            }

            has_exponent = true;
            is_float = true;
            continue;
        }

        break;
    }

    return token_t(is_float ? TokenType::Float : TokenType::Int, number, module->loc().line, _start_col);
}


/**
 * @brief Reads the longest valid operator beginning at the current position.
 *
 * Operators are recognized using a trie, allowing the lexer to efficiently
 * implement the longest-match rule (e.g. ">>=" is preferred over ">>" or ">").
 */
token_t Lexer::read_operator() noexcept {
    int c;
    size_t _start_col = module->loc().column;

    std::string current;
    std::string longest_match;

    while ((c = get()) != EOF) {
        current += c;

        if (!TrieNode::is_prefix(current, operator_table))
            break;

        if (TrieNode::search(current))
            longest_match = current;
    }

    if (c != EOF) --pos;

    return token_t(operator_table.at(longest_match), longest_match, module->loc().line, _start_col);
}

/**
 * @brief Consumes a source code comment.
 *
 * Supports both single-line and block comments.
 * Unterminated block comments produce a diagnostic before reaching EOF.
 */
void Lexer::ignore_comment() noexcept {
    int c = get();

    if (c == '*') {
        while ((c = get()) != EOF) {
            if (c == '*') {
                if (peek() == '/') {
                    get();
                    return;
                }
            }
        }

        diagnostic_engine->report(
            module->loc(),
            "Error: Block comment not closed, '*/' expected",
            "Solution: Append '*/' to the end of the block comment. Example: /* This is a block comment */ <- here\n",
            "More information: <...>",
            DiagnosticsLevel::Error
        );
    }

    else {
        while ((c = get()) != EOF && c != '\n');
    }
}


/**
 * @brief Performs lexical analysis over the entire source module.
 *
 * Repeatedly dispatches to specialized token readers, skips whitespace and
 * comments, emits lexical diagnostics when necessary and appends an explicit
 * EndOfFile token to terminate the token stream.
 *
 * @note This function constitutes the main entry point of the lexer.
 */
void Lexer::tokenize(std::vector<token_t> &tokens) noexcept {
    TrieNode::init(operator_table);
    int c;

    while ((c = get()) != EOF) {
        if (c == ' ' || c == '\t' || c == '\n') {
            continue;
        }

        if (is_symbol(c)) {
            tokens.emplace_back(
                symbol_table.at(c),
                std::string(1, static_cast<char>(c)),
                module->loc().line,
                module->loc().column
            );
            continue;
        }

        if (c == '\'' || c == '"') {
            --pos;
            tokens.emplace_back(read_string());
            continue;
        }

        if (c == '/') {
            if (peek() == '/' || peek() == '*') {
                ignore_comment();
                continue;
            } else {
                tokens.emplace_back(TokenType::Slash, "/", module->loc().line, module->loc().column);
                continue;
            }
        }
   
        if (is_operator(c)) {
            if (c == '+' || c == '-') {
                if (isdigit(peek())) {
                    --pos;
                    tokens.emplace_back(read_number());
                    continue;
                }
            }
           
            --pos;
            tokens.emplace_back(read_operator());
            continue;
        }

        if (isdigit(c)) {
            --pos;
            tokens.emplace_back(read_number());
            continue;
        }

        if (isalpha(c) || c == '_') {
            if ((c == 'f' || c == 'r') && peek() == '"') {
                tokens.emplace_back(
                    (c == 'f' ? TokenType::FString : TokenType::RawString),
                    (c == 'f' ? "f" : "r"),
                    module->loc().line, module->loc().column
                ); 
                continue;
            }

            --pos;
            tokens.emplace_back(read_identifier());
            continue;
        }
    }

    tokens.emplace_back(TokenType::EndOfFile, "EOF", module->loc().line, module->loc().column);
    TrieNode::destroy();
}

#include <iostream>

/**
 * Converts a TokenType enum value to its corresponding string representation for debugging.
 *
 * @param type The TokenType enum value.
 * @return C-string representing the identifier of the token type.
 */
const char *to_string_token(const TokenType type) {
    switch (type) {
#define X(name) case TokenType::name: return #name;
#include "tokens_types.def"
#undef X
        default: return "Unknown";
    }
}

void Lexer::print_tokens(Module &module) noexcept {
    for (auto &token : module.get_tokens()) {
        std::cout << "Token type: " << to_string_token(token.type) << "\n";
        std::cout << "Token value: '" << token.value << "'\n";
        std::cout << "At line: " << token.line << ", in column: " << token.column << "\n";
        std::cout << "In file: " << module.get_path() << "\n\n";
    }
}