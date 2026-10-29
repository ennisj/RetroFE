# Copies the latest Windows test build of feature/management-ui (the management-ui-latest
# pre-release, docs/CI.md) to a Windows share, unpacked, for Install-TestBuild.ps1.
#   python push_to_share.py [--host 192.168.1.21] [--share retrofe] [--keep 5]
# Needs smbprotocol (pip install smbprotocol). See README.md next to this script.
import argparse, hashlib, io, os, posixpath, sys, urllib.request, zipfile

RELEASE = 'https://github.com/ennisj/RetroFE/releases/download/management-ui-latest/'
ZIP = 'RetroFE-management-ui-windows-x64.zip'

def fetch(name):
    with urllib.request.urlopen(RELEASE + name) as r:
        return r.read()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='192.168.1.21')
    parser.add_argument('--share', default='retrofe')
    parser.add_argument('--folder', default='management-ui')
    parser.add_argument('--credentials', default='~/.config/coinops/smb.txt',
                        help='file holding the user name, then the password, one per line')
    parser.add_argument('--keep', type=int, default=5, help='builds to keep on the share')
    args = parser.parse_args()
    try:
        import smbclient
        from smbclient import shutil as smbshutil
    except ImportError:
        sys.exit('smbprotocol is missing: pip install smbprotocol')

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
    if 'retrofe/retrofe.exe' not in [n.replace('\\', '/') for n in names]:
        sys.exit(f'{ZIP}: no retrofe/retrofe.exe')
    # BUILD.txt: branch, commit, commit date, subject, run
    build = archive.read('BUILD.txt').decode('utf-8-sig').splitlines()
    commit, date = build[1].strip(), build[2].strip()
    label = f'{date[:10]}-{commit[:8]}'

    with open(os.path.expanduser(args.credentials), encoding='utf-8') as f:
        user, password = f.read().splitlines()[:2]
    smbclient.register_session(args.host, username=user, password=password)
    root = rf'\\{args.host}\{args.share}\{args.folder}'
    builds = root + r'\builds'
    smbclient.makedirs(builds, exist_ok=True)

    target = builds + '\\' + label
    if smbclient.path.isdir(target):
        print(f'{label} is already on the share')
    else:
        # Unpacked under a temporary name first, so Install-TestBuild.ps1 never sees half a build
        partial = target + '.partial'
        if smbclient.path.isdir(partial):
            smbshutil.rmtree(partial)
        for name in names:
            path = partial + '\\' + name.replace('/', '\\')
            smbclient.makedirs(path.rsplit('\\', 1)[0], exist_ok=True)
            with smbclient.open_file(path, mode='wb') as out:
                out.write(archive.read(name))
        smbclient.rename(partial, target)
        print(f'Copied {label} ({len(names)} files): {build[3].strip()}')

    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'Install-TestBuild.ps1')
    with open(script, 'rb') as f, smbclient.open_file(root + r'\Install-TestBuild.ps1', mode='wb') as out:
        out.write(f.read())

    # Folder names start with the commit's date, so the newest sort last
    done = sorted(e.name for e in smbclient.scandir(builds) if e.is_dir() and not e.name.endswith('.partial'))
    for old in done[:-args.keep] if args.keep > 0 else []:
        smbshutil.rmtree(builds + '\\' + old)
        print(f'Removed {old}')

if __name__ == '__main__':
    main()
