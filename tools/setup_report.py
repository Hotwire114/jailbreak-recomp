"""Clearer setup and build logs (setup_game.py, install_game.py). Independently authored;
LGPL-2.1-or-later. Reads only facts about this PC's folders and drive; no game data.

  header(...)       what a report reader needs first: versions, the folders setup uses, the
                    drive they are on (type, file system, free space) and path lengths
  failure(...)      the step that failed, the error, the file or folder involved, and the
                    Python traceback (where in the scripts it broke)
  tool_failed(...)  a build tool's exit code and the last lines it printed
  drive_problem(p)  a short reason when a folder's drive can't hold the build (FAT32/exFAT,
                    CD/network drive), or None
The reports go into setup.log / build.log, which the launcher's Report a problem zips."""
import ctypes, os, pathlib, platform, re, sys, time, traceback

LONG_PATH = 200   # the build nests deep folders under these; past ~260 characters old tools fail

def _volume(path):
    """(root, drive type, file system) of the drive holding path, Windows only."""
    if os.name != 'nt': return None, '', ''
    try:
        p = str(pathlib.Path(path).resolve())
        root = ctypes.create_unicode_buffer(1024)
        if not ctypes.windll.kernel32.GetVolumePathNameW(p, root, 1024): return None, '', ''
        kinds = {0: 'unknown', 1: 'no root', 2: 'removable', 3: 'fixed', 4: 'network', 5: 'CD/DVD', 6: 'RAM disk'}
        kind = kinds.get(ctypes.windll.kernel32.GetDriveTypeW(root.value), 'unknown')
        fs = ctypes.create_unicode_buffer(64)
        ctypes.windll.kernel32.GetVolumeInformationW(root.value, None, 0, None, None, None, fs, 64)
        return root.value, kind, fs.value
    except Exception:
        return None, '', ''

def _free_gb(path):
    try:
        import shutil
        return shutil.disk_usage(path).free / 2**30
    except OSError:
        return -1

def drive_problem(path):
    root, kind, fs = _volume(path)
    if fs and fs.upper() in ('FAT32', 'FAT', 'EXFAT'):
        return (f'The game folder is on a {fs} drive ({root}), which the build may not work on. If setup keeps '
                'failing, choose a folder on an NTFS drive (Game installation > Change game folder).')
    if kind in ('CD/DVD', 'network'):
        return f'The game folder is on a {kind} drive ({root}); choose a folder on a local drive.'
    return None

NON_ASCII = ('The game folder\'s path has letters the build tools can\'t handle (like a, o or u with umlauts, '
             'or accented letters). Choose a folder whose path only uses English letters, numbers and spaces, '
             'e.g. D:\\Games (Game installation > Change game folder), then choose Run setup again.')

def plain_path(path):
    """A path the build tools can use. CMake writes its build steps as UTF-8 batch files and
    cmd.exe reads them in the PC's code page, so a folder with letters such as a, o, u with
    umlauts or e with an accent is "not found" mid-build (GitHub issue 5). Returns the path
    when it is plain ASCII, else Windows' short (8.3) name for the same folder when the drive
    keeps those, else None."""
    p = str(path)
    if p.isascii(): return pathlib.Path(p)
    if os.name == 'nt':
        try:
            buf = ctypes.create_unicode_buffer(32768)
            if ctypes.windll.kernel32.GetShortPathNameW(p, buf, 32768) and buf.value.isascii():
                return pathlib.Path(buf.value)
        except Exception:
            pass
    return None

def _version(package):
    try:
        text = (pathlib.Path(package) / 'source' / 'launcher_update.h').read_text(encoding='utf-8')
        return re.search(r'#define RR_VERSION "([\d.]+)"', text)[1]
    except Exception:
        return 'unknown'

def header(script, package, data_root, workspace=None):
    lines = [f'==== {script} {time.strftime("%Y-%m-%d %H:%M:%S")} ====',
             f'Jailbreak Recomp {_version(package)}; Windows {platform.version()}; Python {platform.python_version()}']
    for name, folder in (('Game folder', data_root), ('Build tools', workspace), ('Package', package)):
        if not folder: continue
        root, kind, fs = _volume(folder)
        lines.append(f'{name}: {folder} ({len(str(folder))} characters'
                     + (f'; drive {root} {kind} {fs}, {_free_gb(folder):.1f} GB free' if root else '') + ')')
        if len(str(folder)) > LONG_PATH:
            lines.append(f'  NOTE: a long folder path; the build nests deep folders under it')
    problem = drive_problem(data_root)
    if problem: lines.append('  NOTE: ' + problem)
    return '\n'.join(lines)

def _paths_in(error):
    """The file or folder an OS error names (WinError 2 / 3 / 206 and the like)."""
    out = []
    for attr in ('filename', 'filename2'):
        v = getattr(error, attr, None)
        if v: out.append(str(v))
    return out

def failure(error, step, fatal=True):
    """A block for the log: what failed, where, and the traceback."""
    lines = ['', '==== ' + ('SETUP STOPPED' if fatal else 'PROBLEM') + f' during: {step} ====',
             f'Error: {type(error).__name__}: {error}']
    for p in _paths_in(error):
        exists = os.path.exists(p)
        lines.append(f'Path involved: {p} ({len(p)} characters; {"exists" if exists else "does not exist"})')
        parent = pathlib.Path(p).parent
        while not parent.exists() and parent != parent.parent: parent = parent.parent
        if not exists: lines.append(f'  nearest existing folder: {parent}')
    winerror = getattr(error, 'winerror', None)
    if winerror: lines.append(f'Windows error code: {winerror}')
    lines.append('Where it happened (Python traceback):')
    lines += ['  ' + l for l in ''.join(traceback.format_exception(type(error), error, error.__traceback__)).rstrip().splitlines()]
    return '\n'.join(lines)

def tool_failed(tool, code, step, logfile, tail=25):
    """A build tool's failure with its last lines (the reason is nearly always there)."""
    lines = ['', f'==== {tool} failed (exit code {code}) during: {step} ====']
    try:
        text = pathlib.Path(logfile).read_text(encoding='utf-8', errors='replace').rstrip().splitlines()
        last = [l for l in text if l.strip() and not l.startswith('====')][-tail:]
        lines.append(f'Last lines it printed:')
        lines += ['  ' + l for l in last]
    except OSError:
        pass
    return '\n'.join(lines)
