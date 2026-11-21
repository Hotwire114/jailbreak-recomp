"""First-run setup and rebuilds, run by the launcher with the toolkit's own Python.
Independently authored; LGPL-2.1-or-later. No game data or code is downloaded.

  setup_game.py --data-root <dir> [--workspace <dir>] [--archive-cache <dir>]
                [--offline] [--tools-only] [--force-compile]

The launcher has already downloaded, verified (size + SHA-256 from
recipe/toolkit.json) and unpacked the toolchain archive, which is where this
Python comes from. This script does the rest:
  1. the psxrecomp framework and libwebp archives: downloaded once (or taken
     from the cache), verified, and unpacked with only the generic engine,
     compiler and build inputs (no tests' cached data, title projects or retail
     BIOS images); the free OpenBIOS is checked against its pinned hash;
  2. the generic recompiler is built and a receipt of the tools' hashes kept,
     so later rebuilds run offline and detect damaged tools;
  3. the online game's two libraries (recipe/netplay.json) are downloaded once and verified;
  4. install_game.py compiles the native game from the player's installed disc, then the
     online game (runtime-netplay) from the same sources.
Progress goes to build-status.txt (shown by the launcher) and setup.log."""
import argparse, hashlib, json, os, pathlib, re, shutil, subprocess, sys, time, urllib.request, uuid, zipfile

HERE = pathlib.Path(__file__).resolve().parent
PACKAGE = HERE.parent
# Linux: its own toolchain pin (recipe/toolkit.json is the Windows one and never changes in an
# update) and program names without .exe.
WINDOWS = os.name == 'nt'
X = '.exe' if WINDOWS else ''
PIN_FILE = 'recipe/toolkit.json' if WINDOWS else 'recipe/toolkit-linux.json'
TOOL_PYTHON = 'work/tooling/toolchain/python/python.exe' if WINDOWS else 'work/tooling/toolchain/python/bin/python3'
ap = argparse.ArgumentParser()
ap.add_argument('--data-root', required=True); ap.add_argument('--workspace'); ap.add_argument('--archive-cache')
ap.add_argument('--offline', action='store_true'); ap.add_argument('--tools-only', action='store_true'); ap.add_argument('--force-compile', action='store_true')
args = ap.parse_args()
DATA = pathlib.Path(args.data_root).resolve(); DATA.mkdir(parents=True, exist_ok=True)
LOG = DATA / 'setup.log'; STATUS = DATA / 'build-status.txt'   # the launcher starts setup.log fresh

sys.path.insert(0, str(HERE)); import setup_report
STEP = ['starting']   # the step a failure is reported against
def status(message):
    with open(LOG, 'a', encoding='utf-8') as f: f.write(message + '\n')
    STATUS.write_text(message, encoding='utf-8')
    print(message, flush=True)
    if not message.startswith(('SETUP FAILED', 'Verified cached')): STEP[0] = message.split(':')[0]
with open(LOG, 'a', encoding='utf-8') as _f: _f.write(setup_report.header('setup_game.py', PACKAGE, DATA, args.workspace) + '\n')

NEED_GB = 8   # build folders, the two games and the compile cache (about 3 GB measured, cache up to 5 GB)

