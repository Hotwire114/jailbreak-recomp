"""psx_cycles.c recipe: native 60 fps timing support for Road Rash: Jailbreak.

1. Fast-timing state, race gate and PC-option setter used by the generated
   code compiled with fast_timing.py (see FAST_TIMING_SUPPORT below). The gate
   also pauses fast timing while the VBlank IRQ is masked (rr_fast_note_imask)
   and while the game's background list waits for the VBlank callback
   (rr_fast_note_bg; RR_BG_GUARD=0 turns that off for A/B runs).
2. Idle-loop elision that works when instructions cost a fraction of a cycle,
   and chains skips across unobservable device events in races.
3. RR_PERF=0 turns off the 0.15 pile-up work (lean paths, chained skips, the
   I_MASK pause) for A/B measurements; fast timing itself stays on.
4. Frame rate 120 FPS (PC option, or RR_FPS=120): in races the emulated VBlank
   runs at 120 Hz and the VBlank handler keeps the game's 60 Hz bookkeeping
   (see FPS120_SUPPORT); extend_main() is the main.cpp recipe that paces the
   host at the guest's VBlank rate.

With [timing] guest_cycle_scale = N each instruction charges 1/N cycle and the
remainder is carried in g_psx_gcs_frac (Q16). A poll loop then advances the
integer cycle counter by 0 or 1 per iteration, so the detector's "stable,
non-zero quantum" test never held and the loop burnt host CPU 64x faster than
real time while waiting for VBlank. Measure the loop quantum on a Q16 clock
(integer cycles << 16 | carried fraction) instead. At scale 1 the fraction is
always zero, so behaviour there is unchanged.
"""

def _sub(src, old, new, count=1):
    if src.count(old) != count:
        raise RuntimeError(f'race_timing: expected {count} match(es) for {old[:60]!r}, found {src.count(old)}')
    return src.replace(old, new)

