import contextlib, hashlib, io, os, pathlib, sys, tempfile, unittest, zipfile
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import testdrive, make_test_install as maker, deploy_test_build as deploy


class Fixture:
    """A source, fixtures, a drive folder and a faked download, for both drive kinds."""
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


class TestBuild(Fixture, unittest.TestCase):
    def run_make(self, *extra):
        with patch.object(sys, 'argv', ['make_test_install.py', '--fixtures', 'starter', '--source', str(self.source), '--path', str(self.drive), *extra]):
            return maker.main()

    def test_manifest_rejections(self):
        for line in ['/absolute', '../a', 'a/../b', 'C:/a', 'a\\b', 'a => ', 'a => /b', 'a => b\nc => b', 'a => b\\c']:
            with self.subTest(line=line):
                self.manifest.write_text(line)
                with self.assertRaises(ValueError): maker.manifest(self.manifest)
        self.manifest.write_text('# comment\n a => b\n')
        self.assertEqual(maker.manifest(self.manifest), [('collections', 'a', 'b', False)])

    def test_repo_manifests(self):
        # The committed lists parse: no bad paths, no duplicate targets
        for path in pathlib.Path(__file__).resolve().parents[1].glob('fixtures/*.txt'):
            with self.subTest(path=path.name):
                self.assertTrue(maker.manifest(path))

    def test_sections_optional_and_dry_run(self):
        appdata = self.root / 'appdata'
        (appdata / 'meta').mkdir(parents=True)
        (appdata / 'meta/list.xml').write_bytes(b'xml')
        self.manifest.write_text('[collections => collections/]\nNES/roms/\nNES/art/ ?\nmissing/ ?\n[appdata]\nmeta/list.xml ?\n')
        out = io.StringIO()
        with patch.object(sys, 'argv', ['make', '--fixtures', 'starter', '--source', str(self.source), '--appdata', str(appdata), '--dry-run']), contextlib.redirect_stdout(out):
            self.assertEqual(maker.main(), 0)
        self.assertIn('1 files, 1 bytes', out.getvalue())
        self.assertIn('Total: 3 files, 7 bytes', out.getvalue())
        self.assertIn('Not in OneSauce: collections: missing/', out.getvalue())
        self.assertEqual(list(self.drive.iterdir()), [])
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(self.run_make('--appdata', str(appdata)), 0)
        self.assertEqual((self.drive / 'fixtures/starter/meta/list.xml').read_bytes(), b'xml')
        self.assertIn(r'\\JOHN-DESKTOP\retrofe\fixtures\starter\collections\NES\roms', out.getvalue())
        self.manifest.write_text('missing\n')
        with patch.object(sys, 'argv', ['make', '--fixtures', 'starter', '--source', str(self.source), '--dry-run']):
            self.assertEqual(maker.main(), 1)

    def test_starter_entries_unchanged(self):
        path = pathlib.Path(__file__).resolve().parents[1] / 'fixtures/starter.txt'
        entries = maker.manifest(path)
        self.assertTrue(all(section == 'collections' and not optional for section, _, _, optional in entries))
        self.assertIn(('collections', 'Nintendo Entertainment System/roms/Contra (USA).nes', 'Nintendo Entertainment System/roms/More games/Contra (USA).nes', False), entries)

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
        self.assertEqual(maker.manifest(self.manifest), [('collections', 'a', 'b', False)])

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

