#pragma once
#include <cstdint>
// Online two-player (delay-sync lockstep), PC side. netplay_session.cpp.
namespace rrpc {
// The PC layer's netplay flag: PSX_NETPLAY=1 at start-up (set by the launcher's
// Online page) or the framework's session running; RR_NETPLAY_TEST=1 for tests.
bool netplay();
// Session setup both PCs agreed on before the game started (launcher Online
// page handshake, file RR_NET_SESSION). Seat 0 = host (Player 1), 1 = guest.
struct NetSeat {char name[9];bool rename;int voice,paint,tag,outfit,colour;int bike[3];};   // bike: per rank (0 Grunt, 1 Lieutenant, 2 Captain), -1 random
struct NetSession {
    NetSeat seat[2];
    int race,seed;   // the room's vote (race id 1-18) and a shared seed; 0: the players choose in the game
    // The host's settings that change game state.
    int motion,aspect,lod_percent,draw,fast_loading,larger_text;
};
const NetSession* net_session();   // null offline; online without a file: neutral (everything stock)
// The game's VBlank counter (gp+0x7E0 = 0x8005B46C, 60/s also in 120 FPS races).
uint32_t guest_vblanks();
// Milliseconds for the PC layer's short windows: the wall clock offline, the
// guest VBlank counter (x 1000/60) online, so both PCs decide alike.
uint64_t pc_ms();
// From __wrap_gpu_vblank_tick (native_ui.cpp): guest-defined point for the PC
// layer's game-state writes while online.
void net_guest_tick();
// pc_services.cpp / rider_enhancements.cpp parts that run from net_guest_tick.
void pc_guest_start();
void pc_guest_tick();
void look_guest_tick();
}
// main.cpp recipe (tools/netplay_prep.py): folds this build's fingerprint and the
// session's timing settings into the framework's content gate (64 hex).
extern "C" void rr_netplay_fold_fingerprint(char* fingerprint);