FAST_TIMING_SUPPORT = r'''
/* ---- Road Rash fast timing support (RoadRashJailbreak/tools/race_timing.py) ----
 * Generated game code compiled with RR_FAST_TIMING charges one guest cycle per
 * g_rr_fast_div instructions (fast_timing.py). RR_FAST_DIV overrides it. */
#include <stdlib.h>
#include <string.h>
#define RR_BG_STATE 0x0005B588u   /* background-list state word (rr_fast_note_bg) */
int      g_rr_fast_on = 0;
uint32_t g_rr_fast_raw = 0u;
uint32_t g_rr_fast_div = 8u;
/* Lean emulation paths while fast timing is on (inline block edges, loads and
 * stores, chained idle skips): fast_timing.py, interrupt_edges.py,
 * memory_fast.py. RR_PERF=0 keeps fast timing but takes the stock paths, for
 * A/B measurements. */
int      g_rr_perf = 1;
int      g_rr_fast_lean = 0;
/* Guard windows (rr_fast_guard: VBlank IRQ masked, background list queued, panorama
 * decode wait) need guest time to pass at least at PS1 speed, not the PS1's CPU
 * internals. So fast timing stays on there (lean paths, no I-cache, load-delay or
 * stall model) and each block charges g_rr_fast_mul times its stock cycle count
 * instead of 1/g_rr_fast_div of it. The stock model there cost ~10% of a 16-rider
 * race frame (PROGRESS-0.15-speed.md). RR_GUARD_STOCK=1: the old stock-timing
 * guard, for A/B runs. */
uint32_t g_rr_fast_mul = 1u;
static int rr_guard_stock = 0;
static void rr_fast_set_on(int on) {
    g_rr_fast_on = on; g_rr_fast_lean = on && g_rr_perf; g_rr_fast_raw = 0u;
}
static int rr_fast_mode = 1;      /* 0 off, 1 races only (default), 2 always */
static int rr_fast_mode_env = 0;  /* RR_FAST_MODE given: the PC option cannot change it */
static uint32_t rr_fast_race_vblanks = 0u;
static int rr_fast_masked = 0;    /* VBlank IRQ masked in I_MASK (rr_fast_note_imask) */
static int rr_fast_bg = 0;        /* background list queued for the VBlank callback (rr_fast_note_bg) */
static int rr_fast_bg_guard = 1;  /* RR_BG_GUARD=0 turns the guard off (A/B only) */
static int rr_fast_wait = 0;      /* sky panorama decode wait (rr_fast_note_wait) */
extern uint32_t i_mask;
/* Inline block-edge state (fast_timing.py FAST_EDGE): last edge PC and the
 * store/MMIO counts seen there; every 256th edge takes the full check. */
uint32_t g_rr_edge_pc = 0u, g_rr_edge_budget = 256u;
uint64_t g_rr_edge_stores = 0u, g_rr_edge_mmio = 0u;
/* Scratchpad backing for the inline fast loads (fast_timing.py). */
extern uint8_t *memory_get_scratchpad_ptr(void);
uint8_t *g_rr_scratch = NULL;
static void rr_fps120_init(void);
__attribute__((constructor)) static void rr_fast_timing_init(void) {
    g_rr_scratch = memory_get_scratchpad_ptr();   /* static array: valid before main */
    const char *e = getenv("RR_FAST_DIV");
    if (e && atoi(e) >= 1 && atoi(e) <= 4096) g_rr_fast_div = (uint32_t)atoi(e);
    const char *m = getenv("RR_FAST_MODE");
    if (m) { rr_fast_mode = !strcmp(m, "off") ? 0 : !strcmp(m, "always") ? 2 : 1; rr_fast_mode_env = 1; }
    const char *p = getenv("RR_PERF");
    if (p && p[0] == '0') g_rr_perf = 0;
    const char *b = getenv("RR_BG_GUARD");
    if (b && b[0] == '0') rr_fast_bg_guard = 0;
    const char *gs = getenv("RR_GUARD_STOCK");
    if (gs && gs[0] == '1') rr_guard_stock = 1;
    rr_fast_set_on(rr_fast_mode == 2);
    rr_fps120_init();
}
/* PC option (Display > Frame rate): 0 original timing, 1 native 60 in races,
 * 2 always. An RR_FAST_MODE override from the environment wins. */
void rr_fast_set_mode(int mode) {
    if (rr_fast_mode_env) return;
    rr_fast_mode = mode < 0 ? 0 : mode > 2 ? 2 : mode;
    if (rr_fast_mode != 1) rr_fast_set_on(rr_fast_mode == 2);
}
int rr_fast_get_mode(void) { return rr_fast_mode; }
int rr_fast_active(void) { return g_rr_fast_on; }
/* ---- 120 FPS races (Frame rate: 120 FPS; RR_FPS=120 / RR_FPS=60 override) ----
 * While a race runs with fast timing (race gate below, after ten racing
 * VBlanks) the emulated VBlank period is halved and the main.cpp recipe
 * (extend_main) paces the host at 119.88 Hz, so CPU, CD, SPU, timers and DMA
 * keep real time and the game's own loop (RequestSwap 0x8001C3F4, swap in its
 * VBlank handler 0x8001B700) shows a new real frame every 1/120 s. Nothing is
 * interpolated. The handler (a VSync callback) is adjusted at watched PCs
 * (fast_timing.py) so all it counts keeps its 60 Hz meaning:
 *  - game clock ctx+0x0C (+5 per 60 Hz VBlank, 300 ticks/s): +3/+2 alternately,
 *    i.e. exactly 300 ticks/s; rr_carry holds the half tick it lags;
 *  - VBlank counter gp+0x7E0 (0x8005B46C; one-second "now - t < 61" stamps),
 *    pad-read counter ctx+0x64 (pad poll 0x8001C5F8 on odd values, 30 Hz) and
 *    the volume fades in 0x80019990 (gated by bit 2 of 0x8005ACA8) advance on
 *    every second ("full") VBlank only; on the other ("half") VBlanks the
 *    handler swaps a ready frame and flushes queued SPU commands (0x8001EE94).
 *    The per-frame 0x8001C428 zeroes ctx+0x64 every 60 frames; its parity is
 *    kept there, so the pad poll stays at 30 Hz.
 * Physics: the race update 0x80012524 integrates every rider (0x800881B4) and
 * the world (0x8008AB00) with s2 = 218 * dt, dt in 16.16 seconds (65536 / 300).
 * With dt alternating 3/2 ticks riders would step 60/40%; s2 becomes the exact
 * 109 * (half ticks since the last frame), 545 per 1/120 s. Integer-tick users
 * (animation timers, race time ctx+0x10) see 3/2 alternately: the same total,
 * at most half a tick (1.7 ms) early on alternate frames.
 * RR_FPS_TRACE=<file> logs one line per RequestSwap (host ns, guest cycles,
 * clock, carry, s2, rider 0 x/z, divisor). */
#include <stdio.h>
#include "psx_video_timing.h"
extern CPUState *debug_cpu_ptr;
extern uint64_t SDL_GetTicksNS(void);
uint32_t g_rr_vblank_div = 1u;      /* guest VBlanks per stock VBlank (main.cpp paces by it) */
int g_rr_pace_ack = 0;              /* set every VBlank by the main.cpp recipe: host pacing follows the divisor */
static int rr_fps120 = 0;           /* Frame rate: 120 FPS */
static int rr_fps120_env = 0;       /* RR_FPS given: the PC option cannot change it */
static uint32_t rr_hz2_calls;       /* handler calls at 120 Hz: even full, odd half */
static int rr_half;                 /* the running handler call is a half VBlank */
static uint32_t rr_carry;           /* half ticks the integer clock lags real time (0/1) */
static int rr_fade_held, rr_pad_held;
static uint32_t rr_fade_word, rr_pad_word, rr_pad_addr, rr_pad_reset_word;   /* rr_pad_addr: where the pad word was held */
static uint32_t rr_capped_races;   /* races in a row that ended held at 60 Hz (rr_hz2_admit) */
static uint64_t rr_t2_frame, rr_t2_prev; /* clock in half ticks at the last two race frames */
static int rr_t2_fresh, rr_t2_count;
static uint32_t rr_last_s2;
static int rr_exact = 1;
static int rr_fallback_on = 1;   /* host fallback to 60 Hz VBlanks (rr_hz2_admit) */
static FILE *rr_fps_trace;
static uint32_t rr_rd32(uint32_t a) { uint32_t v; memcpy(&v, g_psx_ram + (a & g_psx_ram_mask), 4); return v; }
static void rr_wr32(uint32_t a, uint32_t v) { memcpy(g_psx_ram + (a & g_psx_ram_mask), &v, 4); }
static void rr_fps120_init(void) {
    const char *f = getenv("RR_FPS");
    if (f) { rr_fps120 = atoi(f) >= 120; rr_fps120_env = 1; }
    const char *s = getenv("RR_FPS_STEP");
    if (s && !strcmp(s, "tick")) rr_exact = 0;
    const char *fb = getenv("RR_FPS_FALLBACK");
    if (fb && fb[0] == '0') rr_fallback_on = 0;
    const char *t = getenv("RR_FPS_TRACE");
    if (t && t[0]) rr_fps_trace = fopen(t, "w");
}
/* PC option (Display > Frame rate 120 FPS). Takes effect at the next VBlank. */
void rr_fps120_set(int on) { if (!rr_fps120_env) { if (on && !rr_fps120) rr_capped_races = 0u; rr_fps120 = on != 0; } }
int rr_fps120_get(void) { return rr_fps120; }
/* The gate runs at each VBlank edge before the guest takes that edge's IRQ, so
 * the handler call belongs to the period that just ended: rr_div_ended. */
static uint32_t rr_div_ended = 1u;
static void rr_hz2_set(int on) {
    rr_div_ended = g_rr_vblank_div;
    const uint32_t div = on ? 2u : 1u;
    const uint32_t period = (psx_video_timing_is_pal() ? PSX_VBLANK_CYCLES_PAL : PSX_VBLANK_CYCLES_NTSC) / div;
    if (div == 2u) g_psx_vblank_cycles = period;   /* also re-applies it after a video-standard write */
    if (div == g_rr_vblank_div) return;
    g_rr_vblank_div = div;
    g_psx_vblank_cycles = period;
    rr_hz2_calls = 0u;
}
/* Host fallback. 120 Hz needs the host to emulate a race frame every 1/120 s.
 * When it cannot (a 16-rider pile-up on a busy PC), the frame pacer falls
 * behind and, past its 12-period catch-up window, the game itself slows down.
 * So the wall time of every 48 VBlanks (0.4 s) at 120 Hz is checked: two windows
 * running over 4% with the second 8% over, or three over 4%, returns races to 60 Hz VBlanks (60 FPS:
 * real frames, same game speed) for 2 s, doubling up to 8 s while retries keep
 * failing; 10 s at 120 Hz resets that. A smaller shortfall is left to the frame
 * pacer (as 60 FPS pile-ups dip a few percent): on the owner's i9-12900KF the High
 * end settings run a 16-rider race at ~97% of 120 Hz, and flipping to 60 for
 * seconds at a time looked like lag spikes. RR_FPS_FALLBACK=0 turns it off (A/B).
 * The second fallback of a race keeps it at 60 Hz until that race is over: the
 * owner's 1080p FXAA settings fell short by 8-60% now and then, and retrying 120
 * every few seconds was itself the stutter (14 switches in one session, 23 Oct).
 * Two races in a row that each ended held at 60 Hz keep races at 60 Hz for the
 * rest of the session (turning 120 FPS off and on again retries): a PC that
 * cannot hold 120 on its settings otherwise stumbled twice at the start of every
 * race (22 fallbacks in one owner session, 10 Nov). */
extern uint64_t SDL_GetPerformanceCounter(void);
extern uint64_t SDL_GetPerformanceFrequency(void);
static uint64_t rr_lag_t0, rr_hold_until, rr_hz2_began;
static uint32_t rr_lag_n, rr_lag_slow, rr_backoff_s, rr_fallbacks, rr_race_fallbacks;
static int rr_race_on;   /* a race is running (admit asked with want) */
static int rr_hz2_admit(int want) {
    if (!want) {   /* not racing: the next race tries 120 again, unless the last two could not hold it */
        if (rr_race_on) {
            rr_race_on = 0;
            rr_capped_races = rr_race_fallbacks >= 2u ? rr_capped_races + 1u : 0u;
            if (rr_capped_races == 2u) {
                printf("rr: 120 FPS races: two races in a row could not hold 120 FPS, 60 FPS for this session "
                       "(a lower render resolution or anti-aliasing helps)\n");
                fflush(stdout);
            }
        }
        rr_lag_n = 0; rr_race_fallbacks = 0; return 0;
    }
    rr_race_on = 1;
    if (rr_race_fallbacks >= 2u || rr_capped_races >= 2u) return 0;
    const uint64_t now = SDL_GetPerformanceCounter(), f = SDL_GetPerformanceFrequency();
    if (now < rr_hold_until) return 0;
    if (g_rr_vblank_div != 2u) { rr_lag_n = 0; rr_lag_slow = 0; rr_hz2_began = now; return 1; }
    if (!rr_fallback_on) return 1;
    if (++rr_lag_n == 6u) { rr_lag_t0 = now; return 1; }   /* first window after the switch */
    if (rr_lag_n < 54u) return 1;
    const double nominal = 48.0 * (double)f * (psx_video_timing_is_pal() ? 0.02 : 1.0 / 59.94) / 2.0;
    const double r = (double)(now - rr_lag_t0) / nominal;
    rr_lag_n = 6u; rr_lag_t0 = now;
    /* One window over (a single stall: a 40 ms frame is 10% of a window) is not
     * the host falling behind: only a second window in a row over 4%, or three
     * over 4% with none back at speed between, returns races to 60 Hz. */
    if (r <= 1.04) { rr_lag_slow = 0; return 1; }
    if (++rr_lag_slow < (r > 1.08 ? 2u : 3u)) return 1;
    rr_lag_slow = 0;
    if (now - rr_hz2_began > 10u * f) rr_backoff_s = 0;
    rr_backoff_s = rr_backoff_s ? (rr_backoff_s >= 4u ? 8u : rr_backoff_s * 2u) : 2u;
    rr_hold_until = now + (uint64_t)rr_backoff_s * f;
    rr_fallbacks++;
    if (++rr_race_fallbacks >= 2u)
        printf("rr: 120 FPS race: host %.0f%% behind, 60 Hz VBlank for the rest of this race (fallback %u)\n",
               (r - 1.0) * 100.0, rr_fallbacks);
    else
        printf("rr: 120 FPS race: host %.0f%% behind, 60 Hz VBlank for %u s (fallback %u)\n",
               (r - 1.0) * 100.0, rr_backoff_s, rr_fallbacks);
    fflush(stdout);
    return 0;
}
/* Put back what a half VBlank changed, should a hook have been missed. */
static void rr_hz2_release(uint32_t ctx) {
    if (rr_fade_held) { rr_wr32(0x8005ACA8u, (rr_rd32(0x8005ACA8u) & ~4u) | (rr_fade_word & 4u)); rr_fade_held = 0; }
    (void)ctx;
    if (rr_pad_held) { rr_wr32(rr_pad_addr, rr_pad_word); rr_pad_held = 0; }   /* where it was held, whatever ctx is now */
    rr_half = 0;
}
/* Exact physics step for this frame, in register `reg` (holding 218 * dt).
 * RR_FPS_STEP=tick keeps the game's whole-tick step (A/B only). */
static void rr_hz2_step(CPUState *c, int reg, uint32_t ctx) {
    const uint32_t dt = rr_rd32(ctx + 0x1Cu);   /* the game's dt, clamped to 30 */
    if (rr_exact && rr_t2_fresh && rr_t2_count >= 2 && dt && c->gpr[reg] == 218u * dt) {
        const uint64_t half = rr_t2_frame - rr_t2_prev;
        if (half + 1u >= 2ull * dt && half <= 2ull * dt + 1u)   /* not clamped, not a fixed step */
            c->gpr[reg] = 109u * (uint32_t)half;
    }
    rr_last_s2 = c->gpr[reg];
}
static void rr_hz2_observe(uint32_t pc) {
    CPUState *c = debug_cpu_ptr;
    if (!c || !g_psx_ram) return;
    const uint32_t ctx = rr_rd32(0x8005B2F8u);
    if (ctx < 0x80010000u || ctx >= 0x801FFF00u) return;
    switch (pc) {
    case 0x8001B700u: {   /* handler entry; it then adds 5 to ctx+0x0C, 1 to gp+0x7E0 and ctx+0x64 */
        rr_hz2_release(ctx);
        if (rr_div_ended != 2u) return;
        rr_half = (rr_hz2_calls++ & 1u) != 0u;
        const uint32_t total = rr_carry + 5u, inc = total >> 1;
        rr_carry = total & 1u;
        rr_wr32(ctx + 0x0Cu, rr_rd32(ctx + 0x0Cu) - (5u - inc));
        if (rr_half) {
            rr_wr32(0x8005B46Cu, rr_rd32(0x8005B46Cu) - 1u);
            rr_wr32(ctx + 0x64u, rr_rd32(ctx + 0x64u) - 1u);
            rr_wr32(0x80055ECCu, rr_rd32(0x80055ECCu) - 1u);   /* libetc Vcount (VSync(-1) timeouts), raised before this callback */
        }
        return; }
    case 0x80019990u:     /* volume fades, called from 0x8001B810 */
        if (rr_half && c->gpr[31] == 0x8001B818u && !rr_fade_held) {
            rr_fade_word = rr_rd32(0x8005ACA8u); rr_fade_held = 1;
            rr_wr32(0x8005ACA8u, rr_fade_word & ~4u);
        }
        return;
    case 0x8001B818u:     /* back from the fades; the pad poll follows when ctx+0x64 is odd */
        if (rr_fade_held) { rr_wr32(0x8005ACA8u, (rr_rd32(0x8005ACA8u) & ~4u) | (rr_fade_word & 4u)); rr_fade_held = 0; }
        if (rr_half && !rr_pad_held) {
            rr_pad_addr = ctx + 0x64u; rr_pad_word = rr_rd32(rr_pad_addr); rr_pad_held = 1;
            rr_wr32(rr_pad_addr, rr_pad_word & ~1u);
        }
        return;
    case 0x8001B840u:
        rr_hz2_release(ctx);
        return;
    case 0x8001C44Cu:     /* 0x8001C428 (per game frame) zeroes ctx+0x64 every 60 frames: twice a second at 120 FPS */
        rr_pad_reset_word = rr_rd32(ctx + 0x64u) | 0x80000000u;
        return;
    case 0x8001C454u:     /* after that reset: keep the pad poll's 30 Hz parity */
        if (rr_pad_reset_word && rr_div_ended == 2u && rr_rd32(ctx + 0x64u) == 0u) rr_wr32(ctx + 0x64u, rr_pad_reset_word & 1u);
        rr_pad_reset_word = 0u;
        return;
    case 0x8001CD5Cu:     /* race frame start: the game takes dt = clock - ctx+0x14 */
        rr_t2_prev = rr_t2_frame;
        rr_t2_frame = 2ull * rr_rd32(ctx + 0x0Cu) + rr_carry;
        rr_t2_fresh = 1;
        if (rr_t2_count < 2) rr_t2_count++;
        return;
    case 0x8008AB00u:     /* world update (race overlay), a0 = s2, from 0x8001258C */
        if (c->gpr[31] == 0x80012594u) rr_hz2_step(c, 4, ctx);
        return;
    case 0x80012594u:     /* rider loop: s2 */
        rr_hz2_step(c, 18, ctx);
        rr_t2_fresh = 0;
        return;
    case 0x8001C3F4u:     /* RequestSwap */
        if (rr_fps_trace) {
            const uint32_t r = 0x800CD898u;
            fprintf(rr_fps_trace, "%llu,%llu,%u,%u,%u,%d,%d,%u\n", (unsigned long long)SDL_GetTicksNS(),
                    (unsigned long long)psx_cycle_count, rr_rd32(ctx + 0x0Cu), rr_carry, rr_last_s2,
                    (int)rr_rd32(r + 0xB8u), (int)rr_rd32(r + 0xC0u), g_rr_vblank_div);
            rr_last_s2 = 0u;
        }
        return;
    }
}
/* Road Rash: Jailbreak race gate. The game's state block pointer at
 * 0x8005B2F8 holds 3 through the frontend and CD loading and 1 once a race is
 * running. CD/overlay loading under fast timing breaks the track load, so fast
 * timing waits for ten consecutive racing VBlanks and drops out at once. */
/* 60 Hz frontend (native_ui.cpp rr_menu_fast_ok): the menu loop of the frontend
 * overlay, in its steady state (state 2), with Frame rate: 60. Each menu pass
 * then fits in one VBlank; picture streams keep their stock pace (native_ui.cpp
 * picture_pace). Loading (state 3) and races keep their own rules. */
extern int rr_menu_fast_ok(void);
static uint32_t rr_fast_menu_vblanks = 0u;
static int rr_fast_guard(void) { return rr_fast_masked || rr_fast_bg || rr_fast_wait; }
static int rr_fast_want(void) {
    g_rr_fast_mul = rr_fast_guard() && !rr_guard_stock ? 2u : 1u;
    return (rr_fast_race_vblanks >= 10u || rr_fast_menu_vblanks >= 10u) && (!rr_guard_stock || !rr_fast_guard());
}
static void rr_fast_gate_vblank(void) {
    if (rr_fast_mode != 1) { rr_hz2_set(0); return; }
    int racing = 0;
    if (g_psx_ram) {
        uint32_t ctx;
        memcpy(&ctx, g_psx_ram + (0x8005B2F8u & g_psx_ram_mask), 4);
        if (ctx >= 0x80010000u && ctx < 0x80200000u)
            racing = g_psx_ram[ctx & g_psx_ram_mask] == 1;
    }
    rr_fast_race_vblanks = racing ? rr_fast_race_vblanks + 1u : 0u;
    if (!racing) rr_fast_wait = 0;
    /* Without the main.cpp recipe the host would keep 59.94 Hz pacing and a 120 Hz race would run at half speed. */
    rr_hz2_set(rr_hz2_admit(rr_fps120 && g_rr_pace_ack && rr_fast_race_vblanks >= 10u));   /* 120 Hz VBlank: races only */
    rr_fast_menu_vblanks = !racing && rr_menu_fast_ok() ? rr_fast_menu_vblanks + 1u : 0u;
    rr_fast_masked = g_rr_perf && (i_mask & 1u) == 0u;   /* also after savestate loads */
    if (g_psx_ram) {   /* resync the background-list guard (savestate loads) */
        uint32_t s; memcpy(&s, g_psx_ram + (RR_BG_STATE & g_psx_ram_mask), 4);
        rr_fast_bg = rr_fast_bg_guard && s == 1u;
    }
    int on = rr_fast_want();
    if (on != g_rr_fast_on) rr_fast_set_on(on);
}
/* Background-list guard. At the end of a frame (0x800C8B24) the game builds the
 * next frame's sky/background list (OT at 0x800D9BE4..F0) from its primitive
 * ring and, while the previous swap is still pending, sets 0x8005B588 = 1 so the
 * VBlank callback (0x8001B808) kicks it after the swap (then 2). Those prims sit
 * just below the next render pass's ring segment, protected by a 60-byte margin
 * only (0x80021988). At PS1 speed the VBlank always comes long before a pass
 * could wrap the ~120 KB ring (allocator 0x80021C98). Under fast timing a pass
 * that overflows the ring (a 16-rider pile-up in view; the game's own "Patch
 * polygons exceeded maximum" path) ran before that VBlank and overwrote the
 * queued list: GP0 unknown-command fatals, or a cyclic walk through kernel RAM
 * that left GP0 mid-packet and stalled libgpu's next StoreImage for seconds.
 * So stock CPU timing applies while the list is queued, i.e. from the end of a
 * frame to the next VBlank. memory.c (memory_fast.py) reports every guest store
 * to the state word. */
void rr_fast_note_bg(uint32_t state) {
    int bg = rr_fast_bg_guard && state == 1u;
    if (bg == rr_fast_bg) return;
    rr_fast_bg = bg;
    if (rr_fast_mode == 1) {
        int on = rr_fast_want();
        if (on != g_rr_fast_on) rr_fast_set_on(on);
    }
}
/* VBlank IRQ masked (libetc SetIntrMask(0)): libgpu runs a LoadImage/StoreImage
 * synchronously there and busy-polls GPUSTAT until the emulated GPU is ready,
 * with the game's VBlank handler shut out. CPU speed cannot be observed in that
 * wait, but under fast timing each poll covers 1/8 of the guest time, so a long
 * GPU wait (a whole pack overlapped at the camera: ~40 VBlanks) stretched to
 * ~2.5x real time. Stock CPU timing applies while it is masked; memory.c
 * (memory_fast.py) reports every guest I_MASK write here. */
void rr_fast_note_imask(uint32_t mask) {
    int masked = g_rr_perf && (mask & 1u) == 0u;
    if (masked == rr_fast_masked) return;
    rr_fast_masked = masked;
    if (rr_fast_mode == 1) {
        int on = rr_fast_want();
        if (on != g_rr_fast_on) rr_fast_set_on(on);
    }
}
/* Sky panorama decode wait (native_ui.cpp pano_wait). The one-player horizon
 * builder (race overlay 0x800644F4) busy-waits for a pending MDEC decode of the
 * next panorama columns and gives up after 10000 loop iterations, leaving the
 * tile ring half filled (wrong horizon tiles). That bound is counted in
 * instructions, so it expired before the decode under fast timing. Stock CPU
 * timing applies for the length of the wait. */
void rr_fast_note_wait(int waiting) {
    waiting = waiting != 0;
    if (waiting == rr_fast_wait) return;
    rr_fast_wait = waiting;
    if (rr_fast_mode == 1) {
        int on = rr_fast_want();
        if (on != g_rr_fast_on) rr_fast_set_on(on);
    }
}
/* Per-block fast charge: generated block leaders add their cycle count to
 * g_rr_fast_raw; publish whole guest cycles here. */
void rr_fast_flush(void) {
    if (g_rr_fast_mul != 1u) { uint32_t c = g_rr_fast_raw * g_rr_fast_mul; g_rr_fast_raw = 0u; psx_cyc_charge(c); return; }   /* guard window */
    uint32_t q = g_rr_fast_raw / g_rr_fast_div;
    g_rr_fast_raw -= q * g_rr_fast_div;
    if (q) psx_cyc_charge(q);
}
/* Watched-PC bridge for the fast profile's filtered per-block hook. Calling
 * through this TU keeps the link-time --wrap of debug_server_cyc_observe. */
extern void debug_server_cyc_observe(uint32_t pc);
void rr_cyc_observe_watch(uint32_t pc) {
    if (rr_fps120 || rr_fps_trace || g_rr_vblank_div != 1u || rr_div_ended != 1u) rr_hz2_observe(pc);   /* 120 FPS races (above) */
    debug_server_cyc_observe(pc);
}
/* Sub-cycle part of the idle detector's Q16 clock. */
static inline uint64_t rr_idle_frac(void) {
    uint64_t f = (uint64_t)(g_psx_gcs_frac & 0xFFFFu);
    if (g_rr_fast_mul != 1u) f += ((uint64_t)g_rr_fast_raw * g_rr_fast_mul) << 16;
    else if (g_rr_fast_div > 1u) f += ((uint64_t)g_rr_fast_raw << 16) / g_rr_fast_div;
    return f;
}

/* ===== Idle-loop cycle skip'''

