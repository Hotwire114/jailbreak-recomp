"""Fast-timing include profile for the *generated game code* only.

The pinned framework charges every recompiled instruction with a Beetle-exact
R3000A model (load-delay interlocks, I-cache fills, mult/div and GTE stalls)
and calls a debug hook at every basic block. That keeps the game at PS1 CPU
speed, which is what held races at ~20 fps. This profile is used only when
compiling generated game/overlay translation units (-DRR_FAST_TIMING=1 with a
patched copy of runtime/include first on the include path).

While g_rr_fast_on is set (race_timing.py psx_cycles.c recipe; on during races):
* each instruction charges 1/g_rr_fast_div guest cycle, so a frame's CPU work
  takes a small fraction of a VBlank and the game's own vblank-paced loop
  (swap at 0x8001B700, wait at 0x8001C408) decides the cadence;
* main-RAM loads read RAM directly; MMIO/scratchpad keep the faithful path;
* I-cache, mult/div and GTE completion stalls are not modelled.
While g_rr_fast_lean is also set (RR_PERF not 0), host cost is cut further:
* block edges skip the full interrupt check when nothing can be delivered
  (FAST_EDGE below; repeats at one PC still feed the idle-loop detector);
* main-RAM and scratchpad loads are inline; psx_cyc_step is not called;
* stores go through memory_fast.py's lean path (interrupt_edges.py).
While it is clear (menus, CD loading) the stock model runs unchanged.
In both modes the per-block debug hook runs only at the PCs the PC adapters
observe, and the PGXP value-tracking hooks run only while Texture correction
is Full (PGXP_GATED below). Devices (VBlank, timers, CD, SPU, DMA) always keep real timing.
Struct layouts are untouched, so these objects link with the normal
debug-tools runtime.
"""
import pathlib, re, shutil

# PCs the independent adapters observe through debug_server_cyc_observe
# (taunt_voices.cpp voice_observe: taunt-block handler 0x8001A0C0, speech request,
# voice allocation; rider_enhancements.cpp/native_ui.cpp text spans,
# native_ui.cpp frontend picture pacing at the picture player 0x800627BC and its
# per-track loop head 0x80062824; picture rectangles at 0x80062AB0/0x80062C00;
# native_ui.cpp widescreen horizon in the race overlay: window update 0x80065174,
# streamed window 0x800656A8, panorama builder 0x800645AC/0x80064834/0x80064B74,
# split-screen sprite row 0x80064ED8; the builder's panorama-decode wait bound
# 0x80064588 and its give-up path 0x800647F0).
# 120 FPS races (race_timing.py rr_hz2_observe): VBlank handler 0x8001B700, its
# fade call 0x80019990 and return points 0x8001B818/0x8001B840, race frame start
# 0x8001CD5C, physics step users 0x8008AB00 (race overlay) and 0x80012594,
# pad-counter reset 0x8001C44C/0x8001C454, RequestSwap 0x8001C3F4 (RR_FPS_TRACE).
# Demo courses (demo_courses.cpp demo_observe): the best-run split reads by race
# id in the race-init overlay 0x80060468/0x80060470 to 0x8006056C and the race
# overlay 0x80063824 to 0x80063888.
# Rider look showroom (showroom.cpp showroom_observe): the race frame start above,
# the race overlay's object draw 0x80067AC4 and its return block 0x80068438 (the
# other riders hidden), view draw 0x800C8B24 (camera),
# its HUD draw 0x800C8CD4.
WATCHED_PCS = [0x8001A0C0, 0x8001AD30, 0x8001F174, 0x8001F2D4, 0x8002CDC8, 0x8002D058, 0x8002D078, 0x800627BC, 0x80062824,
               0x80062AB0, 0x80062C00, 0x80065174, 0x800656A8, 0x800645AC, 0x80064834, 0x80064B74, 0x80064ED8,
               0x80064588, 0x800647F0,
               0x8001B700, 0x80019990, 0x8001B818, 0x8001B840, 0x8001CD5C, 0x8008AB00, 0x80012594, 0x8001C44C,
               0x8001C454, 0x8001C3F4,
               0x80060468, 0x80060470, 0x8006056C, 0x80063824, 0x80063888,
               0x80067AC4, 0x80068438, 0x800C8B24, 0x800C8CD4]

