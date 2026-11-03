# Deploys the latest Windows test build of feature/management-ui (the management-ui-latest
# pre-release, docs/CI.md) to the test drive on the Windows PC:
#   builds\<commit date>-<commit>\   every build, kept
#   <install>\retrofe\retrofe.exe    always the newest, for a shortcut that doesn't change
#   <install>\layouts\Aeon Nox\      updated with it (copied over: nothing is deleted)
# An install's retrofe\ folder gets RetroFE's DLLs (Package/Environment/Windows/retrofe) the
# first time. The drive is reached at --path (a mounted folder) or over SMB (--host/--share).
#   python deploy_test_build.py [--install RetroFE-Bare ...] [--path /mnt/remotes/JOHN-DESKTOP_retrofe]
# See README.md next to this script.
import argparse, hashlib, io, os, posixpath, shutil, sys, urllib.request, zipfile

RELEASE = 'https://github.com/ennisj/RetroFE/releases/download/management-ui-latest/'
ZIP = 'RetroFE-management-ui-windows-x64.zip'
REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
DLLS = os.path.join(REPO, 'Package', 'Environment', 'Windows', 'retrofe')
SKIP = {'.git', '.gitignore', 'LICENSE', 'README.md'}


class Local:
    """The drive as a mounted folder."""
    def __init__(self, root): self.root = root
    def join(self, *parts): return os.path.join(self.root, *parts)
    def isdir(self, path): return os.path.isdir(path)
    def exists(self, path): return os.path.exists(path)
    def makedirs(self, path): os.makedirs(path, exist_ok=True)
    def open(self, path, mode): return open(path, mode)
    def listdir(self, path): return os.listdir(path)
    def rename(self, a, b): os.rename(a, b)
    def rmtree(self, path): shutil.rmtree(path)


class Smb:
    """The drive as \\\\host\\share, with the deploy account's login."""
    def __init__(self, host, share, credentials):
        import smbclient
        from smbclient import shutil as smbshutil
        self.c, self.sh = smbclient, smbshutil
        with open(os.path.expanduser(credentials), encoding='utf-8') as f:
            user, password = f.read().splitlines()[:2]
        smbclient.register_session(host, username=user, password=password)
        self.root = rf'\\{host}\{share}'
    def join(self, *parts): return '\\'.join([self.root] + [p.replace('/', '\\') for p in parts])
    def isdir(self, path): return self.c.path.isdir(path)
    def exists(self, path): return self.c.path.exists(path)
    def makedirs(self, path): self.c.makedirs(path, exist_ok=True)
    def open(self, path, mode): return self.c.open_file(path, mode=mode)
    def listdir(self, path): return self.c.listdir(path)
    def rename(self, a, b): self.c.rename(a, b)
    def rmtree(self, path): self.sh.rmtree(path)


def fetch(name):
    with urllib.request.urlopen(RELEASE + name) as r:
        return r.read()


def write(drive, path, data):
    drive.makedirs(os.path.dirname(path) if isinstance(drive, Local) else path.rsplit('\\', 1)[0])
    with drive.open(path, 'wb') as out:
        out.write(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--path', help='the drive as a mounted folder; else SMB')
    parser.add_argument('--host', default='192.168.1.21')
    parser.add_argument('--share', default='retrofe')
    parser.add_argument('--credentials', default='~/.config/coinops/smb.txt',
                        help='file holding the user name, then the password, one per line')
    parser.add_argument('--install', action='append',
                        help='an install folder on the drive to update (repeatable); default RetroFE-Bare')
    args = parser.parse_args()
    installs = args.install or ['RetroFE-Bare']
    drive = Local(args.path) if args.path else Smb(args.host, args.share, args.credentials)

    data = fetch(ZIP)
    expected = fetch(ZIP + '.sha256').decode('ascii').split()[0]
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        sys.exit(f'{ZIP}: SHA-256 {actual} is not the published {expected}')
    archive = zipfile.ZipFile(io.BytesIO(data))
    names = [n for n in archive.namelist() if not n.endswith('/')]
    for name in names:
        clean = posixpath.normpath(name.replace('\\', '/'))
        if clean.startswith(('/', '../')) or clean == '..' or ':' in clean:
            sys.exit(f'{ZIP}: unexpected path {name}')
    if 'retrofe/retrofe.exe' not in names:
        sys.exit(f'{ZIP}: no retrofe/retrofe.exe')
    # BUILD.txt: branch, commit, commit date, subject, run
    build = archive.read('BUILD.txt').decode('utf-8-sig').splitlines()
    label = f'{build[2].strip()[:10]}-{build[1].strip()[:8]}'

    # Every build is kept, unpacked under a temporary name first so it's never seen half-written
    target = drive.join('builds', label)
    if drive.isdir(target):
        print(f'builds\\{label} is already there')
    else:
        partial = target + '.partial'
        if drive.isdir(partial):
            drive.rmtree(partial)
        for name in names:
            write(drive, drive.join('builds', label + '.partial', name), archive.read(name))
        drive.rename(partial, target)
        print(f'Copied builds\\{label} ({len(names)} files): {build[3].strip()}')

    for install in installs:
        program = drive.join(install, 'retrofe')
        if not drive.exists(drive.join(install, 'retrofe', 'SDL2.dll')):
            # A new install: RetroFE's DLLs, as releases ship them
            count = 0
            for entry in sorted(os.listdir(DLLS)):
                if entry in SKIP or os.path.isdir(os.path.join(DLLS, entry)):
                    continue
                with open(os.path.join(DLLS, entry), 'rb') as f:
                    write(drive, drive.join(install, 'retrofe', entry), f.read())
                count += 1
            print(f'{install}: added RetroFE\'s {count} DLLs and fonts')
        try:
            # The program first: if RetroFE is running it can't be replaced, and nothing else changes
            for name in sorted(names, key=lambda n: n != 'retrofe/retrofe.exe'):
                if name == 'BUILD.txt':
                    continue
                write(drive, drive.join(install, name), archive.read(name))
        except OSError as e:
            print(f'{install}: not updated ({e}). Is RetroFE running from it? Close it and run this again.')
            continue
        write(drive, drive.join(install, 'retrofe', 'BUILD.txt'), archive.read('BUILD.txt'))
        print(f'{install}: now {label}')


if __name__ == '__main__':
    main()
