"""Linux first-run setup, step one: the verified build tools. Run by the Linux launcher with the
system's Python 3. Independently authored; LGPL-2.1-or-later. No game data or code is downloaded.

  linux_toolkit.py --package <release folder> --data-root <data folder>

Does on Linux what the Windows launcher does itself before setup_game.py: downloads the
toolchain archive pinned in recipe/toolkit-linux.json (size + SHA-256 checked), unpacks it
into <data>/toolkits/<id>/work/tooling/toolchain with its file modes, then runs
tools/setup_game.py with the toolchain's own Python (python/bin/python3), which does the rest
(framework, recompiler, the local game build). Progress goes to build-status.txt
("<message>\\nprogress=<0-100>", read by the launcher) and setup.log."""
import argparse, hashlib, json, os, pathlib, shutil, stat, subprocess, sys, time, urllib.request, uuid, zipfile

ap = argparse.ArgumentParser()
ap.add_argument('--package', required=True); ap.add_argument('--data-root', required=True)
args = ap.parse_args()
PACKAGE = pathlib.Path(args.package).resolve(); DATA = pathlib.Path(args.data_root).resolve()
DATA.mkdir(parents=True, exist_ok=True)
STATUS = DATA / 'build-status.txt'; LOG = DATA / 'setup.log'
NEED_GB = 12   # the archive, the unpacked tools and setup_game.py's own 8 GB

LOGGED = ['']
def status(message, percent=None):
    """build-status.txt every time; setup.log once per message (the download's every 25%)."""
    text = message + ('\nprogress=%d\n' % percent if percent is not None else '')
    STATUS.write_text(text, encoding='utf-8')
    key = message.split(':')[0] + ('' if percent is None else ' %d%%' % (percent // 25 * 25))
    if key != LOGGED[0]:
        LOGGED[0] = key
        with open(LOG, 'a', encoding='utf-8') as f: f.write(message + '\n')
    print(message, flush=True)

def fail(message):
    with open(LOG, 'a', encoding='utf-8') as f: f.write('SETUP FAILED: ' + message + '\n')
    STATUS.write_text('SETUP FAILED: ' + message, encoding='utf-8')
    sys.exit(1)

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''): h.update(block)
    return h.hexdigest()

def verified(path, pin):
    return path.is_file() and path.stat().st_size == pin['size'] and sha256(path) == pin['sha256']

def download(pin, target):
    partial = target.with_name(target.name + '.partial')
    size_mb = pin['size'] / 2**20
    for attempt in range(1, 4):
        try:
            request = urllib.request.Request(pin['url'], headers={'User-Agent': 'JailbreakRecomp-setup'})
            with urllib.request.urlopen(request, timeout=600) as response, open(partial, 'wb') as out:
                done = 0; shown = -1
                while True:
                    block = response.read(1 << 20)
                    if not block: break
                    out.write(block); done += len(block)
                    percent = min(99, done * 100 // pin['size'])
                    if percent != shown:
                        shown = percent
                        status('Downloading build tools (first installation only): %d of %d MB' % (done // 2**20, size_mb), percent)
            if not verified(partial, pin): raise RuntimeError('the download failed its size/SHA-256 check')
            os.replace(partial, target); return
        except Exception as error:
            if attempt == 3: fail('The build tools could not be downloaded (%s). Check your internet connection, then choose Run setup again.' % error)
            status('Retrying the download (%s)' % error); time.sleep(3)

def unpack(archive, destination):
    """Every entry with the same path checks as setup_game.py, and its Unix mode (the tools
    must stay executable); symbolic links only inside the folder."""
    destination.mkdir(parents=True, exist_ok=True); base = str(destination.resolve()) + os.sep
    with zipfile.ZipFile(archive) as z:
        entries = z.infolist(); total = sum(e.file_size for e in entries) or 1; done = 0; shown = -1
        for entry in entries:
            name = entry.filename
            if '\\' in name or name.startswith('/') or '..' in name.split('/'): raise RuntimeError('unsafe archive path ' + name)
            target = destination / name
            if not str(target.resolve()).startswith(base) and str(target.resolve()) + os.sep != base: raise RuntimeError('archive path escaped the folder: ' + name)
            mode = entry.external_attr >> 16
            if name.endswith('/'): target.mkdir(parents=True, exist_ok=True); continue
            target.parent.mkdir(parents=True, exist_ok=True)
            if stat.S_ISLNK(mode):
                link = z.read(entry).decode('utf-8')
                where = str((target.parent / link).resolve())
                if link.startswith('/') or not (where.startswith(base) or where + os.sep == base): raise RuntimeError('unsafe link ' + name)
                if target.is_symlink() or target.exists(): target.unlink()
                os.symlink(link, target); continue
            with z.open(entry) as src, open(target, 'wb') as out: shutil.copyfileobj(src, out, 1 << 20)
            os.chmod(target, (mode & 0o777) if mode & 0o777 else 0o644)
            done += entry.file_size; percent = done * 100 // total
            if percent != shown: shown = percent; status('Installing the verified compiler and Python', percent)

try:
    pins = json.loads((PACKAGE / 'recipe/toolkit-linux.json').read_text(encoding='utf-8'))
    pin = pins['toolchain']
    ws = DATA / 'toolkits' / pins['id']
    tools = ws / 'work/tooling/toolchain'; python = tools / 'python/bin/python3'
    stamp = ws / 'toolchain-unpacked.txt'
    if not (python.is_file() and stamp.is_file() and stamp.read_text(encoding='utf-8') == pin['sha256']):
        free = shutil.disk_usage(DATA).free / 2**30
        if free < NEED_GB: fail('Not enough space left on the drive (%.1f GB free, about %d GB needed). Free some space, then choose Run setup again.' % (free, NEED_GB))
        downloads = DATA / 'downloads'; downloads.mkdir(parents=True, exist_ok=True)
        archive = downloads / pin['file']
        if verified(archive, pin): status('Verified cached download: ' + pin['file'])
        else: download(pin, archive)
        stage = DATA / 'toolkits' / ('new-' + uuid.uuid4().hex[:8])
        try:
            unpack(archive, stage / 'work/tooling/toolchain')
        except Exception as error:
            shutil.rmtree(stage, ignore_errors=True)
            fail('The build tools could not be unpacked (%s). Choose Run setup again.' % error)
        if ws.exists():
            # Keep what an earlier setup made beside the tools (framework, recompiler); replace the tools.
            old = ws / 'work/tooling/toolchain'
            if old.exists(): shutil.rmtree(old)
            old.parent.mkdir(parents=True, exist_ok=True)
            os.replace(stage / 'work/tooling/toolchain', old); shutil.rmtree(stage, ignore_errors=True)
        else:
            ws.parent.mkdir(parents=True, exist_ok=True); os.replace(stage, ws)
        stamp.write_text(pin['sha256'], encoding='utf-8')
    # The tools' own Python, free of the player's Python settings (PYTHONHOME / PYTHONPATH would
    # point it, and every build step it starts, at the system's library).
    env = {k: v for k, v in os.environ.items() if not k.startswith('PYTHON')}
    if subprocess.run([str(python), '-c', 'import zipfile, hashlib, json'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env).returncode:
        fail('The build tools\' Python could not start on this system. Choose Report a problem on GitHub with setup.log.')
    status('Build tools ready')
    script = PACKAGE / 'tools/setup_game.py'
    os.execve(str(python), [str(python), '-s', str(script), '--data-root', str(DATA)], env)
except SystemExit:
    raise
except Exception as error:
    fail('Setup stopped (%s). Choose Run setup again; if it fails again, report it on GitHub with setup.log.' % error)
