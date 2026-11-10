#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "cpu_state.h"

extern uint64_t g_dirty_ram_insns_run;

/* Called by the guarded dirty-RAM interpreter before each instruction it runs,
 * i.e. only for PCs the static recompile does not cover. 0.14 stopped the game
 * here (exit 71), which players saw as a crash on menu paths the AOT capture
 * never visited (0x800684EC in the frontend). Record the first instruction of
 * each uncovered run once, for later seeding, and let the interpreter execute
 * it. Sequential instructions take the early return. */
void rr_native_gap(CPUState *cpu,uint32_t pc,uint32_t insn) {
    enum { MAX_GAPS = 128 };
    static uint32_t last_pc, seen[MAX_GAPS];
    static unsigned count;
    static FILE *log;
    uint32_t prev = last_pc;
    last_pc = pc;
    if (pc == prev + 4u || pc == prev) return;
    if (count >= MAX_GAPS) return;
    for (unsigned i = 0; i < count; i++) if (seen[i] == pc) return;
    seen[count++] = pc;
    if (!log) log = fopen("rr_native_gaps.log", "a");
    if (log) {
        fprintf(log, "pc=0x%08X insn=0x%08X ra=0x%08X interpreted_so_far=%llu\n", pc, insn, cpu->gpr[31],
                (unsigned long long)g_dirty_ram_insns_run);
        fflush(log);
    }
    fprintf(stderr, "RR_NATIVE_GAP pc=0x%08X ra=0x%08X (interpreting)\n", pc, cpu->gpr[31]);
}