def _sub(src, old, new, name):
    if src.count(old) != 1:
        raise RuntimeError(f'fast_timing/{name}: expected one match for {old[:70]!r}, found {src.count(old)}')
    return src.replace(old, new)

FAST_CORE = r'''
/* ---- RR_FAST_TIMING (RoadRashJailbreak/tools/fast_timing.py) ---- */
extern int      g_rr_fast_on;    /* race gate (psx_cycles.c) */
extern uint32_t g_rr_fast_raw;   /* instructions not yet charged */
extern uint32_t g_rr_fast_div;   /* instructions per guest cycle */
static inline __attribute__((always_inline)) void rr_fast_tick(void) {
    uint32_t r = g_rr_fast_raw + 1u;
    if (r >= g_rr_fast_div) { g_rr_fast_raw = 0u; psx_cyc_charge(1u); }
    else g_rr_fast_raw = r;
}
'''

def patch_psx_cyc(src):
    src = _sub(src,
        "static inline void psx_cyc_step(CPUState* cpu, uint32_t reg_mask) {\n"
        "    psx_cyc_base(cpu);\n",
        FAST_CORE +
        "static inline void psx_cyc_step(CPUState* cpu, uint32_t reg_mask) {\n"
        "#ifdef RR_FAST_TIMING\n"
        "    if (__builtin_expect(g_rr_fast_on, 1)) return;   /* charged per block (psx_slice_block) */\n"
        "#endif\n"
        "    psx_cyc_base(cpu);\n", 'psx_cyc.h')
    for width, ctype in (('word', 'uint32_t'), ('half', 'uint16_t')):
        head = (f"static inline {ctype} psx_cyc_load_{width}(CPUState* cpu, uint32_t addr,\n"
                f"                                          uint32_t rt, uint32_t reg_mask) {{\n"
                f"#ifdef PSX_ENABLE_BLOCK_CYCLES\n"
                f"    uint32_t phys = addr & 0x1FFFFFFFu;\n"
                f"    if (g_ls_mode == 0 && !g_ds_recording && phys < 0x00800000u) {{\n")
        src = _sub(src, head, head +
            "#ifdef RR_FAST_TIMING\n"
            "        if (__builtin_expect(g_rr_fast_on, 1)) {\n"
            f"            {ctype} value;\n"
            "            memcpy(&value, g_psx_ram + (phys & g_psx_ram_mask), sizeof(value));\n"
            "            return value;\n"
            "        }\n"
            "#endif\n", 'psx_cyc.h')
    # Loads: a small always-inline front for main RAM and the scratchpad (the
    # stock helpers are too large to inline, so every load was a call). Other
    # addresses (MMIO, BIOS ROM, mod arenas) keep the faithful charged path.
    src = _sub(src,
        "extern uint8_t psx_cyc_load_byte(CPUState* cpu, uint32_t addr, uint32_t rt, uint32_t reg_mask);\n",
        "extern uint8_t psx_cyc_load_byte(CPUState* cpu, uint32_t addr, uint32_t rt, uint32_t reg_mask);\n"
        "#if defined(RR_FAST_TIMING) && !defined(PSX_OVERLAY_DLL_BUILD)\n"
        "extern uint8_t *g_rr_scratch;   /* scratchpad backing (race_timing.py) */\n"
        "extern int g_rr_fast_lean;      /* fast timing on and RR_PERF not 0 (race_timing.py) */\n"
        "static inline __attribute__((always_inline)) const uint8_t *rr_fast_load_ptr(uint32_t addr, uint32_t width) {\n"
        "    if (!__builtin_expect(g_rr_fast_lean, 1) || g_ls_mode != 0 || g_ds_recording) return 0;\n"
        "    uint32_t phys = addr & 0x1FFFFFFFu;\n"
        "    if (phys < 0x00800000u) return g_psx_ram + (phys & g_psx_ram_mask);\n"
        "    if (phys - 0x1F800000u <= 0x400u - width) return g_rr_scratch + (phys - 0x1F800000u);\n"
        "    return 0;\n"
        "}\n"
        "static inline __attribute__((always_inline)) uint32_t rr_fast_load_word(CPUState* cpu, uint32_t addr, uint32_t rt, uint32_t reg_mask) {\n"
        "    const uint8_t *p = rr_fast_load_ptr(addr, 4u);\n"
        "    if (__builtin_expect(p != 0, 1)) { uint32_t v; memcpy(&v, p, 4); return v; }\n"
        "    return psx_cyc_load_word(cpu, addr, rt, reg_mask);\n"
        "}\n"
        "static inline __attribute__((always_inline)) uint16_t rr_fast_load_half(CPUState* cpu, uint32_t addr, uint32_t rt, uint32_t reg_mask) {\n"
        "    const uint8_t *p = rr_fast_load_ptr(addr, 2u);\n"
        "    if (__builtin_expect(p != 0, 1)) { uint16_t v; memcpy(&v, p, 2); return v; }\n"
        "    return psx_cyc_load_half(cpu, addr, rt, reg_mask);\n"
        "}\n"
        "static inline __attribute__((always_inline)) uint8_t rr_fast_load_byte(CPUState* cpu, uint32_t addr, uint32_t rt, uint32_t reg_mask) {\n"
        "    const uint8_t *p = rr_fast_load_ptr(addr, 1u);\n"
        "    if (__builtin_expect(p != 0, 1)) return *p;\n"
        "    return psx_cyc_load_byte(cpu, addr, rt, reg_mask);\n"
        "}\n"
        "#define psx_cyc_load_word(c, a, r, m) rr_fast_load_word((c), (a), (r), (m))\n"
        "#define psx_cyc_load_half(c, a, r, m) rr_fast_load_half((c), (a), (r), (m))\n"
        "#define psx_cyc_load_byte(c, a, r, m) rr_fast_load_byte((c), (a), (r), (m))\n"
        "/* psx_cyc_step returns at once in fast mode, but it is too large to inline,\n"
        " * so test the gate here instead of paying a call per instruction. */\n"
        "#define psx_cyc_step(c, m) do { if (!__builtin_expect(g_rr_fast_lean, 1)) (psx_cyc_step)((c), (m)); } while (0)\n"
        "#endif\n", 'psx_cyc.h')
    # Block edges and function exits flush the load-charge batch; it is almost
    # always empty, so test that inline (same early-outs as psx_cyc_batch_flush).
    for name in ('psx_cyc_bb_defer_end', 'psx_cyc_bb_defer_flush'):
        old = {'psx_cyc_bb_defer_end':
                   "static inline void psx_cyc_bb_defer_end(void) {\n"
                   "    if (g_psx_cyc_bb_defer > 0) g_psx_cyc_bb_defer--;\n"
                   "    if (g_psx_cyc_bb_defer == 0) psx_cyc_batch_flush();\n"
                   "}\n",
               'psx_cyc_bb_defer_flush':
                   "static inline void psx_cyc_bb_defer_flush(void) { psx_cyc_batch_flush(); }\n"}[name]
        new = old.replace("psx_cyc_batch_flush();",
                          "{\n#ifdef RR_FAST_TIMING\n"
                          "        if (g_psx_cyc_batch != 0u || g_psx_cyc_local_acc != 0)\n"
                          "#endif\n"
                          "        psx_cyc_batch_flush(); }")
        src = _sub(src, old, new, 'psx_cyc.h')
    return src

