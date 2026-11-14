/** @file macro.cpp
 * @brief Parser and bounded scalar interpreter for Optic macro functions.
 *
 * This grammar is private to preprocessing. It is not the runtime Optic parser.
 */
#include "macro.hpp"
#include "compiler/driver/module.hpp"
#include "compiler/lexer/lexer.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <variant>

namespace optic::compiler::preprocessor {
namespace {
using Token = lexer::token_t;
using Kind = lexer::TokenType;
using Location = driver::SourceLocation;
constexpr std::size_t max_string = 1024 * 1024;
constexpr std::size_t max_tokens = 1000000;

struct VoidValue {
    bool operator==(const VoidValue&) const = default;
};

struct Character {
    std::string text;
    bool operator==(const Character&) const = default;
};
struct Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

struct Value : std::variant<std::monostate,
                            std::int64_t,
                            double,
                            bool,
                            std::string,
                            Character,
                            VoidValue,
                            Array,
                            Object> {
    using variant::variant;
    using variant::operator=;
    bool operator==(const Value&) const = default;
};
enum class Type { Null, Int, Float, Bool, String, Char, Void, Array, Object };

struct TypeSpec {
    Type type;
    bool nullable = false;
    bool operator==(const TypeSpec&) const = default;
};
struct Expr;
using Expression = std::shared_ptr<const Expr>;

struct Expr {
    enum class Form {
        Literal,
        Name,
        Unary,
        Binary,
        Call,
        Format,
        Array,
        Object,
        Member,
        MemberCall,
        Index
    } form;
    Token token;
    Value value;
    std::string name;
    std::vector<Expression> children;
    unsigned height = 1;
};

struct Statement {
    enum class Form { Block, Declare, Assign, Evaluate, Return, If, While } form;
    Token token;
    std::string name;
    TypeSpec type{Type::Void};
    Expression expression;
    std::vector<Statement> children;
};

struct Parameter {
    std::string name;
    TypeSpec type;
};

struct Function {
    Token token;
    std::string name;
    std::vector<Parameter> parameters;
    TypeSpec result{Type::Void};
    Statement body;
    std::string display{};
};

struct Definition {
    std::string name;
    Location location;
    bool nullable_expression = true;
    std::map<std::string, std::vector<Function>> functions;
};

struct Macro {
    std::shared_ptr<const Definition> definition;
    Value value;
    Value expression;
};

Location locate(const std::string& file, const Token& token) {
    return {driver::SourceKind::BufferNoAST, file, "", token.line, token.column};
}

[[noreturn]] void fail(const std::string& file, const Token& token, const std::string& message) {
    throw MacroError(locate(file, token), message);
}

Type type_of(const Value& value) {
    return static_cast<Type>(value.index());
}

std::string type_name(TypeSpec spec) {
    static constexpr const char* names[]{
        "null", "int", "float", "bool", "string", "char", "void", "array", "object"};
    return std::string(names[static_cast<unsigned>(spec.type)]) + (spec.nullable ? "?" : "");
}

bool scalar_character(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    const auto lead = static_cast<unsigned char>(value[0]);
    unsigned count = 1, code = lead;
    if (lead >= 0xc2 && lead <= 0xdf) {
        count = 2;
        code = lead & 0x1f;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        count = 3;
        code = lead & 0x0f;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        count = 4;
        code = lead & 0x07;
    } else if (lead >= 0x80) {
        return false;
    }
    if (value.size() != count) {
        return false;
    }
    for (unsigned i = 1; i < count; ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if ((c & 0xc0) != 0x80) {
            return false;
        }
        code = (code << 6) | (c & 0x3f);
    }
    if ((count == 2 && code < 0x80) || (count == 3 && code < 0x800) ||
        (count == 4 && code < 0x10000)) {
        return false;
    }
    return code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff);
}

std::string quote(const std::string& text, char delimiter = '"') {
    constexpr char digits[] = "0123456789abcdef";
    std::string out(1, delimiter);
    for (const unsigned char c : text) {
        if (c == '\\' || c == delimiter) {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 32 || c == 127) {
            out += "\\x";
            out += digits[c >> 4];
            out += digits[c & 15];
        } else {
            out += static_cast<char>(c);
        }
    }
    out += delimiter;
    return out;
}

std::string text(const Value& value, bool source = false) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "null";
    }
    if (const auto* n = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*n);
    }
    if (const auto* n = std::get_if<double>(&value)) {
        char buffer[128];
        const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), *n);
        if (error != std::errc{}) {
            throw std::runtime_error("Cannot format macro float");
        }
        std::string result(buffer, end);
        if (source && result.find_first_of(".eE") == std::string::npos) {
            result += ".0";
        }
        return result;
    }
    if (const auto* b = std::get_if<bool>(&value)) {
        return *b ? "true" : "false";
    }
    if (const auto* s = std::get_if<std::string>(&value)) {
        return source ? quote(*s) : *s;
    }
    if (std::holds_alternative<VoidValue>(value)) {
        throw std::runtime_error("void has no scalar representation");
    }
    if (std::holds_alternative<Array>(value) || std::holds_alternative<Object>(value)) {
        std::string result = std::holds_alternative<Array>(value) ? "[" : "{";
        auto add = [&](const std::string& part) {
            if (part.size() > max_string || result.size() > max_string - part.size()) {
                throw std::runtime_error("Macro collection representation exceeds 1 MiB");
            }
            result += part;
        };
        bool first = true;
        if (const auto* array = std::get_if<Array>(&value)) {
            for (const auto& item : *array) {
                if (!first) {
                    add(", ");
                }
                first = false;
                add(text(item, true));
            }
        } else {
            for (const auto& [key, item] : std::get<Object>(value)) {
                if (!first) {
                    add(", ");
                }
                first = false;
                add(quote(key) + ": " + text(item, true));
            }
        }
        add(std::holds_alternative<Array>(value) ? "]" : "}");
        return result;
    }
    const auto& c = std::get<Character>(value).text;
    return source ? quote(c, '\'') : c;
}

std::vector<Token> lex_fragment(const std::string& source, const Location& invocation) {
    if (source.size() > max_string) {
        throw MacroError(invocation, "Macro string/source exceeds 1 MiB.");
    }
    driver::Module module(invocation.file);
    module.set_buffer(source);
    driver::DiagnosticsEngine diagnostics;
    std::vector<Token> tokens;
    lexer::Lexer(module, diagnostics).tokenize(tokens);
    if (diagnostics.has_errors()) {
        const auto& first = diagnostics.entries().front();
        throw MacroError(invocation,
                         "Invalid macro source at generated line " +
                             std::to_string(first.loc.line) + ": " + first.msg);
    }
    return tokens;
}

class Nesting {
    unsigned& depth;

public:
    Nesting(unsigned& depth, const std::string& file, const Token& token)
        : depth(depth) {
        if (depth >= 128) {
            fail(file, token, "Macro syntax/evaluation nesting exceeds 128.");
        }
        ++depth;
    }

    ~Nesting() {
        --depth;
    }
};

class Parser {
public:
    const std::vector<Token>& tokens;
    std::size_t cursor;
    std::string file;
    unsigned depth = 0;

    Expression make_node(Expr expr) {
        for (const auto& child : expr.children) {
            expr.height = std::max(expr.height, child->height + 1);
        }
        if (expr.height > 128) {
            fail(file, expr.token, "Macro expression tree exceeds 128 levels.");
        }
        return std::make_shared<Expr>(std::move(expr));
    }

    Parser(const std::vector<Token>& tokens, std::size_t cursor, std::string file)
        : tokens(tokens),
          cursor(cursor),
          file(std::move(file)) {}

    const Token& peek(std::size_t offset = 0) const {
        return tokens[std::min(cursor + offset, tokens.size() - 1)];
    }

