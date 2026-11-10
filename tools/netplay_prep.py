"""main.cpp recipe for online play (NETPLAY-PLAN.md R2, R13).

Applies race_timing.extend_main (120 FPS host pacing) first, then folds the
Road Rash build fingerprint and the session's timing settings into the
framework's netplay content gate. The framework fills
net_cfg.content_fingerprint (mod plan, guest_cycle_scale) just before
psx_netplay_start; rr_netplay_fold_fingerprint (source/netplay_session.cpp)
replaces it with SHA-256 over that value, our build fingerprint and the
settings, so PCs with different builds or timing never start a session.
The framework itself stays unmodified; with PSX_NETPLAY off this code is never
reached.
"""
import race_timing

ANCHOR = "        const int nrc = psx_netplay_start(&net_cfg);\n"

def _sub(src, old, new):
    if src.count(old) != 1:
        raise RuntimeError(f'netplay_prep: expected 1 match for {old[:60]!r}, found {src.count(old)}')
    return src.replace(old, new)

def extend_main(src):
    src = race_timing.extend_main(src)
    src = 'extern "C" void rr_netplay_fold_fingerprint(char*);   /* netplay_prep.py */\n' + src
    return _sub(src, ANCHOR,
                "        rr_netplay_fold_fingerprint(net_cfg.content_fingerprint);   /* Road Rash build + session settings */\n" + ANCHOR)

if __name__ == '__main__':
    import pathlib, sys
    source = pathlib.Path(sys.argv[1])
    pathlib.Path(sys.argv[2]).write_text(extend_main(source.read_text(encoding='latin-1')), encoding='latin-1')
