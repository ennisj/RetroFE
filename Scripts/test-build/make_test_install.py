# Stage a fixture and create a test install. See README.md.
import argparse, datetime, os, posixpath, re, shutil, sys
# The scripts run with python -I, which leaves this folder off sys.path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testdrive import Local, Smb, prepare_build, deploy_build, check_dlls

ONESAUCE = '/mnt/remotes/JOHN-DESKTOP_OneSauce'
FIXTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fixtures')


def valid_path(path):
    parts = path.rstrip('/').split('/')
    return (bool(path) and not path.startswith('/') and '\\' not in path
            and not any(ord(c) < 32 or c in ':*?<>|"' for c in path)
            and all(p not in ('', '.', '..') and not p.endswith(('.', ' '))
                    and not re.fullmatch(r'(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?', p, re.IGNORECASE)
                    for p in parts))


def manifest(path):
    entries, targets = [], set()
    section, prefix = 'collections', ''
    with open(path, encoding='utf-8-sig') as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith('#'): continue
            if line.startswith('[') and line.endswith(']'):
                parts = line[1:-1].split('=>')
                section = parts[0].strip()
                prefix = parts[1].strip() if len(parts) == 2 else ''
                if len(parts) > 2 or section not in ('collections', 'appdata') or (prefix and (not valid_path(prefix) or not prefix.endswith('/'))):
                    raise ValueError(f'line {number}: invalid section')
                continue
            optional = line.endswith(' ?')
            if optional: line = line[:-2].rstrip()
            parts = line.split('=>')
            if len(parts) > 2: raise ValueError(f'line {number}: invalid mapping')
            source = parts[0].strip()
            target = parts[1].strip() if len(parts) == 2 else source
            if not valid_path(source) or not valid_path(target):
                raise ValueError(f'line {number}: invalid path')
            if source.endswith('/') != target.endswith('/'):
                raise ValueError(f'line {number}: source and target must both be files or folders')
            target = prefix + target
            key = target.rstrip('/').casefold()
            if key in targets: raise ValueError(f'line {number}: duplicate target {target}')
            for _, old_source, old_target, _ in entries:
                old_key = old_target.rstrip('/').casefold()
                if (old_target.endswith('/') and key.startswith(old_key + '/')) or (target.endswith('/') and old_key.startswith(key + '/')):
                    raise ValueError(f'line {number}: overlapping folder target {target}')
            targets.add(key)
            entries.append((section, source, target, optional))
    return entries


def inspect_sources(sources, entries, dry_run=False):
    available, missing, total_files, total_size = [], [], 0, 0
    for section, src, target, optional in entries:
        path = os.path.join(sources[section], src)
        exists = os.path.isdir(path) if src.endswith('/') else os.path.isfile(path)
        if not exists:
            message = f'{section}: {src}'
            if optional: print(f'Not in OneSauce: {message}')
            else: missing.append(message)
            continue
        available.append((section, src, target, optional))
        if dry_run:
            files = [os.path.join(root, name) for root, _, names in os.walk(path) for name in names] if src.endswith('/') else [path]
            size = sum(os.path.getsize(file) for file in files)
            total_files += len(files)
            total_size += size
            print(f'{section}: {src} => {target}: {len(files)} files, {size} bytes')
    if dry_run: print(f'Total: {total_files} files, {total_size} bytes')
    if missing: raise ValueError('Missing sources:\n' + '\n'.join(missing))
    return available


def stage(drive, name, sources, entries):
    copied = skipped = size = 0
    for section, src, target, _ in entries:
        srcpath = os.path.join(sources[section], src)
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
            drive.makedirs(drive.dirname(dest))
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
    parser.add_argument('--onesauce', default=ONESAUCE)
    parser.add_argument('--source')
    parser.add_argument('--appdata')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--path', help='the drive as a mounted folder; else SMB')
    parser.add_argument('--pc', default='JOHN-DESKTOP')
    parser.add_argument('--host', default='192.168.1.21')
    parser.add_argument('--share', default='retrofe')
    parser.add_argument('--credentials', default='~/.config/coinops/smb.txt')
    args = parser.parse_args()
    try:
        if not valid_path(args.fixtures) or '/' in args.fixtures: raise ValueError('Invalid fixture name')
        install = args.install or 'RetroFE-' + args.fixtures[0].upper() + args.fixtures[1:]
        if not args.install and install.casefold() == 'retrofe-bare': raise ValueError('RetroFE-Bare requires an explicit --install')
        if not valid_path(install) or '/' in install or install.casefold() in ('builds', 'fixtures', 'backups'): raise ValueError('Invalid install name')
        sources = {'collections': args.source or os.path.join(args.onesauce, 'content/retrofe/collections'),
                   'appdata': args.appdata or os.path.join(args.onesauce, 'appdata/retrofe')}
        entries = manifest(os.path.join(FIXTURES, args.fixtures + '.txt'))
        if args.dry_run:
            inspect_sources(sources, entries, True)
            return 0
        if args.path:
            if not os.path.isdir(args.path): raise ValueError('--path must be an existing folder')
            root = os.path.normcase(os.path.realpath(args.path))
            for section, folder in sources.items():
                source = os.path.normcase(os.path.realpath(folder))
                try:
                    common = os.path.commonpath([source, root])
                except ValueError:  # Different drives on Windows
                    common = None
                if common in (source, root):
                    raise ValueError(f'--path and {section} source must not overlap')
        entries = inspect_sources(sources, entries)
        drive = Local(args.path) if args.path else Smb(args.host, args.share, args.credentials)
        stage(drive, args.fixtures, sources, entries)
        path = drive.join(install)
        if drive.exists(path) and not args.reset:
            print(f'{install}: already exists; left as it is')
        else:
            check_dlls()
            build = prepare_build(drive)
            backup = None
            if drive.exists(path):
                drive.makedirs(drive.join('backups'))
                backup = drive.join('backups', install + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
                if drive.exists(backup) or drive.exists(backup + '-failed'): raise ValueError(f'Backup already exists: {backup}')
                drive.rename(path, backup)
                print(f'{install}: moved to {backup}')
            try:
                if not deploy_build(drive, install, build): raise OSError('deployment failed')
            except (OSError, SystemExit) as e:
                if backup:
                    failed = backup + '-failed'
                    if drive.exists(path): drive.rename(path, failed)
                    drive.rename(backup, path)
                    print(f'{install}: restored from {backup}; partial install saved as {failed}', file=sys.stderr)
                print(f'{install}: not updated ({e})', file=sys.stderr)
                return 1
        systems = sorted({(target.split('/roms/', 1)[0] if '/roms/' in target else target.rstrip('/')[:-5]) for _, _, target, _ in entries if '/roms/' in target or target.rstrip('/').endswith('/roms')})
        print('Manage > Add games > From:')
        for system in systems:
            print('\\\\' + args.pc + '\\' + args.share + '\\fixtures\\' + args.fixtures + '\\' + system.replace('/', '\\') + '\\roms')
        return 0
    except (ValueError, OSError) as e:
        print(e, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