    Token take() {
        const auto token = peek();
        if (token.type == Kind::EndOfFile) {
            fail(file, token, "Unexpected EOF in macro.");
        }
        ++cursor;
        return token;
    }

    bool accept(Kind kind) {
        if (peek().type != kind) {
            return false;
        }
        ++cursor;
        return true;
    }

    Token expect(Kind kind, const std::string& message) {
        if (peek().type != kind) {
            fail(file, peek(), message);
        }
        return take();
    }

    void terminator() {
        if (accept(Kind::Semicolon) || peek().type == Kind::RightBrace ||
            peek().type == Kind::EndOfFile) {
            return;
        }
        if (cursor && peek().line > tokens[cursor - 1].line) {
            return;
        }
        fail(file, peek(), "Expected ';' or end of line in macro.");
    }

    static std::optional<Type> type(const Token& token) {
        if (token.type == Kind::Int) {
            return Type::Int;
        }
        if (token.type == Kind::Float) {
            return Type::Float;
        }
        if (token.type == Kind::Bool) {
            return Type::Bool;
        }
        if (token.type == Kind::String) {
            return Type::String;
        }
        if (token.type == Kind::Char) {
            return Type::Char;
        }
        if (token.type == Kind::Array) {
            return Type::Array;
        }
        if (token.type == Kind::Identifier && token.value == "object") {
            return Type::Object;
        }
        if (token.type == Kind::Identifier && token.value == "void") {
            return Type::Void;
        }
        return std::nullopt;
    }

    TypeSpec type_spec(bool allow_void = false) {
        const auto token = take();
        const auto kind = type(token);
        if (!kind || (*kind == Type::Void && !allow_void)) {
            fail(file, token, "Expected an explicit macro type.");
        }
        const bool nullable = accept(Kind::TernaryOp);
        if (*kind == Type::Void && nullable) {
            fail(file, token, "void cannot be nullable.");
        }
        return {*kind, nullable};
    }

    static int precedence(Kind kind) {
        switch (kind) {
            case Kind::Or:
            case Kind::Or_C:
                return 1;
            case Kind::And:
            case Kind::And_C:
                return 2;
            case Kind::BitOr:
                return 3;
            case Kind::BitXor:
                return 4;
            case Kind::BitAnd:
                return 5;
            case Kind::EqualEqual:
            case Kind::NotEqual:
                return 6;
            case Kind::Less:
            case Kind::LessEqual:
            case Kind::Greater:
            case Kind::GreaterEqual:
                return 7;
            case Kind::LeftShift:
            case Kind::RightShift:
                return 8;
            case Kind::Plus:
            case Kind::Minus:
                return 9;
            case Kind::Star:
            case Kind::Slash:
            case Kind::Mod:
                return 10;
            case Kind::Pow:
                return 11;
            default:
                return 0;
        }
    }

    Expression expression(int minimum = 1) {
        Nesting guard(depth, file, peek());
        auto left = unary();
        while (const auto level = precedence(peek().type)) {
            if (level < minimum) {
                break;
            }
            const auto op = take();
            auto right = expression(level + (op.type == Kind::Pow ? 0 : 1));
            left = make_node(Expr{Expr::Form::Binary, op, {}, "", {left, right}});
        }
        return left;
    }

    std::vector<Expression> arguments() {
        expect(Kind::LeftParen, "Expected '(' in macro call.");
        std::vector<Expression> result;
        if (!accept(Kind::RightParen)) {
            do {
                result.push_back(expression());
            } while (accept(Kind::Comma));
            expect(Kind::RightParen, "Expected ')' after macro arguments.");
        }
        return result;
    }

    Expression unary() {
        Nesting guard(depth, file, peek());
        const auto token = take();
        if (token.type == Kind::Plus || token.type == Kind::Minus || token.type == Kind::Not ||
            token.type == Kind::Not_C || token.type == Kind::BitNot) {
            if (token.type == Kind::Minus && peek().type == Kind::IntegerLiteral &&
                peek().value == "9223372036854775808") {
                take();
                return make_node(Expr{
                    Expr::Form::Literal, token, std::numeric_limits<std::int64_t>::min(), "", {}});
            }
            return make_node(Expr{Expr::Form::Unary, token, {}, "", {unary()}});
        }
        if (token.type == Kind::LeftParen) {
            auto value = expression();
            expect(Kind::RightParen, "Expected ')' in macro expression.");
            return postfix(value);
        }
        Expr result{Expr::Form::Literal, token, {}, "", {}};
        if (token.type == Kind::IntegerLiteral) {
            std::int64_t value = 0;
            const auto [end, error] =
                std::from_chars(token.value.data(), token.value.data() + token.value.size(), value);
            if (error != std::errc{} || end != token.value.data() + token.value.size()) {
                fail(file, token, "Macro integer is outside the signed 64-bit range.");
            }
            result.value = value;
        } else if (token.type == Kind::FloatLiteral) {
            double value = 0;
            const auto [end, error] =
                std::from_chars(token.value.data(), token.value.data() + token.value.size(), value);
            if (error != std::errc{} || end != token.value.data() + token.value.size() ||
                !std::isfinite(value)) {
                fail(file, token, "Macro float must be finite and representable.");
            }
            result.value = value;
        } else if (token.type == Kind::LiteralTrue || token.type == Kind::LiteralFalse) {
            result.value = token.type == Kind::LiteralTrue;
        } else if (token.type == Kind::StringLiteral) {
            result.value = token.value;
        } else if (token.type == Kind::CharLiteral) {
            if (!scalar_character(token.value)) {
                fail(file, token, "A macro char must contain one Unicode scalar.");
            }
            result.value = Character{token.value};
        } else if (token.type == Kind::Null) {
            result.value = std::monostate{};
        } else if (token.type == Kind::RawString) {
            result.value = expect(Kind::StringLiteral, "Expected raw string contents.").value;
        } else if (token.type == Kind::FString) {
            result.form = Expr::Form::Format;
            const auto content = expect(Kind::StringLiteral, "Expected formatted string contents.");
            parse_format(result, content);
        } else if (token.type == Kind::Identifier || type(token)) {
            result.form = Expr::Form::Name;
            result.name = token.value;
            if (peek().type == Kind::LeftParen) {
                result.form = Expr::Form::Call;
                result.children = arguments();
            }
        } else if (token.type == Kind::LeftBracket || token.type == Kind::LeftBrace) {
            const bool object = token.type == Kind::LeftBrace;
            result.form = object ? Expr::Form::Object : Expr::Form::Array;
            const auto close = object ? Kind::RightBrace : Kind::RightBracket;
            if (!accept(close)) {
                do {
                    if (object) {
                        const auto key = take();
                        if (key.type != Kind::Identifier && key.type != Kind::StringLiteral) {
                            fail(file, key, "Object keys must be identifiers or strings.");
                        }
                        result.children.push_back(
                            make_node(Expr{Expr::Form::Literal, key, key.value, "", {}}));
                        expect(Kind::Colon, "Expected ':' after object key.");
                    }
                    result.children.push_back(expression());
                } while (accept(Kind::Comma) && peek().type != close);
                expect(close, "Unclosed macro collection.");
            }
        } else {
            fail(file, token, "Expected a macro expression.");
        }
        return postfix(make_node(std::move(result)));
    }

    Expression postfix(Expression base) {
        while (true) {
            if (accept(Kind::Point)) {
                auto name = expect(Kind::Identifier, "Expected member name.");
                Expr node{Expr::Form::Member, name, {}, name.value, {base}};
                if (peek().type == Kind::LeftParen) {
                    node.form = Expr::Form::MemberCall;
                    const auto args = arguments();
                    node.children.insert(node.children.end(), args.begin(), args.end());
                }
                base = make_node(std::move(node));
            } else if (accept(Kind::LeftBracket)) {
                auto index = expression();
                const auto close = expect(Kind::RightBracket, "Expected ']' after index.");
                base = make_node(Expr{Expr::Form::Index, close, {}, "", {base, index}});
            } else {
                break;
            }
        }
        return base;
    }