# Block edges: generated code calls psx_check_interrupts_at at every branch and
# return (~300k times a frame in a 16-rider pile-up). While fast timing is on and
# nothing can be delivered -- no unmasked I_STAT bit, no COP0 software
# interrupt, not in an exception handler, no deferred thread switch, no pending
# load-charge batch -- the full check only feeds the idle-loop detector and runs
# periodic host maintenance. The detector only acts on consecutive checks at the
# SAME PC with no guest store or MMIO access in between, so those repeats (and
# every 256th edge, for maintenance) still take the full path; everything else
# returns inline. interrupt_edges.py exports the two interrupts.c flags.
FAST_EDGE = r'''
#if defined(RR_FAST_TIMING) && !defined(PSX_OVERLAY_DLL_BUILD)
/* ---- RR fast block edge (RoadRashJailbreak/tools/fast_timing.py) ---- */
#include "cpu_state.h"
extern int      g_rr_fast_lean;                      /* race_timing.py */
extern const int *const g_rr_in_exception;           /* interrupt_edges.py */
extern const int *const g_rr_defer_switch_pending;
extern uint32_t i_stat, i_mask;
extern uint32_t g_psx_cyc_batch;
extern int      g_ls_suppress_record;
extern uint64_t g_guest_store_count, g_mmio_access_count;
extern uint32_t g_rr_edge_pc, g_rr_edge_budget;      /* race_timing.py */
extern uint64_t g_rr_edge_stores, g_rr_edge_mmio;
static inline __attribute__((always_inline)) void rr_fast_check_interrupts_at(CPUState *cpu, uint32_t pc) {
    const uint64_t stores = g_guest_store_count, mmio = g_mmio_access_count;
    if (__builtin_expect(g_rr_fast_lean, 1) && (i_stat & i_mask) == 0u &&
        (cpu->cop0[13] & cpu->cop0[12] & 0x300u) == 0u &&
        !*g_rr_in_exception && !*g_rr_defer_switch_pending &&
        g_psx_cyc_batch == 0u && g_ls_suppress_record == 0 &&
        (pc != g_rr_edge_pc || stores != g_rr_edge_stores || mmio != g_rr_edge_mmio) &&
        --g_rr_edge_budget != 0u) {
        g_rr_edge_pc = pc; g_rr_edge_stores = stores; g_rr_edge_mmio = mmio;
        return;
    }
    g_rr_edge_budget = 256u;
    (psx_check_interrupts_at)(cpu, pc);
    g_rr_edge_pc = pc; g_rr_edge_stores = g_guest_store_count; g_rr_edge_mmio = g_mmio_access_count;
}
#define psx_check_interrupts_at(cpu, pc) rr_fast_check_interrupts_at((cpu), (pc))
#endif
'''

