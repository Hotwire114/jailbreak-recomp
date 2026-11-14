"""dma.c recipe: GPU linked-list DMA without per-word device catch-up (PROGRESS-0.15-speed.md).

A linked-list GPU DMA holds the CPU while the list is walked (hold_cpu_for_gpu_linked_list).
The framework advances the guest clock to the walk's next event -- one display-list word, or a
node header -- and then catches up every device (timers, DMA, CD, SPU, SIO, interrupts) before
the next word. A race frame's list is thousands of words, a 16-rider pile-up tens of
thousands: this loop was ~9% of a 16-rider race frame, almost all of it device catch-up.

While race fast timing runs with its lean paths (g_rr_fast_lean, race_timing.py), the hold
walks the list in budgeted passes instead, then charges the guest time those steps would have
taken (the walk's own per-word and per-node costs, accumulated by the same
dma_gpu_ll_cycles_to_event steps) and catches the devices up once. The CPU is held for the
whole walk either way, so no guest instruction can observe the difference; device events that
came due during the walk fire right after it, as they would when the held CPU resumes. Stock
timing (menus, loading, RR_PERF=0) keeps the framework's loop.
"""

def _sub(src, old, new):
    if src.count(old) != 1:
        raise RuntimeError(f'dma_fast: expected one match for {old[:70]!r}, found {src.count(old)}')
    return src.replace(old, new)

FAST_HOLD = r'''
/* ---- RR fast linked-list hold (RoadRashJailbreak/tools/dma_fast.py) ---- */
extern int g_rr_fast_lean;   /* race_timing.py */
static uint32_t rr_hold_fast_walk(void) {
    uint32_t steps = 0u;
    uint64_t charge = 0u;
    while (gpu_linked_list.active && ((channels[2].chcr >> 24) & 1u) && channel_enabled(2) &&
           steps < (1u << 24)) {   /* the walker's own node limit ends cyclic lists long before */
        const uint32_t moved = gpu_linked_list.total_words;
        uint32_t c = dma_gpu_ll_cycles_to_event(&gpu_linked_list);
        if (c == UINT32_MAX) break;
        g_dma_exec_depth++;
        g_dma_cur_ch = 2;
        g_dma_cur_madr = gpu_linked_list.current_addr;
        g_dma_cur_bcr = channels[2].bcr;
        g_dma_initiator_pc = s_dma_ch_initiator_pc[2];
        dma_gpu_ll_advance(&gpu_linked_list, c, &gpu_ll_ops, NULL);
        g_dma_exec_depth--;
        charge += c;
        steps++;
        if (gpu_linked_list.active && gpu_linked_list.total_words == moved &&
            dma_gpu_ll_cycles_to_event(&gpu_linked_list) == c) break;   /* no progress */
    }
    while (charge) {   /* the guest time the walk took, then one device catch-up */
        const uint32_t n = charge > 0x40000000u ? 0x40000000u : (uint32_t)charge;
        psx_advance_cycles(n);
        charge -= n;
    }
    psx_devices_service_to_now();
    return steps;
}
'''

# Graphics worker (gpu_geometry.py gpu_worker): during the fast walk the callbacks hand
# the list to the worker instead of executing it here. Same words, same order.
GW_DECL = r'''
extern int rr_gw_begin(void);
extern void rr_gw_push(uint32_t kind, uint32_t a, uint32_t b);
enum { RR_GW_WORD, RR_GW_NODE, RR_GW_HEADER, RR_GW_END };
static int rr_gw_queueing;
'''

def worker(src):
    src = _sub(src, "static uint32_t gpu_ll_resolve_address(void *opaque, uint32_t address) {\n",
               GW_DECL + "static uint32_t gpu_ll_resolve_address(void *opaque, uint32_t address) {\n")
    src = _sub(src, "    (void)opaque;\n    gpu_ws_validate_linked_list_header(addr, header);\n",
               "    (void)opaque;\n    if (rr_gw_queueing) { rr_gw_push(RR_GW_HEADER, addr, header); return; }\n    gpu_ws_validate_linked_list_header(addr, header);\n")
    src = _sub(src, "    gpu_ws_validate_linked_list_node(addr, num_words);\n    gpu_set_gp0_linked_list_node(addr, num_words);\n    return 1;\n",
               "    if (rr_gw_queueing) { rr_gw_push(RR_GW_NODE, addr, num_words); return 1; }\n    gpu_ws_validate_linked_list_node(addr, num_words);\n    gpu_set_gp0_linked_list_node(addr, num_words);\n    return 1;\n")
    src = _sub(src, "    (void)opaque;\n    gpu_set_gp0_source(address);\n    gpu_write_gp0(word);\n",
               "    (void)opaque;\n    if (rr_gw_queueing) { rr_gw_push(RR_GW_WORD, address, word); return; }\n    gpu_set_gp0_source(address);\n    gpu_write_gp0(word);\n")
    src = _sub(src, "    gpu_ws_end_linked_list();\n    complete_transfer(2);\n",
               "    if (rr_gw_queueing) rr_gw_push(RR_GW_END, 0, 0); else gpu_ws_end_linked_list();\n    complete_transfer(2);\n")
    src = _sub(src, "    if (g_rr_fast_lean) steps = rr_hold_fast_walk();\n",
               "    if (g_rr_fast_lean) { rr_gw_queueing = rr_gw_begin(); steps = rr_hold_fast_walk(); rr_gw_queueing = 0; }\n")
    return src

def extend(src):
    src = _sub(src, "static void hold_cpu_for_gpu_linked_list(void) {\n", FAST_HOLD + "static void hold_cpu_for_gpu_linked_list(void) {\n")
    src = _sub(src,
        "    uint32_t steps = 0;\n"
        "    while (gpu_linked_list.active && ((channels[2].chcr >> 24) & 1u) &&\n"
        "           channel_enabled(2)) {\n",
        "    uint32_t steps = 0;\n"
        "    if (g_rr_fast_lean) steps = rr_hold_fast_walk();\n"
        "    else\n"
        "    while (gpu_linked_list.active && ((channels[2].chcr >> 24) & 1u) &&\n"
        "           channel_enabled(2)) {\n")
    return worker(src)

if __name__ == '__main__':
    import sys
    open(sys.argv[2], 'w').write(extend(open(sys.argv[1]).read()))
