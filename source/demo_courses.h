#pragma once
#include <cstdint>
struct CPUState;
namespace rrpc {
// Restored demo courses (race ids 32-37) on the Time Trial Course row (demo_courses.cpp).
void demo_tick();                 // once per VBlank (native_ui.cpp __wrap_gpu_vblank_tick)
void demo_text(CPUState* c);      // at the text routine's entry (rider_enhancements.cpp)
int demo_preview();               // 0-5: the demo course whose polaroid fills the picture slot, else -1
void demo_observe(uint32_t pc);   // watched PCs (fast_timing.py): the splits' race-id reads
void demo_showroom(int d);        // the Rider look showroom's course for the next Time Trial (0-5), -1 when it is over
}