def plain_reason(error):
    """A failed setup in words a player can act on (launcher.c shows the line after "SETUP FAILED: ").
    The build logs' last lines are read too, since a compiler stopped by antivirus or a full disk
    only says so there."""
    text = str(error)
    for name in ('build.log', 'build-online.log', 'setup.log'):
        try: text += '\n' + (DATA / name).read_text(encoding='utf-8', errors='replace')[-6000:]
        except OSError: pass
    low = text.lower()
    if setup_report.NON_ASCII in text: return setup_report.NON_ASCII
    problem = setup_report.drive_problem(DATA)
    if problem: return problem
    if any(k in low for k in ('winerror 206', 'filename or extension is too long', 'path too long', 'name too long')):
        return ('The game folder\'s path is too long for the build. Choose a shorter folder, e.g. D:\\Games '
                '(Game installation > Change game folder), then choose Run setup again.')
    missing = [p for p in setup_report._paths_in(error) if not os.path.exists(p)]
    if isinstance(error, FileNotFoundError) or 'winerror 3]' in low or 'winerror 2]' in low or 'cannot find the path' in low:
        where = f' ({missing[0]})' if missing else ''
        return (f'Setup couldn\'t find a file or folder it needs{where} during "{STEP[0]}". Choose Run setup '
                'again; if it fails again, choose Report a problem so the log can be checked.')
    if any(k in low for k in ('no space left', 'not enough space', 'disk full', 'errno 28', 'winerror 112')):
        return f'Your drive is full. Setup needs about {NEED_GB} GB free. Free some space, then choose Run setup again.'
    if any(k in low for k in ('contains a virus', 'potentially unwanted', 'winerror 225', 'access is denied', 'permission denied',
                              'winerror 5]', 'is not recognized as an internal', 'being used by another process', 'winerror 32')):
        return ('Your antivirus or another program blocked a build tool. Allow the RoadRashJailbreak folder in '
                'your antivirus (or wait for its scan to finish), then choose Run setup again.')
    if 'unsupported game revision' in low or 'does not contain the supported game' in low:
        return 'This disc image is not Road Rash: Jailbreak (USA). Choose Repair / re-import game with the right .bin.'
    if 'import your original game' in low:
        return 'Import your original game first.'
    if any(k in low for k in ('urlopen error', 'timed out', 'getaddrinfo', 'connection', 'could not be downloaded', 'sha-256')):
        return 'A download failed. Check your internet connection, then choose Run setup again.'
    return (f'Setup stopped during "{STEP[0]}". Choose Run setup again; if it fails again, choose Report a '
            'problem (the log now says exactly what went wrong).')

# The Linux launcher has no Report a problem or Repair rows: point at the log and GitHub instead.
LINUX_WORDS = (('choose Report a problem so the log can be checked', 'report it on GitHub with setup.log (Open the setup log)'),
               ('choose Report a problem (the log now says exactly what went wrong)',
                'report it on GitHub with setup.log and build.log (Open the game folder)'),
               ('Choose Repair / re-import game', 'Choose Change game (.bin)'))
if not WINDOWS:
    _windows_reason = plain_reason
    def plain_reason(error):
        text = _windows_reason(error)
        for old, new in LINUX_WORDS: text = text.replace(old, new)
        return text

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''): h.update(block)
    return h.hexdigest()

def run_tool(command, env=None):
    with open(LOG, 'a', encoding='utf-8', errors='replace') as log:
        code = subprocess.run([str(c) for c in command], stdout=log, stderr=subprocess.STDOUT, env=env,
                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).returncode
    if code: raise RuntimeError(f'Build step failed ({code}). See setup.log and build.log.')

def verified(path, pin):
    return path.is_file() and path.stat().st_size == pin['size'] and sha256(path) == pin['sha256']

def get_archive(pin, cache):
    path = cache / pin['file']
    if verified(path, pin): status('Verified cached download: ' + pin['file']); return path
    if args.offline: raise RuntimeError(f"Offline setup needs a verified {pin['file']} in {cache}")
    status(f"Downloading build tools: {pin['file']} (first installation only)")
    partial = path.with_name(path.name + '.partial')
    for attempt in range(1, 4):
        try:
            with urllib.request.urlopen(pin['url'], timeout=600) as response, open(partial, 'wb') as out:
                shutil.copyfileobj(response, out, 1 << 20)
            if not verified(partial, pin): raise RuntimeError('Download failed its size/SHA256 check.')
            os.replace(partial, path); return path
        except Exception:
            if attempt == 3: raise
            status('Retrying download ' + pin['file']); time.sleep(3)

KEEP = re.compile(r'^(runtime/|recompiler/|cmake/|tools/|overlay_toolchain/|third_party/|bios/|generated/OpenBIOS|LICENSE$|THIRD_PARTY_ATTRIBUTION\.md$)')
def extract_framework(archive, destination, prefix):
    destination.mkdir(parents=True, exist_ok=True); base = destination.resolve()
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            if not entry.filename.startswith(prefix): raise RuntimeError('Unexpected archive root.')
            name = entry.filename[len(prefix):]
            if not name or name.endswith('/'): continue
            if '\\' in name or ':' in name or name.startswith('/') or '..' in name.split('/'): raise RuntimeError('Unsafe archive path.')
            # Keep only generic engine/compiler/build inputs (as the PowerShell setup did).
            if not KEEP.match(name): continue
            if re.search(r'(^|/)(__pycache__|\.cache)/', name): continue
            if re.search(r'\.(bin|exe|dll|iso|cue|png|bmp|tim|str|psh|pst|obj)$', name, re.I) and name != 'bios/openbios.bin': continue
            target = (destination / name).resolve()
            if not str(target).lower().startswith(str(base).lower() + os.sep): raise RuntimeError('Archive path escaped installation.')
            # Extended-length form on Windows: framework files nest past 260 characters.
            longp = pathlib.Path('\\\\?\\' + str(target)) if os.name == 'nt' else target
            longp.parent.mkdir(parents=True, exist_ok=True)
            with z.open(entry) as src, open(longp, 'wb') as out: shutil.copyfileobj(src, out)

