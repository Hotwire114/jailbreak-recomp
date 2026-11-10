"""Linker launcher for reproducible game builds. Independently authored; LGPL-2.1-or-later.

relpath_ld.py <linker> <arguments...>

Run by the game build (runtime-CMakeLists.txt, *_LINKER_LAUNCHER) in front of
the link, the partner of relpath_cc.py. ThinLTO knows each input by its path
as the linker was given it, and some of the order it imports and lays out
data in follows those names. CMake names the objects of sources outside the
build folder after their absolute paths (the data folder, the toolkit, which
contain the Windows user name), so two installs of identical objects linked
slightly different games (the 1.1 build-speed work showed it: two folders,
every object the same, two exes). Here every object and library input is
hard-linked (copied on failure) as link-inputs/NNNN.obj or .a, numbered in
link order, and the linker gets those names: the same in every install.
"""
import os, shlex, shutil, subprocess, sys

INPUTS = 'link-inputs'

def quote(token):
    return '"' + token.replace('\\', '\\\\').replace('"', '\\"') + '"'

def main(args):
    if os.path.isdir(INPUTS):
        for name in os.listdir(INPUTS): os.remove(os.path.join(INPUTS, name))
    else:
        os.makedirs(INPUTS)
    count = 0
    def canonical(token):
        nonlocal count
        ext = os.path.splitext(token)[1].lower()
        if ext not in ('.obj', '.o', '.a') or token.startswith('-') or not os.path.isfile(token): return token
        target = f'{INPUTS}/{count:04d}{ext}'; count += 1
        try: os.link(token, target)
        except OSError: shutil.copyfile(token, target)
        return target
    out = []
    for a in args:
        if a.startswith('@') and os.path.isfile(a[1:]):
            with open(a[1:], encoding='utf-8') as f: tokens = shlex.split(f.read(), posix=True)
            rsp = os.path.splitext(a[1:])[0] + '.canonical.rsp'
            with open(rsp, 'w', encoding='utf-8') as f: f.write('\n'.join(quote(canonical(t)) for t in tokens) + '\n')
            out.append('@' + rsp)
        else:
            out.append(canonical(a))
    return subprocess.run(out).returncode

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
