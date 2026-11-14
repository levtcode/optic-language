# The Optic Programming Language

> An experimental systems programming language focused on simplicity, performance, and accessibility.

Optic is an open-source programming language currently in early development.

The project began as an exploration of language design, compiler construction, and systems programming, with the long-term goal of creating a language that combines the readability of high-level languages with the control traditionally associated with low-level development.

At its current stage, Optic is primarily a learning and research project, and many aspects of the language are still evolving.

---

## Design Goals

Optic is being designed around a few core principles:

- **Readable syntax** inspired by modern high-level languages.
- **Performance-oriented architecture** suitable for systems-level development.
- **Direct interoperability with C and C++ libraries.**
- **Explicit control over program behavior and resources.**
- **A development experience that remains approachable without sacrificing power.**

These goals represent the direction of the project rather than fully implemented features.

---

## Example

### Hello World:

```c++
function main() {
    print("Hello World")
}
```

### Fibonacci:

```c++
function fibonacci(n: int) -> int {
    if n <= 1 {
        return n
    }

    return fibonacci(n - 1) + fibonacci(n - 2)
}
```

### Binary Search:

```c++
function binary_search(arr: array[int], x: int) -> int {
    int low = 0
    int high = arr.size() - 1

    while (low <= high) {
        int mid = low + (high - low) / 2

        if arr[mid] == x {
            return mid
        }

        if arr[mid] < x {
            low = mid + 1
        } else {
            high = mid - 1
        }
    }

    return -1
}
```

---

## Current Status

Optic is currently in the **pre-alpha** stage.

### Implemented

- CLI coordinator (`optic`) and standalone compiler (`opticc`)
- Driver
- Lexer
- Token preprocessing and recursive module imports
- AST parser with brace blocks, bracketed type arguments, implicit generic syntax,
  collections, records, casts and optional semicolons


### In Progress


- Semantic analysis
- Intermediate representation (IR)

### Planned

- Runtime system
- Virtual machine
- Standard library
- Native code generation
- C/C++ interoperability layer
- Integration with Cargo

---

## Repository Structure

```mermaid
flowchart TD
    A["optic.cpp"] --> B["optic CLI: cli/cli_tool"]
    B -->|independent process| C["opticc: compiler/opticc.cpp"]
    C --> D["Compiler driver"]
    D --> F["Preprocessor / Module graph"]
    F --> E["Lexer"]
    F --> I["Parser → AST"]
    I -. pending .-> J["Semantic analysis"]
    D -. pending .-> G["IR / Optimizer / Code generation / Linker"]
    B -. planned .-> H["Runtime / Package manager / Debugger"]
```

The CLI and compiler build as separate executable targets. `optic` delegates
compiler work through the OS process API; it does not embed compiler internals.
C++ APIs share the `optic::` root and are grouped by subsystem: compiler
APIs use `optic::compiler::driver`, `optic::compiler::lexer`, and
`optic::compiler::preprocessor`; CLI coordination uses `optic::cli`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/optic --help
./build/optic debug example.optic
```

A C++23 toolchain with `<format>` support and CMake 3.20+ are required. Python 3
is optional for integration tests. Both `optic` and `opticc` install into `bin`.

`debug` prints compiler configuration, module dependencies, imports, preprocessed tokens
and per-module ASTs. `build` and
`run` fail explicitly after frontend analysis because code generation and the
runtime are pending. Package commands are also unimplemented.

See [parser architecture and initial syntax](docs/parser.md) for blocks, statement
boundaries, supported expressions and AST ownership.

See [preprocessing and module ownership](docs/preprocessor.md) for import syntax,
source locations and the parser integration contract, and [macros](docs/macros.md)
for compile-time fields, functions and expansion rules.

See [CLI architecture and migration](docs/cli.md) for the component contract,
compiler discovery, exit statuses, per-file changes and platform limitations.

---

## Roadmap

The long-term roadmap currently includes:

- Static type system
- Optional runtime features for increased flexibility
- Virtual machine execution
- JIT compilation research
- Native binary generation
- Memory management strategies
- Hardware abstraction layers
- Standard library ecosystem

The roadmap is exploratory and may change significantly as the project evolves.

---

## Philosophy

Optic is not intended to compete with mature languages today.

Instead, it serves as a platform for exploring:

- Programming language design
- Compiler implementation
- Runtime systems
- Systems programming concepts
- Software architecture

Building the project in public allows ideas, mistakes, and improvements to be documented throughout the process.

---

## Contributing

Feedback, discussion, and constructive criticism are always welcome.

See [reading and formatting the C++ code](docs/code-style.md) for the style
configuration, formatter commands and parser reading order.

Areas of particular interest include:

- Parsing techniques
- Semantic analysis
- Compiler architecture
- Runtime design
- Language semantics
- Optimization strategies

Macro support is paused. Its implementation and historical tests remain in the
repository, excluded from compiler builds and active tests. Macro declarations
produce an explicit disabled-feature error; no macro code is evaluated or expanded.