def extract_library(archive, destination, prefix):
    """A source archive, whole, with the same path checks as the framework's."""
    destination.mkdir(parents=True, exist_ok=True); base = destination.resolve()
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            if not entry.filename.startswith(prefix): raise RuntimeError('Unexpected archive root.')
            name = entry.filename[len(prefix):]
            if not name or name.endswith('/'): continue
            if '\\' in name or ':' in name or name.startswith('/') or '..' in name.split('/'): raise RuntimeError('Unsafe archive path.')
            target = (destination / name).resolve()
            if not str(target).lower().startswith(str(base).lower() + os.sep): raise RuntimeError('Archive path escaped installation.')
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(entry) as src, open(target, 'wb') as out: shutil.copyfileobj(src, out)

def netplay_libs():
    """The online game's libraries (recipe/netplay.json, MIT): downloaded once, verified and unpacked
    into netplay-libs. Online is optional: without them only the offline game is built."""
    try:
        pins = json.loads((PACKAGE / 'recipe/netplay.json').read_text(encoding='utf-8'))
        root = DATA / 'netplay-libs'; root.mkdir(parents=True, exist_ok=True)
        cache = pathlib.Path(args.archive_cache).resolve() if args.archive_cache else DATA / 'downloads'; cache.mkdir(parents=True, exist_ok=True)
        for key in ('recomp_net', 'rbengine'):
            pin = pins[key]; target = root / pin['folder']; stamp = root / (pin['folder'] + '.sha256')
            if target.is_dir() and stamp.is_file() and stamp.read_text(encoding='utf-8') == pin['sha256']: continue
            archive = get_archive(pin, cache)
            partial = root / (pin['folder'] + '-' + uuid.uuid4().hex + '.partial')
            extract_library(archive, partial, pin['prefix'])
            if target.exists(): shutil.rmtree(target)
            os.replace(partial, target); stamp.write_text(pin['sha256'], encoding='utf-8')
        return root
    except Exception as e:
        status('Online play libraries unavailable (' + str(e) + '); building the offline game only')
        return None