def extend(src):
    src = _sub(src, "\n/* ===== Idle-loop cycle skip", FAST_TIMING_SUPPORT)
    # Gate evaluation on every VBlank edge (interrupts.c calls this function).
    src = _sub(src,
        "void psx_guest_cycle_scale_vblank(void) {\n",
        "static void rr_fast_gate_vblank(void);\n"
        "void psx_guest_cycle_scale_vblank(void) {\n"
        "    rr_fast_gate_vblank();\n")
    src = _sub(src,
        "    uint64_t cyc = psx_cycle_count;\n    if (check_pc == s_idle_pc && s_idle_have_snap &&",
        "    /* Q16 guest clock: whole cycles plus the scale/fast-timing carry. */\n"
        "    uint64_t cyc = (psx_cycle_count << 16) + rr_idle_frac();\n"
        "    if (check_pc == s_idle_pc && s_idle_have_snap &&")
    src = _sub(src,
        "        uint32_t quantum = (uint32_t)(cyc - s_idle_last_cycle);\n"
        "        if (progress_reg != -2 && quantum > 0 && quantum <= IDLE_QUANTUM_MAX) {",
        "        uint64_t quantum_q16 = cyc - s_idle_last_cycle;\n"
        "        uint32_t quantum = quantum_q16 > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)quantum_q16;\n"
        "        if (progress_reg != -2 && quantum > 0 && quantum <= ((uint32_t)IDLE_QUANTUM_MAX << 16)) {")
    src = _sub(src,
        "    uint32_t q = s_idle_quantum;\n"
        "    uint32_t dist = devices_cycles_to_next_idle_event();\n"
        "    if (dist <= q) return;               /* one real iteration reaches it */\n"
        "    uint32_t k = (dist + q - 1u) / q;    /* first check boundary >= event */",
        "    uint32_t q = s_idle_quantum;         /* Q16 cycles per iteration */\n"
        "    uint32_t dist = devices_cycles_to_next_idle_event();\n"
        "    uint64_t dist_q16 = (uint64_t)dist << 16;\n"
        "    if (dist_q16 <= q) return;           /* one real iteration reaches it */\n"
        "    uint32_t k = (uint32_t)((dist_q16 + q - 1u) / q); /* first check boundary >= event */")
    src = _sub(src,
        "    uint64_t skip = (uint64_t)k * q;\n"
        "    if (skip > IDLE_SKIP_MAX_CYCLES) return;",
        "    uint64_t skip = ((uint64_t)k * q + 0xFFFFu) >> 16;\n"
        "    if (skip > IDLE_SKIP_MAX_CYCLES) return;")
    src = _sub(src,
        "    g_idle_skip_last_quantum = q;",
        "    g_idle_skip_last_quantum = q >> 16;")
    # Chain skips in race fast timing. Road Rash unmasks the SPU IRQ, so every
    # 44.1 kHz sample (768 cycles) is an observable boundary: a VBlank wait was
    # skipped ~460 times a frame, each time re-detected over several real loop
    # iterations at full interrupt-check cost. The loop has no stores, no MMIO
    # and fixed registers, so after a skip that raised nothing deliverable (and
    # no device stored to RAM) skipping on to the next boundary lands on the
    # same iteration boundary the real loop would reach. Stops at the first
    # deliverable IRQ (VBlank bounds every skip), store/MMIO or progress limit.
    src = _sub(src,
        "    uint32_t q = s_idle_quantum;         /* Q16 cycles per iteration */\n"
        "    uint32_t dist = devices_cycles_to_next_idle_event();\n",
        "    uint32_t q = s_idle_quantum;         /* Q16 cycles per iteration */\n"
        "    extern uint32_t i_stat, i_mask;\n"
        "    const uint64_t rr_chain_stores = g_guest_store_count, rr_chain_mmio = g_mmio_access_count;\n"
        "    for (uint32_t rr_chain = 0u;; rr_chain++) {\n"
        "    uint32_t dist = devices_cycles_to_next_idle_event();\n")
    # Inside the chain a later link that cannot skip ends the chain (the
    # post-skip re-arm below still runs); only the first link returns.
    for cond in ("if (dist_q16 <= q) return;", "if (k == 0) return;", "if (skip > IDLE_SKIP_MAX_CYCLES) return;"):
        src = _sub(src, cond, cond.replace("return;", "{ if (rr_chain) break; return; }"))
    src = _sub(src,
        "    psx_advance_cycles((uint32_t)skip);  /* replays device events exactly */\n",
        "    psx_advance_cycles((uint32_t)skip);  /* replays device events exactly */\n"
        "    if (!g_rr_fast_lean || rr_chain >= 4096u || (i_stat & i_mask) != 0u ||\n"
        "        g_guest_store_count != rr_chain_stores || g_mmio_access_count != rr_chain_mmio)\n"
        "        break;\n"
        "    }\n")
    src = _sub(src,
        "    s_idle_last_cycle  = psx_cycle_count;\n"
        "    s_idle_last_stores = g_guest_store_count;\n"
        "    s_idle_last_mmio   = g_mmio_access_count;\n"
        "#endif\n}",
        "    s_idle_last_cycle  = (psx_cycle_count << 16) + rr_idle_frac();\n"
        "    s_idle_last_stores = g_guest_store_count;\n"
        "    s_idle_last_mmio   = g_mmio_access_count;\n"
        "#endif\n}")
    return src