def patch_interrupts(src):
    tail = "#ifdef __cplusplus\n}\n#endif\n\n#endif /* PSXRECOMP_INTERRUPTS_H */"
    return _sub(src, tail, FAST_EDGE + tail, 'interrupts.h')

def patch_icache(src):
    return _sub(src,
        "void psx_icache_fetch_miss(CPUState *cpu, uint32_t addr);\n",
        "void psx_icache_fetch_miss(CPUState *cpu, uint32_t addr);\n"
        "#ifdef RR_FAST_TIMING\n"
        "extern int g_rr_fast_on;\n"
        "#define psx_icache_fetch(cpu, addr) do { if (!g_rr_fast_on) (psx_icache_fetch)((cpu), (addr)); } while (0)\n"
        "#endif\n", 'psx_icache.h')

def patch_cpu_state(src):
    src = _sub(src,
        "extern uint32_t psx_mult_latency_u(uint32_t rs);   /* MULTU (unsigned) */\n",
        "extern uint32_t psx_mult_latency_u(uint32_t rs);   /* MULTU (unsigned) */\n"
        "#ifdef RR_FAST_TIMING\n"
        "extern int g_rr_fast_on;\n"
        "#define psx_muldiv_set(cpu, latency) do { if (!g_rr_fast_on) (psx_muldiv_set)((cpu), (latency)); } while (0)\n"
        "#define psx_muldiv_stall(cpu) do { if (!g_rr_fast_on) (psx_muldiv_stall)(cpu); } while (0)\n"
        "#endif\n", 'cpu_state.h')
    src = _sub(src,
        "extern uint32_t psx_gte_cmd_latency(uint32_t cmd);  /* cost-1 for the 6-bit op  */\n",
        "extern uint32_t psx_gte_cmd_latency(uint32_t cmd);  /* cost-1 for the 6-bit op  */\n"
        "#ifdef RR_FAST_TIMING\n"
        "#define psx_gte_stall(cpu) do { if (!g_rr_fast_on) (psx_gte_stall)(cpu); } while (0)\n"
        "#define psx_gte_read(cpu, rt) do { if (!g_rr_fast_on) (psx_gte_read)((cpu), (rt)); } while (0)\n"
        "#endif\n", 'cpu_state.h')
    # Per-block charge: every compiled block leader calls psx_slice_block with
    # the block's cycle count, so fast timing accumulates that instead of
    # ticking per instruction; rr_fast_flush (race_timing.py) converts the raw
    # count to guest cycles every 64 or so.
    src = _sub(src,
        "static inline int psx_slice_block(CPUState* cpu, uint32_t block_addr, uint32_t bcyc, int side_effects) {\n"
        "    if (!g_psx_precise_slice) return 0;\n",
        "#ifdef RR_FAST_TIMING\n"
        "extern int g_rr_fast_on;\n"
        "extern uint32_t g_rr_fast_raw;\n"
        "void rr_fast_flush(void);\n"
        "#endif\n"
        "static inline int psx_slice_block(CPUState* cpu, uint32_t block_addr, uint32_t bcyc, int side_effects) {\n"
        "#ifdef RR_FAST_TIMING\n"
        "    if (__builtin_expect(g_rr_fast_on, 1) && (g_rr_fast_raw += bcyc) >= 64u) rr_fast_flush();\n"
        "#endif\n"
        "    if (!g_psx_precise_slice) return 0;\n", 'cpu_state.h')
    watched = ' || '.join(f'pc == 0x{p:08X}u' for p in WATCHED_PCS)
    src = _sub(src,
        "#endif /* PSXRECOMP_CPU_STATE_H */",
        "#if defined(RR_FAST_TIMING) && !defined(PSX_NO_DEBUG_TOOLS)\n"
        "/* Per-block debug observation only where an adapter listens; every site\n"
        " * passes a constant, so all other blocks fold to nothing. */\n"
        "void rr_cyc_observe_watch(uint32_t pc);\n"
        "static inline void rr_cyc_observe_filter(uint32_t pc) {\n"
        f"    if ({watched}) rr_cyc_observe_watch(pc);\n"
        "}\n"
        "#define debug_server_cyc_observe(pc) rr_cyc_observe_filter(pc)\n"
        "#endif\n"
        "#endif /* PSXRECOMP_CPU_STATE_H */", 'cpu_state.h')
    return src

