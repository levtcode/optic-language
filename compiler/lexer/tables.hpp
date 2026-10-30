/* tables.hpp */

#pragma once

#include "compiler/lexer/tokens.hpp"

#include <unordered_map>
#include <string_view>

const std::unordered_map<std::string_view, TokenType> operator_table = {
    { "=", TokenType::Equal },
    { "+", TokenType::Plus },
    { "-", TokenType::Minus },
    { "*", TokenType::Star },
    { "/", TokenType::Slash },
    { "%", TokenType::Mod },
    { "**", TokenType::Pow },
    { "&", TokenType::BitAnd },
    { "|", TokenType::BitOr },
    { "~", TokenType::BitNot },
    { "^", TokenType::BitXor },
    { "<<", TokenType::LeftShift },
    { ">>", TokenType::RightShift },
    { "++", TokenType::Increment },
    { "--", TokenType::Decrement },
    { "+=", TokenType::PlusAssign },
    { "-=", TokenType::MinusAssign },
    { "*=", TokenType::MulAssign },
    { "/=", TokenType::DivAssign },
    { "%=", TokenType::ModAssign },
    { "&=", TokenType::AndAssign },
    { "|=", TokenType::OrAssign },
    { "<<=", TokenType::LShiftAssign },
    { ">>=", TokenType::RShiftAssign },
    { "==", TokenType::EqualEqual },
    { "!=", TokenType::NotEqual },
    { "<", TokenType::Less },
    { ">", TokenType::Greater },
    { "<=", TokenType::LessEqual },
    { ">=", TokenType::GreaterEqual },
    { "->", TokenType::Arrow },   // Not operator, but it have 2 chars; symbol_table accepts int type not string
};

const std::unordered_map<std::string_view, TokenType> keyword_table = {
    { "import", TokenType::Import },
    { "directive", TokenType::Directive },
    { "macro", TokenType::Macro },
    { "compiletime", TokenType::CompileTime },
    { "asm", TokenType::Asm },
    { "if", TokenType::If },
    { "elif", TokenType::Elif },
    { "else", TokenType::Else },
    { "function", TokenType::Function },
    { "null", TokenType::Null },
    { "return", TokenType::Return },
    { "for", TokenType::ForLoop },
    { "while", TokenType::WhileLoop },
    { "break", TokenType::Break },
    { "continue", TokenType::Continue },
    { "switch", TokenType::Switch },
    { "case", TokenType::Case },
    { "try", TokenType::Try },
    { "catch", TokenType::Catch },
    { "throw", TokenType::Throw },
    { "enum", TokenType::Enum },
    { "struct", TokenType::Struct },
    { "class", TokenType::Class },
    { "lambda", TokenType::Lambda },
    { "int", TokenType::Int },
    { "float", TokenType::Float },
    { "bool", TokenType::Bool },
    { "string", TokenType::String },
    { "char", TokenType::Char },
    { "const", TokenType::Const },
    { "pointer", TokenType::Pointer },
    { "array", TokenType::Array },
    { "tuple", TokenType::Tuple },
    { "dict", TokenType::Dict },
    { "and", TokenType::And },
    { "or", TokenType::Or },
    { "not", TokenType::Not },
    { "true", TokenType::LiteralTrue },
    { "false", TokenType::LiteralFalse }
};

const std::unordered_map<int, TokenType> symbol_table = {
    { '{', TokenType::LeftBrace },
    { '}', TokenType::RightBrace },
    { '(', TokenType::LeftParen },
    { ')', TokenType::RightParen },
    { '[', TokenType::LeftBracket },
    { ']', TokenType::RightBracket },
    { ':', TokenType::Colon },
    { ';', TokenType::Semicolon },
    { ',', TokenType::Comma },
    { '.', TokenType::Point },
    { '?', TokenType::TernaryOp },
    { 'f', TokenType::FString }
};