# Macro declarations and evaluation

Macros are compile-time objects with a mutable `value`, an `expression` prefix,
and typed functions. They are processed before the runtime Optic parser.
Declarations disappear from the parser token stream; invocations either inject
source or produce a scalar literal. The keyword is case-sensitive: only `macro`
is accepted; `Macro` is an ordinary identifier. Declarations must be at module scope.

```optic
macro MyMacro {
    value = -1;
    expression: "int value = "

    fn macro_funcion(int val) {
        value = val;
    }

    fn to_string() -> string {
        return f"{expression}{value};"
    }
}

MyMacro                           // injects: int value = -1;
MyMacro.macro_funcion(42);         // changes this module's macro state
MyMacro                           // injects: int value = 42;
string source = MyMacro.to_string(); // a string literal: "int value = 42;"
string description = MyMacro.repr(); // a string describing the macro object
```

`fn` is a contextual word recognized by the macro parser, not a global language
keyword. Function parameters and local variables require explicit types.
Omitting a function's `-> type` means `void`, as in the mutating function above.
Function bodies are parsed on declaration and evaluated when called.

## Fields and nullability

`value = expression` is required and accepts a scalar value: signed 64-bit `int`,
finite `float` (double precision), `bool`, `string`, Unicode `char`, or `null`.
Its type can change on assignment; parameter, local and return types are checked.

`expression: expression` declares a non-null string prefix. Use `expression?`
to permit `null`. Omitting the field initializes it to nullable `null`:

```optic
macro Optional {
    value = 7;
    expression?: null

    fn set_prefix(string? prefix) {
        expression = prefix;
    }
}

Optional.set_prefix("return ");
Optional                           // injects: return 7
Optional.set_prefix(null);
Optional                           // injects: 7
```

Fields accept scalar expressions and calls to visible macro functions.
Initializers run in declaration order after function definitions have been
parsed. `value` and `expression` refer to the current macro's fields inside its
functions and initializers. These names cannot be used for parameters or locals.
`OtherMacro.value` and `OtherMacro.expression` read another visible macro.

Field declarations, assignments, returns, local declarations and expression
statements end at a semicolon, a closing brace or the end of their source line.
A multiline expression may continue where an operator or parentheses require it.
An omitted expression after `return` returns `void`.

## Invocations and special functions

| Form | Result |
| --- | --- |
| `MyMacro` or `MyMacro()` | Call zero-argument `to_string`, lex its returned source and recursively expand macros in it. |
| `MyMacro(arguments)` | Select a `to_string` overload and inject its returned source. |
| `MyMacro.method(arguments)` | Evaluate a macro function; emit its scalar result as a literal. |
| `MyMacro.value`, `MyMacro.expression` | Emit the current field value as a literal. |
| `MyMacro.to_string(arguments)` | Return source text as a string literal, without injecting that text. |
| `MyMacro.repr()` | Return a read-only representation of the macro object as a string literal. |

Void function calls are standalone preprocessing statements and emit no tokens;
their optional semicolon is consumed. A void call cannot initialize a runtime
variable, become another call's argument or supply a scalar expression. Macro
calls inside a runtime function still execute during preprocessing, in source
order, independently of that function's eventual runtime control flow.

The default `to_string()` concatenates the expression prefix (empty when null)
and the source spelling of `value`. Strings and characters are quoted and
escaped; booleans and null keep their language spellings. A space is inserted
when adjoining identifier-like characters would merge tokens, so prefix
`"return"` with value `12` produces `return 12`. Other whitespace and punctuation
must be included in the prefix or an overridden `to_string`.

Every `to_string` overload must explicitly declare `-> string`. The zero-argument
built-in remains available when only parameterized overloads are declared.
A custom zero-argument overload replaces that built-in behavior.

`repr()` takes no arguments, cannot be overridden, and never calls `to_string`.
It reports the macro name, current fields and custom function signatures. For
example:

```text
macro MyMacro { value = 42; expression = "int value = "; functions = [macro_funcion(int) -> void, to_string() -> string] }
```

Function groups are represented in name order, with overloads in declaration
order. The representation is diagnostic text, not a serialization to execute.

## Typed functions and overloads

Functions accept `int`, `float`, `bool`, `string` and `char`, optionally followed
by `?` to allow null. Return types accept those types plus `void`. Argument
expressions must be evaluable at preprocessing time; runtime variables are not
available.

