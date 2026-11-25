import importlib.util
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1]/'screens'/'screens.py'
spec = importlib.util.spec_from_file_location('screens', SCRIPT)
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)

class ScreensTests(unittest.TestCase):
    def test_parser(self):
        steps = s.parse_script('# hi\nwait 1\nwait-log hello world 2\nkey Down x3 hold 0.2\ntype  a b  \nshot view\nexpect-file x/y words here\nexpect-log hi\nquit\n')
        self.assertEqual(len(steps), 8)
        self.assertEqual(steps[3]['args'], [' a b  '])
        self.assertEqual(steps[1]['args'], ['hello world', 2])
        for bad in ['key Nope', 'wait -1', 'shot ../x', 'quit now', 'wat', 'expect-file ../x', 'wait-log']:
            with self.assertRaisesRegex(ValueError, 'line 2:'): s.parse_script('\n'+bad)
    def test_keys(self):
        self.assertEqual(s.key_code('a'), 65)
        self.assertEqual(s.key_lparam(65, 30), 1 | (30 << 16))
        self.assertEqual(s.key_lparam(s.key_code('Up'), 72, True), 1 | (72 << 16) | (1 << 24) | (3 << 30))
        with self.assertRaises(ValueError): s.key_code('invalid')
    def test_png(self):
        rgb = bytes([1,2,3,4,5,6]); data = s.png(2,1,rgb)
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        self.assertEqual(struct.unpack('>II',data[16:24]), (2,1))
        length = struct.unpack('>I', data[33:37])[0]
        self.assertEqual(zlib.decompress(data[41:41+length]), b'\0'+rgb)
        self.assertTrue(s.is_blank(b'\1\2\3'*3)); self.assertFalse(s.is_blank(rgb))
    def test_parse_failure_writes_results_without_starting(self):
        import contextlib
        import io
        import json
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); script = root/'script.txt'; script.write_text('wait 0\nkey nope\n')
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(s.run(root, script, root/'out'), 1)
            result = json.loads((root/'out/results.json').read_text())
            self.assertTrue(output.getvalue().startswith('FAIL line 2:'))
            self.assertEqual(result['steps'][0]['line'], 2)
            self.assertFalse(result['steps'][0]['ok'])

    def test_installs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); dlls = root/'dlls'; dlls.mkdir(); (dlls/'SDL2.dll').write_bytes(b'dll'); (dlls/'README.md').touch(); (dlls/'dir').mkdir()
            common = root/'common'; layout = common/'layouts'/'Aeon Nox'; layout.mkdir(parents=True); (layout/'layout.xml').touch()
            exe = root/'exe'; exe.touch(); fake = root/'fake'; fake.touch()
            bare = root/'bare'; s.install('bare',exe,dlls,common,bare)
            self.assertEqual({str(p.relative_to(bare)) for p in bare.rglob('*') if p.is_file()}, {'retrofe/retrofe.exe','retrofe/SDL2.dll','layouts/Aeon Nox/layout.xml'})
            out = root/'starter'; s.install('starter',exe,dlls,common,out,fake)
            for _, target in s.make_test_install.manifest(Path(s.make_test_install.FIXTURES)/'starter.txt'):
                if '/roms/' in target: self.assertTrue((out/'collections'/target).exists(), target)
            self.assertFalse(list(out.rglob('medium_artwork')))
            self.assertIn('Missing Game (USA)', (out/'collections/Nintendo Entertainment System/include.txt').read_text())
            self.assertTrue((out/'emulators/RetroArch/retroarch.exe').is_file())
            with self.assertRaises(ValueError): s.install('bare',exe,dlls,common,out)

if __name__ == '__main__': unittest.main()
