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

# The scripts run with python -I, which leaves this folder off sys.path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import testdrive
import make_test_install

# Windows messages posted to RetroFE's window
WM_CLOSE = 0x0010
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_CHAR = 0x0102

# Windows virtual-key codes by the names scripts use
KEYS = {
    'up': 0x26,
    'down': 0x28,
    'left': 0x25,
    'right': 0x27,
    'return': 0x0d,
    'escape': 0x1b,
    'space': 0x20,
    'tab': 0x09,
    'backspace': 0x08,
    'delete': 0x2e,
    'home': 0x24,
    'end': 0x23,
    'pageup': 0x21,
    'pagedown': 0x22,
    'insert': 0x2d,
}
KEYS.update({chr(code).lower(): code for code in list(range(65, 91)) + list(range(48, 58))})
KEYS.update({f'f{index}': 0x6f + index for index in range(1, 13)})
# Keys Windows flags as extended (bit 24): arrows, Insert, Delete, Home, End, PageUp, PageDown
EXTENDED = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2d, 0x2e}


def key_code(name):
    """Look up a Windows virtual key."""
    try:
        return KEYS[name.lower()]
    except KeyError:
        raise ValueError('unknown key: ' + name)


def key_lparam(virtual_key, scan_code, released=False):
    """Build the Windows keyboard message flags."""
    # Scan code occupies bits 16–23; extended, previous and transition bits
    # distinguish arrow keys and key-up messages.
    return 1 | (scan_code & 255) << 16 | (virtual_key in EXTENDED) << 24 | (3 << 30 if released else 0)


def parse_script(text):
    """Validate and parse all script commands."""
    steps = []
    for line, raw in enumerate(text.splitlines(), 1):
        stripped = raw.strip()
        if not stripped or stripped.startswith('#'):
            continue
        if steps and steps[-1]['op'] == 'quit':
            raise ValueError(f'line {line}: nothing may follow quit')
        parts = stripped.split(None, 1)
        command = parts[0]
        remainder = parts[1] if len(parts) > 1 else ''
        try:
            args = []
            if command == 'wait':
                args = [float(remainder)]
                if not math.isfinite(args[0]) or args[0] < 0:
                    raise ValueError('invalid duration')
            elif command == 'wait-log':
                if not remainder:
                    raise ValueError('missing text')
                match = re.match(r'^(.*\S)\s+(\d+(?:\.\d+)?)$', remainder)
                args = [match.group(1), float(match.group(2))] if match else [remainder, 30.0]
            elif command == 'key':
                match = re.fullmatch(r'(\S+)(?:\s+x([1-9]\d*))?(?:\s+hold\s+(\d+(?:\.\d+)?))?', remainder, re.I)
                if not match:
                    raise ValueError('expected key NAME [xN] [hold SECONDS]')
                # The 0.1 second default spans several 30 Hz keyboard samples.
                args = [key_code(match.group(1)), int(match.group(2) or 1), float(match.group(3) or 0.1)]
            elif command in ('type', 'expect-log'):
                if not remainder:
                    raise ValueError('missing text')
                args = [raw.lstrip()[len(command) + 1:] if command == 'type' else remainder]
            elif command == 'shot':
                if not re.fullmatch('[A-Za-z0-9_-]+', remainder):
                    raise ValueError('invalid shot name')
                args = [remainder]
            elif command == 'expect-differs':
                fields = remainder.split()
                if len(fields) != 5 or not re.fullmatch('[A-Za-z0-9_-]+', fields[0]):
                    raise ValueError('expected shot name and four region fractions')
                coords = [float(value) for value in fields[1:]]
                if not all(math.isfinite(value) and 0 <= value <= 1 for value in coords) or not (coords[0] < coords[2] and coords[1] < coords[3]):
                    raise ValueError('invalid region')
                args = [fields[0]] + coords
            elif command == 'wait-drawn':
                args = [float(remainder) if remainder else 60.0]
                if not math.isfinite(args[0]) or args[0] <= 0:
                    raise ValueError('invalid timeout')
            elif command == 'expect-file':
                path, sep, value = remainder.partition(' ')
                if not path or not make_test_install.valid_path(path):
                    raise ValueError('invalid relative path')
                args = [path, value.lstrip() if sep else None]
            elif command == 'quit':
                if remainder:
                    raise ValueError('quit takes no arguments')
            else:
                raise ValueError('unknown command: ' + command)
            steps.append(dict(line=line, command=stripped, op=command, args=args))
        except ValueError as error:
            raise ValueError(f'line {line}: {error}') from error
    return steps


