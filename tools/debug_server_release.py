"""Recipe: debug_server.c with the TCP debug server off in the public release.

debug_server_release.py <framework runtime/src/debug_server.c> <output .c>

The framework's debug tools stay compiled in (the PC adapters hook their
observation points), but a public build (RR_PUBLIC_RELEASE) never opens the
localhost debug port: debug_server_init returns where it would when the port
is already in use, the path a second game instance takes, so the game runs
exactly as it does then. Builds without the define are unchanged.
Independently authored; LGPL-2.1-or-later."""
import pathlib, sys

ANCHOR = '    s_listen = socket(AF_INET, SOCK_STREAM, 0);\n'
GUARD = ('#ifdef RR_PUBLIC_RELEASE\n'
         '    /* Public release: no debug port (tools/debug_server_release.py). */\n'
         '    fprintf(stdout, "psxrecomp: debug server off in this release\\n");\n'
         '    return;\n'
         '#endif\n')

def extend(source):
    if source.count(ANCHOR) != 1:
        raise ValueError('debug_server.c changed: review the release guard')
    return source.replace(ANCHOR, GUARD + ANCHOR)

if __name__ == '__main__':
    src, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    text = extend(src.read_text(encoding='utf-8', errors='surrogateescape'))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding='utf-8', errors='surrogateescape')
