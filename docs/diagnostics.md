# Source diagnostics

Diagnostics use a severity header, a `file:line:column` location, and the original
source with one context line on either side:

```text
Error: invalid expression
  --> main.optic:2:9
  |
1 | function main() {
2 |     x = void + 1
  |         ^~~~~~~~
3 | }
```

This illustrates presentation, not a new semantic check. The current frontend
reports lexical, import and syntax errors. A point diagnostic uses `^`; a known
range uses `^` followed by `~`. Multiline ranges mark each affected line and
abbreviate long spans. Function names, solutions and extra information appear as
`= in function:`, `= help:` and `= note:` when supplied.

`DiagnosticsEngine::register_source(file, source)` owns a copy of the original
buffer. The lexer and parser register their module sources; rendering does not
reopen files from disk. Without a registered buffer or with invalid positions,
the header and location still appear, without a misleading source excerpt.
Command-line diagnostics use `<stdin>` without a fictitious line number.

`report` accepts an optional `SourceLocationRange`. Both endpoints use one-based
byte positions and the end is exclusive. `SourceLocationRange::from_text` computes
an endpoint from an original source fragment, including line breaks. The range
must begin at the diagnostic location and end in the same source. Invalid ranges
fall back to a point. Parser diagnostics underline the raw token rather than its
decoded value; EOF and synthetic tokens may have zero-length ranges.

Tabs expand to four-column stops in both source and markers. CRLF endings are
removed for display. UTF-8 continuation bytes do not add marker columns; terminal
widths for combining characters and double-width glyphs are not modeled.

`render(diagnostic)` returns plain text for tools and tests. `show`/`show_all` use
the diagnostic color configuration. The CLI's `auto` mode enables colors only
when stderr is a terminal; `enable` and `disable` override this. Severity and
markers use bold bright red (`\033[1;91m`) for errors, yellow for warnings and cyan for hints; gutters are
gray. ANSI color is optional and does not carry unique information.

The existing `format_msg` helper remains available for callers with one source
line and a custom marker; normal diagnostic output uses the full-source renderer.

## Error recovery

Lexical and import errors do not suppress parsing of modules with a complete,
EOF-terminated token stream. Such modules are parsed only to collect diagnostics;
failed modules do not publish an AST, and errors still prevent semantic analysis
and later compilation phases. Unloadable modules and import-depth failures have
no parser input and are skipped.

Parser recovery stops at statement boundaries and module declarations so a broken
expression does not consume a later function. The existing limit of 32 syntax
errors per module still applies. Recovery is best effort: an unterminated string
or comment can hide source text, and one malformed construct may cause subsequent
diagnostics. There is no guarantee of one message for every intended mistake.

Control-flow statements and standalone blocks must occur inside functions, not
at module scope. Expression operands still follow the grammar: negative values
and collection literals are permitted. Type compatibility is a later semantic
concern, not a parser error.
