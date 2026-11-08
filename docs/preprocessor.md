# Preprocessing and module ownership

`opticc` loads each input and its transitive imports, tokenizes each source, and
preprocesses its tokens before passing the modules to later frontend phases.
Use `optic debug main.optic` (or `opticc debug main.optic`) to inspect the
result. Debug output includes the dependency graph and the preprocessed tokens
in dependency-first order. This inspection works in both Release and Debug
builds; it depends on `config.mode == CompilerMode::Debug`.

## Imports

Imports are module-scope statements. Use a dotted module name or a quoted path:

```optic
import math.vector
import "utilities.optic";
import "folder with spaces/helper"
```

A dotted name maps to path components: `math.vector` loads `math/vector.optic`.
Quoted paths preserve their spelling. A path without an extension receives
`.optic`; other extensions are rejected. Paths resolve relative to the importing
module's directory, independently of the compiler's working directory. Absolute
quoted paths are also supported. There is currently no package search path,
implicit standard-library lookup, alias syntax or wildcard import.

An import ends at a semicolon or the end of its source line. Comments are
handled by the lexer. Missing names, empty paths, embedded NUL bytes, trailing
arguments and imports inside runtime braces produce errors at the import's
source location. Strings and comments containing `import` remain ordinary
strings and comments.

Each canonical filesystem path identifies one module. Repeated imports,
relative path aliases and symlinks reuse it. Source files must be readable regular files and
use the `.optic` extension; directories and named pipes are rejected. A missing or lexically invalid dependency fails the
compilation. Direct and indirect cycles are errors, including self-imports;
rejecting an edge preserves the rest of the graph. Import nesting is limited to
256 modules so pathological inputs cannot exhaust the C++ call stack.

Modules are ordered deterministically: dependencies precede their importers,
with input/discovery order breaking ties. Dependency edges retain declaration
order and duplicates are ignored. Rebuilding the order never appends stale
entries.

## Pending language semantics

`macro`, `directive` and `compiletime` have no specification in this repository.
Their old implementation silently dropped keywords or left unfinished stubs.
Until their language contract is supplied, these constructs produce explicit
errors rather than silently changing the program. General compile-time
execution will also need the parser and semantic evaluator.

## Frontend contract

`CompilerContext` owns one `ModuleGraph`. The graph owns stable `ModuleID`s and
one `Module` for each source file. `Module` owns:

- The source path and original source bytes, retained for diagnostics.
- The final token stream for that module, including exactly one `EndOfFile`.
- Dependency IDs and preprocessing state (`Loaded`, `Processing`, `Ready`,
  `Failed`).

`Preprocessor` borrows the context by reference. It loads and lexes dependencies
on demand and updates the existing modules. There is no separate
`PreprocessedModule`, secondary dependency graph, combined source buffer or
unimplemented `SourceManager` API. Import statements are removed from the parser
token stream; dependencies remain available through `Module::get_dependencies()`.

The future parser should iterate `ModuleGraph::compilation_order()`, parse each
ready module separately, and associate its AST with that module's ID. A token's
line/column is relative to its owning module. A parser must not concatenate
module token streams: each stream has its own EOF, source identity and scope.
Name visibility and symbol linking across imports belong to semantic analysis,
not preprocessing. The parser and semantic analysis are still pending.

## Lexical guarantees

Preprocessing operates on tokens, so it never scans keywords inside comments or
literals. Comments behave as whitespace and keep subsequent source positions.
The lexer owns its cursor; it never stores mutable scan state in `Module`.
It records one-based token-start line/column positions, accepts CRLF
whitespace, preserves division and compound operators, and emits separate sign
operators (`a-1` is identifier, minus, integer). The longest valid operator wins.

Empty strings and escapes in the first character work. Normal quoted literals
support standard escapes, one to three octal digits, one to two hexadecimal
digits after `\\x`, four after `\\u` and eight after `\\U`. Unicode escapes must
be scalar values and are encoded as UTF-8. Octal escapes must fit in one byte.
Single quotes produce `CharLiteral` tokens; double quotes produce `StringLiteral`
tokens. Numeric values use `IntegerLiteral` and `FloatLiteral`; type keywords
retain `Int`, `Float`, `String` and `Char`. The parser can distinguish types from
values without inspecting token text. Token kinds and debug names share the
`tokens_types.def` definition list.
Raw `r"..."` strings preserve backslashes and formatted `f"..."` strings retain
their prefix token for the future parser. Interpolation evaluation is not part
of the lexer. Multiline quoted strings are not supported.

Unknown characters/escapes and unterminated strings/comments are diagnosed,
including truncation at EOF. The lexer never silently discards an unknown byte.
Repeated tokenization clears the destination and starts source positions at 1:1.

## Validation

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The C++ `frontend` checks exercise graph invariants, token positions, escapes,
operators and malformed lexical inputs. Python `preprocessor_integration`
checks real compiler sessions, import ordering, path aliases, cycles, failures,
EOF handling and inspection mode. Existing CLI integration checks also run.
