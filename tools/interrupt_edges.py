"""interrupts.c recipe: support for the race fast-timing block-edge path.

Generated code calls psx_check_interrupts_at at every block edge (~300k per
frame in a 16-rider pile-up). fast_timing.py inlines a pre-check into the
generated code that skips the full call when nothing can be delivered; it needs
two interrupts.c statics, exported here read-only:

* in_exception            - handler context takes the full path,
* s_defer_switch_pending  - a deferred cooperative thread switch is honoured
                            only by the full path.

The full path (psx_check_interrupts_at) also points cpu->write_word/half/byte
at memory_fast.py's lean stores while fast timing is on, and back to the stock
psx_write_* when it is off. Only the stock pointers are swapped, so any other
store routing (lockstep, tools) is left alone.
"""

def _sub(src, old, new):
    if src.count(old) != 1:
        raise RuntimeError(f'interrupt_edges: expected one match for {old[:60]!r}, found {src.count(old)}')
    return src.replace(old, new)

def extend(src):
    src = _sub(src,
        "int psx_get_in_exception(void) { return in_exception; }\n",
        "int psx_get_in_exception(void) { return in_exception; }\n"
        "/* Road Rash fast block edges (RoadRashJailbreak/tools/interrupt_edges.py). */\n"
        "const int *const g_rr_in_exception = &in_exception;\n"
        "const int *const g_rr_defer_switch_pending = &s_defer_switch_pending;\n")
    src = _sub(src,
        "void psx_check_interrupts_at(CPUState* cpu, uint32_t resume_pc) {\n"
        "    uint32_t prev = s_compiled_interrupt_resume_pc;\n",
        "extern int g_rr_fast_lean;\n"
        "extern void psx_write_word(uint32_t, uint32_t);\n"
        "extern void psx_write_half(uint32_t, uint16_t);\n"
        "extern void psx_write_byte(uint32_t, uint8_t);\n"
        "extern void rr_fast_write_word(uint32_t, uint32_t);\n"
        "extern void rr_fast_write_half(uint32_t, uint16_t);\n"
        "extern void rr_fast_write_byte(uint32_t, uint8_t);\n"
        "/* Lean stores (memory_fast.py) while race fast timing is on. */\n"
        "static inline void rr_fast_sync_store_hooks(CPUState *cpu) {\n"
        "    if (g_rr_fast_lean) {\n"
        "        if (cpu->write_word == psx_write_word) cpu->write_word = rr_fast_write_word;\n"
        "        if (cpu->write_half == psx_write_half) cpu->write_half = rr_fast_write_half;\n"
        "        if (cpu->write_byte == psx_write_byte) cpu->write_byte = rr_fast_write_byte;\n"
        "    } else {\n"
        "        if (cpu->write_word == rr_fast_write_word) cpu->write_word = psx_write_word;\n"
        "        if (cpu->write_half == rr_fast_write_half) cpu->write_half = psx_write_half;\n"
        "        if (cpu->write_byte == rr_fast_write_byte) cpu->write_byte = psx_write_byte;\n"
        "    }\n"
        "}\n"
        "void psx_check_interrupts_at(CPUState* cpu, uint32_t resume_pc) {\n"
        "    rr_fast_sync_store_hooks(cpu);\n"
        "    uint32_t prev = s_compiled_interrupt_resume_pc;\n")
    return src

if __name__ == '__main__':
    import pathlib, sys
    # latin-1 round-trips the framework's UTF-8 comments byte for byte
    pathlib.Path(sys.argv[2]).write_text(extend(pathlib.Path(sys.argv[1]).read_text(encoding='latin-1')), encoding='latin-1')
