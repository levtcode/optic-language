# Optic parser and compiler AST

The parser recognizes Optic syntax and preserves it in `compiler/ast/` for later
semantic analysis. It does not execute code, specialize functions, enforce static
types or implement memory management. The grammar summary is in
[grammar.ebnf](grammar.ebnf); executable examples are in
`tests/_parser_/fixtures/valid/` and negative examples in `fixtures/invalid/`.

## Blocks, separators and multiline expressions

Blocks **always use braces**. Indentation and tabs are visual only. The old
colon/indentation grammar and Indent/Dedent tokens have been removed.

```optic
function main() {
    array[int, 4] arr = {1, 2, 3, 4}
    for (int i = 0; i < arr.length; i++) {
        arr[i] = i
    }
    print(arr[-1]); return 0
}
```

A physical newline ends a complete simple statement. A semicolon is optional
before a newline, `}` or EOF and separates multiple simple statements on the
same line. The three `for` clauses use **semicolons**, not commas. Initialization,
condition and update may be omitted (`for (;;) { ... }`). A declaration may
contain multiple names (`int x = 5, y = 2`); updates may be comma-separated.

Expressions continue across newlines inside calls, grouped expressions, indices,
type arguments and collection literals, or after an operator needing an operand:

```optic
result = offset +
    bytes * size
print(
    result,
    "done"
)
```

No final semicolon is required for multiline expressions. A leading operator on
a new line does not retroactively continue the preceding complete statement.
A newline immediately after `return` produces a return without a value. `else if`
is the conditional chaining spelling. A newline, but not a semicolon, may separate
an `if` block from its `else`.

## Types, implicit generics and aliases

Type arguments follow the **type**, use brackets and precede the variable name:

```optic
array[int, 4] numbers
vector[string] names
dict[string, dict[string, int]] people = {"angel": {"edad": 15}}
tuple[int, string, bool] item = (1, "hello", true)
```

`array<int, 4> numbers`, `int numbers[4]` and `array numbers[int, 4]` are rejected.
Both type and non-type arguments are retained, including nested applications and
extent expressions. A bare argument name is unresolved syntax: later analysis
must determine whether it names a type or a value.

Parameters use `name` or `name: Type`. No annotation means an **implicit static
generic parameter**, not a dynamically changing variable. Restrictions and return
annotations accept unions:

```optic
alias [int | float | double | string] as func1_params
alias [unsigned long] as size_t
alias size_t as bytes_t
function add(a: func1_params, b: func1_params) -> func1_params { return a + b }
function inferred_add(a, b) { return a + b }
```

Aliases are **type-only**, with `original as new` order. Compound type expressions
are enclosed in brackets. Named types can be referenced without that enclosing
pair. String-quoted aliases, value-expression aliases and textual macro expansion
are not supported. Import `as` bindings are separate module bindings.

`const` is explicit (`const size_t bytes = 1024`). Capitalization does not imply
constness. Pointer/reference declarators (`Tree *next`, `system::error &exception`)
and qualified names are retained as structured types.

## Represented syntax

- Functions, optional return annotations, typed/untyped parameters and calls.
- Typed declarations, declaration groups and assignments used for inferred names.
- Classes/structs, per-member public/private/protected visibility, fields,
  constructors and methods. Visibility defaults are left to semantic analysis.
- Brace blocks, `if`/`else if`/`else`, `while`, three-clause `for`, return,
  break, continue, `try`/typed `catch` and `throw`.
- Scalar literals, raw strings, adjacent string concatenation and f-string
  interpolation with expression nodes. Normal strings support backslash-newline
  splicing; raw quoted strings can contain literal newlines. Indentation following
  a spliced newline remains string content. Interpolation supports `{expression}`
  and doubled literal braces; Python formatting/conversion directives are not
  implemented in this version.
- Array `{...}`, vector `[...]`, tuple `(...)` and dictionary `{key: value}`
  literals, including nesting and trailing commas. `{}` in an initializer is an
  unresolved empty brace literal; the receiving type determines its meaning.
  `(value)` groups an expression; `()` and `(value,)` are tuples.
- Indexing (including negative-index syntax), member `.`, qualified `::`, calls,
  prefix/postfix increment/decrement, unary arithmetic/logical/bitwise operators,
  address/dereference, arithmetic/comparison/logical/bitwise/shift operators,
  assignment and compound assignment, C-style casts, `new Type(...)` and `delete`.

Assignment associates right. Other binary operators associate left, with C-like
precedence (logical-or, logical-and, bitwise-or/xor/and, equality, comparison,
shifts, addition, multiplication, unary and postfix). `and`/`or`/`not` share the
corresponding symbolic operators' precedence. Assignment syntax allows names,
members, indices, dereferences and calls: a call such as `arr.at(2)` may return
an assignable reference, which semantic analysis must verify.

