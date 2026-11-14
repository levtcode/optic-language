"""Run real compiler preprocessing sessions with isolated module trees."""
from pathlib import Path
import os
import re
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
        values = [b'"' + value + b'"' for value in (b"leaf_value", b"mid_value", b"main_value")]
        for value in values:
            self.assertEqual(result.stdout.count(value), 1)
        self.assertLess(result.stdout.index(values[0]), result.stdout.index(values[1]))
        self.assertLess(result.stdout.index(values[1]), result.stdout.index(values[2]))
        self.assertNotIn(b"  Import  ", result.stdout)
        self.assertEqual(result.stdout.count(b"EndOfFile"), 3)
        self.assertIn(str(leaf).encode(), result.stdout)

    def test_relative_to_importer_and_spaces(self):
        self.source("folder with spaces/dep.optic", "dependency\n")
        main = self.source("folder with spaces/main.optic", 'import "dep.optic"\nmain\n')
        result = self.run_compiler(main)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, rb'Identifier +"dependency"')

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
        self.assertRegex(result.stdout, rb'4:1 +Identifier +"identifier_1"')
        self.assertRegex(result.stdout, rb'Slash +"/"')
        self.assertRegex(result.stdout, rb'StringLiteral +"import missing /"')
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
        self.assertEqual(len(re.findall(rb'Identifier +"only_once"', result.stdout)), 1)

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

    def test_debug_reports_imports_graph_reuse_and_counts(self):
        dep = self.source("dep.optic", "dependency")
        main = self.source("main.optic", 'import dep\nimport dep\nmain_value')
        for files in ((main, dep), (dep, main)):
            with self.subTest(files=files):
                result = self.run_compiler(*files)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(b"[opticc] Mode: debug", result.stdout)
                self.assertIn(b"[preprocessor] Imported " + str(dep).encode(), result.stdout)
                self.assertIn(b"Reusing module", result.stdout)
                self.assertIn(b"Already processed module", result.stdout)
                self.assertIn(b"Import requests: 2", result.stdout)
                self.assertIn(b"Successful imports: 2", result.stdout)
                self.assertIn(b"Failed imports: 0", result.stdout)
                self.assertIn(b"Modules: 2, edges: 1, ready: 2, failed: 0", result.stdout)
                self.assertIn(b"Imported by:", result.stdout)
                self.assertIn(b"Dependency-first order:", result.stdout)
                self.assertEqual(len(re.findall(rb'Identifier +"dependency"', result.stdout)), 1)
        for mode in ("build", "run"):
            result = self.run_compiler(main, dep, mode=mode)
            for marker in (b"[opticc]", b"[preprocessor]", b"PREPROCESSOR SUMMARY", b"MODULE DEPENDENCIES"):
                self.assertNotIn(marker, result.stdout)

    def test_debug_graph_is_visible_when_preprocessing_fails(self):
        self.source("good.optic", "ready")
        main = self.source("main.optic", 'import good\nimport missing\n')
        result = self.run_compiler(main)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"Result: failed", result.stdout)
        self.assertIn(b"Load failed", result.stdout)
        self.assertIn(b"Modules: 2, edges: 1, ready: 1, failed: 1", result.stdout)
        self.assertIn(b"Successful imports: 1", result.stdout)
        self.assertIn(b"Failed imports: 1", result.stdout)
        self.assertIn(b"Token dump skipped", result.stdout)
        self.assertNotIn(b"Representation", result.stdout)
        missing = self.run_compiler("absent.optic")
        self.assertIn(b"Modules: 0, edges: 0", missing.stdout)
        self.assertIn(b"Result: failed", missing.stdout)
        self.source("a.optic", 'import b\n')
        self.source("b.optic", 'import a\n')
        cycle = self.run_compiler("a.optic")
        self.assertIn(b"Import cycle", cycle.stderr)
        self.assertIn(b"ready: 0, failed: 2", cycle.stdout)
        self.assertIn(b"Order describes accepted edges", cycle.stdout)

    def test_macros_are_disabled_without_evaluation(self):
        main = self.source("main.optic", "macro M { value: 1 / 0; expression: ? }\nM\n")
        result = self.run_compiler(main)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"Macros are currently disabled", result.stderr)
        self.assertNotIn(b"Division by zero", result.stderr)
        self.assertNotIn(b"PREPROCESSOR MACROS", result.stdout)
        self.source("dep.optic", "macro M { value: 42; }\n")
        main.write_text('import "dep.optic"\nM\n')
        result = self.run_compiler(main)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"Macros are currently disabled", result.stderr)

    def test_identifiers_are_not_expanded(self):
        main = self.source("main.optic", "M.to_string()\n")
        result = self.run_compiler(main)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, rb'Identifier +"M"')
        self.assertRegex(result.stdout, rb'Identifier +"to_string"')
        self.assertNotIn(b"PREPROCESSOR MACROS", result.stdout)

    def test_debug_mode_is_independent_of_build_configuration(self):
        main = self.source("main.optic", "hello")
        self.assertIn(b"MODULE DEPENDENCIES", self.run_compiler(main).stdout)
        result = self.run_compiler(main, mode="build")
        self.assertNotIn(b"Representation", result.stdout)
        self.assertIn(b"not implemented", result.stderr)


if __name__ == "__main__":
    unittest.main()
