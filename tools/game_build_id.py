"""Game build ID: a fingerprint of everything that goes into the compiled game. Independently
authored; LGPL-2.1-or-later.

  game_build_id.py [package folder]        prints the ID
  game_build_id.py --write [package folder] writes game-build-id.txt beside the launcher

The files are every recipe/ file, every tools/*.py, source/runtime-CMakeLists.txt, the source
files tools/install_game.py copies into the build, and source/netplay-shim/. Launcher-only
files (launcher.c and its headers, the account sign-in) are not in it, so an update that only
changes the launcher or the docs keeps the ID: the launcher then skips the rebuild (launcher.c
reads game-build-id.txt and compares it with the ID in local-build.json). The online games'
fingerprint is built on it too, so players pair whenever their games are the same."""
import hashlib, pathlib, re, sys

def files(package):
    package = pathlib.Path(package)
    install = (package / 'tools/install_game.py').read_text(encoding='utf-8')
    m = re.search(r"for name in \[([^\]]*)\]:shutil\.copy2\(package/'source'/name", install)
    if not m: raise RuntimeError('game_build_id: the installer copy list was not found')
    names = re.findall(r"'([^']+)'", m[1])
    out = [package / 'source' / n for n in names] + [package / 'source/runtime-CMakeLists.txt']
    out += sorted((package / 'source/netplay-shim').rglob('*'))
    out += sorted((package / 'tools').glob('*.py')) + sorted((package / 'recipe').rglob('*'))
    return [p for p in out if p.is_file() and '__pycache__' not in p.parts]

def build_id(package):
    package = pathlib.Path(package).resolve(); h = hashlib.sha256()
    for p in sorted(set(files(package))):
        body = p.read_bytes(); rel = p.relative_to(package).as_posix().encode()
        h.update(rel + b'\0' + len(body).to_bytes(8, 'little') + body)
    return h.hexdigest()

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--write']
    package = pathlib.Path(args[0] if args else pathlib.Path(__file__).resolve().parent.parent)
    value = build_id(package)
    if '--write' in sys.argv: (package / 'game-build-id.txt').write_text(value + '\n', encoding='ascii')
    print(value)