    void parse_format(Expr& format, const Token& content) {
        // Alternate literal and expression nodes; doubled braces are literal braces.
        std::string literal;
        auto flush = [&] {
            format.children.push_back(
                make_node(Expr{Expr::Form::Literal, content, literal, "", {}}));
            literal.clear();
        };
        const auto& value = content.value;
        for (std::size_t i = 0; i < value.size();) {
            if ((value[i] == '{' || value[i] == '}') && i + 1 < value.size() &&
                value[i] == value[i + 1]) {
                literal += value[i];
                i += 2;
                continue;
            }
            if (value[i] == '}') {
                fail(file, content, "Unmatched '}' in macro formatted string.");
            }
            if (value[i] != '{') {
                literal += value[i++];
                continue;
            }
            flush();
            const auto start = ++i;
            char quote_char = 0;
            bool escape = false;
            unsigned nesting = 0;
            for (; i < value.size(); ++i) {
                const auto c = value[i];
                if (quote_char) {
                    if (escape) {
                        escape = false;
                    } else if (c == '\\') {
                        escape = true;
                    } else if (c == quote_char) {
                        quote_char = 0;
                    }
                } else if (c == '"' || c == '\'') {
                    quote_char = c;
                } else if (c == '{') {
                    ++nesting;
                } else if (c == '}') {
                    if (nesting == 0) {
                        break;
                    }
                    --nesting;
                }
            }
            if (i == value.size()) {
                fail(file, content, "Unclosed macro interpolation.");
            }
            auto tokens = lex_fragment(value.substr(start, i - start), locate(file, content));
            for (auto& token : tokens) {
                token.line = content.line;
                token.column = content.column;
            }
            Parser parser(tokens, 0, file);
            parser.depth = depth;
            auto expr = parser.expression();
            if (parser.peek().type != Kind::EndOfFile) {
                fail(file, content, "Unexpected tokens in macro interpolation.");
            }
            format.children.push_back(std::move(expr));
            ++i;
        }
        flush();
    }

    Statement block() {
        Nesting guard(depth, file, peek());
        const auto open = expect(Kind::LeftBrace, "Expected '{' before macro function body.");
        Statement result{Statement::Form::Block, open, "", {Type::Void}, {}, {}};
        while (!accept(Kind::RightBrace)) {
            result.children.push_back(statement());
        }
        return result;
    }

    Statement statement() {
        Nesting guard(depth, file, peek());
        if (peek().type == Kind::LeftBrace) {
            return block();
        }
        const auto token = peek();
        if (accept(Kind::Semicolon)) {
            return {Statement::Form::Block, token, "", {Type::Void}, {}, {}};
        }
        if (accept(Kind::Return)) {
            Expression value;
            if (peek().type != Kind::Semicolon && peek().type != Kind::RightBrace &&
                peek().line == token.line) {
                value = expression();
            }
            terminator();
            return {Statement::Form::Return, token, "", {Type::Void}, value, {}};
        }
        if (accept(Kind::If)) {
            return conditional(token);
        }
        if (accept(Kind::WhileLoop)) {
            auto condition = expression();
            auto body = block();
            return {Statement::Form::While, token, "", {Type::Void}, condition, {std::move(body)}};
        }
        if (type(peek())) {
            const auto spec = type_spec();
            const auto name = expect(Kind::Identifier, "Expected a name after macro local type.");
            expect(Kind::Equal, "Macro locals require an initializer.");
            auto value = expression();
            terminator();
            return {Statement::Form::Declare, name, name.value, spec, value, {}};
        }
        if (peek().type == Kind::Identifier &&
            (peek(1).type == Kind::Equal || peek(1).type == Kind::PlusAssign ||
             peek(1).type == Kind::MinusAssign || peek(1).type == Kind::MulAssign ||
             peek(1).type == Kind::DivAssign || peek(1).type == Kind::ModAssign)) {
            const auto name = take();
            const auto op = take();
            auto value = expression();
            if (op.type != Kind::Equal) {
                auto lhs = make_node(Expr{Expr::Form::Name, name, {}, name.value, {}});
                auto operation = op;
                switch (op.type) {
                    case Kind::PlusAssign:
                        operation.type = Kind::Plus;
                        break;
                    case Kind::MinusAssign:
                        operation.type = Kind::Minus;
                        break;
                    case Kind::MulAssign:
                        operation.type = Kind::Star;
                        break;
                    case Kind::DivAssign:
                        operation.type = Kind::Slash;
                        break;
                    default:
                        operation.type = Kind::Mod;
                        break;
                }
                value = make_node(Expr{Expr::Form::Binary, operation, {}, "", {lhs, value}});
            }
            terminator();
            return {Statement::Form::Assign, name, name.value, {Type::Void}, value, {}};
        }
        auto value = expression();
        terminator();
        return {Statement::Form::Evaluate, token, "", {Type::Void}, value, {}};
    }

    Statement conditional(const Token& token) {
        Nesting guard(depth, file, token);
        auto condition = expression();
        std::vector<Statement> branches;
        branches.push_back(block());
        if (accept(Kind::Elif)) {
            branches.push_back(conditional(tokens[cursor - 1]));
        } else if (accept(Kind::Else)) {
            branches.push_back(block());
        }
        return {Statement::Form::If, token, "", {Type::Void}, condition, std::move(branches)};
    }

    Function function() {
        const auto start = cursor;
        take(); // 'fn' is contextual, not a lexer keyword.
        const auto name = expect(Kind::Identifier, "Expected macro function name after 'fn'.");
        Function result{name,
                        name.value,
                        {},
                        {Type::Void},
                        {Statement::Form::Block, name, "", {Type::Void}, {}, {}}};
        expect(Kind::LeftParen, "Expected '(' after macro function name.");
        std::set<std::string> names;
        if (!accept(Kind::RightParen)) {
            do {
                const auto type = type_spec();
                const auto parameter =
                    expect(Kind::Identifier, "Expected parameter name after its type.");
                if (parameter.value == "value" || parameter.value == "expression" ||
                    !names.insert(parameter.value).second) {
                    fail(file, parameter, "Duplicate or reserved macro parameter name.");
                }
                result.parameters.push_back({parameter.value, type});
            } while (accept(Kind::Comma));
            expect(Kind::RightParen, "Expected ')' after macro parameters.");
        }
        if (accept(Kind::Arrow)) {
            result.result = type_spec(true);
        }
        if (name.value == "__repr__") {
            fail(file, name, "__repr__ is a built-in read-only macro function.");
        }
        if (name.value == "to_string" && result.result != TypeSpec{Type::String}) {
            fail(file, name, "to_string overloads must explicitly return string.");
        }
        result.body = block();
        unsigned line = tokens[start].line;
        for (auto i = start; i < cursor; ++i) {
            const auto& token = tokens[i];
            while (line < token.line) {
                result.display += '\n';
                ++line;
            }
            if (i != start && tokens[i - 1].type != Kind::FString &&
                tokens[i - 1].type != Kind::RawString) {
                result.display += ' ';
            }
            result.display += token.type == Kind::StringLiteral ? quote(token.value)
                              : token.type == Kind::CharLiteral ? quote(token.value, '\'')
                                                                : token.value;
        }
        return result;
    }
};

struct Budget {
    std::size_t steps = 0;
    unsigned depth = 0;
    unsigned calls = 0;

    void step(const std::string& file, const Token& token) {
        if (++steps > 100000) {
            fail(file, token, "Macro evaluation exceeds 100000 steps.");
        }
    }
};

struct Binding {
    TypeSpec type;
    Value value;
};

struct Frame {
    Macro& macro;
    std::vector<std::map<std::string, Binding>> scopes{{}};

    const std::string& file() const {
        return macro.definition->location.file;
    }
};

struct Returned {
    Value value;
};

