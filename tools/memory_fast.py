"""memory.c recipe: lean guest stores while race fast timing is on, and the
I_MASK hook of the race gate.

Every generated store calls cpu->write_word/half/byte, which runs psx_write_*
(render-pass, lockstep and DS-recording dispatch) and then psx_write_*_raw with
its RAM hooks. In a 16-rider pile-up that layering costs several percent of the
emulation thread. While g_rr_fast_lean is set (race fast timing and RR_PERF not
0; race_timing.py) and none of those modes is active, rr_fast_write_* store
directly to main RAM or the scratchpad and keep every hook that guards code:

* text_guard_note_write  - game-EXE text overwrite detection (native/interp
                           switch),
* overlay_watch_note_write - overlay code pages (variant generations,
                           self-modification of a running entry),
* g_guest_store_count    - the idle-loop detector's "no stores" evidence.

The kernel window (< 0x10000, dirty-page tracking and boot-scratch special
cases), the D44 probe word, mirrors outside live RAM, IsC (cache isolation),
DMA/host stores and everything that is not RAM/scratchpad take the full path.
Debugger-only rings (parity trace, card-data trace) are not fed by these fast
stores; arm them with fast timing off (Options > Frame rate: Original).
interrupt_edges.py points cpu->write_* here while fast timing is on.

Guest I_MASK writes are reported to race_timing.py (rr_fast_note_imask), which
uses stock CPU timing while the VBlank IRQ is masked.

memory.c is compiled in release mode (PSX_NO_DEBUG_TOOLS), as before.
"""

FAST_STORES = r'''

/* ---- Road Rash fast stores (RoadRashJailbreak/tools/memory_fast.py) ---- */
extern int g_rr_fast_lean;   /* fast timing on and RR_PERF not 0 (race_timing.py) */
/* RR_BG_STATE_PHYS and rr_fast_note_bg are declared above psx_write_word (BG_DECL). */
/* Returns the RAM/scratchpad byte to write at, or NULL for the full path. */
static inline __attribute__((always_inline)) uint8_t *rr_fast_store_target(uint32_t addr, uint32_t width) {
    extern void (*g_overlay_flush_pending_cycles)(void);
    if (!g_rr_fast_lean || g_psx_render_pass_active || g_ls_mode != 0 || g_ds_recording ||
        g_dma_exec_depth > 0 || g_host_store_depth > 0 || cpu_store_isolated() ||
        addr >= 0xC0000000u || (addr & (width - 1u)))
        return NULL;
    uint32_t phys = addr & 0x1FFFFFFFu;
    if (phys >= 0x1F800000u && phys <= 0x1F800400u - width) {
        if (g_overlay_flush_pending_cycles) g_overlay_flush_pending_cycles();
        g_guest_store_count++;
        return &scratchpad[phys - 0x1F800000u];
    }
    if (phys >= 0x00800000u) return NULL;
    phys = psx_phys_addr_store(addr);
    if (phys < DIRTY_RAM_KERNEL_TRACK_BYTES || phys > RAM_SIZE - width || phys == D44_PHYS)
        return NULL;
    if (g_overlay_flush_pending_cycles) g_overlay_flush_pending_cycles();
    g_guest_store_count++;
    return &ram[phys];
}
static inline __attribute__((always_inline)) void rr_fast_note_ram(uint8_t *p, uint32_t val, uint32_t width) {
    if (p >= ram && p < ram + RAM_SIZE) {
        uint32_t phys = (uint32_t)(p - ram);
        text_guard_note_write(phys, val, (int)width);
        overlay_watch_note_write(phys, width);
    }
}
void rr_fast_write_word(uint32_t addr, uint32_t val) {
    uint8_t *p = rr_fast_store_target(addr, 4u);
    if (!p) { psx_write_word(addr, val); return; }
    if ((addr & 0x1FFFFFFFu) == RR_BG_STATE_PHYS) rr_fast_note_bg(val);
    rr_fast_note_ram(p, val, 4u);
    memcpy(p, &val, 4);
}
void rr_fast_write_half(uint32_t addr, uint16_t val) {
    uint8_t *p = rr_fast_store_target(addr, 2u);
    if (!p) { psx_write_half(addr, val); return; }
    rr_fast_note_ram(p, val, 2u);
    memcpy(p, &val, 2);
}
void rr_fast_write_byte(uint32_t addr, uint8_t val) {
    uint8_t *p = rr_fast_store_target(addr, 1u);
    if (!p) { psx_write_byte(addr, val); return; }
    rr_fast_note_ram(p, val, 1u);
    *p = val;
}
'''