class TestSmb(Fixture, unittest.TestCase):
    """The same runs over Smb, with a stand-in smbclient on a temporary folder."""
    def setUp(self):
        super().setUp()
        import types, shutil
        self.local_drive = self.drive
        self.drive = self.root / 'smb'
        self.drive.mkdir()
        credentials = self.root / 'credentials'
        credentials.write_text('user\npassword\n')
        self.credentials = str(credentials)
        prefix = r'\\host\share'
        def mapped(path):
            if not isinstance(path, str) or '/' in path or not (path == prefix or path.startswith(prefix + '\\')):
                raise ValueError('invalid SMB path: ' + str(path))
            parts = path[len(prefix):].strip('\\').split('\\')
            if any(p in ('.', '..') for p in parts): raise ValueError('invalid SMB path')
            return self.drive.joinpath(*parts)
        client = types.ModuleType('smbclient')
        path = types.ModuleType('smbclient.path')
        copy = types.ModuleType('smbclient.shutil')
        client.path, client.shutil = path, copy
        client.register_session = lambda host, username, password: self.assertEqual((host, username, password), ('host', 'user', 'password'))
        client.open_file = lambda path, mode: open(mapped(path), mode)
        client.makedirs = lambda path, exist_ok: os.makedirs(mapped(path), exist_ok=exist_ok)
        client.listdir = lambda path: os.listdir(mapped(path))
        def rename(a, b):
            source, target = mapped(a), mapped(b)
            if target.exists():
                raise FileExistsError(str(target))
            os.rename(source, target)
        client.rename = rename
        path.isdir = lambda p: os.path.isdir(mapped(p))
        path.exists = lambda p: os.path.exists(mapped(p))
        path.getsize = lambda p: os.path.getsize(mapped(p))
        copy.rmtree = lambda p: shutil.rmtree(mapped(p))
        modules = patch.dict(sys.modules, {'smbclient': client, 'smbclient.path': path, 'smbclient.shutil': copy})
        modules.start()
        self.addCleanup(modules.stop)

    def run_make(self, *extra):
        with patch.object(sys, 'argv', ['make', '--fixtures', 'starter', '--source', str(self.source), '--host', 'host', '--share', 'share', '--credentials', self.credentials, *extra]):
            return maker.main()

    def test_smb_matches_local(self):
        def tree(root):
            return {str(p.relative_to(root)): p.read_bytes() for p in root.rglob('*') if p.is_file()}
        with patch.object(sys, 'argv', ['make', '--fixtures', 'starter', '--source', str(self.source), '--path', str(self.local_drive)]):
            self.assertEqual(maker.main(), 0)
        self.assertEqual(self.run_make(), 0)
        self.assertEqual(tree(self.local_drive), tree(self.drive))
        for root, args in [(self.local_drive, ['--path', str(self.local_drive)]), (self.drive, ['--host', 'host', '--share', 'share', '--credentials', self.credentials])]:
            (root / 'RetroFE-Starter/retrofe/retrofe.exe').write_bytes(b'old')
            with patch.object(sys, 'argv', ['deploy', *args]): self.assertEqual(deploy.main(), 0)
        self.assertEqual(tree(self.local_drive), tree(self.drive))

    def test_sections_and_optional_over_smb(self):
        appdata = self.root / 'appdata'
        appdata.mkdir()
        (appdata / 'list.xml').write_bytes(b'xml')
        self.manifest.write_text('[collections => collections/]\nNES/roms/\nmissing/ ?\n[appdata]\nlist.xml ?\n')
        self.assertEqual(self.run_make('--appdata', str(appdata)), 0)
        self.assertEqual((self.drive / 'fixtures/starter/collections/NES/roms/a.nes').read_bytes(), b'a')
        self.assertEqual((self.drive / 'fixtures/starter/list.xml').read_bytes(), b'xml')

    def test_reset_restores_after_failure(self):
        self.assertEqual(self.run_make(), 0)
        (self.drive / 'RetroFE-Starter/original').write_text('kept')
        def fail(drive, name, build):
            testdrive.write(drive, drive.join(name, 'partial'), b'partial')
            raise OSError('failed')
        with patch.object(maker, 'deploy_build', side_effect=fail):
            self.assertEqual(self.run_make('--reset'), 1)
        self.assertEqual((self.drive / 'RetroFE-Starter/original').read_text(), 'kept')
        self.assertEqual(len(list((self.drive / 'backups').glob('RetroFE-Starter-*-failed/partial'))), 1)


if __name__ == '__main__': unittest.main()