class Evaluator {
    std::map<std::string, Macro>& macros;
    Budget& budget;
    std::string invocation_file;

    const std::string& file(Frame* frame) const {
        return frame ? frame->file() : invocation_file;
    }

    [[noreturn]] void error(Frame* frame, const Token& token, const std::string& message) const {
        fail(file(frame), token, message);
    }

    bool boolean(const Value& value, Frame* frame, const Token& token) const {
        if (const auto* b = std::get_if<bool>(&value)) {
            return *b;
        }
        error(frame, token, "Macro condition/logical operand must be bool.");
    }

    std::int64_t integer(const Value& value, Frame* frame, const Token& token) const {
        if (const auto* n = std::get_if<std::int64_t>(&value)) {
            return *n;
        }
        error(frame, token, "Macro operator requires int operands.");
    }

    double number(const Value& value, Frame* frame, const Token& token) const {
        if (const auto* n = std::get_if<double>(&value)) {
            return *n;
        }
        if (const auto* n = std::get_if<std::int64_t>(&value)) {
            return static_cast<double>(*n);
        }
        error(frame, token, "Macro operator requires numeric operands.");
    }

    Value finite(double value, Frame* frame, const Token& token) const {
        if (!std::isfinite(value)) {
            error(frame, token, "Macro floating-point result is not finite.");
        }
        return value;
    }

    std::string
    append(std::string lhs, const std::string& rhs, Frame* frame, const Token& token) const {
        if (rhs.size() > max_string || lhs.size() > max_string - rhs.size()) {
            error(frame, token, "Macro string/source exceeds 1 MiB.");
        }
        lhs += rhs;
        return lhs;
    }

    std::int64_t
    arithmetic(Kind op, std::int64_t a, std::int64_t b, Frame* frame, const Token& token) const {
        constexpr auto low = std::numeric_limits<std::int64_t>::min();
        constexpr auto high = std::numeric_limits<std::int64_t>::max();
        auto overflow = [&]() -> std::int64_t { error(frame, token, "Macro integer overflow."); };
        switch (op) {
            case Kind::Plus:
                if ((b > 0 && a > high - b) || (b < 0 && a < low - b)) {
                    return overflow();
                }
                return a + b;
            case Kind::Minus:
                if ((b > 0 && a < low + b) || (b < 0 && a > high + b)) {
                    return overflow();
                }
                return a - b;
            case Kind::Star:
                if (a == 0 || b == 0) {
                    return 0;
                }
                if (a > 0 ? (b > 0 ? a > high / b : b < low / a)
                          : (b > 0 ? a < low / b : a < high / b)) {
                    return overflow();
                }
                return a * b;
            case Kind::Slash:
            case Kind::Mod:
                if (b == 0) {
                    error(frame, token, "Division by zero in macro.");
                }
                if (a == low && b == -1) {
                    return overflow();
                }
                return op == Kind::Slash ? a / b : a % b;
            default:
                error(frame, token, "Unsupported macro integer operation.");
        }
    }

