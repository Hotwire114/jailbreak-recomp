"""SPU interrupt prediction (PROGRESS-0.16-native-audio.md). On by default; RR_SPU_EVENTS=0 restores
the stock per-sample SPU deadlines for A/B runs.

Road Rash unmasks the SPU interrupt in races and menus: music is streamed through SPU voices and
refilled when a voice reaches the IRQ address. The framework therefore makes every 44.1 kHz sample a
device deadline and runs the whole host audio pump at each one (44,100 times per guest second).

Instead, the next IRQ-address hit is predicted exactly from each voice's block-fetch
schedule (pitch, phase, sample index and the block flags in SPU RAM, replaying voice_next_sample's
counters block by block), and only that sample is a deadline. The SPU still renders every sample, in
the same order and with the same results; only the number of times the CPU stops to ask changes.
Per-sample deadlines remain when the IRQ address lies in the capture or reverb area, before the SPU
clock exists, or when the pump did not catch the SPU up (muted turbo loads).

* spu.c (extend_spu, after music_mixer.extend): prediction, invalidated by every SPU register
  write that can move a fetch (pitch, loop address, KEYON, IRQ address, SPUCNT, FIFO), SPU DMA and
  output flushes (savestate loads, reset).
* interrupts.c (extend_interrupts, after interrupt_edges.extend): the SPU sample deadline asks the
  prediction (cached until reached or invalidated) and pumps only when it is due.
No guest instructions are read, copied or replaced here.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))   # sibling recipes, also under python -I
import interrupt_edges
import music_mixer

def _sub(src, old, new):
    if src.count(old) != 1:
        raise RuntimeError(f'spu_events: expected one match for {old[:70]!r}, found {src.count(old)}')
    return src.replace(old, new)

_TOP = r'''
/* ---- SPU IRQ prediction (RoadRashJailbreak/tools/spu_events.py) declarations ---- */
int g_spu_ev_on;                  /* on unless RR_SPU_EVENTS=0; latched at the first spu_init */
uint64_t g_spu_ev_due_cycle = UINT64_MAX;
int g_spu_ev_cache_ok;            /* interrupts.c may answer from g_spu_ev_due_cycle */
static uint64_t ev_samples;       /* 44.1 kHz samples rendered since start; never rewinds */
static int ev_gdirty = 1;'''

_BODY = r'''
/* ==== SPU IRQ prediction (RoadRashJailbreak/tools/spu_events.py) ============================= */
/* Recompute the device deadline at the next charge: a register or SPU RAM write may have moved the
 * next IRQ earlier than the deadline computed before it (memory.c syncs before the write). */
static void ev_poke(void) {
    extern uint64_t psx_next_service_cycle;
    extern int g_psx_guest_time_frozen;
    g_spu_ev_cache_ok = 0;
    if (!g_psx_guest_time_frozen) psx_next_service_cycle = 0;
}

/* The render loop's only interrupt source is the IRQ-address compare. Outside the capture and
 * reverb areas its only trigger is a voice block fetch, whose schedule is a pure function of pitch,
 * phase, sample index and the block flags in SPU RAM. */
#define EV_HORIZON 4096u
#define EV_NEVER UINT64_MAX
static uint64_t ev_vdue[SPU_VOICE_COUNT];   /* absolute sample: predicted hit or re-check point */
static uint32_t ev_vdirty = 0xFFFFFFu;      /* voices to re-predict */
static uint32_t ev_vloop;                   /* voices whose address loop provably misses the IRQ */
static uint64_t ev_due_sample = EV_NEVER;

static uint64_t ev_voice_predict(int i, uint32_t unit) {
    const SpuVoice *v = &voices[i];
    ev_vloop &= ~(1u << i);
    if (!v->active || v->sample_bank) return EV_NEVER;    /* bank voices skip the IRQ compare */
    const uint32_t pitch = spu_regs[(uint32_t)i * 8u + 2u] & 0x3FFFu;
    const uint32_t block = SPU_BLOCK_SAMPLES * 4096u;
    uint32_t ph = v->phase, cur = v->cur_addr, rep = v->repeat_addr, first_jump = UINT32_MAX;
    uint8_t flags = v->flags;
    uint64_t c = 0;                          /* samples from now until the next fetch */
    if (v->sample_idx < SPU_BLOCK_SAMPLES) {
        if (!pitch) return EV_NEVER;
        const int64_t need = (int64_t)(SPU_BLOCK_SAMPLES - v->sample_idx) * 4096 - (int64_t)ph;
        c = need > 0 ? (uint64_t)((need + pitch - 1) / pitch) : 0u;
        ph = (uint32_t)((int64_t)ph + (int64_t)c * pitch - (int64_t)(SPU_BLOCK_SAMPLES - v->sample_idx) * 4096);
    }
    for (;;) {
        if (c >= EV_HORIZON) return ev_samples + EV_HORIZON;
        int stop = 0;
        uint32_t addr;
        if (flags & 0x01u) {                 /* END: jump to the repeat address */
            addr = rep & (SPU_RAM_SIZE - 1u);
            if (!(flags & 0x02u)) stop = 1;  /* no REPEAT: envelope forced to 0, voice ends */
            else if (first_jump == addr) { ev_vloop |= 1u << i; return EV_NEVER; }
            else if (first_jump == UINT32_MAX) first_jump = addr;
        } else {
            addr = cur & (SPU_RAM_SIZE - 1u);
        }
        if (addr + 16u > SPU_RAM_SIZE) addr = 0;
        if ((addr >> 3) == unit || (addr >> 3) + 1u == unit) return ev_samples + c;
        flags = spu_ram[addr + 1u];
        if (flags & 0x04u) rep = addr;
        cur = (addr + 16u) & (SPU_RAM_SIZE - 1u);
        if (stop || !pitch) return EV_NEVER;
        uint32_t m = (block - ph + pitch - 1u) / pitch;
        if (m < 1u) m = 1u;
        ph = ph + m * pitch - block;
        c += m;
    }
}

uint32_t spu_ev_cycles_to_next(void) {
    g_spu_ev_cache_ok = 1;                   /* every path below sets g_spu_ev_due_cycle */
    const uint16_t ctrl = spu_regs[reg_index(0x1F801DAAu)];
    if (!(ctrl & 0x0040u)) { g_spu_ev_due_cycle = UINT64_MAX; return UINT32_MAX; }
    const uint64_t now = psx_get_cycle_count();
    const uint32_t phase = (uint32_t)(now % SPU_CYCLES_PER_SAMPLE);
    const uint32_t per_sample = phase ? SPU_CYCLES_PER_SAMPLE - phase : SPU_CYCLES_PER_SAMPLE;
    const uint32_t unit = spu_regs[reg_index(0x1F801DA4u)];
    if (!s_clock_valid || (unit << 3) < 0x1000u || (unit << 3) >= rev_mbase()) {
        /* Capture/reverb area or no SPU clock yet: every sample stays a deadline (stock). */
        g_spu_ev_due_cycle = now + per_sample;
        return per_sample;
    }
    if (!(ctrl & 0x8000u) || irq_flag) {     /* disabled SPU / unacknowledged IRQ: nothing can fire */
        g_spu_ev_due_cycle = UINT64_MAX;
        return UINT32_MAX;
    }
    if (ev_gdirty) { ev_vdirty = 0xFFFFFFu; ev_gdirty = 0; }
    if (ev_due_sample <= ev_samples)         /* a hit or re-check point was reached */
        for (int i = 0; i < SPU_VOICE_COUNT; i++) if (ev_vdue[i] <= ev_samples) ev_vdirty |= 1u << i;
    if (ev_vdirty) {
        uint64_t due = EV_NEVER;
        for (int i = 0; i < SPU_VOICE_COUNT; i++) {
            if (ev_vdirty & (1u << i)) ev_vdue[i] = ev_voice_predict(i, unit);
            if (ev_vdue[i] < due) due = ev_vdue[i];
        }
        ev_vdirty = 0;
        ev_due_sample = due;
    }
    if (ev_due_sample == EV_NEVER) { g_spu_ev_due_cycle = UINT64_MAX; return UINT32_MAX; }
    /* Sample n is rendered (and its fetch compared) once the guest reaches its end. */
    const uint64_t due = s_clock + (ev_due_sample - ev_samples + 1u) * SPU_CYCLES_PER_SAMPLE;
    if (ev_due_sample < ev_samples || due <= now) {
        g_spu_ev_due_cycle = now + per_sample;   /* SPU not caught up (muted pump): stock cadence */
        return per_sample;
    }
    g_spu_ev_due_cycle = due;
    const uint64_t d = due - now;
    return d >= UINT32_MAX ? UINT32_MAX - 1u : (uint32_t)d;
}

static void ev_write(uint32_t addr, uint32_t value) {
    if (addr >= 0x1F801C00u && addr < 0x1F801D80u) {
        const uint32_t idx = (addr - 0x1F801C00u) >> 1, voice = idx >> 3, reg = idx & 7u;
        /* pitch: only the timing of a fetch schedule changes, not a proven address loop */
        if ((reg == 2u && !(ev_vloop & (1u << voice))) || reg == 7u) { ev_vdirty |= 1u << voice; ev_poke(); }
        return;
    }
    switch (addr) {
    case 0x1F801D88u: ev_vdirty |= value & 0xFFFFu; ev_poke(); break;            /* KEYON */
    case 0x1F801D8Au: ev_vdirty |= (value & 0xFFu) << 16; ev_poke(); break;
    case 0x1F801DA2u: case 0x1F801DA4u: case 0x1F801DA8u: case 0x1F801DAAu:
        ev_gdirty = 1; ev_poke(); break;   /* reverb base, IRQ address, FIFO data, SPUCNT */
    default: break;
    }
}
'''

def extend_spu(src):
    src = music_mixer.extend(src)
    src = _sub(src, 'static uint8_t irq_flag;', 'static uint8_t irq_flag;' + _TOP)
    anchor = 'void spu_set_output_gate(int (*suppressed)(void)) { s_output_suppressed = suppressed; }'
    src = _sub(src, anchor, _BODY + '\n' + anchor)
    src = _sub(src, 'void spu_init(void) {\n',
               'void spu_init(void) {\n'
               '    { static int latched; if (!latched) { const char *e = getenv("RR_SPU_EVENTS");\n'
               '      latched = 1; g_spu_ev_on = !(e && e[0] == \'0\');\n'
               '      if (!g_spu_ev_on) fprintf(stderr, "rr spu events: off (RR_SPU_EVENTS=0)\\n"); } }\n'
               '    ev_gdirty = 1;\n')
    src = _sub(src,
               '        if (keep) spu_out_push(block, (uint32_t)n);\n'
               '        frames -= (uint64_t)n;\n',
               '        if (keep) spu_out_push(block, (uint32_t)n);\n'
               '        ev_samples += (uint64_t)n;\n'
               '        frames -= (uint64_t)n;\n')
    src = _sub(src, 'void spu_output_flush(void) { s_out_head = 0; s_out_count = 0; }',
               'void spu_output_flush(void) { s_out_head = 0; s_out_count = 0; ev_gdirty = 1; g_spu_ev_cache_ok = 0; }')
    src = _sub(src, 'void spu_write(uint32_t addr, uint32_t value) {\n    spu_catch_up();\n',
               'void spu_write(uint32_t addr, uint32_t value) {\n    spu_catch_up();\n'
               '    if (g_spu_ev_on) ev_write(addr, value);\n')
    src = _sub(src, 'void spu_dma_write(uint32_t word) {\n    spu_catch_up();\n',
               'void spu_dma_write(uint32_t word) {\n    spu_catch_up();\n'
               '    if (g_spu_ev_on) { ev_gdirty = 1; ev_poke(); }\n')
    return src

def extend_interrupts(src):
    src = interrupt_edges.extend(src)
    src = _sub(src, 'static int spu_sample_event_mode(void) {\n',
               '/* SPU IRQ prediction (tools/spu_events.py): spu.c knows the next due sample. */\n'
               'extern int g_spu_ev_on;\n'
               'extern uint64_t g_spu_ev_due_cycle;\n'
               'extern int g_spu_ev_cache_ok;\n'
               'extern uint32_t spu_ev_cycles_to_next(void);\n'
               'static int spu_sample_event_mode(void) {\n')
    src = _sub(src, 'uint32_t psx_spu_sample_event_cycles_to_next(void) {\n    g_spu_sample_deadline_queries++;\n',
               'uint32_t psx_spu_sample_event_cycles_to_next(void) {\n    g_spu_sample_deadline_queries++;\n'
               '    if (g_spu_ev_on) {\n'
               '        /* Asked on every deadline recompute: answer from the last prediction until it is\n'
               '         * reached or an SPU write/DMA invalidates it. */\n'
               '        if (g_spu_ev_cache_ok) {\n'
               '            const uint64_t now = psx_get_cycle_count();\n'
               '            if (now < g_spu_ev_due_cycle) {\n'
               '                const uint64_t d = g_spu_ev_due_cycle - now;\n'
               '                return d >= UINT32_MAX ? UINT32_MAX : (uint32_t)d;\n'
               '            }\n'
               '        }\n'
               '        return spu_sample_event_mode() && s_midframe_audio_pump ? spu_ev_cycles_to_next() : UINT32_MAX;\n'
               '    }\n')
    src = _sub(src, 'void psx_spu_sample_event_service(void) {\n    g_spu_sample_service_checks++;\n',
               'void psx_spu_sample_event_service(void) {\n    g_spu_sample_service_checks++;\n'
               '    if (g_spu_ev_on) {\n'
               '        /* One pump per predicted IRQ: its catch-up renders the sample that raises it. */\n'
               '        if (s_midframe_audio_pump && psx_get_cycle_count() >= g_spu_ev_due_cycle &&\n'
               '            spu_sample_event_mode()) {\n'
               '            g_spu_ev_due_cycle = UINT64_MAX;\n'
               '            g_spu_ev_cache_ok = 0;\n'
               '            g_spu_sample_service_pumps++;\n'
               '            s_midframe_audio_pump();\n'
               '        }\n'
               '        return;\n'
               '    }\n')
    return src

if __name__ == '__main__':
    src_path, out_path = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    text = src_path.read_text(encoding='latin-1')
    out = extend_interrupts(text) if src_path.name == 'interrupts.c' else extend_spu(text)
    out_path.write_text(out, encoding='latin-1')