# Texture correction "Full" (PROGRESS-0.15-geometry-correction.md). The recompiler writes a
# PGXP_*() hook after every load, store, ALU op, mult/div and COP2 transfer of the generated
# game code; the framework expands them to calls only in its -DPSX_PGXP=1 flavour and to
# nothing otherwise. Under fast timing, hooks everywhere cost 60% of the emulation (Full could
# not hold 60 FPS) and even a not-taken test at every site cost 30%. So only the functions on
# the vertex path keep them: every function with a GTE transfer, plus every function whose
# stores carried a projected vertex when all sites were hooked (pgxp_store_ring, slots 2 and 7;
# work/claude/geo_ring.py). Each of those is compiled twice (split_vertex_path): the plain
# function, with one test of g_rr_pgxp_full (pc_services.cpp, on while Texture correction is
# Full) at its entry, and a copy NAME__rr_pgxp whose hooks are real calls. A test inside the
# hot render functions cost On 15%; this costs one branch per call. Everywhere else the hooks
# compile to nothing, as before. The interpreters call the hooks directly in every build, as
# before. Addresses are the game's own function entry points (any overlay).
PGXP_FULL_FUNCS = [
    0x8001034C, 0x80010438, 0x800104C8, 0x80010550, 0x800105CC, 0x80010600, 0x80010624, 0x8001064C, 0x800106B4, 0x800220A4,
    0x800251E4, 0x80025EE0, 0x80026960, 0x80028C78, 0x80028E8C, 0x8002926C, 0x800295AC, 0x8002B878, 0x8002BAE8, 0x8002BE14,
    0x8002C4F8, 0x8002E080, 0x8002E14C, 0x8002E388, 0x8002E468, 0x8002E698, 0x80035040, 0x800351EC, 0x8003FA40, 0x8004CEEC,
    0x8004CF74, 0x8004D154, 0x8004D184, 0x8004D1A4, 0x8004D1B4, 0x8004D1E4, 0x8005D63C, 0x80063908, 0x8006396C, 0x80063A20,
    0x80063B90, 0x800644F4, 0x80064610, 0x800647E4, 0x800649BC, 0x80064B9C, 0x80066B98, 0x80067064, 0x80067AC4, 0x80068468,
    0x8006929C, 0x80069394, 0x80069440, 0x80069618, 0x800696B8, 0x80069784, 0x8006982C, 0x80069A8C, 0x80069B8C, 0x80069C60,
    0x80069CF0, 0x80069DEC, 0x80069FF0, 0x8006A0C8, 0x8006A25C, 0x8006A390, 0x8006A518, 0x8006A630, 0x8006A8FC, 0x8006AD20,
    0x8006AD4C, 0x8006B03C, 0x8006B75C, 0x8006C770, 0x8006C888, 0x8006CE60, 0x8006D10C, 0x8006D174, 0x8006D350, 0x8006D658,
    0x8006DB5C, 0x8006DC20, 0x8006DE5C, 0x8006E008, 0x8006E3BC, 0x8006E400, 0x8006E474, 0x8006E7A4, 0x8006E894, 0x8006E8FC,
    0x8006ECDC, 0x8006EF30, 0x8006F0B8, 0x8006F4A0, 0x8006F5D0, 0x8006F764, 0x8006F9A4, 0x800706A4, 0x80071D24, 0x8007AC04,
    0x8007B840, 0x8007EA64, 0x8007EC30, 0x8007FA4C, 0x80080B10, 0x80080D1C, 0x80081D7C, 0x8008F404, 0x8008F754, 0x80091468,
    0x80096564, 0x80097BCC, 0x8009989C, 0x8009BB48, 0x800A13D4, 0x800A2A64, 0x800A3CBC, 0x800A3F14, 0x800A966C, 0x800A9868,
    0x800AC958, 0x800B1978, 0x800BC618, 0x800CA564]

