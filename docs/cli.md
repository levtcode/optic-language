# Optic CLI and component architecture

`optic` is a short-lived command-line coordinator. `opticc` is an independent
compiler process. There is no resident core or persistent IPC protocol.

```text
optic.cpp -> optic::cli::CliTool -> process launcher -> opticc
                                                     -> optic::compiler::driver::CompilerInstance
```

## Build and installation

A C++23 compiler and standard library with `<format>` support, plus CMake 3.20+
are required. Python 3 is optional for integration tests.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix ./install
```

On multi-configuration generators, select `--config Debug` for build/install
and `-C Debug` for CTest. `cmake --build build --target optic` builds only the
coordinator; `--target opticc` builds only the compiler. Disable test targets
with `-DBUILD_TESTING=OFF`.

Both tools install into `bin/`. When running directly from a build directory,
they also sit beside each other (in the configuration directory where applicable).

## Commands and current functionality

| Command | Current behavior |
| --- | --- |
| `optic help`, `optic --help`, `optic -h` | Print coordinator help without launching a component. |
| `optic --version` | Print the coordinator version. |
| `optic build <files>` | Delegate to `opticc build`; analyze sources, then report missing native code generation and fail. |
| `optic run <files>` | Delegate to `opticc run`; analyze sources, then report missing bytecode generation/runtime and fail. |
| `optic debug <files>` | Delegate to `opticc debug`; print compiler configuration, dependencies and preprocessed tokens. This is frontend inspection, not source-level debugging. |
| `optic install`, `optic uninstall`, `optic test` | Report that the command is not implemented and fail. |

`optic` without a command prints usage and returns 2. Unknown commands return 2.
The standalone compiler uses `opticc <build|run|debug> [options] <files.optic>`.
Use `optic debug --help` or `opticc --help` to inspect compiler arguments.

Examples:

```sh
optic debug "examples/hello world.optic"
opticc debug --color-diagnostics=disable -g sample.optic
opticc debug -- -filename.optic
```

`--` terminates compiler option parsing. An empty argument is rejected. Repeated
flags enable the flag once rather than toggling it. `--name=value` parses the
value after `=` and validates its type against the declared option.
`--color-diagnostics` accepts `auto`, `enable`, or `disable`; the configuration
records this choice, but the current diagnostic renderer does not implement
ANSI colors or terminal-based automatic selection. `-Wall` updates diagnostic
configuration; warning coverage still depends on frontend implementation. `-g`
is recorded for a future backend and does not yet emit debug information.

## Component discovery and process contract

Compiler discovery uses this order:

1. A nonempty `OPTIC_COMPILER` environment variable (an executable path or name).
2. `opticc` beside the running `optic` executable (`opticc.exe` on Windows).
3. `opticc` found through `PATH`.

An explicit override or an existing sibling that fails to launch produces an
error; the coordinator does not silently substitute another compiler.
Linux, macOS and Windows use their OS executable-location interfaces. Other POSIX
systems use a path-qualified `argv[0]` when available, then fall back to `PATH`.

The coordinator passes the command and following arguments as distinct argv
entries. It never constructs a shell command. Standard input, output and error
are inherited so diagnostics and token dumps appear while the compiler runs.
On Windows, arguments are encoded for the Microsoft CRT argv convention; the
current narrow-character spawn API is limited to paths representable by the
active code page. Windows and macOS implementations require platform validation.

The coordinator waits synchronously and returns the component's exit status.
On POSIX a signaled child maps to `128 + signal`. Terminal-generated signals
reach the shared foreground process group; there is no separate signal relay
for signals sent only to the coordinator PID. Missing components return 127;
other launch/wait errors return 126. The compiler returns 1 for invalid input,
frontend diagnostics or unavailable backend functionality.

`CompilerInstance::run` receives only compiler arguments, excluding both the
executable name and the mode. The standalone entry point selects `CompilerMode`.
The CLI owns general help; the compiler owns compiler-specific help.

## Namespaces and file responsibilities

The shared root is `optic::`; each component owns its namespace, with compiler
subsystems nested under `optic::compiler`:

| Namespace | Members |
| --- | --- |
| `optic::cli` | `CliTool`, component discovery and process execution. |
| `optic::compiler::driver` | `CompilerInstance`, configuration, diagnostics, `Module`, `ModuleGraph` and compiler argument helpers. |
| `optic::compiler::lexer` | `Lexer`, tokens, lookup tables and operator trie. |
| `optic::compiler::preprocessor` | Token preprocessing and recursive import loading. |

Namespaces describe subsystems, not individual files. For example, `Module`
belongs to the driver, so there is no `module` namespace. The future parser
should use `optic::compiler::parser`; no parser namespace or API is introduced
until that subsystem exists. Compiler argument helpers belong directly to the
driver, rather than a separate `cli_tooling` namespace. Dependencies between
subsystems use explicit qualifications such as `lexer::token_t` and
`driver::DiagnosticsEngine`.

The two process entry points
remain global `main` functions. `tokens_types.def` remains a macro expansion
list included inside the compiler implementation; it is not a standalone API.

| Files | Responsibility and migration |
| --- | --- |
| `optic.cpp` | Minimal CLI process entry point and exception reporting. |
| `cli/cli_tool.hpp`, `cli/cli_tool.cpp` | Replace removed `core/core.hpp` and `core/core.cpp`; own command routing, usage, and delegation. |
| `cli/process.hpp`, `cli/process.cpp` | Component discovery, process creation, inherited streams and exit status. |
| `compiler/opticc.cpp` | Standalone compiler entry point, mode selection and compiler help routing. |
| `compiler/driver/compiler_instance.hpp`, `.cpp` | Compiler configuration and phase coordination; explicit argv contract, input checks and unavailable-backend errors. |
| `compiler/driver/args.hpp`, `.cpp` | Compiler argument helpers; corrected option values, empty-input guards and double parsing. |
| `compiler/driver/diagnostics_engine.hpp`, `.cpp` | Diagnostics under `optic::compiler::driver`; configuration setter connects parsed diagnostic options. |
| `compiler/driver/module.hpp`, `.cpp` | Canonical source ownership, dependency edges and deterministic ordering. |
| `compiler/lexer/lexer.hpp`, `.cpp` | Lexical analyzer under `optic::compiler::lexer`; system includes stay outside the namespace. |
| `compiler/lexer/tables.hpp` | Lexical lookup tables under `optic::compiler::lexer`. |
| `compiler/lexer/tokens.hpp` | Token types under `optic::compiler::lexer`. |
| `compiler/lexer/lexer.cpp` | Longest operator matching reads ahead without cursor rollback or a dynamically allocated trie. |
| `compiler/preprocessor/preprocessor.hpp`, `.cpp` | Token preprocessing over the driver module graph; see [preprocessor documentation](preprocessor.md). |
| `CMakeLists.txt` | Separate build/install targets and optional CTest integration. |
| `tests/cli_component.cpp` | Executable fixture for argv, streams, exit codes and POSIX signals. |
| `tests/cli_integration.py` | Integration checks using real processes and temporary directories. |
| `README.md`, `docs/cli.md` | Current structure, usage, migration scope and limitations. |

Each modified C++ file has a Doxygen file description. The namespace migration
changes qualified C++ API names; downstream C++ callers must use the appropriate `optic::compiler::<subsystem>::` qualification.

## Future orchestration

The parser, semantic analysis, optimization, linking and code-generation driver
methods remain placeholders. Preprocessing is connected to the driver and resolves imports before the parser.
The parser consumes each module separately in dependency order; it must retain
module identity for diagnostics and symbol resolution. See
[preprocessing and module ownership](preprocessor.md).

When artifacts exist, build should generate a declared output path; run should
compile bytecode and invoke a VM, or compile a native executable and launch it
directly. A native executable does not need the bytecode VM. Real debugger
integration should distinguish debugging the generated program from compiler
tracing, while preserving or explicitly migrating the current `debug` command.

Package management should live in its own component and share internal operations
for install/uninstall/reinstall. Persistent IPC should be introduced only if a
component needs interactive requests or useful state between commands.