def extend_main(src):
    """main.cpp recipe: host pacing follows g_rr_vblank_div (120 FPS races).

    Host pacing already follows the guest's video standard: once per guest
    VBlank (sdl_vblank_present) sync_guest_cadence_to_video_standard() sets the
    wall-clock period and re-evaluates the host-refresh vsync match. The same
    path now also divides the period by the guest VBlanks per stock VBlank, so a
    120 Hz race VBlank is paced at 119.88 Hz (a 120 Hz panel can take driver
    vsync, as a 60 Hz panel does at 60) and 60 Hz returns exactly to 59.94."""
    src = _sub(src,
        "static void sync_guest_cadence_to_video_standard(void) {\n"
        "    const uint32_t gen = psx_video_timing_generation();\n"
        "    if (gen == g_video_timing_generation_seen)\n"
        "        return;\n"
        "    g_video_timing_generation_seen = gen;\n",
        "extern \"C\" uint32_t g_rr_vblank_div;   /* race_timing.py: 2 in 120 FPS races */\n"
        "static uint32_t g_rr_vblank_div_seen = 1u;\n"
        "extern \"C\" int g_rr_pace_ack;   /* tells race_timing.py this pacing is present */\n"
        "static void sync_guest_cadence_to_video_standard(void) {\n"
        "    g_rr_pace_ack = 1;\n"
        "    const uint32_t gen = psx_video_timing_generation();\n"
        "    if (gen == g_video_timing_generation_seen && g_rr_vblank_div == g_rr_vblank_div_seen)\n"
        "        return;\n"
        "    g_video_timing_generation_seen = gen;\n"
        "    g_rr_vblank_div_seen = g_rr_vblank_div;\n")
    src = _sub(src,
        "    const double period_ms = psx_video_timing_is_pal()\n"
        "        ? 1000.0 / 50.0\n"
        "        : PSX_FRAME_PERIOD_MS;\n"
        "    if (period_ms == g_guest_frame_period_ms)\n",
        "    const double period_ms = (psx_video_timing_is_pal()\n"
        "        ? 1000.0 / 50.0\n"
        "        : PSX_FRAME_PERIOD_MS) / (double)(g_rr_vblank_div > 1u ? g_rr_vblank_div : 1u);\n"
        "    if (period_ms == g_guest_frame_period_ms)\n")
    # Dynamic resolution only when [video] dynamic_resolution asks for it (PROGRESS-0.15-lag.md).
    # pc_services.cpp turns the backend's dynamic resolution on before the GL context exists, only
    # to reserve surfaces for supersampling chosen in game; dynres_setup then started the adaptive
    # controller whenever that ceiling was >= 2. Debug-tools builds switch it back off at start
    # (their live-scale keys), but a release build kept it on: it stepped the internal resolution
    # between the floor and the ceiling during races, overriding the chosen Render resolution.
    src = _sub(src,
        "    g_dynres.active = floor_s < ceiling;\n",
        "    g_dynres.active = floor_s < ceiling && dynres_requested() != 0;   /* RR: only when asked for */\n")
    return src

if __name__ == '__main__':
    import pathlib, sys
    source = pathlib.Path(sys.argv[1])
    if source.name == 'main.cpp':   # UTF-8 text cp1252 cannot decode: keep its bytes exactly
        pathlib.Path(sys.argv[2]).write_text(extend_main(source.read_text(encoding='latin-1')), encoding='latin-1')
    else:
        pathlib.Path(sys.argv[2]).write_text(extend(source.read_text()))