    Binding* binding(Frame* frame, const std::string& name) const {
        if (!frame) {
            return nullptr;
        }
        for (auto it = frame->scopes.rbegin(); it != frame->scopes.rend(); ++it) {
            if (const auto found = it->find(name); found != it->end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    Macro& named_macro(const std::string& name, Frame* frame, const Token& token) {
        if (frame && frame->macro.definition->name == name) {
            return frame->macro;
        }
        const auto it = macros.find(name);
        if (it == macros.end()) {
            error(frame, token, "Unknown macro '" + name + "'.");
        }
        return it->second;
    }

    Value lookup(const Expr& expr, Frame* frame) {
        const auto dot = expr.name.find('.');
        if (dot != std::string::npos) {
            auto& macro = named_macro(expr.name.substr(0, dot), frame, expr.token);
            const auto field = expr.name.substr(dot + 1);
            if (field == "value") {
                return macro.value;
            }
            if (field == "expression") {
                return macro.expression;
            }
            error(frame, expr.token, "Unknown macro field '" + field + "'.");
        }
        if (const auto* found = binding(frame, expr.name)) {
            return found->value;
        }
        if (frame && expr.name == "value") {
            return frame->macro.value;
        }
        if (frame && expr.name == "expression") {
            return frame->macro.expression;
        }
        error(frame, expr.token, "Unknown macro variable '" + expr.name + "'.");
    }

    Value convert(const std::string& name,
                  const std::vector<Value>& args,
                  Frame* frame,
                  const Token& token) {
        if (args.size() != 1 || type_of(args.front()) == Type::Void) {
            error(frame, token, "Scalar conversion requires exactly one value.");
        }
        const auto& arg = args.front();
        const auto source_type = type_of(arg);
        if (name == "string") {
            return text(arg);
        }
        if (name == "char") {
            if (source_type == Type::Char) {
                return arg;
            }
            if (const auto* s = std::get_if<std::string>(&arg)) {
                if (scalar_character(*s)) {
                    return Character{*s};
                }
            }
            error(frame, token, "char conversion requires one Unicode scalar.");
        }
        if (name == "bool") {
            if (source_type == Type::Bool) {
                return arg;
            }
            if (source_type == Type::Int) {
                return std::get<std::int64_t>(arg) != 0;
            }
            if (source_type == Type::Float) {
                return std::get<double>(arg) != 0;
            }
            if (const auto* s = std::get_if<std::string>(&arg)) {
                if (*s == "true") {
                    return true;
                }
                if (*s == "false") {
                    return false;
                }
            }
            error(frame, token, "Invalid explicit bool conversion.");
        }
        if (name == "int") {
            if (source_type == Type::Int) {
                return arg;
            }
            if (source_type == Type::Bool) {
                return static_cast<std::int64_t>(std::get<bool>(arg));
            }
            if (source_type == Type::Float) {
                const auto n = std::get<double>(arg);
                if (n < -0x1p63 || n >= 0x1p63) {
                    error(frame, token, "Float-to-int conversion is out of range.");
                }
                return static_cast<std::int64_t>(n);
            }
            if (const auto* s = std::get_if<std::string>(&arg)) {
                std::int64_t n = 0;
                const auto [end, status] = std::from_chars(s->data(), s->data() + s->size(), n);
                if (status == std::errc{} && end == s->data() + s->size()) {
                    return n;
                }
            }
            error(frame, token, "Invalid explicit int conversion.");
        }
        if (name == "float") {
            if (source_type == Type::Float) {
                return arg;
            }
            if (source_type == Type::Int) {
                return static_cast<double>(std::get<std::int64_t>(arg));
            }
            if (source_type == Type::Bool) {
                return std::get<bool>(arg) ? 1.0 : 0.0;
            }
            if (const auto* s = std::get_if<std::string>(&arg)) {
                double n = 0;
                const auto [end, status] = std::from_chars(s->data(), s->data() + s->size(), n);
                if (status == std::errc{} && end == s->data() + s->size()) {
                    return finite(n, frame, token);
                }
            }
            error(frame, token, "Invalid explicit float conversion.");
        }
        error(frame, token, "Unknown scalar conversion.");
    }

    Value binary(const Expr& expr, Frame* frame) {
        const auto lhs = eval(expr.children[0], frame);
        const auto op = expr.token.type;
        if (op == Kind::And || op == Kind::And_C) {
            if (!boolean(lhs, frame, expr.token)) {
                return false;
            }
            return boolean(eval(expr.children[1], frame), frame, expr.token);
        }
        if (op == Kind::Or || op == Kind::Or_C) {
            if (boolean(lhs, frame, expr.token)) {
                return true;
            }
            return boolean(eval(expr.children[1], frame), frame, expr.token);
        }
        const auto rhs = eval(expr.children[1], frame);
        if (type_of(lhs) == Type::Void || type_of(rhs) == Type::Void) {
            error(frame, expr.token, "void cannot be used as a scalar macro value.");
        }
        const bool numbers = (type_of(lhs) == Type::Int || type_of(lhs) == Type::Float) &&
                             (type_of(rhs) == Type::Int || type_of(rhs) == Type::Float);
        if (op == Kind::EqualEqual || op == Kind::NotEqual) {
            const bool equal =
                numbers && (type_of(lhs) != type_of(rhs))
                    ? number(lhs, frame, expr.token) == number(rhs, frame, expr.token)
                    : lhs == rhs;
            return op == Kind::EqualEqual ? equal : !equal;
        }
        if (op == Kind::Less || op == Kind::LessEqual || op == Kind::Greater ||
            op == Kind::GreaterEqual) {
            auto compare = [op](const auto& a, const auto& b) {
                if (op == Kind::Less) {
                    return a < b;
                }
                if (op == Kind::LessEqual) {
                    return a <= b;
                }
                if (op == Kind::Greater) {
                    return a > b;
                }
                return a >= b;
            };
            if (type_of(lhs) == Type::Int && type_of(rhs) == Type::Int) {
                return compare(std::get<std::int64_t>(lhs), std::get<std::int64_t>(rhs));
            }
            if (numbers) {
                return compare(number(lhs, frame, expr.token), number(rhs, frame, expr.token));
            }
            if (type_of(lhs) == Type::String && type_of(rhs) == Type::String) {
                return compare(std::get<std::string>(lhs), std::get<std::string>(rhs));
            }
            if (type_of(lhs) == Type::Char && type_of(rhs) == Type::Char) {
                return compare(std::get<Character>(lhs).text, std::get<Character>(rhs).text);
            }
            error(frame, expr.token, "Incompatible macro comparison operands.");
        }
        if (op == Kind::Plus && type_of(lhs) == Type::String && type_of(rhs) == Type::String) {
            return append(
                std::get<std::string>(lhs), std::get<std::string>(rhs), frame, expr.token);
        }
        if (op == Kind::BitAnd || op == Kind::BitOr || op == Kind::BitXor ||
            op == Kind::LeftShift || op == Kind::RightShift) {
            auto a = integer(lhs, frame, expr.token);
            const auto b = integer(rhs, frame, expr.token);
            if (op == Kind::BitAnd) {
                return a & b;
            }
            if (op == Kind::BitOr) {
                return a | b;
            }
            if (op == Kind::BitXor) {
                return a ^ b;
            }
            if (b < 0 || b >= 64) {
                error(frame, expr.token, "Macro shift count must be in [0, 63].");
            }
            if (op == Kind::RightShift) {
                return a >> b;
            }
            for (std::int64_t i = 0; i < b; ++i) {
                a = arithmetic(Kind::Star, a, 2, frame, expr.token);
            }
            return a;
        }
        if (op == Kind::Pow && type_of(lhs) == Type::Int && type_of(rhs) == Type::Int &&
            std::get<std::int64_t>(rhs) >= 0) {
            auto a = std::get<std::int64_t>(lhs), b = std::get<std::int64_t>(rhs);
            std::int64_t result = 1;
            while (b) {
                if (b & 1) {
                    result = arithmetic(Kind::Star, result, a, frame, expr.token);
                }
                b >>= 1;
                if (b) {
                    a = arithmetic(Kind::Star, a, a, frame, expr.token);
                }
            }
            return result;
        }
        if (numbers && type_of(lhs) == Type::Int && type_of(rhs) == Type::Int && op != Kind::Pow) {
            return arithmetic(
                op, std::get<std::int64_t>(lhs), std::get<std::int64_t>(rhs), frame, expr.token);
        }
        const auto a = number(lhs, frame, expr.token), b = number(rhs, frame, expr.token);
        switch (op) {
            case Kind::Plus:
                return finite(a + b, frame, expr.token);
            case Kind::Minus:
                return finite(a - b, frame, expr.token);
            case Kind::Star:
                return finite(a * b, frame, expr.token);
            case Kind::Slash:
                if (b == 0) {
                    error(frame, expr.token, "Division by zero in macro.");
                }
                return finite(a / b, frame, expr.token);
            case Kind::Pow:
                return finite(std::pow(a, b), frame, expr.token);
            default:
                error(frame, expr.token, "Unsupported macro binary operation.");
        }
    }

    Value call(const Expr& expr, Frame* frame) {
        std::vector<Value> arguments;
        for (const auto& child : expr.children) {
            auto arg = eval(child, frame);
            if (type_of(arg) == Type::Void) {
                error(frame, expr.token, "void cannot be a macro argument.");
            }
            arguments.push_back(std::move(arg));
        }
        const auto dot = expr.name.find('.');
        if (dot == std::string::npos &&
            (expr.name == "int" || expr.name == "float" || expr.name == "bool" ||
             expr.name == "string" || expr.name == "char")) {
            return convert(expr.name, arguments, frame, expr.token);
        }
        Macro* target = frame ? &frame->macro : nullptr;
        std::string method = expr.name;
        if (dot != std::string::npos) {
            target = &named_macro(expr.name.substr(0, dot), frame, expr.token);
            method = expr.name.substr(dot + 1);
        }
        if (!target) {
            error(frame, expr.token, "Macro argument calls require an explicit macro member.");
        }
        return invoke(*target, method, arguments, expr.token, file(frame));
    }

    Value collection(Value value, Frame* frame, const Token& token) {
        std::vector<std::pair<const Value*, unsigned>> pending{{&value, 1}};
        while (!pending.empty()) {
            const auto [item, depth] = pending.back();
            pending.pop_back();
            budget.step(file(frame), token);
            if (depth > 128) {
                error(frame, token, "Macro value nesting exceeds 128.");
            }
            if (type_of(*item) == Type::Void) {
                error(frame, token, "Collections cannot contain void.");
            }
            if (const auto* a = std::get_if<Array>(item)) {
                for (const auto& child : *a) {
                    pending.emplace_back(&child, depth + 1);
                }
            }
            if (const auto* o = std::get_if<Object>(item)) {
                for (const auto& [key, child] : *o) {
                    pending.emplace_back(&child, depth + 1);
                }
            }
        }
        return value;
    }

    Value member(const Expr& expr, Frame* frame) {
        const auto& receiver = expr.children[0];
        std::vector<Value> args;
        for (std::size_t i = 1; i < expr.children.size(); ++i) {
            args.push_back(eval(expr.children[i], frame));
        }
        const bool call = expr.form == Expr::Form::MemberCall;
        if (receiver->form == Expr::Form::Name && macros.contains(receiver->name)) {
            auto& macro = named_macro(receiver->name, frame, expr.token);
            if (call) {
                return invoke(macro, expr.name, args, expr.token, file(frame));
            }
            if (expr.name == "value") {
                return macro.value;
            }
            if (expr.name == "expression") {
                return macro.expression;
            }
            error(frame, expr.token, "Macro methods require '()'.");
        }
        auto value = eval(receiver, frame);
        if (!call) {
            if (const auto* object = std::get_if<Object>(&value)) {
                if (const auto it = object->find(expr.name); it != object->end()) {
                    return it->second;
                }
            }
            if (expr.name == "length") {
                if (const auto* a = std::get_if<Array>(&value)) {
                    return static_cast<std::int64_t>(a->size());
                }
                if (const auto* o = std::get_if<Object>(&value)) {
                    return static_cast<std::int64_t>(o->size());
                }
                if (const auto* t = std::get_if<std::string>(&value)) {
                    return static_cast<std::int64_t>(std::count_if(
                        t->begin(), t->end(), [](unsigned char c) { return (c & 0xc0) != 0x80; }));
                }
            }
        } else if (expr.name == "sorted" && args.empty() && std::holds_alternative<Array>(value)) {
            auto result = std::get<Array>(value);
            for (const auto& item : result) {
                if (type_of(item) != Type::Int) {
                    error(frame, expr.token, "sorted currently requires an int array.");
                }
            }
            std::sort(result.begin(), result.end(), [](const Value& a, const Value& b) {
                return std::get<std::int64_t>(a) < std::get<std::int64_t>(b);
            });
            return result;
        } else if ((expr.name == "upper" || expr.name == "lower") && args.empty() &&
                   std::holds_alternative<std::string>(value)) {
            auto result = std::get<std::string>(value);
            for (auto& c : result) {
                if (expr.name == "upper" && c >= 'a' && c <= 'z') {
                    c -= 32;
                } else if (expr.name == "lower" && c >= 'A' && c <= 'Z') {
                    c += 32;
                }
            }
            return result;
        }
        error(frame,
              expr.token,
              "Unknown object member or incompatible method arguments: '" + expr.name + "'.");
    }

    void assign(Frame& frame, const Statement& statement, Value value) {
        if (type_of(value) == Type::Void) {
            error(&frame, statement.token, "Cannot assign void in a macro.");
        }
        if (auto* local = binding(&frame, statement.name)) {
            local->value = typed(std::move(value), local->type, frame.file(), statement.token);
        } else if (statement.name == "value") {
            frame.macro.value = std::move(value);
        } else if (statement.name == "expression") {
            frame.macro.expression =
                typed(std::move(value),
                      {Type::String, frame.macro.definition->nullable_expression},
                      frame.file(),
                      statement.token);
        } else {
            error(&frame,
                  statement.token,
                  "Assignment to undeclared macro variable '" + statement.name + "'.");
        }
    }

    std::optional<Returned> execute(const Statement& statement, Frame& frame) {
        budget.step(frame.file(), statement.token);
        Nesting guard(budget.depth, frame.file(), statement.token);
        switch (statement.form) {
            case Statement::Form::Block: {
                frame.scopes.emplace_back();
                for (const auto& child : statement.children) {
                    if (auto returned = execute(child, frame)) {
                        frame.scopes.pop_back();
                        return returned;
                    }
                }
                frame.scopes.pop_back();
                break;
            }
            case Statement::Form::Declare: {
                if (statement.name == "value" || statement.name == "expression" ||
                    frame.scopes.back().contains(statement.name)) {
                    error(&frame, statement.token, "Duplicate or reserved macro local name.");
                }
                auto value = typed(eval(statement.expression, &frame),
                                   statement.type,
                                   frame.file(),
                                   statement.token);
                frame.scopes.back().emplace(statement.name,
                                            Binding{statement.type, std::move(value)});
                break;
            }
            case Statement::Form::Assign:
                assign(frame, statement, eval(statement.expression, &frame));
                break;
            case Statement::Form::Evaluate:
                eval(statement.expression, &frame);
                break;
            case Statement::Form::Return:
                return Returned{statement.expression ? eval(statement.expression, &frame)
                                                     : Value{VoidValue{}}};
            case Statement::Form::If:
                if (boolean(eval(statement.expression, &frame), &frame, statement.token)) {
                    return execute(statement.children[0], frame);
                }
                if (statement.children.size() == 2) {
                    return execute(statement.children[1], frame);
                }
                break;
            case Statement::Form::While:
                while (boolean(eval(statement.expression, &frame), &frame, statement.token)) {
                    if (auto returned = execute(statement.children[0], frame)) {
                        return returned;
                    }
                }
                break;
        }
        return std::nullopt;
    }

public:
    Evaluator(std::map<std::string, Macro>& macros, Budget& budget, std::string file)
        : macros(macros),
          budget(budget),
          invocation_file(std::move(file)) {}

    static int match(const Value& value, TypeSpec expected) {
        const auto actual = type_of(value);
        if (actual == Type::Null) {
            return expected.nullable ? 1 : -1;
        }
        if (actual == expected.type) {
            return expected.nullable ? 1 : 0;
        }
        if (actual == Type::Int && expected.type == Type::Float) {
            return expected.nullable ? 3 : 2;
        }
        return -1;
    }

    static Value
    typed(Value value, TypeSpec expected, const std::string& file, const Token& token) {
        if (expected == TypeSpec{Type::Void} && type_of(value) == Type::Void) {
            return value;
        }
        if (match(value, expected) < 0) {
            fail(file,
                 token,
                 "Expected macro type " + type_name(expected) + ", got " +
                     type_name({type_of(value)}) + ".");
        }
        if (type_of(value) == Type::Int && expected.type == Type::Float) {
            return static_cast<double>(std::get<std::int64_t>(value));
        }
        return value;
    }

    Value eval(const Expression& expression, Frame* frame = nullptr) {
        const auto& expr = *expression;
        budget.step(file(frame), expr.token);
        Nesting guard(budget.depth, file(frame), expr.token);
        switch (expr.form) {
            case Expr::Form::Literal:
                if (const auto* s = std::get_if<std::string>(&expr.value);
                    s && s->size() > max_string) {
                    error(frame, expr.token, "Macro string/source exceeds 1 MiB.");
                }
                return expr.value;
            case Expr::Form::Member:
            case Expr::Form::MemberCall:
                return member(expr, frame);
            case Expr::Form::Array: {
                Array result;
                for (const auto& child : expr.children) {
                    result.push_back(eval(child, frame));
                }
                return collection(std::move(result), frame, expr.token);
            }
            case Expr::Form::Object: {
                Object result;
                for (std::size_t i = 0; i < expr.children.size(); i += 2) {
                    const auto key = std::get<std::string>(eval(expr.children[i], frame));
                    if (result.contains(key)) {
                        error(frame, expr.token, "Duplicate object key.");
                    }
                    result.emplace(key, eval(expr.children[i + 1], frame));
                }
                return collection(std::move(result), frame, expr.token);
            }
            case Expr::Form::Index: {
                const auto base = eval(expr.children[0], frame),
                           index = eval(expr.children[1], frame);
                if (const auto* a = std::get_if<Array>(&base)) {
                    const auto n = integer(index, frame, expr.token);
                    if (n < 0 || static_cast<std::size_t>(n) >= a->size()) {
                        error(frame, expr.token, "Array index out of range.");
                    }
                    return (*a)[n];
                }
                if (const auto* o = std::get_if<Object>(&base);
                    o && std::holds_alternative<std::string>(index)) {
                    const auto it = o->find(std::get<std::string>(index));
                    if (it != o->end()) {
                        return it->second;
                    }
                }
                error(frame, expr.token, "Invalid object index.");
            }
            case Expr::Form::Name:
                return lookup(expr, frame);
            case Expr::Form::Binary:
                return binary(expr, frame);
            case Expr::Form::Call:
                return call(expr, frame);
            case Expr::Form::Unary: {
                const auto value = eval(expr.children[0], frame);
                if (expr.token.type == Kind::Not || expr.token.type == Kind::Not_C) {
                    return !boolean(value, frame, expr.token);
                }
                if (expr.token.type == Kind::BitNot) {
                    return ~integer(value, frame, expr.token);
                }
                if (type_of(value) == Type::Int) {
                    return expr.token.type == Kind::Minus
                               ? arithmetic(Kind::Minus,
                                            0,
                                            std::get<std::int64_t>(value),
                                            frame,
                                            expr.token)
                               : value;
                }
                const auto n = number(value, frame, expr.token);
                return expr.token.type == Kind::Minus ? -n : n;
            }
            case Expr::Form::Format: {
                std::string result;
                for (const auto& child : expr.children) {
                    const auto value = eval(child, frame);
                    if (type_of(value) == Type::Void) {
                        error(frame, expr.token, "Cannot interpolate void.");
                    }
                    result = append(std::move(result), text(value), frame, expr.token);
                }
                return result;
            }
        }
        error(frame, expr.token, "Invalid macro expression node.");
    }

    std::string representation(const Macro& macro) const {
        std::string result;
        auto add = [&](const std::string& piece) {
            if (piece.size() > max_string || result.size() > max_string - piece.size()) {
                throw MacroError(macro.definition->location, "Macro representation exceeds 1 MiB.");
            }
            result += piece;
        };
        add("macro " + macro.definition->name + " { value: ");
        add(text(macro.value, true));
        add("; expression: ");
        add(text(macro.expression, true));
        add("; functions = [");
        bool first = true;
        for (const auto& [name, overloads] : macro.definition->functions) {
            for (const auto& function : overloads) {
                if (!first) {
                    add(", ");
                }
                first = false;
                add(name + "(");
                for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                    if (i) {
                        add(", ");
                    }
                    add(type_name(function.parameters[i].type));
                }
                add(") -> " + type_name(function.result));
            }
        }
        add("] }");
        return result;
    }

    Value invoke(Macro& macro,
                 const std::string& name,
                 const std::vector<Value>& args,
                 const Token& token,
                 const std::string& caller_file) {
        budget.step(caller_file, token);
        if (name == "__repr__") {
            if (!args.empty()) {
                fail(caller_file, token, "__repr__ takes no arguments.");
            }
            auto result = representation(macro);
            if (result.size() > max_string) {
                fail(caller_file, token, "Macro representation exceeds 1 MiB.");
            }
            return result;
        }
        const Function* selected = nullptr;
        int best = std::numeric_limits<int>::max();
        bool ambiguous = false;
        if (const auto it = macro.definition->functions.find(name);
            it != macro.definition->functions.end()) {
            for (const auto& candidate : it->second) {
                if (candidate.parameters.size() != args.size()) {
                    continue;
                }
                int score = 0;
                bool valid = true;
                for (std::size_t i = 0; i < args.size(); ++i) {
                    const auto part = match(args[i], candidate.parameters[i].type);
                    if (part < 0) {
                        valid = false;
                        break;
                    }
                    score += part;
                }
                if (!valid) {
                    continue;
                }
                if (score < best) {
                    selected = &candidate;
                    best = score;
                    ambiguous = false;
                } else if (score == best) {
                    ambiguous = true;
                }
            }
        }
        if (!selected && name == "to_string" && args.empty()) {
            const auto prefix = std::get_if<std::string>(&macro.expression);
            auto source = text(macro.value, true);
            if (!prefix) {
                return source;
            }
            // Keep identifier-like prefix/value boundaries from fusing ("return" + "1").
            std::string result = *prefix;
            auto word = [](unsigned char c) { return std::isalnum(c) || c == '_'; };
            if (!result.empty() && !source.empty() && word(result.back()) && word(source.front())) {
                result += ' ';
            }
            return append(std::move(result), source, nullptr, token);
        }
        if (!selected || ambiguous) {
            fail(caller_file,
                 token,
                 std::string(ambiguous ? "Ambiguous" : "No matching") + " macro overload '" +
                     macro.definition->name + "." + name + "'.");
        }
        if (budget.calls >= 64) {
            fail(caller_file, token, "Macro function call depth exceeds 64.");
        }
        ++budget.calls;
        Frame frame{macro};
        for (std::size_t i = 0; i < args.size(); ++i) {
            frame.scopes.front().emplace(
                selected->parameters[i].name,
                Binding{selected->parameters[i].type,
                        typed(args[i], selected->parameters[i].type, caller_file, token)});
        }
        Value result = VoidValue{};
        try {
            if (auto returned = execute(selected->body, frame)) {
                result = std::move(returned->value);
            }
            result = typed(std::move(result), selected->result, frame.file(), selected->token);
        } catch (const MacroError& error) {
            --budget.calls;
            throw MacroError(error.location,
                             std::string(error.what()) + "\nWhile calling " +
                                 macro.definition->name + "." + name + " at " + caller_file + ":" +
                                 std::to_string(token.line) + ":" + std::to_string(token.column) +
                                 ".");
        }
        --budget.calls;
        return result;
    }
};
} // namespace

struct MacroEnvironment::Impl {
    std::map<std::string, Macro> macros;
    std::size_t emitted_tokens = 0;

