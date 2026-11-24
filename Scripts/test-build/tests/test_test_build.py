import hashlib, io, os, pathlib, sys, tempfile, unittest, zipfile
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import testdrive, make_test_install as maker, deploy_test_build as deploy


class TestBuild(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.drive = self.root / 'drive'
        self.drive.mkdir()
        self.source = self.root / 'source'
        self.source.mkdir()
        self.fixtures = self.root / 'fixtures'
        self.fixtures.mkdir()
        self.manifest = self.fixtures / 'starter.txt'
        self.manifest.write_text('NES/roms/a.nes => NES/roms/more/a.nes\nNES/art/\n')
        (self.source / 'NES/roms').mkdir(parents=True)
        (self.source / 'NES/roms/a.nes').write_bytes(b'a')
        (self.source / 'NES/art').mkdir()
        (self.source / 'NES/art/pic').write_bytes(b'pic')
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w') as z:
            z.writestr('retrofe/retrofe.exe', b'exe')
            z.writestr('BUILD.txt', 'branch\nabcdef123\n2026-01-02\nsubject\nrun\n')
        blob = data.getvalue()
        self.fetch = patch.object(testdrive, 'fetch', side_effect=lambda name: hashlib.sha256(blob).hexdigest().encode() if name.endswith('.sha256') else blob)
        self.fetch.start()
        self.addCleanup(self.fetch.stop)
        self.fixture_patch = patch.object(maker, 'FIXTURES', str(self.fixtures))
        self.fixture_patch.start()
        self.addCleanup(self.fixture_patch.stop)

    def run_make(self, *extra):
        with patch.object(sys, 'argv', ['make_test_install.py', '--fixtures', 'starter', '--source', str(self.source), '--path', str(self.drive), *extra]):
            return maker.main()

    def test_manifest_rejections(self):
        for line in ['/absolute', '../a', 'a/../b', 'C:/a', 'a\\b', 'a => ', 'a => /b', 'a => b\nc => b', 'a => b\\c']:
            with self.subTest(line=line):
                self.manifest.write_text(line)
                with self.assertRaises(ValueError): maker.manifest(self.manifest)
        self.manifest.write_text('# comment\n a => b\n')
        self.assertEqual(maker.manifest(self.manifest), [('a', 'b')])

    def test_repo_manifests(self):
        # The committed lists parse: no bad paths, no duplicate targets
        for path in pathlib.Path(__file__).resolve().parents[1].glob('fixtures/*.txt'):
            with self.subTest(path=path.name):
                self.assertTrue(maker.manifest(path))

    def test_missing_before_copy(self):
        self.manifest.write_text('NES/roms/a.nes\nmissing\nmissing2\n')
        self.assertEqual(self.run_make(), 1)
        self.assertEqual(list(self.drive.iterdir()), [])

    def test_staging_and_installs(self):
        bare = self.drive / 'RetroFE-Bare'
        bare.mkdir()
        (bare / 'keep').write_text('bare')
        self.assertEqual(self.run_make(), 0)
        staged = self.drive / 'fixtures/starter/NES'
        self.assertEqual((staged / 'roms/more/a.nes').read_bytes(), b'a')
        self.assertEqual((staged / 'art/pic').read_bytes(), b'pic')
        install = self.drive / 'RetroFE-Starter'
        self.assertEqual((install / 'retrofe/retrofe.exe').read_bytes(), b'exe')
        (install / 'keep').write_text('old')
        (staged / 'art/pic').write_bytes(b'old')
        (staged / 'roms/more/a.nes').write_bytes(b'longer')
        self.assertEqual(self.run_make(), 0)
        self.assertEqual((staged / 'art/pic').read_bytes(), b'old')
        self.assertEqual((staged / 'roms/more/a.nes').read_bytes(), b'a')
        self.assertTrue((install / 'keep').exists())
        self.assertEqual(self.run_make('--reset'), 0)
        self.assertFalse((install / 'keep').exists())
        self.assertEqual(len(list((self.drive / 'backups').glob('RetroFE-Starter-*/keep'))), 1)
        self.assertEqual((bare / 'keep').read_text(), 'bare')

    def test_discovery(self):
        drive = testdrive.Local(str(self.drive))
        self.assertEqual(deploy.discover_installs(drive), ['RetroFE-Bare'])
        for name in ['RetroFE-Z', 'RetroFE-A', 'Other']:
            path = self.drive / name / 'retrofe'
            path.mkdir(parents=True)
            (path / 'retrofe.exe').touch()
        (self.drive / 'RetroFE-Empty').mkdir()
        self.assertEqual(deploy.discover_installs(drive), ['RetroFE-A', 'RetroFE-Z'])


if __name__ == '__main__': unittest.main()