Nested functions, enum/switch/lambda/assembly constructs, ternary expressions,
explicit function template declarations and power `**` are not implemented.
Their tokens or surrounding syntax produce errors; unsupported syntax is never
silently skipped. Macros remain disabled.

## Compiler ownership and phase boundaries

| Component | Responsibility |
| --- | --- |
| `compiler/lexer/lexer.cpp`, `tokens.hpp` | Raw lexical tokens, decoded values and original byte spans. |
| `compiler/lexer/layout.cpp` | Preserve physical newline boundaries, including newlines in intervening comments. No indentation policy. |
| `compiler/parser/parser.cpp` | Module orchestration, statements, braces and recovery. |
| `compiler/parser/types.cpp` | Structured type references and declaration lookahead. |
| `compiler/parser/declarations.cpp` | Functions, declarations, records, loops, handlers and aliases. |
| `compiler/parser/expressions.cpp` | Precedence, casts, collections and postfix syntax. |
| `compiler/parser/strings.cpp` | Formatted-string expression parsing and original-source locations. |
| `compiler/ast/ast.hpp`, `dump.cpp` | Compiler-owned nodes and deterministic inspection. |
| `compiler/preprocessor/preprocessor.cpp` | Resolve import lists/aliases; retain module binding metadata. |

`Lexer::tokenize()` remains the raw API and `tokenize_layout()` preserves
Newline markers. The parser suppresses their separator role in continuation
contexts. Quoted multiline contents do not introduce statement boundaries.

The driver parses each Ready module separately in dependency-first order and
stores successful trees in `CompilerContext::syntax_trees()` by `ModuleID`.
Each module owns its source/tokens and import bindings (target ID, written name,
optional alias and location). Imports are removed from parser tokens; their
bindings remain available for future name visibility. Streams are never joined.
`ModuleState::Ready` continues to mean preprocessing success.

AST nodes own children with `unique_ptr`; immutable type value-argument syntax
can be shared across a declaration group. Type references preserve applications,
unions, qualifiers and unresolved names. Spans identify a module and half-open
original byte range plus start line/column, independent of decoded string length.
F-string expressions retain their positions in the original file, even when
nested. Trees remain valid after parser instances have been destroyed.

C-style syntax can require name lookup: `a * b` may be a declaration or a product,
and `(T) + x * y` may be a cast or a grouped arithmetic expression. Unknown-name
cases are retained explicitly as `AmbiguousStmt`/`UnresolvedExpr`; the latter
keeps the **whole expression's tokens** because the choice affects precedence.
Semantic name resolution must disambiguate/reparse these before using a resolved
tree. Built-in and declared type names provide syntactic lookahead without
claiming to resolve their meanings or validate aliases.

## Semantic work still pending

This change recognizes the syntax needed to implement these contracts, but does
not yet check or execute them:

- Fixed variable types, static specialization of implicit generics, numeric
  conversions/promotions, return constraints and constant enforcement.
- Type alias resolution/cycles, imported name visibility and overload selection.
- `type()` evaluation and elimination of statically discarded specialization paths.
- Default initialization of all collection elements, fixed array lengths,
  reference validity and valid assignment results of calls.
- Bounds checks, negative-index interpretation, catchable conversion/bounds errors,
  exception unwinding, object lifetimes, destructors and allocation ownership.

A semantically invalid but syntactically well-formed program can therefore still
pass Debug parsing. Build/Run continue to report the pending backend/runtime;
this is not a claim that these programs can already execute.

## Diagnostics and verification

Errors go through `DiagnosticsEngine` with source identity. Recovery advances to
statement boundaries and preserves block terminators. A module with parser
errors publishes no partial AST. Other ready modules can still be inspected;
subsequent phases are skipped when errors exist. Debug dumps each successful AST;
Build/Run do not print inspection output.

Limits are 128 parser nesting levels, expression tree height 256 and 32 syntax
errors per module. Height is checked independently of parser recursion, including
collections, calls and formatted strings, to bound tree destruction/dumping.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests check AST shape/ownership, precedence, casts and unresolved ambiguity,
original spans, type restrictions/aliases, mixed collections, records, loops,
exception syntax, string forms, multiline boundaries, module aliases, malformed
imports, legacy syntax rejection, truncated programs and randomized malformed
inputs. Fixtures follow the supplied examples with their agreed corrections;
ellipses and intentionally semantic-only errors are not treated as executable
implementations of unresolved runtime behavior.