```optic
macro Calculate {
    value = 3;

    fn choose(int input) -> int { return input * value; }
    fn choose(string input) -> string { return f"{input}:{value}"; }

    fn factorial(int n) -> int {
        int result = 1;
        while n > 1 {
            result *= n;
            n -= 1;
        }
        return result;
    }

    fn to_string(int n) -> string {
        return f"return {factorial(n)};"
    }
}

Calculate.choose(4)                 // integer literal 12
Calculate.choose("test")            // string literal "test:3"
Calculate(5)                       // injects: return 120;
```

Overloads are distinguished by their complete parameter type lists, including
nullability. A different return type alone does not define another overload.
Selection requires the same number of arguments and uses these conversion costs
per parameter: exact non-null type `0`, matching nullable type or null to any
nullable parameter `1`, `int` to `float` `2`, and `int` to `float?` `3`. The unique
candidate with the lowest total cost wins; ties are errors. There are no
implicit bool, string or float-to-int conversions. Assignment and return checks
use the same permitted type conversions. A missing value from a non-void
function, or a value returned by a void function, is an error when invoked.

Bodies support block-scoped typed locals, assignments (`=`, `+=`, `-=`, `*=`,
`/=`, `%=`), expression statements, `return`, `if`/`elif`/`else`, and `while`.
Conditions must be boolean. Parameters are local mutable bindings. Functions
may call other overloads by name, recurse, or call `OtherMacro.method(...)`.
Unsupported runtime-language statements are diagnosed by the macro parser;
this is a scalar macro interpreter, not general runtime Optic execution.

## Expressions and strings

Expressions support literals, field/local references, parentheses, function
calls, unary `+`, `-`, `!`/`not`, `~`, arithmetic `+ - * / % **`, comparisons,
bitwise operators and shifts, and boolean `&&`/`and`, `||`/`or`. Power is right
associative; unary operators bind before power. Boolean operators short-circuit.
Integer division truncates toward zero; `%` and bitwise operations require ints.
Mixed numeric arithmetic promotes ints to float. String `+` concatenates two
strings. Equality accepts different scalar types (numeric pairs are promoted);
ordered comparisons require numeric pairs, strings or characters of the same
kind.

Integer overflow, invalid shift counts (outside `0..63`), division by zero and
non-finite floating-point results are errors. Floating-point rounding follows
the host's double-precision arithmetic. Right shifts are arithmetic; left shifts
are checked as repeated multiplication by two.

Explicit scalar conversions use `int(value)`, `float(value)`, `bool(value)`,
`string(value)` and `char(value)`. Int/float conversions accept numeric values,
bools and fully parsed numeric strings; float-to-int truncates toward zero and
checks range. Bool accepts a bool, zero/nonzero numeric value or the strings
`"true"` and `"false"`. Char accepts a char or a one-scalar UTF-8 string. String
uses a scalar's textual form without adding source quotes; null becomes
`"null"`. Void cannot be converted or interpolated.

Formatted strings evaluate expressions between braces. Double braces produce
literal braces, and raw strings preserve backslashes:

```optic
fn description(int count) -> string {
    return f"{{{count + 1}}}:{value}";
}
```

Calling `description(2)` when `value` is `7` returns `"{3}:7"`. Interpolation does
not perform a second pass on braces inside substituted strings.

## Modules, diagnostics and limits

Names become visible in source order. Imports copy macro field state and share
immutable function definitions; subsequent mutations belong only to the
importing module. Repeated/diamond imports of the same definition are harmless;
first-visible field state wins. Different definitions using the same visible
name are errors. Local declarations cannot replace an imported macro. See
[module preprocessing](preprocessor.md) for import resolution.

A definition or invocation commits state only after successful processing. A
failed call, initializer, invalid generated source or expansion rolls back all
macro mutations from that operation. Runtime errors report the function's
original source and include calling locations, including imported definitions.
Generated tokens use the invocation's owning module and source position, and
never insert another module's EOF.

Injected source is lexed and expanded recursively. It must have balanced
parentheses, brackets and braces and cannot introduce imports or macro
declarations. Strings returned by explicit member calls remain literal data
and are not reinterpreted as source. Empty injected source is allowed.

The following independent limits turn runaway computations into diagnostics:

- 100,000 evaluation steps per definition or top-level invocation, shared by
  nested function calls and generated expansions.
- 128 nested parser/evaluator operations, 64 nested function calls and 64 nested
  source expansions. The first applicable limit stops processing.
- 1 MiB per string or generated-source fragment.
- 1,000,000 generated tokens per module across successful invocations.

There is no macro filesystem/process I/O, runtime-variable access, generic
container/object type, wildcard namespace lookup or separate `compiletime`
construct. `directive` and `compiletime` are ordinary identifiers. The runtime
parser, semantic analysis and code generation remain separate future phases.
