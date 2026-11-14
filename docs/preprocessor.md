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
import io, utils
import system as os
```

A dotted name maps to path components: `math.vector` loads `math/vector.optic`.
Quoted paths preserve their spelling. A path without an extension receives
`.optic`; other extensions are rejected. Paths resolve relative to the importing
module's directory, independently of the compiler's working directory. Absolute
quoted paths are also supported. There is currently no package search path,
implicit standard-library lookup or wildcard import. Optional `as` bindings
and comma-separated import lists are supported. Bindings are retained on the
owning module for future semantic name resolution.

An import ends at a semicolon or the end of its source line. Comments are
handled by the lexer. Missing names, empty paths, embedded NUL bytes, trailing
arguments and imports inside runtime blocks or parenthesized/bracketed expressions produce errors
at the import's source location. Strings and comments containing `import` remain ordinary
strings and comments.

Each canonical filesystem path identifies one module. Repeated imports,
relative path aliases and symlinks reuse it. Source files must be readable
regular files and use the `.optic` extension; directories and named pipes are
rejected. A missing or lexically invalid dependency fails the compilation. Direct and indirect cycles are errors, including self-imports;
rejecting an edge preserves the rest of the graph. Import nesting is limited to
256 modules so pathological inputs cannot exhaust the C++ call stack.

Modules are ordered deterministically: dependencies precede their importers,
with input/discovery order breaking ties. Dependency edges retain declaration
order and duplicates are ignored. Rebuilding the order never appends stale
entries.

## Macros (inactive)

Macro support is paused. The implementation remains in `macro.hpp`/`macro.cpp`
but is excluded from the compiler target and active test suite. Preprocessing
creates no macro environments, evaluates no macro bodies, imports no definitions
and performs no macro expansion. Debug output lists imports and module/token
information, with no macro inventory. A `macro` declaration fails explicitly
with "Macros are currently disabled."; its body is never executed. Ordinary
identifiers pass through unchanged. There is no compiler flag to enable macros.
Historical design and examples remain in [macros.md](macros.md).

`directive` and `compiletime` have been removed as language constructs and token
kinds. Both spellings are now ordinary identifiers. Platform APIs belong in a
future library; compiler mode is selected through the CLI.

## Frontend contract

`CompilerContext` owns one `ModuleGraph`. The graph owns stable `ModuleID`s and
one `Module` for each source file. `Module` owns:

- The source path and original source bytes, retained for diagnostics.
- The final token stream for that module, including physical `Newline`
  markers and exactly one `EndOfFile`.
- Import bindings (target module, written name, optional alias and source location).
- Dependency IDs and preprocessing state (`Loaded`, `Processing`, `Ready`,
  `Failed`).

`Preprocessor` borrows the context by reference. It loads and lexes dependencies
on demand and updates the existing modules. There is no separate
`PreprocessedModule`, secondary dependency graph, combined source buffer or
unimplemented `SourceManager` API. Import statements are removed from the parser
token stream; dependencies remain available through `Module::get_dependencies()`.

The parser iterates `ModuleGraph::compilation_order()`, parses each
ready module separately, and associates its AST with that module's ID in
`CompilerContext::syntax_trees()`. The AST definitions live in `compiler/ast/`. A token's
line/column is relative to its owning module. A parser must not concatenate
module token streams: each stream has its own EOF, source identity and scope.
Name visibility and symbol linking across imports belong to semantic analysis,
not preprocessing. The initial parser is implemented; semantic analysis is still
pending. See
[parser architecture](parser.md) for the supported grammar and ownership.

## Lexical guarantees

Preprocessing operates on tokens, so it never scans keywords inside comments or
literals. Comments behave as whitespace and keep subsequent source positions.
The lexer owns its cursor; it never stores mutable scan state in `Module`.
It records one-based token-start line/column positions, accepts CRLF
whitespace, preserves division and compound operators, and emits separate sign
operators (`a-1` is identifier, minus, integer). The longest valid operator wins.

Empty strings and escapes in the first character work. Normal quoted literals
support standard escapes, one to three octal digits, one to two hexadecimal
digits after `\x`, four after `\u` and eight after `\U`. Unicode escapes must
be scalar values and are encoded as UTF-8. Octal escapes must fit in one byte.
Single quotes produce `CharLiteral` tokens; double quotes produce `StringLiteral`
tokens. Numeric values use `IntegerLiteral` and `FloatLiteral`; type keywords
retain `Int`, `Float`, `String` and `Char`. The parser can distinguish types from
values without inspecting token text. Token kinds and debug names share the
`tokens_types.def` definition list.
Raw `r"..."` strings preserve backslashes and formatted `f"..."` strings retain
their prefix token. Interpolation evaluation is not part of the lexer; formatted
strings are parsed into literal segments and embedded expressions. Raw strings
may contain literal newlines; normal strings support backslash-newline splicing
and adjacent-string concatenation. See [parser syntax](parser.md).

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
EOF handling and inspection mode. The macro contract and integration test files are preserved as historical
fixtures and are not registered with CTest while macro support is paused.

## Debug trace and graph inspection

Run `./build/optic debug .vscode/file1.optic .vscode/file2.optic` to select runtime
Debug mode. Both files may be supplied even when one imports the other; canonical
identity ensures each module is lexed once. A CMake Debug build alone does not
select this mode. `build` and `run` commands do not emit the inspection output.

Debug first prints the invoked opticc path (`argv[0]`), version and runtime mode.
When the coordinator launches an unexpected compiler, inspect this path and the
`OPTIC_COMPILER` override described in [CLI discovery](cli.md). Rebuild both
executables with `cmake --build build` after source changes. No specific stale
binary or invocation fault was established for the reported missing import log;
the two-file reproduction prints it both directly and through the coordinator.

The preprocessor logs module loading/reuse, processing depth, source byte and
lexical token counts, import locations and resolved paths, successful edges,
and loading/processing failures. A reused dependency still produces a successful
import log. Logs use the same runtime Debug condition as the other frontend dumps.

The summary lists input count, unique loaded modules, import requests, successful
and failed imports, lexical tokens and new errors. Import counts refer to
requested modules (each entry in a list counts separately), so repeated imports
count separately while graph edges remain unique. Token counts exclude EOF.

The graph prints all loaded modules, including failed ones: stable IDs, canonical
paths, processing state, source bytes, output token counts, dependency IDs and
reverse edges (`Imported by`). It also prints dependency-first order with paths.
Rejected cycle edges appear in diagnostics/trace, not in the graph's accepted
edges. An order containing failed nodes is inspection data, not a compilation of
those modules. Unreadable paths have no loaded graph node.

The graph and summary remain visible after preprocessing errors. Parser token
output is explicitly skipped on failure, so partial streams are not presented
as successful frontend output. Existing per-module token dumps remain available
after success, and macros stay inactive.