def png(width, height, rgb):
    """Encode an RGB image as a PNG."""
    if width <= 0 or height <= 0 or len(rgb) != width * height * 3:
        raise ValueError('invalid RGB image')

    def chunk(tag, data):
        """One PNG chunk: length, tag, data and CRC."""
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    rows = b''.join((b'\x00' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height)))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')


def is_blank(rgb):
    """A blank capture has the same colour at every pixel."""
    return bool(rgb) and all((rgb[index:index + 3] == rgb[:3] for index in range(0, len(rgb), 3)))


def region_differs(width, height, before, after, region):
    """Ignore channel differences of at most 8 and require 0.1% changed pixels."""
    left, top, right, bottom = region
    x0, y0 = int(left * width), int(top * height)
    x1, y1 = math.ceil(right * width), math.ceil(bottom * height)
    changed = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            offset = (y * width + x) * 3
            if any(abs(before[offset + c] - after[offset + c]) > 8 for c in range(3)):
                changed += 1
    return changed > (x1 - x0) * (y1 - y0) * 0.001


def exit_text(code):
    """An exit code as Windows reports crashes: 0xC0000005 rather than -1073741819."""
    if code is None:
        return 'none'
    if code < 0 or code > 0xffff:
        return f'0x{code & 0xffffffff:08X}'
    return str(code)


def install(kind, exe, dlls, common, out, setup=None):
    """Build a bare install, optionally adding a finished first setup."""
    out, exe, dlls, common = map(Path, (out, exe, dlls, common))
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        raise ValueError('OUT exists and is not empty')
    if kind == 'starter' and (not setup):
        raise ValueError('starter requires --setup')
    if kind == 'bare' and setup:
        raise ValueError('bare rejects --setup')
    if not exe.is_file() or not dlls.is_dir() or (not (common / 'layouts' / 'Aeon Nox').is_dir()):
        raise ValueError('missing executable, DLL directory, or layout')
    if setup:
        setup = Path(setup)
        if not (setup / 'collections').is_dir() or not any(((setup / name).is_dir() for name in ('launchers.windows', 'launchers.linux'))):
            raise ValueError('setup needs collections and an OS launchers directory')
        if (setup / 'retrofe' / 'retrofe.exe').exists() or (setup / 'layouts').exists():
            raise ValueError('setup conflicts with bare install')
    retro = out / 'retrofe'
    retro.mkdir(parents=True, exist_ok=True)
    for file in sorted(dlls.iterdir()):
        if file.name not in testdrive.SKIP and file.is_file():
            shutil.copy2(file, retro / file.name)
    shutil.copy2(exe, retro / 'retrofe.exe')
    shutil.copytree(common / 'layouts' / 'Aeon Nox', out / 'layouts' / 'Aeon Nox')
    if setup:
        # The setup may not replace files supplied by the bare install.
        for source in setup.rglob('*'):
            destination = out / source.relative_to(setup)
            if (destination.exists() or destination.is_symlink()) and (not source.is_dir() or not destination.is_dir() or source.is_symlink()):
                raise ValueError('setup conflicts with bare install: ' + str(source.relative_to(setup)))
        shutil.copytree(setup, out, dirs_exist_ok=True, symlinks=True)
        # Not part of first setup: discard launches made by the test itself.
        launches = out / 'emulators' / 'RetroArch' / 'launches.txt'
        launches.unlink(missing_ok=True)
        # Not part of first setup: add a missing game to the complete Browse list.
        nes = out / 'collections' / 'Nintendo Entertainment System'
        settings = nes / 'settings.conf'
        data = settings.read_bytes()
        # ImportDirectory reads only configured extensions and ignores folders by
        # default; Add games flattened More games into this directory.
        match = re.search(rb'(?m)^\s*list\.extensions\s*=\s*([^\r\n#]+)', data)
        if not match:
            raise ValueError('NES setup has no list.extensions')
        extensions = {'.' + value.strip() for value in match.group(1).decode('utf-8').split(',')}
        games = sorted({path.stem for path in (nes / 'roms').iterdir()
                        if path.is_file() and path.suffix in extensions})
        (nes / 'include.txt').write_text(''.join(name + '\n' for name in games) + 'Missing Game (USA)\n', encoding='utf-8')
        newline = b'\r\n' if b'\r\n' in data else b'\n'
        if data and (not data.endswith((b'\n', b'\r'))):
            data += newline
        settings.write_bytes(data + b'list.includeMissingItems = true' + newline)
    print(f'Made {kind} install in {out}')