try:
    # The build tools can't use a folder whose path has letters outside plain English (GitHub
    # issue 5): build through the folder's short name when there is one, else say so now.
    BUILD = setup_report.plain_path(DATA)
    if BUILD is None: raise RuntimeError(setup_report.NON_ASCII)
    if BUILD != DATA: status(f'Building through the folder\'s short name {BUILD} (its path has non-English letters)')
    if not args.tools_only and not (DATA / 'game-disc.bin').is_file(): raise RuntimeError('Import your original game in the launcher first.')
    free = shutil.disk_usage(DATA).free / 2**30
    if free < NEED_GB: raise RuntimeError(f'Not enough space left on the drive ({free:.1f} GB free, about {NEED_GB} GB needed)')
    manifest = PACKAGE / PIN_FILE; pins = json.loads(manifest.read_text(encoding='utf-8')); pin_hash = sha256(manifest)
    if args.workspace:
        ws = setup_report.plain_path(pathlib.Path(args.workspace).resolve()); status('Using the selected local development toolkit')
        if ws is None: raise RuntimeError(setup_report.NON_ASCII)
    else:
        ws = BUILD / 'toolkits' / pins['id']
        receipt_path = ws / 'toolkit-ready.json'
        if receipt_path.is_file():
            receipt = json.loads(receipt_path.read_text(encoding='utf-8-sig'))
            if receipt.get('manifest_sha256') != pin_hash: raise RuntimeError('Installed toolkit version differs from this release.')
            for f in receipt.get('files', []):
                p = ws / f['path']
                if not p.is_file() or sha256(p) != f['sha256']: raise RuntimeError('Installed build tool is damaged. Remove only the toolkits folder to prepare it again.')
            status('Verified installed build tools - no download required')
        else:
            unpacked = ws / 'toolkit-unpacked.txt'
            framework = ws / 'work/upstream/psxrecomp'
            if unpacked.is_file() and unpacked.read_text(encoding='utf-8') == pin_hash:
                status('Resuming the verified toolkit build')
            else:
                cache = pathlib.Path(args.archive_cache).resolve() if args.archive_cache else DATA / 'downloads'
                cache.mkdir(parents=True, exist_ok=True)
                fw_zip = get_archive(pins['framework'], cache); webp = get_archive(pins['webp'], cache)
                status('Installing generic engine sources and the free OpenBIOS')
                partial = framework.with_name('psxrecomp-' + uuid.uuid4().hex + '.partial')
                extract_framework(fw_zip, partial, pins['framework']['prefix'])
                shutil.copy2(webp, partial / 'third_party' / pins['webp']['installed_file'])
                if sha256(partial / 'bios/openbios.bin') != pins['openbios_sha256']: raise RuntimeError('Free OpenBIOS integrity check failed.')
                if framework.exists(): shutil.rmtree('\\\\?\\' + str(framework) if os.name == 'nt' else framework)
                os.replace(partial, framework)
                unpacked.write_text(pin_hash, encoding='utf-8')
            tools = ws / 'work/tooling/toolchain/bin'; build = ws / 'work/build-framework'
            env = dict(os.environ); env['PATH'] = str(tools) + os.pathsep + env.get('PATH', '')
            temp = ws / 'temp'; temp.mkdir(parents=True, exist_ok=True); env['TMP'] = env['TEMP'] = env['TMPDIR'] = str(temp)
            status('Building the generic recompiler (two jobs)')
            run_tool([tools / ('cmake' + X), '-S', framework / 'recompiler', '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                      f'-DCMAKE_C_COMPILER={tools.as_posix()}/clang{X}', f'-DCMAKE_CXX_COMPILER={tools.as_posix()}/clang++{X}',
                      '-DPSXRECOMP_ENABLE_CHD=OFF', '-DBUILD_TESTING=OFF'], env)
            run_tool([tools / ('cmake' + X), '--build', build, '--target', 'psxrecomp-game', '-j', '2'], env)
            bios = ws / 'work/build-stage2/bios'; bios.mkdir(parents=True, exist_ok=True)
            shutil.copy2(framework / 'bios/openbios.bin', bios); shutil.copy2(framework / 'bios/OpenBIOS.LICENSE', bios)
            names = ['work/tooling/toolchain/bin/clang' + X, 'work/tooling/toolchain/bin/cmake' + X, 'work/tooling/toolchain/bin/ninja' + X,
                     TOOL_PYTHON, 'work/build-framework/psxrecomp-game' + X, 'work/build-stage2/bios/openbios.bin',
                     'work/upstream/psxrecomp/third_party/' + pins['webp']['installed_file']]
            receipt = {'schema': 1, 'manifest_sha256': pin_hash, 'framework_revision': pins['framework_revision'], 'game_code_in_toolkit': False,
                       'files': [{'path': n, 'sha256': sha256(ws / n)} for n in names]}
            receipt_path.write_text(json.dumps(receipt, indent=2), encoding='utf-8')
            status('Build tools ready - retained for future offline rebuilds')
    if args.tools_only: sys.exit(0)
    # Linux (first native builds): the offline game only; online play there is not tested yet.
    libs = netplay_libs() if WINDOWS else None
    status('Compiling your native game from installed game data')
    command = [setup_report.plain_path(sys.executable) or sys.executable, HERE / 'install_game.py', '--data-root', BUILD, '--workspace', ws]
    if args.force_compile: command.append('--force-compile')
    if libs: command += ['--netplay-libs', libs]
    run_tool(command)
    status('LOCAL GAME BUILD READY - SELECT PLAY')
    sys.exit(0)
except SystemExit:
    raise
except Exception as e:
    report = setup_report.failure(e, STEP[0])
    for name in ('setup.log', 'build.log'):
        with open(DATA / name, 'a', encoding='utf-8') as f: f.write(report + '\n')
    status('SETUP FAILED: ' + plain_reason(e))
    sys.exit(1)
