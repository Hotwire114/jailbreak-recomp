"""render_thread.c recipe: cheaper command publishing to the render thread (PROGRESS-0.15-speed.md).

The emulation thread records every GPU draw as several small commands in the render
thread's ring and publishes each with rt_cmd_commit, which stored the ring's write position
with a sequentially consistent atomic store. On x86 that is a full memory fence (xchg) per
record: rt_cmd_commit alone was 5.4% of a 16-rider race frame.

Publishing ring data only needs release order (the payload is written before the position):
on x86 a plain store. The full order is needed in one place only, the sleep/wake handshake
(producer: publish, then read render_sleeping; render thread: set render_sleeping, then
re-read the write position). wake_render therefore issues a seq_cst fence before it reads
render_sleeping, which keeps that handshake exactly as strong as before. Wakes still happen at
the same thresholds.
"""

def _sub(src, old, new):
    if src.count(old) != 1:
        raise RuntimeError(f'render_fast: expected one match for {old[:70]!r}, found {src.count(old)}')
    return src.replace(old, new)

def extend(src):
    src = _sub(src,
        "    uint64_t end = R.pend_start + R.pend_size;\n"
        "    atomic_store(&R.wpos, end);\n",
        "    uint64_t end = R.pend_start + R.pend_size;\n"
        "    /* RR (render_fast.py): release store, no fence per record; wake_render fences. */\n"
        "    atomic_store_explicit(&R.wpos, end, memory_order_release);\n")
    src = _sub(src,
        "    R.unwoken_bytes = R.unwoken_records = 0;\n"
        "    if (atomic_load(&R.render_sleeping)) {\n",
        "    R.unwoken_bytes = R.unwoken_records = 0;\n"
        "    atomic_thread_fence(memory_order_seq_cst);   /* RR: publish before the sleep check */\n"
        "    if (atomic_load(&R.render_sleeping)) {\n")
    return src

if __name__ == '__main__':
    import sys
    open(sys.argv[2], 'w').write(extend(open(sys.argv[1]).read()))
