# Stage a fixture and create a test install. See README.md.
import argparse, datetime, os, posixpath, shutil, sys
# The scripts run with python -I, which leaves this folder off sys.path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testdrive import Local, Smb, prepare_build, deploy_build

SOURCE = '/mnt/remotes/JOHN-DESKTOP_OneSauce/content/retrofe/collections'
FIXTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fixtures')


def valid_path(path):
    return bool(path) and not path.startswith('/') and '\\' not in path and ':' not in path and all(p not in ('', '.', '..') for p in path.rstrip('/').split('/'))


def manifest(path):
    entries, targets = [], set()
    with open(path, encoding='utf-8') as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith('#'): continue
            parts = line.split('=>')
            if len(parts) > 2: raise ValueError(f'line {number}: invalid mapping')
            source = parts[0].strip()
            target = parts[1].strip() if len(parts) == 2 else source
            if not valid_path(source) or not valid_path(target):
                raise ValueError(f'line {number}: invalid path')
            key = target.rstrip('/').casefold()
            if key in targets: raise ValueError(f'line {number}: duplicate target {target}')
            targets.add(key)
            entries.append((source, target))
    return entries


def stage(drive, name, source, entries):
    missing = [src for src, _ in entries if not (os.path.isdir(os.path.join(source, src)) if src.endswith('/') else os.path.isfile(os.path.join(source, src)))]
    if missing:
        raise ValueError('Missing sources:\n' + '\n'.join(missing))
    copied = skipped = size = 0
    for src, target in entries:
        srcpath = os.path.join(source, src)
        files = []
        if src.endswith('/'):
            drive.makedirs(drive.join('fixtures', name, target))
            for root, dirs, names in os.walk(srcpath):
                for directory in dirs:
                    relative = os.path.relpath(os.path.join(root, directory), srcpath).replace(os.sep, '/')
                    drive.makedirs(drive.join('fixtures', name, posixpath.join(target, relative)))
                dirs.sort()
                for file in sorted(names):
                    path = os.path.join(root, file)
                    files.append((path, posixpath.join(target, os.path.relpath(path, srcpath).replace(os.sep, '/'))))
        else:
            files.append((srcpath, target))
        for path, dest in files:
            dest = drive.join('fixtures', name, dest)
            length = os.path.getsize(path)
            if drive.exists(dest) and drive.getsize(dest) == length:
                skipped += 1
                continue
            drive.makedirs(os.path.dirname(dest) if isinstance(drive, Local) else dest.rsplit('\\', 1)[0])
            with open(path, 'rb') as inp, drive.open(dest, 'wb') as out:
                shutil.copyfileobj(inp, out)
            copied += 1
            size += length
    print(f'Copied {copied}, skipped {skipped} files ({size / 1000000:.2f} MB copied)')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fixtures', required=True)
    parser.add_argument('--install')
    parser.add_argument('--reset', action='store_true')
    parser.add_argument('--source', default=SOURCE)
    parser.add_argument('--path', help='the drive as a mounted folder; else SMB')
    parser.add_argument('--host', default='192.168.1.21')
    parser.add_argument('--share', default='retrofe')
    parser.add_argument('--credentials', default='~/.config/coinops/smb.txt')
    args = parser.parse_args()
    try:
        if not valid_path(args.fixtures) or '/' in args.fixtures: raise ValueError('Invalid fixture name')
        install = args.install or 'RetroFE-' + args.fixtures.capitalize()
        if not valid_path(install) or '/' in install or install.casefold() in ('builds', 'fixtures', 'backups'): raise ValueError('Invalid install name')
        entries = manifest(os.path.join(FIXTURES, args.fixtures + '.txt'))
        drive = Local(args.path) if args.path else Smb(args.host, args.share, args.credentials)
        stage(drive, args.fixtures, args.source, entries)
        path = drive.join(install)
        if drive.exists(path) and not args.reset:
            print(f'{install}: already exists; left as it is')
        else:
            build = prepare_build(drive)
            if drive.exists(path):
                drive.makedirs(drive.join('backups'))
                backup = drive.join('backups', install + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
                if drive.exists(backup): raise ValueError(f'Backup already exists: {backup}')
                drive.rename(path, backup)
                print(f'{install}: moved to {backup}')
            if not deploy_build(drive, install, build): return 1
        systems = sorted({target.split('/')[0] for _, target in entries if '/roms/' in target})
        print('Manage > Add games > From:')
        for system in systems:
            print(drive.join('fixtures', args.fixtures, system, 'roms'))
        return 0
    except (ValueError, OSError) as e:
        print(e, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
