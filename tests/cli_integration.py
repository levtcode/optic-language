"""Exercise the CLI/compiler contract with actual executables and isolated files.

Run through CTest, which supplies optic, opticc and the fixture executable paths.
Every subprocess has a timeout so a regression cannot leave a test hanging.
"""
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest

OPTIC, OPTICC, FIXTURE = map(lambda arg: Path(arg).resolve(), sys.argv[1:4])
sys.argv = sys.argv[:1]


class CliIntegration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="optic cli ")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.env = os.environ.copy()
        for key in ("OPTIC_COMPILER", "OPTIC_TEST_EXIT", "OPTIC_TEST_SIGNAL"):
            self.env.pop(key, None)

    def run_tool(self, executable, *args):
        return subprocess.run([str(executable), *map(str, args)], cwd=self.directory,
                              env=self.env, capture_output=True, timeout=5)

    def source(self, name="source with spaces.optic", contents="function main() { return 1 }\n"):
        path = self.directory / name
        path.write_text(contents)
        return path

    def test_help_version_and_usage(self):
        for executable in (OPTIC, OPTICC):
            self.assertEqual(self.run_tool(executable).returncode, 2)
            for arg in ("help", "--help", "-h", "--version"):
                with self.subTest(executable=executable, arg=arg):
                    self.assertEqual(self.run_tool(executable, arg).returncode, 0)
            self.assertEqual(self.run_tool(executable, "unknown").returncode, 2)
        for mode in ("build", "run", "debug"):
            self.assertEqual(self.run_tool(OPTIC, mode, "--help").returncode, 0)

    def test_unimplemented_commands(self):
        for command in ("install", "uninstall", "test"):
            result = self.run_tool(OPTIC, command)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"not implemented", result.stderr)

    def test_delegation_preserves_arguments_and_exit_code(self):
        self.env["OPTIC_COMPILER"] = str(FIXTURE)
        self.env["OPTIC_TEST_EXIT"] = "37"
        # No shell may interpret these as substitutions, separators or globbing.
        arguments = ["debug", "space name.optic", "", 'quote"here', "a\\b\\",
                     "$(touch injected)", "x; touch injected", "*", "--", "-file.optic"]
        result = self.run_tool(OPTIC, *arguments)
        self.assertEqual(result.returncode, 37)
        self.assertEqual(result.stdout.split(b"\0")[:-1], [x.encode() for x in arguments])
        self.assertIn(b"component stderr", result.stderr)
        self.assertFalse((self.directory / "injected").exists())

    def test_component_path_with_spaces(self):
        component = self.directory / FIXTURE.name
        shutil.copy2(FIXTURE, component)
        self.env["OPTIC_COMPILER"] = str(component)
        self.assertEqual(self.run_tool(OPTIC, "build", "a.optic").returncode, 0)

    def test_missing_component(self):
        self.env["OPTIC_COMPILER"] = str(self.directory / "missing-compiler")
        result = self.run_tool(OPTIC, "debug", "a.optic")
        self.assertEqual(result.returncode, 127)
        self.assertIn(b"cannot launch", result.stderr)
        self.assertEqual(self.run_tool(OPTIC, "--help").returncode, 0)

    def test_sibling_component_and_path_fallback(self):
        cli = self.directory / OPTIC.name
        component = self.directory / OPTICC.name
        shutil.copy2(OPTIC, cli)
        shutil.copy2(FIXTURE, component)
        self.env["OPTIC_TEST_EXIT"] = "23"
        self.assertEqual(self.run_tool(cli, "build", "a.optic").returncode, 23)
        search_dir = self.directory / "search path"
        search_dir.mkdir()
        component.rename(search_dir / component.name)
        self.env["PATH"] = str(search_dir) + os.pathsep + self.env.get("PATH", "")
        self.assertEqual(self.run_tool(cli, "build", "a.optic").returncode, 23)

    @unittest.skipIf(os.name == "nt", "POSIX signal statuses")
    def test_child_signal_status(self):
        self.env["OPTIC_COMPILER"] = str(FIXTURE)
        self.env["OPTIC_TEST_SIGNAL"] = "1"
        self.assertEqual(self.run_tool(OPTIC, "debug", "a.optic").returncode,
                         128 + signal.SIGTERM)

    def test_frontend_debug_and_option_values(self):
        self.env["OPTIC_COMPILER"] = str(OPTICC)
        source = self.source()
        result = self.run_tool(OPTIC, "debug", "--color-diagnostics=disable", "-g", "-g", source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"Token type:", result.stdout)
        self.assertIn(b"--color-diagnostics = disable", result.stdout)
        self.assertIn(b"-g -> on", result.stdout)
        self.assertIn(str(source).encode(), result.stdout)
        self.assertEqual(self.run_tool(OPTICC, "debug", source).returncode, 0)
        dash_source = self.source("-dash.optic")
        self.assertEqual(self.run_tool(OPTICC, "debug", "--", dash_source.name).returncode, 0)

    def test_debug_two_sources_in_vscode_through_cli(self):
        directory = self.directory / ".vscode"
        directory.mkdir()
        (directory / "file1.optic").write_text('import file2\nfunction main() { print(hello("levtcode")) return 0 }\n')
        (directory / "file2.optic").write_text('function hello(string name) { return "Hola " + name + "!"; }\n')
        # Use sibling compiler discovery, as in optic debug .vscode/file1.optic .vscode/file2.optic.
        result = self.run_tool(OPTIC, "debug", ".vscode/file1.optic", ".vscode/file2.optic")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"[opticc] Executable:", result.stdout)
        self.assertIn(b"[preprocessor] Imported " + str(directory / "file2.optic").encode(), result.stdout)
        self.assertIn(b"Modules: 2, edges: 1, ready: 2, failed: 0", result.stdout)
        self.assertIn(b"Dependency-first order: [2, 1]", result.stdout)
        self.assertEqual(result.stdout.count(b"Token value: 'hello'"), 2)

    def test_compiler_errors_and_backend_limitations(self):
        source = self.source()
        for arguments in (("debug",), ("debug", ""), ("debug", "--unknown", source),
                          ("debug", "--color-diagnostics=", source),
                          ("debug", "--color-diagnostics=true", source),
                          ("debug", "--color-diagnostics=invalid", source),
                          ("debug", "missing.optic"),
                          ("debug", self.source("wrong.txt")),
                          ("debug", self.source("comment.optic", "/* unclosed"))):
            with self.subTest(arguments=arguments):
                self.assertNotEqual(self.run_tool(OPTICC, *arguments).returncode, 0)
        for mode in ("build", "run"):
            result = self.run_tool(OPTICC, mode, source)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"not implemented", result.stderr)
            self.assertFalse(source.with_suffix(".exe").exists())


if __name__ == "__main__":
    unittest.main()