PGXP_SUFFIX = '__rr_pgxp'
PGXP_GATED = r'''#elif defined(RR_FAST_TIMING) && !defined(PSX_OVERLAY_DLL_BUILD)
/* ---- RR vertex-path PGXP hooks (RoadRashJailbreak/tools/fast_timing.py) ---- */
extern int g_rr_pgxp_full;   /* Texture correction Full (pc_services.cpp) */
/* Real calls only in the NAME__rr_pgxp copies (split_vertex_path); the test folds at compile time. */
static inline __attribute__((always_inline)) int rr_pgxp_copy(const char *f, unsigned long n) {
    const unsigned long k = sizeof("@SUFFIX@") - 1u;
    if (n < k + 1u) return 0;
    for (unsigned long i = 0; i < k; i++) if (f[n - 1u - k + i] != "@SUFFIX@"[i]) return 0;
    return 1;
}
#define RR_PGXP_GATE(call)                       (rr_pgxp_copy(__func__, sizeof(__func__)) ? (call) : (void)0)
#define PGXP_LOAD(instr, addr, val)              RR_PGXP_GATE(psx_pgxp_load(cpu, (instr), (addr), (val)))
#define PGXP_STORE(instr, addr, val)             RR_PGXP_GATE(psx_pgxp_store(cpu, (instr), (addr), (val)))
#define PGXP_ALU(instr, res, s1, s2)             RR_PGXP_GATE(psx_pgxp_alu(cpu, (instr), (res), (s1), (s2)))
#define PGXP_MULDIV(instr, hi, lo, s1, s2)       RR_PGXP_GATE(psx_pgxp_muldiv(cpu, (instr), (hi), (lo), (s1), (s2)))
#define PGXP_COP2(instr, val, addr)              RR_PGXP_GATE(psx_pgxp_cop2(cpu, (instr), (val), (addr)))
#else
'''

