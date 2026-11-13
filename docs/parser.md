# Parser and compiler AST

Optic's first runtime parser uses recursive descent for declarations/statements
and precedence climbing for expressions. Its grammar is in [grammar.ebnf](grammar.ebnf).
`compiler/ast/` is a compiler component shared by the parser and future semantic
analysis; its nodes depend on no parser implementation objects. Public namespaces
are `optic::compiler::ast` and `optic::compiler::parser`.

## Blocks and statement boundaries

A function, `if`/`elif`/`else`, or `while` accepts either a brace block or a colon
followed by a newline and an indented body. Both forms produce an `ast::Block`:

```optic
function positive(n: int) -> bool:
    if n > 0:
        return true
    else { return false }

function main() {
    int value = 2
    while value > 0:
        print(value)
        value = value - 1
    return 0
}
```

Colon bodies use spaces and must be indented beyond the header. The first
statement establishes the body indentation; later statements at that level must
align. A dedent must match the header or an enclosing colon body. There is no
fixed indentation width. Leading tabs are diagnosed. Blank and comment-only
lines do not establish or end a block; CRLF is supported. Brace blocks may be
empty, and their indentation is otherwise cosmetic. Colon blocks must contain
a statement and cannot have an inline body. Mixed nesting is supported: a brace
block suspends the surrounding indentation comparison; colon blocks inside it
introduce their own indentation levels. A closing brace also closes colon blocks
contained in that brace scope. EOF closes any remaining indentation levels,
but never supplies a missing brace.

Semicolons are optional at logical line ends and before `}`, a dedent or EOF.
Multiple simple statements on one line require semicolons. Physical newlines
inside parentheses/brackets are ignored. An expression can also continue after
a supported operator that requires an operand:

```optic
int value = 1 +
    2 * 3
print(
    value,
    "done"
)
```

A new line beginning with `+`, `-`, `(` or a member suffix does not extend the
preceding complete expression. A newline immediately after `return` means return
without a value. These are Optic's rules; they do not implement JavaScript's
complete automatic semicolon insertion algorithm. `else`/`elif` may follow a
block across newlines, but not across a semicolon.

## Initial language subset

- Module-scope functions: `function f(n: int) -> int` and legacy `function f(int n)`.
- Optional syntactic return type; semantic return inference/checking is pending.
- Variables: `int value = 1`, with an optional initializer. A named type is also
  retained as a syntactic reference for future name resolution.
- Blocks, `if`/`elif`/`else`, `while`, `return`, `break`, `continue`.
- Integer, floating, boolean, string, character, raw-string and null literals.
- Names, grouped expressions, calls and member access; no trailing commas yet.
- Unary `+`, `-`, `not`, `!`; binary `* / %`, `+ -`, comparisons, equality,
  `and`/`&&`, `or`/`||`; assignment `=` to a name or member.

Precedence increases from assignment to logical-or, logical-and, equality,
comparison, addition, multiplication, unary, and postfix operations. Assignment
associates right; other binary operators associate left. Top-level statements
remain accepted, matching the existing frontend inspection fixtures. Nested
functions, collection/indexing syntax, compound assignment, increment/decrement,
power/bitwise operators, formatted interpolation, `for`, classes, exceptions,
lambdas and assembly are not implemented. Unsupported syntax is rejected, not
silently skipped. Imports remain a preprocessing concern and macros remain disabled.
The lexer recognizing a keyword does not mean the runtime grammar supports it.

Parsing checks structure and assignable expression shapes. Name visibility,
resolved types, argument counts, return compatibility and whether a break/return
is semantically valid in context are **not yet checked**. Build/Run still fail
with the explicit backend/runtime-pending result after successful frontend work.

## Components and ownership

| File | Responsibility |
| --- | --- |
| `compiler/lexer/lexer.cpp`, `tokens.hpp` | Raw tokens, decoded values and original half-open byte ranges. |
| `compiler/lexer/layout.cpp` | Logical newlines and colon indentation boundaries. |
| `compiler/parser/token_cursor.hpp` | Read-only bounded access to one EOF-terminated stream. |
| `compiler/parser/parser.hpp/.cpp` | Grammar, precedence, diagnostics and recovery. |
| `compiler/ast/ast.hpp` | Typed node classes, source spans and owning module trees. |
| `compiler/ast/dump.hpp/.cpp` | Deterministic debug inspection of the tree. |
| `compiler/driver/compiler_instance.hpp/.cpp` | Module traversal and ownership of successful results. |

`Lexer::tokenize()` remains the raw lexical API. `tokenize_layout()` adds layout
markers and is the API used by preprocessing for the parser stream. This keeps
layout interpretation separate from basic character/token recognition. Imports
are parsed up to NEWLINE/semicolon and may only occur outside brace and indented
blocks. Preprocessing preserves all remaining layout markers.

The driver parses Ready modules in dependency-first order. Each call borrows
one module and its diagnostics engine, returns an owning `ModuleAST`, and stores
it in `CompilerContext::syntax_trees()` keyed by stable `ModuleID`. The module
continues to own source bytes and tokens; module streams are never concatenated.
`ModuleState::Ready` describes preprocessing, not parser success.

Nodes own children with `std::unique_ptr`; there are no owning back-pointers.
Each node records `ModuleID`, start line/column and an original-source byte range
`[begin, end)`. Escaped strings use their original source length, not decoded
value length. Synthetic markers have zero-width spans. Numeric spellings are
retained without evaluating or narrowing them in the parser. `TypeRef` holds a
spelling, not a resolved semantic type. The AST outlives the Parser, and subsequent
phases can store resolved symbols/types separately without depending on its cursor.

## Errors, limits and inspection

Errors use the existing `DiagnosticsEngine` with the owning file and position.
Recovery advances to a statement boundary while preserving enclosing block
terminators. A module with any parser error yields no published AST. Other ready
modules can still be inspected; semantic/backend phases are skipped if there are
errors. Debug prints successful ASTs in dependency order after the existing token
dump; Build/Run print no AST inspection.

Parsing is bounded to 256 recursive levels, expression tree height 256, 256
operators/postfix operations per expression segment, and 32 syntax errors per
module. Expression tree height is checked independently of parser recursion so
long left-associated chains cannot exhaust the stack during dumping/destruction.
These are implementation limits and produce diagnostics, not crashes.

## Validation

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

`parser` checks AST shapes, precedence/associativity, decoded source spans,
ownership after parser destruction, both block styles and mixed nesting, optional
semicolons, recovery, limits, truncated programs and deterministic randomized
malformed inputs. `parser_integration` exercises real compiler sessions, imported
module ownership/order, misplaced imports, dependency syntax errors, fixtures and
backend limitations. Existing frontend, CLI and preprocessing suites stay active.
The former CLI fixture with two adjacent same-line statements now uses the
required semicolon; before parser integration that input was only tokenized.
