"""Exercise typed compile-time macros through the real compiler executable."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

OPTICC = Path(sys.argv[1]).resolve()
sys.argv = sys.argv[:1]
TOKEN = re.compile(r"Token type: (\w+)\nToken value: '(.*?)'\nAt line: (\d+), in column: (\d+)\nIn file: (.*?)\n", re.S)


class MacroIntegration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="optic macros ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def source(self, name, contents):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")
        return path

    def run_compiler(self, *files):
        return subprocess.run([str(OPTICC), "debug", "--color-diagnostics=disable", *map(str, files)],
                              cwd=self.root, capture_output=True, timeout=15)

    def compile(self, source):
        main = self.source("main.optic", source)
        result = self.run_compiler(main)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        return TOKEN.findall(result.stdout.decode())

    def values(self, tokens):
        return [(kind, value) for kind, value, *_ in tokens if kind != "EndOfFile"]

    def invalid(self, source, message):
        result = self.run_compiler(self.source("main.optic", source))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn(message.encode(), result.stderr)
        self.assertIn(b"main.optic", result.stderr)

    def test_requested_value_expression_and_special_methods(self):
        tokens = self.compile('''macro MyMacro {
    value = -1;
    expression: "int value = "
    fn macro_funcion(int val) { value = val; }
    fn to_string() -> string { return f"{expression}{value};"; }
}
MyMacro
MyMacro.macro_funcion(42);
MyMacro
MyMacro.to_string()
MyMacro.repr()
''')
        values = self.values(tokens)
        self.assertEqual(values[:6], [("Int", "int"), ("Identifier", "value"), ("Equal", "="),
                                      ("Minus", "-"), ("IntegerLiteral", "1"), ("Semicolon", ";")])
        self.assertEqual(values[6:11], [("Int", "int"), ("Identifier", "value"), ("Equal", "="),
                                       ("IntegerLiteral", "42"), ("Semicolon", ";")])
        self.assertEqual(values[11], ("StringLiteral", "int value = 42;"))
        self.assertEqual(values[12], ("StringLiteral", 'macro MyMacro { value = 42; expression = "int value = "; functions = [macro_funcion(int) -> void, to_string() -> string] }'))
        self.assertEqual({(line, column) for _, _, line, column, _ in tokens[:6]}, {("7", "1")})
        self.assertEqual(sum(token[0] == "EndOfFile" for token in tokens), 1)

    def test_default_to_string_and_literal_types(self):
        tokens = self.compile('''macro Return { value = 12; expression: "return" }
macro Text { value = "hello"; }
macro Flag { value = true; }
macro Number { value = 2.0; }
macro Letter { value = '\\u03A9'; }
macro Empty { value = null; expression?: null }
Return
Text
Flag
Number
Letter
Empty
Text.value
Empty.expression
''')
        self.assertEqual(self.values(tokens), [("Return", "return"), ("IntegerLiteral", "12"),
            ("StringLiteral", "hello"), ("LiteralTrue", "true"), ("FloatLiteral", "2.0"),
            ("CharLiteral", "Ω"), ("Null", "null"), ("StringLiteral", "hello"), ("Null", "null")])

    def test_nullable_expression_and_typed_parameters(self):
        tokens = self.compile('''macro M {
    value = 1;
    expression?: null
    fn set_expression(string? val) { expression = val; }
    fn pick(int? val) -> int { if val == null { return value; } else { return val; } }
}
M.pick(null)
M.pick(8)
M.set_expression("return ");
M
M.set_expression(null);
M
''')
        self.assertEqual(self.values(tokens), [("IntegerLiteral", "1"), ("IntegerLiteral", "8"),
            ("Return", "return"), ("IntegerLiteral", "1"), ("IntegerLiteral", "1")])
        self.invalid('macro M { value = 1; expression: null }', "Expected macro type string")
        self.invalid('macro M { value = 1; expression: ""; fn reset() { expression = null; } } M.reset();',
                     "Expected macro type string")

    def test_overloads_and_to_string_call_arguments(self):
        tokens = self.compile('''macro M {
    value = 3;
    fn choose(int val) -> string { return "int"; }
    fn choose(float val) -> string { return "float"; }
    fn choose(string val) -> string { return val; }
    fn promote(float val) -> float { return val + 0.5; }
    fn to_string(int val) -> string { return f"return {val * value};"; }
}
M.choose(2)
M.choose(2.0)
M.choose("text")
M.promote(2)
M(4)
M.to_string(5)
M()
''')
        self.assertEqual(self.values(tokens), [("StringLiteral", "int"), ("StringLiteral", "float"),
            ("StringLiteral", "text"), ("FloatLiteral", "2.5"), ("Return", "return"),
            ("IntegerLiteral", "12"), ("Semicolon", ";"), ("StringLiteral", "return 15;"), ("IntegerLiteral", "3")])
        self.invalid('macro M { value = 1; fn f(int? x) {} fn f(string? x) {} } M.f(null);', "Ambiguous macro overload")
        self.invalid('macro M { value = 1; fn f(int x) {} } M.f(true);', "No matching macro overload")

    def test_typed_locals_scopes_loops_and_return(self):
        tokens = self.compile('''macro M {
    value = 0;
    fn factorial(int n) -> int {
        int result = 1;
        while n > 1 { result *= n; n -= 1; }
        return result;
    }
    fn scoped(int n) -> int {
        int result = n;
        { int result = 99; result += 1; }
        if n < 0 { return -1; } elif n == 0 { return 0; } else { return result; }
    }
    fn recursive(int n) -> int { if n <= 1 { return 1; } return n * recursive(n - 1); }
}
M.factorial(5)
M.scoped(-2)
M.scoped(0)
M.scoped(7)
M.recursive(6)
''')
        self.assertEqual(self.values(tokens), [("IntegerLiteral", "120"), ("Minus", "-"),
            ("IntegerLiteral", "1"), ("IntegerLiteral", "0"), ("IntegerLiteral", "7"), ("IntegerLiteral", "720")])

    def test_arithmetic_short_circuit_and_explicit_conversions(self):
        tokens = self.compile('''macro M {
    value = 1;
    fn arithmetic() -> int { return ((2 + 3 * 4) << 1) | 1; }
    fn safe() -> bool { return false and (1 / 0 > 0) or true || (1 / 0 > 0); }
    fn conversions() -> string { return f"{int(2.9)}:{float(3)}:{bool(0)}:{string(null)}:{char('x')}"; }
    fn power() -> int { return 2 ** 3 ** 2; }
}
M.arithmetic()
M.safe()
M.conversions()
M.power()
''')
        self.assertEqual(self.values(tokens), [("IntegerLiteral", "29"), ("LiteralTrue", "true"),
            ("StringLiteral", "2:3:false:null:x"), ("IntegerLiteral", "512")])

    def test_fstrings_raw_strings_and_escaping(self):
        tokens = self.compile(r'''macro M {
    value = "a\n\"b\\c";
    fn formatted(int n) -> string { return f"{{{n + 1}}}:{value}"; }
    fn raw() -> string { return r"\n"; }
    fn plain() -> string { "void"; return "void"; }
}
M
M.formatted(2)
M.raw()
M.plain()
''')
        self.assertEqual(self.values(tokens), [("StringLiteral", 'a\n"b\\c'),
            ("StringLiteral", '{3}:a\n"b\\c'), ("StringLiteral", "\\n"), ("StringLiteral", "void")])

    def test_recursive_expansion_and_empty_code(self):
        tokens = self.compile('''macro A { value = 1; fn to_string() -> string { return "B"; } }
macro B { value = 9; }
macro Empty { value = 0; fn to_string() -> string { return ""; } }
A
Empty
''')
        self.assertEqual(self.values(tokens), [("IntegerLiteral", "9")])

    def test_generated_multiline_calls_keep_statement_boundaries(self):
        tokens = self.compile(r'''macro M { value = 1; fn set(int n) { value = n; } }
macro Generator {
    value = 0;
    fn to_string() -> string { return "M.set(2)\nM.value"; }
}
Generator
M.value
''')
        self.assertEqual(self.values(tokens), [("IntegerLiteral", "2"), ("IntegerLiteral", "2")])
        self.assertEqual(tokens[0][2:4], ("6", "1"))

    def test_imported_macros_and_module_state_isolation(self):
        self.source("dep.optic", 'macro Shared { value = 1; fn set(int n) { value = n; } }\n')
        self.source("left.optic", 'import "dep.optic"\nShared.set(2);\n')
        self.source("right.optic", 'import "dep.optic"\nShared.set(3);\n')
        main = self.source("main.optic", 'import "left.optic"\nShared.value\nimport "right.optic"\nShared.value\nimport "dep.optic"\nShared.value\n')
        other = self.source("other.optic", 'import "dep.optic"\nShared.value\n')
        result = self.run_compiler(main, other)
        self.assertEqual(result.returncode, 0, result.stderr)
        tokens = TOKEN.findall(result.stdout.decode())
        main_values = self.values([token for token in tokens if token[-1] == str(main)])
        other_values = self.values([token for token in tokens if token[-1] == str(other)])
        self.assertEqual(main_values, [("IntegerLiteral", "2")] * 3)
        self.assertEqual(other_values, [("IntegerLiteral", "1")])
        self.assertEqual(sum(token[0] == "EndOfFile" for token in tokens), 5)

    def test_import_name_conflicts(self):
        self.source("one.optic", "macro M { value = 1; }")
        self.source("two.optic", "macro M { value = 2; }")
        self.invalid('import "one.optic"\nimport "two.optic"\n', "Imported macro name conflict")
        self.invalid('import "one.optic"\nmacro M { value = 2; }', "Duplicate or reserved macro name")

    def test_function_keywords_are_contextual_and_removed_constructs_are_identifiers(self):
        tokens = self.compile('int fn = 1; int directive = 2; int compiletime = 3; int Macro = 4;')
        self.assertEqual([value for kind, value in self.values(tokens) if kind == "Identifier"],
                         ["fn", "directive", "compiletime", "Macro"])
        tokens = self.compile('macro Legacy { value = 4; } Legacy')
        self.assertEqual(self.values(tokens), [("IntegerLiteral", "4")])

    def test_malformed_definitions_and_types(self):
        cases = [
            ('macro M { expression: "x" }', "requires a 'value'"),
            ('macro M { value = 1; value = 2; }', "duplicate macro field"),
            ('macro M { value = 1; unknown = 2; }', "Unknown or duplicate"),
            ('macro M { value = 1; fn f(x) {} }', "explicit macro type"),
            ('macro M { value = 1; fn f(int x, int x) {} }', "parameter name"),
            ('macro M { value = 1; fn f(int x) {} fn f(int y) {} }', "Duplicate macro function signature"),
            ('macro M { value = 1; fn to_string() { return "x"; } }', "explicitly return string"),
            ('macro M { value = 1; fn repr() -> string { return "x"; } }', "read-only"),
            ('macro M { value = 1; fn f() -> int {} } M.f()', "got void"),
            ('macro M { value = 1; fn f() -> int { return "bad"; } } M.f()', "got string"),
            ('macro M { value = 1; fn f() { missing = 2; } } M.f();', "undeclared macro variable"),
            ('macro M { value = 1; fn f() { int n = "bad"; } } M.f();', "got string"),
            ('macro M { value = 1; fn f() { if 1 {} } } M.f();', "must be bool"),
            ('function main() { macro M { value = 1; } }', "module scope"),
            ('macro M { value = 1; } M.unknown()', "No matching"),
            ('macro M { value = 1; } M.repr(1)', "no arguments"),
            ('macro M { value = 1; } M.unknown', "require '()'"),
            ('macro M { value = 1; fn set(int n) { value = n; } } int x = M.set(2);', "standalone statement"),
        ]
        for source, message in cases:
            with self.subTest(source=source):
                self.invalid(source, message)

    def test_checked_arithmetic_and_invalid_conversions(self):
        for expression, message in [
            ("9223372036854775807 + 1", "integer overflow"),
            ("-9223372036854775808 - 1", "integer overflow"),
            ("-9223372036854775808 * -1", "integer overflow"),
            ("-9223372036854775808 / -1", "integer overflow"),
            ("-(-9223372036854775808)", "integer overflow"),
            ("2 ** 63", "integer overflow"),
            ("1 / 0", "Division by zero"),
            ("1 % 0", "Division by zero"),
            ("1 << 64", "shift count"),
            ("1 << -1", "shift count"),
            ("1.0 / 0.0", "Division by zero"),
            ("1e308 * 1e308", "not finite"),
            ("int(1e100)", "out of range"),
            ('int("invalid")', "Invalid explicit int conversion"),
            ('char("multiple")', "one Unicode scalar"),
            ("9223372036854775808", "signed 64-bit range"),
            ("1e1000", "finite and representable"),
        ]:
            with self.subTest(expression=expression):
                self.invalid(f"macro M {{ value = {expression}; }} M", message)
        tokens = self.compile('macro M { value = -9223372036854775808; } M')
        self.assertEqual(self.values(tokens), [("Minus", "-"), ("IntegerLiteral", "9223372036854775808")])

    def test_invalid_generated_code_and_interpolation(self):
        for result, message in [("@", "Invalid macro source"), ("/*", "Invalid macro source"),
                                ("(", "Unbalanced delimiters"), ("}", "Unbalanced delimiters"),
                                ('import "missing.optic"', "cannot introduce imports"),
                                ("macro N { value = 1; }", "cannot introduce imports")]:
            with self.subTest(result=result):
                escaped = result.replace('\\', '\\\\').replace('"', '\\"')
                self.invalid(f'macro M {{ value = 1; fn to_string() -> string {{ return "{escaped}"; }} }} M', message)
        for expression in ('f"{"', 'f"}"', 'f"{value +}"', 'f"{unknown}"'):
            with self.subTest(expression=expression):
                self.invalid(f'macro M {{ value = 1; fn text() -> string {{ return {expression}; }} }} M.text()', "macro")

    def test_limits_stop_recursion_loops_and_oversized_strings(self):
        self.invalid('macro M { value = 0; fn run() { while true {} } } M.run();', "100000 steps")
        self.invalid('macro M { value = 0; fn run() { run(); } } M.run();', "exceeds")
        self.invalid('macro M { value = 0; fn to_string() -> string { return "M"; } } M', "64 levels")
        self.invalid('''macro M { value = "x";
            fn grow() { int i = 0; while i < 21 { value = value + value; i += 1; } }
        } M.grow();''', "1 MiB")
        self.invalid('''macro Many {
            value = 0;
            fn to_string() -> string {
                string result = ";";
                int n = 0;
                while n < 19 { result = result + result; n += 1; }
                return result;
            }
        }
        Many
        Many
        ''', "1000000 generated tokens")


if __name__ == "__main__":
    unittest.main()
