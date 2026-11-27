import contextlib
import importlib.util
import io
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'screens' / 'screens.py'
spec = importlib.util.spec_from_file_location('screens', SCRIPT)
screens = importlib.util.module_from_spec(spec)
spec.loader.exec_module(screens)


class ScreensTests(unittest.TestCase):
    def setUp(self):
        """Create a small release and a finished setup."""
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.dlls = self.root / 'dlls'
        self.dlls.mkdir()
        (self.dlls / 'SDL2.dll').write_bytes(b'dll')
        (self.dlls / 'README.md').touch()
        (self.dlls / 'dir').mkdir()
        self.common = self.root / 'common'
        layout = self.common / 'layouts' / 'Aeon Nox'
        layout.mkdir(parents=True)
        (layout / 'layout.xml').touch()
        self.exe = self.root / 'exe'
        self.exe.touch()
        self.setup = self.root / 'setup'
        self.nes = self.setup / 'collections' / 'Nintendo Entertainment System'
        (self.nes / 'roms').mkdir(parents=True)
        (self.nes / 'roms' / 'First.nes').touch()
        (self.nes / 'roms' / 'Second.dat').touch()
        (self.nes / 'settings.conf').write_bytes(b'launcher = NES\r\nlist.extensions = nes,dat\r\n')
        (self.setup / 'launchers.linux').mkdir()
        launches = self.setup / 'emulators' / 'RetroArch' / 'launches.txt'
        launches.parent.mkdir(parents=True)
        launches.write_text('old')

    def install(self, kind, setup=None):
        """Build an install in the test directory."""
        output = self.root / 'output'
        screens.install(kind, self.exe, self.dlls, self.common, output, setup)
        return output

    def test_parser_commands(self):
        """Parse all supported commands and preserve typed spaces."""
        steps = screens.parse_script('# hi\nwait 1\nwait-log hello world 2\nkey Down x3 hold 0.2\ntype  a b  \nshot view\nexpect-file x/y words here\nexpect-log hi\nquit\n')
        self.assertEqual(len(steps), 8)
        self.assertEqual(steps[3]['args'], [' a b  '])
        self.assertEqual(steps[1]['args'], ['hello world', 2])

    def test_region_difference(self):
        before = bytes(10 * 10 * 3)
        inside = bytearray(before)
        inside[(7 * 10 + 5) * 3] = 100
        outside = bytearray(before)
        outside[0] = 100
        region = (0.2, 0.5, 0.8, 1.0)
        self.assertFalse(screens.region_differs(10, 10, before, before, region))
        self.assertTrue(screens.region_differs(10, 10, before, inside, region))
        self.assertFalse(screens.region_differs(10, 10, before, outside, region))

    def test_expect_differs_parse(self):
        self.assertEqual(screens.parse_script('expect-differs view 0.1 0.5 0.9 1')[0]['args'], ['view', 0.1, 0.5, 0.9, 1.0])
        for command in ('expect-differs view 0 0 2 1', 'expect-differs view 0 1 1 0', 'quit\nwait 1'):
            with self.assertRaises(ValueError):
                screens.parse_script(command)

    def test_wait_drawn(self):
        """wait-drawn takes an optional timeout, 60 seconds by default."""
        self.assertEqual(screens.parse_script('wait-drawn\n')[0]['args'], [60.0])
        self.assertEqual(screens.parse_script('wait-drawn 90\n')[0]['args'], [90.0])
        with self.assertRaisesRegex(ValueError, 'line 1:'):
            screens.parse_script('wait-drawn 0\n')

    def test_parser_rejects_bad_commands(self):
        """Report the line for malformed commands."""
        for bad in ['key Nope', 'wait -1', 'shot ../x', 'quit now', 'wat', 'expect-file ../x', 'wait-log']:
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, 'line 2:'):
                screens.parse_script('\n' + bad)

    def test_default_hold(self):
        """A key without a hold spans keyboard samples."""
        self.assertEqual(screens.parse_script('key Right x2')[0]['args'][2], 0.1)

    def test_keys(self):
        """Encode ordinary and extended key messages."""
        self.assertEqual(screens.key_code('a'), 65)
        self.assertEqual(screens.key_lparam(65, 30), 1 | (30 << 16))
        self.assertEqual(screens.key_lparam(screens.key_code('Up'), 72, True), 1 | (72 << 16) | (1 << 24) | (3 << 30))
        with self.assertRaises(ValueError):
            screens.key_code('invalid')

    def test_png(self):
        """Encode RGB pixels in a PNG."""
        rgb = bytes([1, 2, 3, 4, 5, 6])
        data = screens.png(2, 1, rgb)
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        self.assertEqual(struct.unpack('>II', data[16:24]), (2, 1))
        length = struct.unpack('>I', data[33:37])[0]
        self.assertEqual(zlib.decompress(data[41:41 + length]), b'\0' + rgb)

    def test_exit_text(self):
        """Crash codes read as Windows shows them; ordinary codes stay decimal."""
        self.assertEqual(screens.exit_text(-1073741819), '0xC0000005')
        self.assertEqual(screens.exit_text(3221225781), '0xC0000135')
        self.assertEqual(screens.exit_text(1), '1')
        self.assertEqual(screens.exit_text(None), 'none')

    def test_blank(self):
        """Only uniform captures are blank."""
        self.assertTrue(screens.is_blank(b'\1\2\3' * 3))
        self.assertFalse(screens.is_blank(bytes([1, 2, 3, 4, 5, 6])))

    def test_parse_failure_writes_results_without_starting(self):
        """Write the invalid line to results without starting RetroFE."""
        script = self.root / 'script.txt'
        script.write_text('wait 0\nkey nope\n')
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(screens.run(self.root, script, self.root / 'out'), 1)
        result = json.loads((self.root / 'out/results.json').read_text())
        self.assertTrue(output.getvalue().startswith('FAIL line 2:'))
        self.assertEqual(result['steps'][0]['line'], 2)
        self.assertFalse(result['steps'][0]['ok'])
        self.assertEqual(result['failure_kind'], 'setup')

    def test_run_failure_kinds_and_wait_file_content(self):
        """Classify process crashes and wait for complete file contents."""
        class Process:
            pid = 1
            returncode = None
            def poll(self):
                return self.returncode
            def kill(self):
                self.returncode = 1
            def wait(self):
                return self.returncode

        process = Process()
        class Window:
            def window(self, pid):
                return 1
            def post(self, *args):
                process.returncode = 0

        script = self.root / 'script.txt'
        script.write_text('wait-file launches.txt 30 expected game\nquit\n')
        launches = self.root / 'launches.txt'
        launches.write_text('partial')
        def finish_write(seconds):
            launches.write_text('expected game')
        with patch.object(screens, 'Windows', Window), patch.object(screens.subprocess, 'Popen', return_value=process), patch.object(screens.time, 'sleep', side_effect=finish_write):
            self.assertEqual(screens.run(self.root, script, self.root / 'out'), 0)
        result = json.loads((self.root / 'out/results.json').read_text())
        self.assertTrue(result['steps'][0]['ok'])
        self.assertEqual(launches.read_text(), 'expected game')

        process = Process()
        process.returncode = -1073741819
        with patch.object(screens, 'Windows', Window), patch.object(screens.subprocess, 'Popen', return_value=process), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(screens.run(self.root, script, self.root / 'crash'), 1)
        result = json.loads((self.root / 'crash/results.json').read_text())
        self.assertEqual(result['failure_kind'], 'crash')
        self.assertEqual(result['exit_code'], '0xC0000005')

        process = Process()
        script.write_text('expect-file missing\n')
        with patch.object(screens, 'Windows', Window), patch.object(screens.subprocess, 'Popen', return_value=process), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(screens.run(self.root, script, self.root / 'step'), 1)
        result = json.loads((self.root / 'step/results.json').read_text())
        self.assertEqual(result['failure_kind'], 'step')

    def test_bare_install(self):
        """Copy only the release files needed for a bare install."""
        output = self.install('bare')
        files = {path.relative_to(output).as_posix() for path in output.rglob('*') if path.is_file()}
        self.assertEqual(files, {'retrofe/retrofe.exe', 'retrofe/SDL2.dll', 'layouts/Aeon Nox/layout.xml'})

    def test_starter_install(self):
        """Copy the setup and add a complete include list and missing item."""
        output = self.install('starter', self.setup)
        nes = output / 'collections' / self.nes.name
        self.assertTrue((output / 'launchers.linux').is_dir())
        self.assertTrue((nes / 'roms' / 'First.nes').is_file())
        self.assertFalse((output / 'emulators/RetroArch/launches.txt').exists())
        self.assertEqual(set((nes / 'include.txt').read_text().splitlines()), {'First', 'Second', 'Missing Game (USA)'})
        self.assertEqual((nes / 'settings.conf').read_bytes(), b'launcher = NES\r\nlist.extensions = nes,dat\r\nlist.includeMissingItems = true\r\n')

    def test_setup_install(self):
        """Only the pre-setup emulator and downloads reach the install."""
        handover = self.root / 'handover'
        retroarch = handover / 'root/emulators/RetroArch'
        (retroarch / 'cores').mkdir(parents=True)
        (retroarch / 'info').mkdir()
        (retroarch / 'retroarch.exe').touch()
        (retroarch / 'launches.txt').touch()
        (handover / 'root/settings.conf').touch()
        (handover / 'downloads').mkdir()
        (handover / 'downloads/game.nes').touch()
        output = self.install('setup', handover)
        self.assertTrue((output / 'downloads/game.nes').is_file())
        self.assertTrue((output / 'emulators/RetroArch/retroarch.exe').is_file())
        self.assertFalse((output / 'settings.conf').exists())
        self.assertFalse((output / 'emulators/RetroArch/launches.txt').exists())

    def test_setup_requires_handover(self):
        with self.assertRaises(ValueError):
            self.install('setup')
        with self.assertRaises(ValueError):
            self.install('setup', self.setup)

    def test_wait_file_parse(self):
        self.assertEqual(screens.parse_script('wait-file "a b/file" 12')[0]['args'], ['a b/file', 12, None])
        self.assertEqual(screens.parse_script('wait-file x 30 expected game')[0]['args'], ['x', 30, 'expected game'])
        self.assertEqual(screens.parse_script('expect-file "a b/file" text')[0]['args'], ['a b/file', 'text'])
        for command in ('wait-file ../bad', 'wait-file x 0', 'wait-file x nope'):
            with self.assertRaises(ValueError):
                screens.parse_script(command)

    def test_nonempty_output_refused(self):
        """Do not overwrite an existing install."""
        self.install('bare')
        with self.assertRaises(ValueError):
            self.install('bare')

    def test_starter_requires_setup(self):
        """Starter needs the first-setup hand-over."""
        with self.assertRaises(ValueError):
            self.install('starter')

    def test_bare_rejects_setup(self):
        """Bare cannot receive a setup."""
        with self.assertRaises(ValueError):
            self.install('bare', self.setup)

    def test_setup_without_collections_refused(self):
        """A setup must contain collections."""
        import shutil
        shutil.rmtree(self.setup / 'collections')
        with self.assertRaises(ValueError):
            self.install('starter', self.setup)

    def test_setup_with_executable_refused(self):
        """A setup must not replace the release executable."""
        (self.setup / 'retrofe').mkdir()
        (self.setup / 'retrofe/retrofe.exe').touch()
        with self.assertRaises(ValueError):
            self.install('starter', self.setup)


if __name__ == '__main__':
    unittest.main()