    void define(const std::vector<Token>& input, std::size_t& cursor, const std::string& file) {
        Parser parser(input, cursor, file);
        const auto name = parser.expect(Kind::Identifier, "Expected macro name.");
        if (name.value == "fn" || name.value == "value" || name.value == "expression" ||
            macros.contains(name.value)) {
            fail(file, name, "Duplicate or reserved macro name '" + name.value + "'.");
        }
        parser.expect(Kind::LeftBrace, "Expected '{' after macro name.");
        auto definition = std::make_shared<Definition>();
        definition->name = name.value;
        definition->location = locate(file, name);

        struct Initializer {
            Token token;
            std::string name;
            Expression expression;
        };

        std::vector<Initializer> initializers;
        std::set<std::string> fields;
        while (!parser.accept(Kind::RightBrace)) {
            if (parser.accept(Kind::Semicolon)) {
                continue;
            }
            if (parser.peek().type == Kind::Identifier && parser.peek().value == "fn") {
                auto function = parser.function();
                auto& overloads = definition->functions[function.name];
                for (const auto& existing : overloads) {
                    if (existing.parameters.size() != function.parameters.size()) {
                        continue;
                    }
                    bool same = true;
                    for (std::size_t i = 0; i < existing.parameters.size(); ++i) {
                        same = same && existing.parameters[i].type == function.parameters[i].type;
                    }
                    if (same) {
                        fail(file, function.token, "Duplicate macro function signature.");
                    }
                }
                overloads.push_back(std::move(function));
                continue;
            }
            const auto field = parser.expect(
                Kind::Identifier, "Expected 'value', 'expression' or contextual 'fn' in macro.");
            if ((field.value != "value" && field.value != "expression") ||
                !fields.insert(field.value).second) {
                fail(file, field, "Unknown or duplicate macro field '" + field.value + "'.");
            }
            parser.expect(Kind::Colon, "Expected ':' after macro field.");
            Expression expression;
            if (field.value == "expression" && parser.accept(Kind::TernaryOp)) {
                definition->nullable_expression = true;
                expression = parser.make_node(Expr{Expr::Form::Literal, field, {}, "", {}});
            } else {
                if (field.value == "expression") {
                    definition->nullable_expression = false;
                }
                expression = parser.expression();
            }
            parser.terminator();
            initializers.push_back({field, field.value, std::move(expression)});
        }
        parser.accept(Kind::Semicolon);
        cursor = parser.cursor;
        if (!fields.contains("value")) {
            fail(file, name, "Macro requires a 'value' initializer.");
        }
        auto& macro = macros.emplace(name.value, Macro{definition, {}, {}}).first->second;
        Budget budget;
        Evaluator evaluator(macros, budget, file);
        Frame frame{macro};
        for (const auto& initializer : initializers) {
            auto value = evaluator.eval(initializer.expression, &frame);
            if (type_of(value) == Type::Void) {
                fail(file, initializer.token, "Macro field cannot contain void.");
            }
            if (initializer.name == "value") {
                macro.value = std::move(value);
            } else {
                macro.expression = Evaluator::typed(std::move(value),
                                                    {Type::String, definition->nullable_expression},
                                                    file,
                                                    initializer.token);
            }
        }
    }

