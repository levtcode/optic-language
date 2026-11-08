"""Run real compiler preprocessing sessions with isolated module trees."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

OPTICC = Path(sys.argv[1]).resolve()
sys.argv = sys.argv[:1]


class PreprocessorIntegration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="optic modules ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def source(self, name, contents):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")
        return path

    def run_compiler(self, *files, mode="debug"):
        return subprocess.run([str(OPTICC), mode, "--color-diagnostics=disable", *map(str, files)],
                              cwd=self.root, capture_output=True, timeout=10)

    def test_dependency_order_and_aliases(self):
        leaf = self.source("lib/leaf.optic", "leaf_value\n")
        mid = self.source("mid.optic", 'import lib.leaf\nmid_value\n')
        main = self.source("main.optic", 'import "mid";\nimport "lib/leaf.optic";\nimport "./mid.optic";\nmain_value\n')
        result = self.run_compiler(main, mid)
        self.assertEqual(result.returncode, 0, result.stderr)
        values = [b"Token value: '" + value + b"'" for value in (b"leaf_value", b"mid_value", b"main_value")]
        for value in values:
            self.assertEqual(result.stdout.count(value), 1)
        self.assertLess(result.stdout.index(values[0]), result.stdout.index(values[1]))
        self.assertLess(result.stdout.index(values[1]), result.stdout.index(values[2]))
        self.assertNotIn(b"Token type: Import", result.stdout)
        self.assertEqual(result.stdout.count(b"Token type: EndOfFile"), 3)
        self.assertIn(str(leaf).encode(), result.stdout)

    def test_relative_to_importer_and_spaces(self):
        self.source("folder with spaces/dep.optic", "dependency\n")
        main = self.source("folder with spaces/main.optic", 'import "dep.optic"\nmain\n')
        result = self.run_compiler(main)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"Token value: 'dependency'", result.stdout)

    def test_cycle_and_self_import_are_errors(self):
        a = self.source("a.optic", 'import "b.optic"\n')
        self.source("b.optic", 'import "a.optic"\n')
        result = self.run_compiler(a)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"Import cycle", result.stderr)
        a.write_text('import "a.optic"\n')
        self.assertIn(b"Import cycle", self.run_compiler(a).stderr)

    def test_malformed_missing_and_lexically_invalid_imports(self):
        self.source("bad.optic", "/* unclosed")
        for text in ('import\n', 'import a.\n', 'import ""\n', 'import "x.txt"\n',
                     'import "missing.optic"\n', 'import "bad.optic"\n',
                     'import "bad.optic" extra\n', 'function f() {\nimport bad\n}\n'):
            with self.subTest(text=text):
                main = self.source("main.optic", text)
                result = self.run_compiler(main)
                self.assertEqual(result.returncode, 1)
                self.assertIn(str(main).encode(), result.stderr)

    def test_comments_literals_eof_and_locations(self):
        main = self.source("main.optic", '// import missing\n/* import missing */\n"import missing /"\nidentifier_1/2')
        result = self.run_compiler(main)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"Token value: 'identifier_1'\nAt line: 4, in column: 1", result.stdout)
        self.assertIn(b"Token type: Slash", result.stdout)
        self.assertIn(b"Token value: 'import missing /'", result.stdout)
        empty = self.source("empty.optic", "")
        self.assertEqual(self.run_compiler(empty).returncode, 0)
        end = self.source("end.optic", "/* comment */")
        self.assertEqual(self.run_compiler(end).returncode, 0)

    @unittest.skipIf(os.name == "nt", "symlink creation is privilege-dependent on Windows")
    def test_symlink_is_one_module(self):
        dep = self.source("dep.optic", "only_once")
        (self.root / "alias.optic").symlink_to(dep)
        main = self.source("main.optic", 'import "dep.optic"\nimport "alias.optic"\n')
        result = self.run_compiler(main)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count(b"Token value: 'only_once'"), 1)

    def test_directories_and_special_files_are_rejected(self):
        (self.root / "directory.optic").mkdir()
        main = self.source("main.optic", 'import "directory.optic"\n')
        result = self.run_compiler(main)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"regular source file", result.stderr)
        if hasattr(os, "mkfifo"):
            os.mkfifo(self.root / "pipe.optic")
            main.write_text('import "pipe.optic"\n')
            result = self.run_compiler(main)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"regular source file", result.stderr)

    def test_import_depth_limit_is_diagnostic(self):
        for i in range(258):
            self.source(f"m{i}.optic", f'import "m{i+1}.optic"\n' if i < 257 else "leaf")
        result = self.run_compiler(self.root / "m0.optic")
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"Import nesting exceeds", result.stderr)

    def test_debug_mode_is_independent_of_build_configuration(self):
        main = self.source("main.optic", "hello")
        self.assertIn(b"MODULE DEPENDENCIES", self.run_compiler(main).stdout)
        result = self.run_compiler(main, mode="build")
        self.assertNotIn(b"Token type:", result.stdout)
        self.assertIn(b"not implemented", result.stderr)


if __name__ == "__main__":
    unittest.main()
