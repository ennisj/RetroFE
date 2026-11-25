#!/usr/bin/env python3
"""Build small RetroFE installs and drive a Windows window for screenshots."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import testdrive
import make_test_install

SYSTEMS = [('Nintendo Entertainment System', 'RetroArch NES', 'fceumm', 'fds,nes,unf,unif,dat'), ('Sega Genesis', 'RetroArch Genesis', 'genesis_plus_gx', 'mdx,md,smd,gen,bin,cue,iso,chd,bms,sms,gg,sg,68k,sgd,m3u'), ('MAME', 'RetroArch MAME', 'mame2003_plus', 'zip')]
KEYS = dict(up=0x26, down=0x28, left=0x25, right=0x27, return_=0x0d, escape=0x1b, space=0x20, tab=9, backspace=8, delete=0x2e, home=0x24, end=0x23, pageup=0x21, pagedown=0x22, insert=0x2d)
KEYS['return'] = KEYS.pop('return_')
KEYS.update({chr(c).lower(): c for c in list(range(65, 91)) + list(range(48, 58))})
KEYS.update({f'f{i}': 0x6f+i for i in range(1, 13)})
EXTENDED = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2d, 0x2e}


def key_code(name):
    try: return KEYS[name.lower()]
    except KeyError: raise ValueError('unknown key: ' + name)


def key_lparam(vk, scan, up=False):
    return 1 | ((scan & 255) << 16) | ((vk in EXTENDED) << 24) | ((3 << 30) if up else 0)


def parse_script(text):
    steps = []
    for line, raw in enumerate(text.splitlines(), 1):
        s = raw.strip()
        if not s or s.startswith('#'): continue
        parts = s.split(None, 1)
        cmd = parts[0]
        rest = parts[1] if len(parts) > 1 else ''
        try:
            args = []
            if cmd == 'wait':
                args = [float(rest)]
                if not math.isfinite(args[0]) or args[0] < 0: raise ValueError('invalid duration')
            elif cmd == 'wait-log':
                if not rest: raise ValueError('missing text')
                m = re.match(r'^(.*\S)\s+(\d+(?:\.\d+)?)$', rest)
                args = [m.group(1), float(m.group(2))] if m else [rest, 30.0]
            elif cmd == 'key':
                m = re.fullmatch(r'(\S+)(?:\s+x([1-9]\d*))?(?:\s+hold\s+(\d+(?:\.\d+)?))?', rest, re.I)
                if not m: raise ValueError('expected key NAME [xN] [hold SECONDS]')
                args = [key_code(m.group(1)), int(m.group(2) or 1), float(m.group(3) or 0)]
            elif cmd in ('type', 'expect-log'):
                if not rest: raise ValueError('missing text')
                # Preserve the remainder after the first separator for type.
                args = [raw.lstrip()[len(cmd)+1:] if cmd == 'type' else rest]
            elif cmd == 'shot':
                if not re.fullmatch(r'[A-Za-z0-9_-]+', rest): raise ValueError('invalid shot name')
                args = [rest]
            elif cmd == 'expect-file':
                path, sep, value = rest.partition(' ')
                if not path or not make_test_install.valid_path(path): raise ValueError('invalid relative path')
                args = [path, value.lstrip() if sep else None]
            elif cmd == 'quit':
                if rest: raise ValueError('quit takes no arguments')
            else: raise ValueError('unknown command: ' + cmd)
            steps.append(dict(line=line, command=s, op=cmd, args=args))
        except ValueError as e: raise ValueError(f'line {line}: {e}') from e
    return steps


def png(width, height, rgb):
    if width <= 0 or height <= 0 or len(rgb) != width*height*3: raise ValueError('invalid RGB image')
    def chunk(tag, data): return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag+data) & 0xffffffff)
    rows = b''.join(b'\0' + rgb[y*width*3:(y+1)*width*3] for y in range(height))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')


def is_blank(rgb):
    return bool(rgb) and all(rgb[i:i+3] == rgb[:3] for i in range(0, len(rgb), 3))


def install(kind, exe, dlls, common, out, fake=None):
    out, exe, dlls, common = map(Path, (out, exe, dlls, common))
    if out.exists() and (not out.is_dir() or any(out.iterdir())): raise ValueError('OUT exists and is not empty')
    if kind == 'starter' and not fake: raise ValueError('starter requires --fake-emulator')
    if not exe.is_file() or not dlls.is_dir() or not (common/'layouts'/'Aeon Nox').is_dir() or (fake and not Path(fake).is_file()): raise ValueError('missing executable, DLL directory, layout, or fake emulator')
    retro = out/'retrofe'; retro.mkdir(parents=True, exist_ok=True)
    for f in sorted(dlls.iterdir()):
        if f.name not in testdrive.SKIP and f.is_file(): shutil.copy2(f, retro/f.name)
    shutil.copy2(exe, retro/'retrofe.exe')
    shutil.copytree(common/'layouts'/'Aeon Nox', out/'layouts'/'Aeon Nox')
    if kind == 'starter':
        dest = out/'emulators'/'RetroArch'/'retroarch.exe'; dest.parent.mkdir(parents=True); shutil.copy2(fake, dest)
        def write(path, value):
            path = out/path; path.parent.mkdir(parents=True, exist_ok=True); path.write_text(value, encoding='utf-8', newline='\n')
        # Persist the built-in defaults as Manage's settings setup does; this also
        # prevents the first-start Manage screen from reopening on every run.
        write(Path('settings.conf'), '# Created by RetroFE.\n# Run "retrofe -createconfig" to get a reference of every setting.\n\nlog = ERROR,WARNING,NOTICE\nnumScreens = 1\nfullscreen = no\nhorizontal = auto\nvertical = auto\nwindowBorder = true\nwindowResize = true\nhideMouse = no\nlayout = Aeon Nox\nfirstCollection = Main\nhomeMenu = yes\n')
        # FirstSetupTests c: Manage > Launchers creates a RetroArch launcher per core.
        # FirstSetupTests d: Systems > Add a system writes settings and Main/menu.txt.
        for name, launcher, core, extensions in SYSTEMS:
            write(Path('launchers.windows')/(launcher+'.conf'), f'executable = %RETROFE_PATH%\\emulators\\RetroArch\\retroarch.exe\narguments = -L "%RETROFE_PATH%\\emulators\\RetroArch\\cores\\{core}_libretro.dll" "%ITEM_FILEPATH%"\n')
            write(Path('collections')/name/'settings.conf', f'list.extensions = {extensions}\nlist.includeMissingItems = {"true" if name == SYSTEMS[0][0] else "false"}\nlist.menuSort = yes\nlauncher = {launcher}\n')
        write(Path('collections/Main/menu.txt'), ''.join(x[0]+'\n' for x in SYSTEMS))
        # FirstSetupTests e: Add games copies the staged ROMs, including subfolders.
        nes = []
        for _, target in make_test_install.manifest(Path(make_test_install.FIXTURES)/'starter.txt'):
            parts = target.split('/')
            if len(parts) < 3 or parts[1] != 'roms': continue
            path = out/'collections'/target
            if target.endswith('/'): path.mkdir(parents=True, exist_ok=True)
            else:
                path.parent.mkdir(parents=True, exist_ok=True); path.touch()
                if parts[0] == SYSTEMS[0][0]: nes.append(Path(target).stem)
        write(Path('collections')/SYSTEMS[0][0]/'include.txt', ''.join(n+'\n' for n in nes)+ 'Missing Game (USA)\n')
    print(f'Made {kind} install in {out}')

class Windows:
    def __init__(self):
        if sys.platform != 'win32': raise OSError('run requires Windows')
        import ctypes as c
        from ctypes import wintypes as w
        self.c, self.w = c, w
        self.u, self.g = c.WinDLL('user32', use_last_error=True), c.WinDLL('gdi32', use_last_error=True)
        u, g = self.u, self.g
        for lib, name, result, args in [
            (u,'GetDC',w.HDC,[w.HWND]), (u,'ReleaseDC',c.c_int,[w.HWND,w.HDC]),
            (u,'PrintWindow',w.BOOL,[w.HWND,w.HDC,w.UINT]), (u,'GetClientRect',w.BOOL,[w.HWND,c.POINTER(w.RECT)]),
            (u,'PostMessageW',w.BOOL,[w.HWND,w.UINT,w.WPARAM,w.LPARAM]), (u,'MapVirtualKeyW',w.UINT,[w.UINT,w.UINT]),
            (u,'IsWindowVisible',w.BOOL,[w.HWND]), (u,'GetWindowThreadProcessId',w.DWORD,[w.HWND,c.POINTER(w.DWORD)]),
            (g,'CreateCompatibleDC',w.HDC,[w.HDC]), (g,'CreateCompatibleBitmap',w.HBITMAP,[w.HDC,c.c_int,c.c_int]),
            (g,'SelectObject',w.HGDIOBJ,[w.HDC,w.HGDIOBJ]), (g,'DeleteObject',w.BOOL,[w.HGDIOBJ]),
            (g,'DeleteDC',w.BOOL,[w.HDC]), (g,'GetDIBits',c.c_int,[w.HDC,w.HBITMAP,w.UINT,w.UINT,c.c_void_p,c.c_void_p,w.UINT])]:
            fn = getattr(lib,name); fn.restype = result; fn.argtypes = args
        self.callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
        u.EnumWindows.argtypes = [self.callback, w.LPARAM]; u.EnumWindows.restype = w.BOOL

    def window(self, pid):
        found = []
        @self.callback
        def visit(hwnd, _):
            owner = self.w.DWORD(); self.u.GetWindowThreadProcessId(hwnd, self.c.byref(owner))
            if owner.value == pid and self.u.IsWindowVisible(hwnd): found.append(hwnd); return False
            return True
        self.u.EnumWindows(visit, 0)
        return found[0] if found else None

    def post(self, hwnd, msg, wp=0, lp=0):
        if not self.u.PostMessageW(hwnd, msg, wp, lp): raise OSError('PostMessageW failed')

    def capture(self, hwnd):
        c, w, u, g = self.c, self.w, self.u, self.g
        class Header(c.Structure):
            _fields_ = [('size',w.DWORD),('width',w.LONG),('height',w.LONG),('planes',w.WORD),('bits',w.WORD),('compression',w.DWORD),('image',w.DWORD),('x',w.LONG),('y',w.LONG),('used',w.DWORD),('important',w.DWORD)]
        rect = w.RECT()
        if not u.GetClientRect(hwnd,c.byref(rect)): raise OSError('GetClientRect failed')
        width, height = rect.right, rect.bottom
        if width <= 0 or height <= 0: raise OSError('empty client area')
        dc = u.GetDC(hwnd); mem = g.CreateCompatibleDC(dc); bitmap = g.CreateCompatibleBitmap(dc,width,height)
        if not dc or not mem or not bitmap: raise OSError('cannot create capture bitmap')
        old = g.SelectObject(mem, bitmap)
        try:
            if not u.PrintWindow(hwnd,mem,3): raise OSError('PrintWindow failed') # client-only | render full content
            g.SelectObject(mem, old); old = None
            info = Header(c.sizeof(Header),width,-height,1,32,0,0,0,0,0,0)
            data = (c.c_ubyte*(width*height*4))()
            if g.GetDIBits(mem,bitmap,0,height,data,c.byref(info),0) != height: raise OSError('GetDIBits failed')
            raw = bytes(data); rgb = bytearray(width*height*3)
            rgb[0::3], rgb[1::3], rgb[2::3] = raw[2::4], raw[1::4], raw[0::4]
            return width,height,bytes(rgb)
        finally:
            if old: g.SelectObject(mem,old)
            g.DeleteObject(bitmap); g.DeleteDC(mem); u.ReleaseDC(hwnd,dc)


def run(install_dir, script, out, width=960, height=540, timeout=120):
    root, script, out = Path(install_dir).resolve(), Path(script).resolve(), Path(out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    exe = root/'retrofe'/'retrofe.exe'; log = root/'log.txt'
    args = ['-fullscreen','no','-horizontal',str(width),'-vertical',str(height),'-windowBorder','false','-log','ALL']
    result = dict(install=str(root), script=str(script), exe=str(exe), args=args, steps=[])
    proc = None; started = time.monotonic(); deadline = started + timeout; current = None
    def check():
        if time.monotonic() >= deadline: raise TimeoutError('overall timeout')
        if proc is not None and proc.poll() is not None: raise RuntimeError('RetroFE exited before quit')
    def pause(seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            check(); time.sleep(min(.05, end-time.monotonic()))
    def contents(path):
        try: return path.read_text(encoding='utf-8', errors='replace')
        except FileNotFoundError: return ''
    try:
        source = script.read_text(encoding='utf-8-sig')
        try:
            steps = parse_script(source)
        except ValueError as error:
            match = re.match(r'line (\d+):', str(error))
            if match:
                number = int(match.group(1))
                current = dict(line=number, command=source.splitlines()[number-1].strip(), ok=False, seconds=0, message='', shot=None)
                result['steps'].append(current)
            raise
        if timeout <= 0 or width <= 0 or height <= 0: raise ValueError('width, height and timeout must be positive')
        win = Windows()
        env = os.environ.copy(); env['SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN'] = '1'
        proc = subprocess.Popen([str(exe)]+args, cwd=root, env=env)
        end = min(deadline, time.monotonic()+30); hwnd = None
        while time.monotonic() < end:
            check(); hwnd = win.window(proc.pid)
            if hwnd: break
            time.sleep(.05)
        if not hwnd: raise TimeoutError('window did not appear within 30 seconds' if time.monotonic() < deadline else 'overall timeout')
        shots = 0
        for step in steps:
            current = dict(line=step['line'], command=step['command'], ok=False, seconds=0, message='', shot=None)
            result['steps'].append(current); tick = time.monotonic()
            try:
                check(); op, a = step['op'], step['args']
                if op == 'wait': pause(a[0])
                elif op == 'wait-log':
                    end = time.monotonic()+a[1]
                    while a[0] not in contents(log):
                        check()
                        if time.monotonic() >= end: raise TimeoutError('wait-log timed out: '+a[0])
                        pause(.05)
                elif op == 'key':
                    vk, count, hold = a; scan = win.u.MapVirtualKeyW(vk,0)
                    for i in range(count):
                        win.post(hwnd,0x100,vk,key_lparam(vk,scan)); pause(hold)
                        win.post(hwnd,0x101,vk,key_lparam(vk,scan,True))
                        if i+1 < count: pause(.15)
                elif op == 'type':
                    encoded = a[0].encode('utf-16-le')
                    for i in range(0,len(encoded),2): win.post(hwnd,0x102,int.from_bytes(encoded[i:i+2],'little'),1)
                elif op == 'shot':
                    shots += 1; name = f'{shots:02d}-{a[0]}.png'; w,h,rgb = win.capture(hwnd)
                    (out/name).write_bytes(png(w,h,rgb)); current['shot'] = name; current['blank'] = is_blank(rgb)
                elif op == 'expect-file':
                    path = root/a[0]
                    if not path.is_file(): raise RuntimeError('file missing: '+a[0])
                    if a[1] is not None and a[1] not in contents(path): raise RuntimeError('text missing from '+a[0]+': '+a[1])
                elif op == 'expect-log':
                    if a[0] not in contents(log): raise RuntimeError('text missing from log: '+a[0])
                elif op == 'quit':
                    win.post(hwnd,0x10); end = min(deadline,time.monotonic()+20)
                    while proc.poll() is None and time.monotonic() < end: time.sleep(.05)
                    if proc.poll() is None: raise TimeoutError('overall timeout' if time.monotonic() >= deadline else 'quit timed out')
                current['ok'] = True; current['message'] = 'ok'
            finally: current['seconds'] = round(time.monotonic()-tick,3)
        if proc.poll() is None: raise RuntimeError('script ended without quit')
        return 0
    except Exception as e:
        message = str(e).replace('\n',' ')
        if current and not current['ok']: current['message'] = message
        else: result['error'] = message
        print('FAIL '+ (f"line {current['line']}: " if current and not current['ok'] else '') + message)
        return 1
    finally:
        if proc is not None and proc.poll() is None: proc.kill(); proc.wait()
        try:
            if log.is_file() and log.resolve() != (out/'log.txt').resolve(): shutil.copy2(log,out/'log.txt')
        except OSError as error:
            result['log_copy_error'] = str(error)
        (out/'results.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')


def main():
    p = argparse.ArgumentParser(); sub = p.add_subparsers(dest='action',required=True)
    i = sub.add_parser('install'); i.add_argument('--kind',choices=['bare','starter'],required=True); i.add_argument('--exe',required=True); i.add_argument('--dlls',required=True); i.add_argument('--common',required=True); i.add_argument('--fake-emulator'); i.add_argument('out')
    r = sub.add_parser('run'); r.add_argument('--install',required=True); r.add_argument('--script',required=True); r.add_argument('--out',required=True); r.add_argument('--width',type=int,default=960); r.add_argument('--height',type=int,default=540); r.add_argument('--timeout',type=float,default=120)
    a = p.parse_args()
    try:
        if a.action == 'install': install(a.kind,a.exe,a.dlls,a.common,a.out,a.fake_emulator); return 0
        return run(a.install,a.script,a.out,a.width,a.height,a.timeout)
    except (OSError,ValueError) as e: print('FAIL '+str(e)); return 1

if __name__ == '__main__': sys.exit(main())
