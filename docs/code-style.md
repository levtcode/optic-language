# Reading and formatting the C++ code

The repository uses four spaces, a 100-column limit and braces around control
flow. Operations on different pieces of state occupy separate statements;
functions and node definitions are separated with blank lines. `.editorconfig`
sets editor defaults and `.clang-format` makes the C++ layout reproducible.
Use clang-format 23.1.5 for the same output as this revision.

## Spacing inside functions

Use one blank line between logical steps: preparing input, validating it,
consuming a grammar element, updating state and returning a result. Keep closely
related operations together, such as reading a token and advancing its cursor.
Do not insert a blank line after every statement or every brace.

Early-return checks may be separated when each handles a distinct condition.
Keep paired operations, such as entering/leaving a delimiter scope, visually
together. The goal is to expose the function's steps rather than increase its
line count. `TokenCursor` is a compact example of this balance.

When a function combines different responsibilities, extract named operations
before adding more spacing. The preprocessor's `process` coordinates module
state, tokenization and filtering; `imports.cpp` handles import syntax and
dependency resolution. Formatting tools preserve this grouping, but cannot
choose the logical boundaries for the author.

From the repository root, format tracked C++ files with:

```sh
git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format -i
```

Check formatting without changing files:

```sh
git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror
```

Do not format `.optic` fixtures as C++: they describe Optic's grammar, including
significant statement boundaries. Macro implementation files remain inactive;
formatting them does not enable the feature.

## Following the parser

Start at `Parser::parse()` in `compiler/parser/parser.cpp`. It walks one module's
statements, collecting diagnostics and retaining a tree only on success.

Methods beginning with `parse_` consume grammar productions; `is_` predicates
answer grammar questions. `skip_newlines`, `expect_statement_end` and
`recover_statement` make token-boundary handling explicit. Policy limits are
named constants in `parser.hpp`.

Expression parsing follows this path:

```text
parse_expression
  parse_binary_expression       operator precedence and assignment
    parse_unary_expression      prefix operators
      try_parse_cast            speculative cast recognition
      parse_allocation          new Type(...)
      parse_postfix_expression  calls, members, indices and postfix operators
        parse_primary_expression
```

`declarations.cpp` holds declarations and compound statements. `types.cpp`
recognizes type syntax and declaration lookahead. `strings.cpp` parses embedded
formatted-string expressions and maps them back to source positions.

The `Depth` and `SoftLines` guards delimit temporary parsing modes. Lookahead
may fail without producing diagnostics; actual parsing reports errors. The
`AmbiguousStmt` and `UnresolvedExpr` nodes deliberately preserve decisions that
require later name resolution. See [parser.md](parser.md) for the language rules
and the responsibilities of each compiler phase.
