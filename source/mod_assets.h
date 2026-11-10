#pragma once
#include <cstdint>
/* Player mod packs (.rrmod textures, sounds, models) and the modding dump.
 * Enhanced only; see mod_assets.cpp and tools/mod_manager.py. */
namespace rrpc {
void mods_tick(bool racing);
/* SPU hooks, called from __wrap_spu_write (taunt_voices.cpp): a voice register
 * write (returns the value to write) and a KEYON of one channel (before it;
 * taken = a taunt line already plays from its own bank). */
uint32_t mod_sound_register(uint32_t addr, uint32_t value);
void mod_sound_keyon(unsigned channel, bool taken);
}