def patch_pgxp_hooks(src):
    return _sub(src, "#else\n#define PGXP_LOAD(instr, addr, val)              ((void)0)\n",
                PGXP_GATED.replace('@SUFFIX@', PGXP_SUFFIX) +
                "#define PGXP_LOAD(instr, addr, val)              ((void)0)\n", 'pgxp_hooks.h')

# Generated function definitions: "void NAME(CPUState* cpu)\n{\n" ... "\n}\n" at column 0
# (bodies are indented; no static locals). NAME ends in the PS1 address.
_DEF = re.compile(r'^(?:static )?void (\w*func_([0-9A-F]{8}))\(CPUState\* cpu\)\n\{\n', re.M)

def split_vertex_path(src, origin_dir=None):
    """Two copies of every PGXP_FULL_FUNCS function in one generated unit (see above).
    Returns (source, number of functions split). Quoted includes that exist beside the
    original unit become absolute, so the result compiles from another folder."""
    want, out, pos, n = set(PGXP_FULL_FUNCS), [], 0, 0
    for m in _DEF.finditer(src):
        if int(m[2], 16) not in want or m.start() < pos: continue
        end = src.find('\n}\n', m.end() - 1)
        body = src[m.end():end + 1]
        if end < 0 or re.search(r'^(?:static |void )', body, re.M):
            raise RuntimeError(f'fast_timing/split_vertex_path: cannot find the end of {m[1]}')
        name = m[1]
        out.append(src[pos:m.start()])
        # The hooked copy goes in its own section, after the rest of the code (COFF groups
        # .text$<name> into .text in name order), so the plain copies keep their old neighbours:
        # with the copies beside them, Texture correction On ran ~10% slower (PROGRESS-0.15-geometry-correction.md).
        # Not static, so split_pieces can put a copy and its function in different pieces (each of
        # the biggest takes minutes to compile); the function declares its copy.
        out.append(f'__attribute__((noinline, section(".text$zrrpgxp"))) void {name}{PGXP_SUFFIX}(CPUState* cpu)\n{{\n{body}}}\n')
        out.append(m[0] + f'    extern void {name}{PGXP_SUFFIX}(CPUState* cpu);\n'
                   f'    if (__builtin_expect(g_rr_pgxp_full, 0)) {{ {name}{PGXP_SUFFIX}(cpu); return; }}\n' + body + '}\n')
        pos = end + 3; n += 1
    out.append(src[pos:])
    src = ''.join(out)
    if n and origin_dir:
        origin_dir = pathlib.Path(origin_dir)
        src = re.sub(r'^#include "([^"/\\]+)"', lambda i: f'#include "{(origin_dir / i[1]).as_posix()}"'
                     if (origin_dir / i[1]).exists() else i[0], src, flags=re.M)
    return src, n