IMASK_OLD = ("    i_mask = next;\n"
             "    imask_trace_record(old, i_mask, width);\n")
# Every guest I_MASK write is reported to the race gate (race_timing.py
# rr_fast_note_imask: stock CPU timing while the VBlank IRQ is masked).
IMASK_NEW = ("    i_mask = next;\n"
             "    { extern void rr_fast_note_imask(uint32_t mask); rr_fast_note_imask(i_mask); }\n"
             "    imask_trace_record(old, i_mask, width);\n")

# Guest stores to the game's background-list state word (0x8005B588: 1 = the
# next frame's sky list waits for the VBlank callback) go to race_timing.py
# rr_fast_note_bg, which uses stock CPU timing while the list is queued. The
# lean path (rr_fast_write_word) and the full path both report it; host stores
# (debugger, savestates) and DMA do not.
BG_OLD = ("void psx_write_word(uint32_t addr, uint32_t val) {\n"
          "    extern void (*g_overlay_flush_pending_cycles)(void);\n"
          "    if (g_overlay_flush_pending_cycles) g_overlay_flush_pending_cycles();\n"
          "    if (g_psx_render_pass_active) { render_pass_store(addr, val, 4); return; }\n"
          "    if (g_ls_mode == 2) { ls_write_hook(addr, 4, val); return; }\n")
BG_DECL = ("#define RR_BG_STATE_PHYS 0x0005B588u   /* Road Rash background-list state (memory_fast.py) */\n"
           "extern void rr_fast_note_bg(uint32_t state);   /* race_timing.py */\n")
BG_NEW = (BG_DECL + BG_OLD +
          "    if ((addr & 0x1FFFFFFFu) == RR_BG_STATE_PHYS && g_dma_exec_depth == 0 && g_host_store_depth == 0)\n"
          "        rr_fast_note_bg(val);\n")

def extend(src):
    if src.count(IMASK_OLD) != 1:
        raise RuntimeError(f'memory_fast: expected one I_MASK write, found {src.count(IMASK_OLD)}')
    src = src.replace(IMASK_OLD, IMASK_NEW)
    if src.count(BG_OLD) != 1:
        raise RuntimeError(f'memory_fast: expected one psx_write_word head, found {src.count(BG_OLD)}')
    src = src.replace(BG_OLD, BG_NEW)
    for needle in ('static uint8_t scratchpad[SCRATCHPAD_SIZE];', 'static uint8_t ram[RAM_SIZE];',
                   '#define D44_PHYS 0x00079D44u', 'static inline void text_guard_note_write(',
                   'static inline void overlay_watch_note_write(', 'static inline int cpu_store_isolated(void)',
                   'void psx_write_byte(uint32_t addr, uint8_t val)'):
        if src.count(needle) != 1:
            raise RuntimeError(f'memory_fast: expected one match for {needle!r}, found {src.count(needle)}')
    return src.rstrip('\n') + '\n' + FAST_STORES

if __name__ == '__main__':
    import pathlib, sys
    # latin-1 round-trips the framework's UTF-8 comments byte for byte
    pathlib.Path(sys.argv[2]).write_text(extend(pathlib.Path(sys.argv[1]).read_text(encoding='latin-1')), encoding='latin-1')
