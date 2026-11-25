import contextlib, hashlib, io, os, pathlib, sys, tempfile, unittest, zipfile
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
        dlls = self.root / 'dlls'
        dlls.mkdir()
        (dlls / 'SDL2.dll').write_bytes(b'dll')
        (dlls / 'README.md').write_text('not copied')
        self.dlls = patch.object(testdrive, 'DLLS', str(dlls))
        self.dlls.start()
        self.addCleanup(self.dlls.stop)
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
        self.assertEqual((install / 'retrofe/SDL2.dll').read_bytes(), b'dll')
        self.assertFalse((install / 'retrofe/README.md').exists())
        (install / 'keep').write_text('old')
        (install / 'retrofe/retrofe.exe').write_bytes(b'marker')
        (staged / 'art/pic').write_bytes(b'old')
        (staged / 'roms/more/a.nes').write_bytes(b'longer')
        self.assertEqual(self.run_make(), 0)
        self.assertEqual((staged / 'art/pic').read_bytes(), b'old')
        self.assertEqual((staged / 'roms/more/a.nes').read_bytes(), b'a')
        self.assertTrue((install / 'keep').exists())
        self.assertEqual((install / 'retrofe/retrofe.exe').read_bytes(), b'marker')
        self.assertEqual(self.run_make('--reset'), 0)
        self.assertFalse((install / 'keep').exists())
        self.assertEqual(len(list((self.drive / 'backups').glob('RetroFE-Starter-*/keep'))), 1)
        self.assertEqual((bare / 'keep').read_text(), 'bare')
        self.assertEqual([p.name for p in bare.iterdir()], ['keep'])

    def test_no_dlls(self):
        # Without the DLL submodule a new install would not start: stop instead
        os.remove(os.path.join(testdrive.DLLS, 'SDL2.dll'))
        with self.assertRaises(SystemExit):
            self.run_make()
        self.assertFalse((self.drive / 'RetroFE-Starter/retrofe/retrofe.exe').exists())

    def test_manifest_windows_and_overlap(self):
        for line in ['a => b/', 'a/ => b', 'a/ => x/\nb => x/y',
                     'b => x/y\na/ => x/', 'a => CON.txt', 'a => dir/Lpt9.log',
                     'a => bad.', 'a => bad /x', 'a => bad?/x', 'a => bad\x01']:
            with self.subTest(line=line):
                self.manifest.write_text(line + '\n')
                with self.assertRaises(ValueError): maker.manifest(self.manifest)
        self.manifest.write_bytes(b'\xef\xbb\xbfa => b\n')
        self.assertEqual(maker.manifest(self.manifest), [('a', 'b')])

    def test_paths_rejected_before_writes(self):
        for path, source in [(self.root / 'missing', self.source),
                             (self.drive, self.drive), (self.drive, self.drive / 'nested'),
                             (self.drive, self.root)]:
            with self.subTest(path=path, source=source):
                with patch.object(sys, 'argv', ['make', '--fixtures', 'starter', '--path', str(path), '--source', str(source)]):
                    self.assertEqual(maker.main(), 1)
                self.assertEqual(list(self.drive.iterdir()), [])
        alias = self.root / 'alias'
        try:
            alias.symlink_to(self.drive, target_is_directory=True)
        except OSError:
            pass  # Windows may not permit creating symlinks.
        else:
            with patch.object(sys, 'argv', ['make', '--fixtures', 'starter', '--path', str(alias), '--source', str(self.drive)]):
                self.assertEqual(maker.main(), 1)
        with patch.object(sys, 'argv', ['deploy', '--path', str(self.root / 'missing')]):
            self.assertEqual(deploy.main(), 1)

    def test_names_and_printed_paths(self):
        (self.fixtures / 'bigSet.txt').write_text('NES/roms/a.nes => deep/NES/roms/a.nes\nNES/art/ => deep/Other/roms/\n')
        out = io.StringIO()
        with patch.object(sys, 'argv', ['make', '--fixtures', 'bigSet', '--source', str(self.source), '--path', str(self.drive), '--pc', 'MYPC', '--share', 'games']), contextlib.redirect_stdout(out):
            self.assertEqual(maker.main(), 0)
        self.assertTrue((self.drive / 'RetroFE-BigSet').is_dir())
        self.assertIn(r'\\MYPC\games\fixtures\bigSet\deep\NES\roms', out.getvalue())
        self.assertIn(r'\\MYPC\games\fixtures\bigSet\deep\Other\roms', out.getvalue())
        (self.fixtures / 'bare.txt').write_text('NES/roms/a.nes\n')
        with patch.object(sys, 'argv', ['make', '--fixtures', 'bare', '--source', str(self.source), '--path', str(self.drive)]):
            self.assertEqual(maker.main(), 1)
        with patch.object(sys, 'argv', ['make', '--fixtures', 'bare', '--source', str(self.source), '--path', str(self.drive), '--install', 'RetroFE-Bare']):
            self.assertEqual(maker.main(), 0)
        self.assertTrue((self.drive / 'RetroFE-Bare/retrofe/retrofe.exe').exists())

    def test_reset_restores_after_failure(self):
        self.assertEqual(self.run_make(), 0)
        install = self.drive / 'RetroFE-Starter'
        (install / 'original').write_text('kept')
        def fail(drive, name, build):
            drive.makedirs(drive.join(name))
            pathlib.Path(drive.join(name, 'partial')).write_text('partial')
            raise OSError('failed')
        with patch.object(maker, 'deploy_build', side_effect=fail):
            self.assertEqual(self.run_make('--reset'), 1)
        self.assertEqual((install / 'original').read_text(), 'kept')
        self.assertEqual(len(list((self.drive / 'backups').glob('RetroFE-Starter-*-failed/partial'))), 1)

    def test_reset_missing_dll_preserves_install(self):
        self.assertEqual(self.run_make(), 0)
        os.remove(os.path.join(testdrive.DLLS, 'SDL2.dll'))
        with self.assertRaises(SystemExit): self.run_make('--reset')
        self.assertTrue((self.drive / 'RetroFE-Starter/retrofe/retrofe.exe').exists())
        self.assertFalse((self.drive / 'backups').exists())

    def test_deploy_reports_all_failures(self):
        out = io.StringIO()
        with patch.object(sys, 'argv', ['deploy', '--path', str(self.drive), '--install', 'one', '--install', 'two']), \
             patch.object(deploy, 'deploy_build', side_effect=[False, True]) as run, contextlib.redirect_stderr(out):
            self.assertEqual(deploy.main(), 1)
        self.assertEqual(run.call_count, 2)
        self.assertIn('one', out.getvalue())

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
