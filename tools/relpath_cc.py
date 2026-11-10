"""Compiler launcher for reproducible game builds. Independently authored; LGPL-2.1-or-later.

relpath_cc.py <next launcher or compiler> <arguments...>

Run by the game build (runtime-CMakeLists.txt, *_COMPILER_LAUNCHER) in front of
ccache / clang. It hands the compiler the source file as a path relative to
the build folder instead of an absolute one. ThinLTO names the file-private
symbols it shares between files after a hash of each file's compiled code,
which includes that source path, and the linker orders some data by those
names: with absolute paths every install folder gave a slightly different
game. Relative paths are the same in every install (the data folder's layout
is fixed), so every player's build of the same disc is byte-identical.

A define naming a header inside the build folder (the framework's
PSX_EXECUTION_PROFILE_CONFIG) is made relative the same way, and the build
folder is searched last for it: the define's text is part of the compiled
code's source positions, which the speed profile build hashes too. For
the same reason the whole data folder is mapped to "data", which also covers
the toolchain's own headers."""
import os, re, subprocess, sys

def rel(p):
    try: return os.path.relpath(p).replace('\\', '/')
    except ValueError: return None   # another drive: keep the absolute path

args = sys.argv[1:]
for i in range(len(args) - 1):
    if args[i] == '-c' and os.path.isabs(args[i + 1]):
        args[i + 1] = rel(args[i + 1]) or args[i + 1]
        break
here = False
for i, a in enumerate(args):
    m = re.fullmatch(r'(-D\w+=")([^"]+)(")', a)
    if m and os.path.isabs(m.group(2)):
        r = rel(m.group(2))
        if r and not r.startswith('..'): args[i] = m.group(1) + r + m.group(3); here = True
if here: args += ['-idirafter', '.']   # appended: args[0] may be ccache, not the compiler
# Every other path under the data folder (toolchain headers included) maps to "data".
args.append('-ffile-prefix-map=' + os.path.dirname(os.getcwd()).replace('\\', '/') + '=data')
sys.exit(subprocess.run(args).returncode)
