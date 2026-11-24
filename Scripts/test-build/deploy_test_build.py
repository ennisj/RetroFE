# Deploy the latest Windows test build to the test drive. See README.md.
import argparse, os, sys
# The scripts run with python -I, which leaves this folder off sys.path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testdrive import Local, Smb, prepare_build, deploy_build


def discover_installs(drive):
    return sorted(name for name in drive.listdir(drive.root)
                  if name.startswith('RetroFE-') and drive.isdir(drive.join(name))
                  and drive.exists(drive.join(name, 'retrofe', 'retrofe.exe'))) or ['RetroFE-Bare']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--path', help='the drive as a mounted folder; else SMB')
    parser.add_argument('--host', default='192.168.1.21')
    parser.add_argument('--share', default='retrofe')
    parser.add_argument('--credentials', default='~/.config/coinops/smb.txt',
                        help='file holding the user name, then the password, one per line')
    parser.add_argument('--install', action='append',
                        help='an install folder to update (repeatable); default all RetroFE-* installs')
    args = parser.parse_args()
    drive = Local(args.path) if args.path else Smb(args.host, args.share, args.credentials)
    installs = args.install or discover_installs(drive)
    build = prepare_build(drive)
    for install in installs:
        deploy_build(drive, install, build)


if __name__ == '__main__':
    main()