class Windows:
    """user32 and gdi32 calls for finding the window, posting input and capturing it."""

    def __init__(self):
        """Load Windows APIs used for input and capture."""
        if sys.platform != 'win32':
            raise OSError('run requires Windows')
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes
        self.wintypes = wintypes
        self.user32 = ctypes.WinDLL('user32', use_last_error=True)
        self.gdi32 = ctypes.WinDLL('gdi32', use_last_error=True)
        user32, gdi32 = (self.user32, self.gdi32)
        for lib, name, result, args in [
            (user32, 'GetDC', wintypes.HDC, [wintypes.HWND]),
            (user32, 'ReleaseDC', ctypes.c_int, [wintypes.HWND, wintypes.HDC]),
            (user32, 'PrintWindow', wintypes.BOOL, [wintypes.HWND, wintypes.HDC, wintypes.UINT]),
            (user32, 'GetClientRect', wintypes.BOOL, [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]),
            (user32, 'PostMessageW', wintypes.BOOL, [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]),
            (user32, 'MapVirtualKeyW', wintypes.UINT, [wintypes.UINT, wintypes.UINT]),
            (user32, 'IsWindowVisible', wintypes.BOOL, [wintypes.HWND]),
            (user32, 'GetWindowThreadProcessId', wintypes.DWORD, [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]),
            (gdi32, 'CreateCompatibleDC', wintypes.HDC, [wintypes.HDC]),
            (gdi32, 'CreateCompatibleBitmap', wintypes.HBITMAP, [wintypes.HDC, ctypes.c_int, ctypes.c_int]),
            (gdi32, 'SelectObject', wintypes.HGDIOBJ, [wintypes.HDC, wintypes.HGDIOBJ]),
            (gdi32, 'DeleteObject', wintypes.BOOL, [wintypes.HGDIOBJ]),
            (gdi32, 'DeleteDC', wintypes.BOOL, [wintypes.HDC]),
            (gdi32, 'GetDIBits', ctypes.c_int, [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT, ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]),
        ]:
            function = getattr(lib, name)
            function.restype = result
            function.argtypes = args
        self.window_callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        user32.EnumWindows.argtypes = [self.window_callback, wintypes.LPARAM]
        user32.EnumWindows.restype = wintypes.BOOL

    def window(self, pid):
        """Find a visible window belonging to the process."""
        found = []

        @self.window_callback
        def visit(window_handle, _):
            owner = self.wintypes.DWORD()
            self.user32.GetWindowThreadProcessId(window_handle, self.ctypes.byref(owner))
            if owner.value == pid and self.user32.IsWindowVisible(window_handle):
                found.append(window_handle)
                return False
            return True
        self.user32.EnumWindows(visit, 0)
        return found[0] if found else None

    def post(self, window_handle, message, wparam=0, lparam=0):
        """Post a message to the window."""
        if not self.user32.PostMessageW(window_handle, message, wparam, lparam):
            raise OSError('PostMessageW failed')

    def capture(self, window_handle):
        """Capture the client area as RGB pixels."""
        ctypes, wintypes, user32, gdi32 = (self.ctypes, self.wintypes, self.user32, self.gdi32)

        class Header(ctypes.Structure):
            _fields_ = [
                ('size', wintypes.DWORD),
                ('width', wintypes.LONG),
                ('height', wintypes.LONG),
                ('planes', wintypes.WORD),
                ('bits', wintypes.WORD),
                ('compression', wintypes.DWORD),
                ('image', wintypes.DWORD),
                ('x', wintypes.LONG),
                ('y', wintypes.LONG),
                ('used', wintypes.DWORD),
                ('important', wintypes.DWORD),
            ]
        rect = wintypes.RECT()
        if not user32.GetClientRect(window_handle, ctypes.byref(rect)):
            raise OSError('GetClientRect failed')
        width, height = (rect.right, rect.bottom)
        if width <= 0 or height <= 0:
            raise OSError('empty client area')
        device_context = user32.GetDC(window_handle)
        memory_context = gdi32.CreateCompatibleDC(device_context)
        bitmap = gdi32.CreateCompatibleBitmap(device_context, width, height)
        if not device_context or not memory_context or (not bitmap):
            raise OSError('cannot create capture bitmap')
        old_object = gdi32.SelectObject(memory_context, bitmap)
        try:
            # Flag 3 requests the client area and full rendered content.
            if not user32.PrintWindow(window_handle, memory_context, 3):
                raise OSError('PrintWindow failed')
            gdi32.SelectObject(memory_context, old_object)
            old_object = None
            info = Header(ctypes.sizeof(Header), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
            data = (ctypes.c_ubyte * (width * height * 4))()
            if gdi32.GetDIBits(memory_context, bitmap, 0, height, data, ctypes.byref(info), 0) != height:
                raise OSError('GetDIBits failed')
            raw = bytes(data)
            rgb = bytearray(width * height * 3)
            rgb[0::3], rgb[1::3], rgb[2::3] = (raw[2::4], raw[1::4], raw[0::4])
            return (width, height, bytes(rgb))
        finally:
            if old_object:
                gdi32.SelectObject(memory_context, old_object)
            gdi32.DeleteObject(bitmap)
            gdi32.DeleteDC(memory_context)
            user32.ReleaseDC(window_handle, device_context)


def run(install_dir, script, out, width=960, height=540, timeout=120):
    """Run a script against the Windows executable."""
    root, script, out = (Path(install_dir).resolve(), Path(script).resolve(), Path(out).resolve())
    out.mkdir(parents=True, exist_ok=True)
    exe = root / 'retrofe' / 'retrofe.exe'
    log = root / 'log.txt'
    args = ['-fullscreen', 'no', '-horizontal', str(width), '-vertical', str(height), '-windowBorder', 'false', '-log', 'ALL']
    result = dict(install=str(root), script=str(script), exe=str(exe), args=args, steps=[])
    process = None
    console = None
    started = time.monotonic()
    deadline = started + timeout
    current = None

    def check():
        """Stop the script when the time is up or RetroFE has gone."""
        if time.monotonic() >= deadline:
            raise TimeoutError('overall timeout')
        if process is not None and process.poll() is not None:
            raise RuntimeError('RetroFE exited before quit (exit code ' + exit_text(process.returncode) + ')')

    def pause(seconds):
        """Wait, still checking that RetroFE is running."""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            check()
            time.sleep(max(0.0, min(0.05, end - time.monotonic())))

    def contents(path):
        """A text file's contents, or nothing if it isn't there yet."""
        try:
            return path.read_text(encoding='utf-8', errors='replace')
        except FileNotFoundError:
            return ''
    try:
        source = script.read_text(encoding='utf-8-sig')
        try:
            steps = parse_script(source)
        except ValueError as error:
            match = re.match(r'line (\d+):', str(error))
            if match:
                number = int(match.group(1))
                current = dict(line=number, command=source.splitlines()[number - 1].strip(), ok=False, seconds=0, message='', shot=None)
                result['steps'].append(current)
            raise
        if timeout <= 0 or width <= 0 or height <= 0:
            raise ValueError('width, height and timeout must be positive')
        win = Windows()
        env = os.environ.copy()
        # Avoid stealing focus from the CI desktop when the SDL window appears.
        env['SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN'] = '1'
        # RetroFE's console output (messages from before logging starts, crashes) goes to stdout.txt
        console = open(out / 'stdout.txt', 'wb')
        process = subprocess.Popen([str(exe)] + args, cwd=root, env=env, stdout=console, stderr=subprocess.STDOUT)
        end = min(deadline, time.monotonic() + 30)
        window_handle = None
        while time.monotonic() < end:
            check()
            window_handle = win.window(process.pid)
            if window_handle:
                break
            time.sleep(0.05)
        if not window_handle:
            raise TimeoutError('window did not appear within 30 seconds' if time.monotonic() < deadline else 'overall timeout')
        shots = 0
        saved_shots = {}
        drawn = False
        for step in steps:
            current = dict(line=step['line'], command=step['command'], ok=False, seconds=0, message='', shot=None)
            result['steps'].append(current)
            tick = time.monotonic()
            try:
                check()
                operation, arguments = (step['op'], step['args'])
                if operation == 'wait':
                    pause(arguments[0])
                elif operation == 'wait-log':
                    end = time.monotonic() + arguments[1]
                    while arguments[0] not in contents(log):
                        check()
                        if time.monotonic() >= end:
                            raise TimeoutError('wait-log timed out: ' + arguments[0])
                        pause(0.05)
                elif operation == 'wait-drawn':
                    # RetroFE takes several seconds to start on a CI runner (GStreamer's
                    # plugin scan), and its window is black until the first page is drawn
                    end = time.monotonic() + arguments[0]
                    while is_blank(win.capture(window_handle)[2]):
                        if time.monotonic() >= end:
                            raise TimeoutError('the window stayed blank')
                        pause(0.5)
                    drawn = True
                elif operation == 'key':
                    virtual_key, count, hold = arguments
                    scan_code = win.user32.MapVirtualKeyW(virtual_key, 0)
                    for index in range(count):
                        win.post(window_handle, WM_KEYDOWN, virtual_key, key_lparam(virtual_key, scan_code))
                        pause(hold)
                        win.post(window_handle, WM_KEYUP, virtual_key, key_lparam(virtual_key, scan_code, True))
                        if index + 1 < count:
                            # A press while a list is still scrolling to the last one is
                            # dropped (seen on CI: 0.15 s apart, two Rights moved once)
                            pause(0.5)
                elif operation == 'type':
                    encoded = arguments[0].encode('utf-16-le')
                    for index in range(0, len(encoded), 2):
                        win.post(window_handle, WM_CHAR, int.from_bytes(encoded[index:index + 2], 'little'), 1)
                elif operation == 'shot':
                    shots += 1
                    name = f'{shots:02d}-{arguments[0]}.png'
                    shot_width, shot_height, rgb = win.capture(window_handle)
                    (out / name).write_bytes(png(shot_width, shot_height, rgb))
                    current['shot'] = name
                    current['blank'] = is_blank(rgb)
                    saved_shots[arguments[0]] = (shot_width, shot_height, rgb)
                    if drawn and current['blank']:
                        raise RuntimeError('blank screenshot')
                elif operation == 'expect-differs':
                    if arguments[0] not in saved_shots:
                        raise RuntimeError('unknown shot: ' + arguments[0])
                    old_width, old_height, old_rgb = saved_shots[arguments[0]]
                    new_width, new_height, new_rgb = win.capture(window_handle)
                    if (old_width, old_height) != (new_width, new_height):
                        raise RuntimeError('screenshot dimensions changed')
                    if not region_differs(new_width, new_height, old_rgb, new_rgb, arguments[1:]):
                        raise RuntimeError('screenshot region did not differ: ' + arguments[0])
                elif operation == 'expect-file':
                    path = root / arguments[0]
                    if not path.is_file():
                        raise RuntimeError('file missing: ' + arguments[0])
                    if arguments[1] is not None and arguments[1] not in contents(path):
                        raise RuntimeError('text missing from ' + arguments[0] + ': ' + arguments[1])
                elif operation == 'expect-log':
                    if arguments[0] not in contents(log):
                        raise RuntimeError('text missing from log: ' + arguments[0])
                elif operation == 'quit':
                    # A second close: the first may only show Manage's unsaved-changes warning
                    for attempt in range(2):
                        win.post(window_handle, WM_CLOSE)
                        end = min(deadline, time.monotonic() + 10)
                        while process.poll() is None and time.monotonic() < end:
                            time.sleep(0.05)
                        if process.poll() is not None:
                            break
                    if process.poll() is None:
                        raise TimeoutError('overall timeout' if time.monotonic() >= deadline else 'quit timed out')
                    if process.returncode != 0:
                        raise RuntimeError('RetroFE exited with exit code ' + exit_text(process.returncode) + ' after quit')
                current['ok'] = True
                current['message'] = 'ok'
            finally:
                current['seconds'] = round(time.monotonic() - tick, 3)
        if process.poll() is None:
            raise RuntimeError('script ended without quit')
        return 0
    except Exception as error:
        message = str(error).replace('\n', ' ')
        if current and (not current['ok']):
            current['message'] = message
        else:
            result['error'] = message
        print('FAIL ' + (f"line {current['line']}: " if current and (not current['ok']) else '') + message)
        return 1
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        if process is not None:
            result['exit_code'] = exit_text(process.returncode)
        if console is not None:
            console.close()
        try:
            if log.is_file() and log.resolve() != (out / 'log.txt').resolve():
                shutil.copy2(log, out / 'log.txt')
        except OSError as error:
            result['log_copy_error'] = str(error)
        (out / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


def main():
    """The install and run commands."""
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest='action', required=True)
    installer = sub.add_parser('install')
    installer.add_argument('--kind', choices=['bare', 'starter'], required=True)
    installer.add_argument('--exe', required=True)
    installer.add_argument('--dlls', required=True)
    installer.add_argument('--common', required=True)
    installer.add_argument('--setup')
    installer.add_argument('out')
    runner = sub.add_parser('run')
    runner.add_argument('--install', required=True)
    runner.add_argument('--script', required=True)
    runner.add_argument('--out', required=True)
    runner.add_argument('--width', type=int, default=960)
    runner.add_argument('--height', type=int, default=540)
    runner.add_argument('--timeout', type=float, default=120)
    arguments = parser.parse_args()
    try:
        if arguments.action == 'install':
            install(arguments.kind, arguments.exe, arguments.dlls, arguments.common, arguments.out, arguments.setup)
            return 0
        return run(arguments.install, arguments.script, arguments.out, arguments.width, arguments.height, arguments.timeout)
    except (OSError, ValueError) as error:
        print('FAIL ' + str(error))
        return 1


if __name__ == '__main__':
    sys.exit(main())