# Big units are compiled in pieces. Clang's time grows much faster than a unit's size: the
# 60 MB race overlay unit took 18 minutes of a 27 minute build on its own (and was the longest
# ThinLTO backend job), while 1-8 MB units compile at about 0.2 MB a second, and pieces build
# in parallel. A piece ends before the function that would take it over 4 MB. Smaller pieces
# did not shorten the build (one function, func_8007B840, takes 8 minutes whatever its piece)
# and 1 MB ones cost 30% more compile work in all, which matters on PCs with few jobs.
# Cuts fall only before a function definition; every piece repeats the unit's
# header (prototypes for every function in the unit, externs, stateless static helpers).
# Functions keep their names and order, so the speed profile still matches them.
PIECE_BYTES = 4 << 20
_TOP = re.compile(r'^(?:__attribute__[^\n]*?)?void (\w+)\(CPUState\* cpu\)\n\{\n', re.M)

def split_pieces(src, name):
    """[src], or the unit cut into pieces of about PIECE_BYTES."""
    if len(src) <= PIECE_BYTES * 3 // 2: return [src]
    starts = [m.start() for m in _TOP.finditer(src)]
    if len(starts) < 2: return [src]
    head = src[:starts[0]]
    if re.search(r'^static (?!(?:uint32_t|void|int) \w+\(CPUState\* cpu, )', head, re.M):
        raise RuntimeError(f'fast_timing/split_pieces: {name} has file-level static data')
    pieces, begin = [], starts[0]
    for s, end in zip(starts[1:], starts[2:] + [len(src)]):
        if end - begin > PIECE_BYTES: pieces.append(head + src[begin:s]); begin = s   # s would not fit
    pieces.append(head + src[begin:])
    return pieces

def split_units(out_dir, files, pieces=True, vertex=True):
    """Generated units with vertex-path functions are written split into out_dir, and big
    units are written in pieces (split_pieces, unless pieces is False); returns the list to
    compile (other units unchanged). Used by build_rr.py (one unit at a time, no pieces) and
    runtime-CMakeLists.txt."""
    out_dir = pathlib.Path(out_dir); out_dir.mkdir(parents=True, exist_ok=True); result = []
    for f in map(pathlib.Path, files):
        src, n = split_vertex_path(f.read_text(encoding='latin-1'), f.parent) if vertex else (f.read_text(encoding='latin-1'), 0)
        parts = split_pieces(src, f.name) if pieces else [src]
        if not n and len(parts) == 1:
            result.append(f); continue
        if not n:   # the pieces compile from out_dir (split_vertex_path does this when n)
            parts = [re.sub(r'^#include "([^"/\\]+)"', lambda i: f'#include "{(f.parent / i[1]).as_posix()}"'
                            if (f.parent / i[1]).exists() else i[0], p, flags=re.M) for p in parts]
        names = [f.name] if len(parts) == 1 else [f'{f.stem}_p{i:02d}{f.suffix}' for i in range(len(parts))]
        for piece, piece_name in zip(parts, names):
            dst = out_dir / piece_name
            if not dst.exists() or dst.read_text(encoding='latin-1') != piece: dst.write_text(piece, encoding='latin-1')
            result.append(dst)
    return result

def make_include_dir(src_dir, dst_dir):
    src_dir, dst_dir = pathlib.Path(src_dir), pathlib.Path(dst_dir)
    if dst_dir.exists(): shutil.rmtree(dst_dir)
    shutil.copytree(src_dir, dst_dir)
    for name, fn in (('psx_cyc.h', patch_psx_cyc), ('psx_icache.h', patch_icache), ('cpu_state.h', patch_cpu_state),
                     ('interrupts.h', patch_interrupts), ('pgxp_hooks.h', patch_pgxp_hooks)):
        p = dst_dir / name
        p.write_text(fn(p.read_text()))
    return dst_dir

if __name__ == '__main__':
    import sys
    if sys.argv[1] == '--split':   # --split <out dir> <unit.c>...: prints the units to compile (CMake list)
        print(';'.join(p.as_posix() for p in split_units(sys.argv[2], sys.argv[3:])))
    elif sys.argv[1] == '--pieces':   # the same without the vertex-path copies
        print(';'.join(p.as_posix() for p in split_units(sys.argv[2], sys.argv[3:], vertex=False)))
    else:
        make_include_dir(sys.argv[1], sys.argv[2])
