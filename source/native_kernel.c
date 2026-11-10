/* Native dispatch for proven kernel installation images. No guest patching. */
#include <stdint.h>
#include "cpu_state.h"

typedef struct {
    uint32_t pc, guard_address, guard_word_count;
    uint32_t words[32];
    void (*function)(CPUState *);
} RRKernelEntry;
#include "rr_kernel_entries.h"

uint64_t rr_kernel_native_hits;
int rr_kernel_native_dispatch(CPUState *cpu, uint32_t pc) {
    for (unsigned e = 0; e < sizeof(rr_kernel_entries)/sizeof(rr_kernel_entries[0]); ++e) {
        const RRKernelEntry *entry = &rr_kernel_entries[e];
        if (pc != entry->pc) continue;
        for (unsigned w = 0; w < entry->guard_word_count; ++w)
            if (cpu->read_word(entry->guard_address + 4u*w) != entry->words[w]) return 0;
        ++rr_kernel_native_hits;
        entry->function(cpu);
        return 1;
    }
    return 0;
}
