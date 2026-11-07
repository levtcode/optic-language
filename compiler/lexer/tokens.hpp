/**
 * @file tokens.hpp
 * @brief Token kinds and source-positioned lexical tokens.
 *
 * C++ declarations and definitions belong to namespace optic.
 */

#pragma once

#include <string>

namespace optic {

enum class TokenType {
    // Types
    Int,            // int
    Float,          // float
    Bool,           // bool
    String,         // string
    Char,           // char
    Identifier,

    // Combined types
    Const,          // const
    Pointer,        // pointer
    Array,          // array
    Tuple,          // tuple
    Dict,           // dict

    // Symbols
    LeftParen,      // (
    RightParen,     // )
    LeftBrace,      // {
    RightBrace,     // }
    LeftBracket,    // [
    RightBracket,   // ]
    Colon,          // :
    Semicolon,      // ;
    Comma,          // ,
    Point,          // .
    TernaryOp,      // ?
    Arrow,          // ->

    // Simple operators
    Equal,          // =
    Plus,           // +
    Minus,          // -
    Star,           // *
    Slash,          // /
    Mod,            // %
    Pow,            // **

    // Bit & Bit operators
    BitAnd,         // &
    BitOr,          // |
    BitNot,         // ~
    BitXor,         // ^
    LeftShift,      // <<
    RightShift,     // >>

    // Assign operators
    Increment,      // ++
    Decrement,      // --
    PlusAssign,     // +=
    MinusAssign,    // -=
    MulAssign,      // *=
    DivAssign,      // /=
    ModAssign,      // %=
    AndAssign,      // &=
    OrAssign,       // |=
    LShiftAssign,   // <<=
    RShiftAssign,   // >>=

    // Logical operators
    And_C,          // &&
    Or_C,           // ||
    Not_C,          // !
    And,            // and
    Or,             // or
    Not,            // not

    // Comparison operators
    EqualEqual,     // ==
    NotEqual,       // !=
    Less,           // <
    LessEqual,      // <=
    Greater,        // >
    GreaterEqual,   // >=

    // Preprocessor keywords
    Import,         // import
    Directive,      // directive
    Macro,          // macro
    CompileTime,    // compiletime

    // Keywords
    Asm,            // asm
    New,            // new
    Delete,         // delete
    If,             // if
    Elif,           // elif
    Else,           // else
    Function,       // function
    Null,           // null
    Return,         // return
    ForLoop,        // for
    WhileLoop,      // while
    Break,          // break
    Continue,       // continue
    Switch,         // switch
    Case,           // case
    Try,            // try
    Catch,          // catch
    Throw,          // throw
    Enum,           // enum
    Struct,         // struct
    Class,          // Class
    Lambda,         // lambda

    // Especial Tokens
    LiteralTrue,    // true
    LiteralFalse,   // false
    FString,        // f"{}"
    RawString,      // r""
    EndOfFile       // EOF
};

struct token_t {
    TokenType type;
    std::string value;
    unsigned line;
    unsigned column;
    
    token_t(const TokenType typ, const std::string &val, const unsigned ln, const unsigned col) : 
    type(typ), value(val), line(ln), column(col) {}
};

} // namespace optic
