"""Exercise parser/module ownership and syntax through the real compiler process."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

OPTICC = Path(sys.argv[1]).resolve()
FIXTURES = Path(__file__).parent / "fixtures"
sys.argv = sys.argv[:1]


class ParserIntegration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="optic parser ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def source(self, name, contents):
        path = self.root / name
        path.write_text(contents, encoding="utf-8")
        return path

    def compile(self, *files, mode="debug"):
        return subprocess.run([str(OPTICC), mode, "--color-diagnostics=disable", *map(str, files)],
                              capture_output=True, timeout=10)

    def test_fixtures(self):
        for category, status in (("valid", 0), ("invalid", 1)):
            for source in (FIXTURES / category).glob("*.optic"):
                with self.subTest(source=source):
                    result = self.compile(source)
                    self.assertEqual(result.returncode, status, result.stderr)

    def test_each_module_has_its_own_ast(self):
        dep = self.source("dep.optic", "function helper() -> int {\n    return 42\n}\n")
        main = self.source("main.optic", 'import dep\nimport dep\nfunction main() { return helper() }\n')
        result = self.compile(main, dep)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count(b"ModuleAST ["), 2)
        self.assertLess(result.stdout.index(b"Function helper"), result.stdout.index(b"Function main"))
        self.assertEqual(result.stdout.count(b"Function helper"), 1)
        for mode in ("build", "run"):
            result = self.compile(main, mode=mode)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"not implemented", result.stderr)
            self.assertNotIn(b"ModuleAST", result.stdout)

    def test_nested_imports_are_never_loaded(self):
        for source in ('function main() {\n    import missing\n}\n',
                       'function main() {\n    if true {\n        import missing\n    }\n}\n'):
            main = self.source("main.optic", source)
            result = self.compile(main)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"Imports are only allowed at module scope", result.stderr)
            self.assertNotIn(b"Cannot open module", result.stderr)

    def test_import_after_block_and_semicolon(self):
        self.source("dep.optic", "function dep() {}\n")
        main = self.source("main.optic", 'function main() {\n    return 0\n}\nimport dep; int x = 1\n')
        result = self.compile(main)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_multiple_imports_and_namespace_alias(self):
        self.source("system.optic", "function input() {}\n")
        self.source("utils.optic", "function is_numeric(s) -> bool { return true }\n")
        main = self.source("main.optic", 'import system as os, utils\nfunction main() {\n s = os::input(\"number: \")\n if (not utils::is_numeric(s)) { throw not_numeric_exception }\n}\n')
        result = self.compile(main)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count(b"ModuleAST ["), 3)
        self.assertIn(b" as os", result.stdout)
        self.assertIn(b"Qualified input", result.stdout)

    def test_malformed_import_list_and_alias(self):
        for text in ("import io as\n", "import io,\n", "import io as os extra\n", "call(\nimport missing\n)\n"):
            source = self.source("bad-import.optic", text)
            result = self.compile(source)
            self.assertEqual(result.returncode, 1)
            self.assertNotIn(b"Cannot open module", result.stderr)

    def test_error_handling_syntax(self):
        self.source("system.optic", "function read() {}\n")
        main = self.source("main.optic", 'import system\nfunction main() {\n const size_t bytes = 1024\n buffer = \"\"\n try {\n  if (read(system::stdin, bytes, buffer.data()) != 0) { return 1 }\n } catch (system::stream_exception &exception) { throw exception }\n}\n')
        result = self.compile(main)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"Catch system::stream_exception& exception", result.stdout)

    def test_dependency_syntax_error_is_located_in_its_file(self):
        dep = self.source("dep.optic", "function helper() {\n    int x =\n}\n")
        main = self.source("main.optic", "import dep\nfunction main() {}\n")
        result = self.compile(main)
        self.assertEqual(result.returncode, 1)
        self.assertIn(str(dep).encode(), result.stderr)
        self.assertIn(b"At line: 3", result.stderr)
        self.assertNotIn(b"Function helper", result.stdout)

    def test_unsupported_and_multiple_errors(self):
        source = self.source("bad.optic", "int = 1\nclass {}\nint = 2\n")
        result = self.compile(source)
        self.assertEqual(result.returncode, 1)
        self.assertGreaterEqual(result.stderr.count(b"Error:"), 2)
        self.assertNotIn(b"ModuleAST", result.stdout)


if __name__ == "__main__":
    unittest.main()