    void append(std::vector<Token>& out, Token token, const Location& location) {
        if (++emitted_tokens > max_tokens) {
            throw MacroError(location,
                             "Macro expansion exceeds 1000000 generated tokens per module.");
        }
        out.push_back(std::move(token));
    }

    std::vector<Token> expand(const std::vector<Token>& input,
                              std::size_t& cursor,
                              const std::string& file,
                              Budget& budget,
                              unsigned depth = 0) {
        const auto start = cursor;
        const auto invocation = input[cursor];
        const auto location = locate(file, invocation);
        if (depth >= 64) {
            throw MacroError(location, "Recursive macro expansion exceeds 64 levels.");
        }
        auto& macro = macros.at(invocation.value);
        ++cursor;
        Evaluator evaluator(macros, budget, file);
        std::string method = "to_string";
        const bool member = input[cursor].type == Kind::Point;
        Value result;
        const bool call = input[cursor].type == Kind::LeftParen;
        if (member) {
            Parser parser(input, start, file);
            result = evaluator.eval(parser.unary());
            cursor = parser.cursor;
        } else {
            std::vector<Value> arguments;
            if (call) {
                Parser parser(input, cursor, file);
                for (const auto& argument : parser.arguments()) {
                    auto value = evaluator.eval(argument);
                    if (type_of(value) == Type::Void) {
                        fail(file, invocation, "void cannot be a macro argument.");
                    }
                    arguments.push_back(std::move(value));
                }
                cursor = parser.cursor;
            }
            result = evaluator.invoke(macro, method, arguments, invocation, file);
        }
        if (type_of(result) == Type::Void) {
            if (start != 0 && input[start - 1].line == invocation.line &&
                input[start - 1].type != Kind::Semicolon &&
                input[start - 1].type != Kind::LeftBrace &&
                input[start - 1].type != Kind::RightBrace) {
                fail(file, invocation, "Void macro call must be a standalone statement.");
            }
            // A mutating void call is a preprocessing statement, not an empty runtime expression.
            if (input[cursor].type == Kind::Semicolon) {
                ++cursor;
            } else if (input[cursor].type != Kind::EndOfFile &&
                       input[cursor].line == input[cursor - 1].line) {
                fail(file, invocation, "Void macro call must end its source statement.");
            }
            return {};
        }
        if (member) {
            std::vector<Token> tokens;
            if (const auto* s = std::get_if<std::string>(&result)) {
                append(tokens,
                       {Kind::StringLiteral, *s, invocation.line, invocation.column},
                       location);
            } else if (const auto* c = std::get_if<Character>(&result)) {
                append(tokens,
                       {Kind::CharLiteral, c->text, invocation.line, invocation.column},
                       location);
            } else {
                auto literals = lex_fragment(text(result, true), location);
                for (auto& token : literals) {
                    if (token.type == Kind::EndOfFile) {
                        break;
                    }
                    token.line = invocation.line;
                    token.column = invocation.column;
                    append(tokens, std::move(token), location);
                }
            }
            return tokens;
        }
        if (type_of(result) != Type::String) {
            fail(file, invocation, "Macro to_string must return string source.");
        }
        auto generated = lex_fragment(std::get<std::string>(result), location);
        std::vector<Token> output;
        for (std::size_t i = 0; i + 1 < generated.size();) {
            if (generated[i].type == Kind::Import || generated[i].type == Kind::Macro) {
                fail(file,
                     invocation,
                     "Macro expansions cannot introduce imports or macro declarations.");
            }
            if (generated[i].type == Kind::Identifier && macros.contains(generated[i].value)) {
                try {
                    const auto generated_file = "<generated " + macro.definition->name + ">";
                    auto nested = expand(generated, i, generated_file, budget, depth + 1);
                    output.insert(output.end(),
                                  std::make_move_iterator(nested.begin()),
                                  std::make_move_iterator(nested.end()));
                } catch (const MacroError& error) {
                    throw MacroError(location,
                                     std::string(error.what()) +
                                         "\nGenerated expansion failed at " + error.location.file +
                                         ":" + std::to_string(error.location.line) + ":" +
                                         std::to_string(error.location.column) + ".");
                }
            } else {
                append(output, generated[i++], location);
            }
        }
        for (auto& token : output) {
            token.line = invocation.line;
            token.column = invocation.column;
        }
        std::vector<Kind> delimiters;
        for (const auto& token : output) {
            if (token.type == Kind::LeftParen) {
                delimiters.push_back(Kind::RightParen);
            } else if (token.type == Kind::LeftBracket) {
                delimiters.push_back(Kind::RightBracket);
            } else if (token.type == Kind::LeftBrace) {
                delimiters.push_back(Kind::RightBrace);
            } else if (token.type == Kind::RightParen || token.type == Kind::RightBracket ||
                       token.type == Kind::RightBrace) {
                if (delimiters.empty() || delimiters.back() != token.type) {
                    fail(file, invocation, "Unbalanced delimiters in macro-generated source.");
                }
                delimiters.pop_back();
            }
        }
        if (!delimiters.empty()) {
            fail(file, invocation, "Unbalanced delimiters in macro-generated source.");
        }
        return output;
    }
};

MacroEnvironment::MacroEnvironment()
    : impl(std::make_unique<Impl>()) {}

MacroEnvironment::~MacroEnvironment() = default;

MacroEnvironment::MacroEnvironment(const MacroEnvironment& other)
    : impl(std::make_unique<Impl>(*other.impl)) {}

MacroEnvironment& MacroEnvironment::operator=(const MacroEnvironment& other) {
    if (this != &other) {
        impl = std::make_unique<Impl>(*other.impl);
    }
    return *this;
}

MacroEnvironment::MacroEnvironment(MacroEnvironment&&) noexcept = default;
MacroEnvironment& MacroEnvironment::operator=(MacroEnvironment&&) noexcept = default;

void MacroEnvironment::print(std::ostream& out) const {
    auto display = [](const Value& value) {
        try {
            return text(value, true);
        } catch (const std::runtime_error&) {
            return std::string("<representation exceeds limit>");
        }
    };
    for (const auto& [name, macro] : impl->macros) {
        out << name << ":\n    value: " << display(macro.value)
            << "\n    expression: " << display(macro.expression) << "\n    method: to_string"
            << (macro.definition->functions.contains("to_string") ? " (overridden)" : " (built-in)")
            << "\n";
        out << "    built-in: __repr__() -> string\n";
        for (const auto& [method, overloads] : macro.definition->functions) {
            for (const auto& function : overloads) {
                out << "    " << function.display << "\n";
            }
        }
        out << '\n';
    }
}

bool MacroEnvironment::contains(const std::string& name) const {
    return impl->macros.contains(name);
}

void MacroEnvironment::define(const std::vector<Token>& input,
                              std::size_t& cursor,
                              const std::string& file) {
    if (input.empty() || input.back().type != Kind::EndOfFile || cursor >= input.size()) {
        throw MacroError({driver::SourceKind::BufferNoAST, file, "", 1, 1},
                         "Invalid macro definition token stream.");
    }
    auto next = std::make_unique<Impl>(*impl);
    auto position = cursor;
    next->define(input, position, file);
    impl = std::move(next);
    cursor = position;
}

void MacroEnvironment::import(const MacroEnvironment& dependency, const Location& location) {
    for (const auto& [name, macro] : dependency.impl->macros) {
        if (const auto it = impl->macros.find(name);
            it != impl->macros.end() && it->second.definition != macro.definition) {
            throw MacroError(location,
                             "Imported macro name conflict for '" + name + "' (" +
                                 it->second.definition->location.file + " and " +
                                 macro.definition->location.file + ").");
        }
    }
    for (const auto& [name, macro] : dependency.impl->macros) {
        impl->macros.try_emplace(name, macro);
    }
}

std::vector<Token> MacroEnvironment::expand(const std::vector<Token>& input,
                                            std::size_t& cursor,
                                            const std::string& file) {
    if (input.empty() || input.back().type != Kind::EndOfFile || cursor >= input.size()) {
        throw MacroError({driver::SourceKind::BufferNoAST, file, "", 1, 1},
                         "Invalid macro invocation token stream.");
    }
    if (!contains(input[cursor].value)) {
        fail(file, input[cursor], "Unknown macro '" + input[cursor].value + "'.");
    }
    auto next = std::make_unique<Impl>(*impl);
    auto position = cursor;
    Budget budget;
    auto tokens = next->expand(input, position, file, budget);
    impl = std::move(next);
    cursor = position;
    return tokens;
}
} // namespace optic::compiler::preprocessor
