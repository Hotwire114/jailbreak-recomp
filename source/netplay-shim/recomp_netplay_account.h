/* Netplay build shim (work/claude/build_rr.py --netplay). LGPL-2.1-or-later.
 * Original author code.
 *
 * The framework's main.cpp includes this header from recomp-ui (its shared
 * Dear ImGui launcher, not used by this title) when built with recomp-net: it
 * points the lobby account client at the lobby server so players can sign in.
 * LAN / direct-IP sessions (stage 1, the launcher's Online page) need no
 * account, so the sync does nothing here. Internet play through the public
 * lobby will need the real one (recomp-ui) or an equivalent. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*recomp_netplay_path_fn)(void *user, const char *leaf, char *out, size_t cap);
static inline void recomp_netplay_account_sync(const char *lobby_url, recomp_netplay_path_fn exe_dir_path, void *user)
{
    (void)lobby_url; (void)exe_dir_path; (void)user;
}
#ifdef __cplusplus
}
#endif
