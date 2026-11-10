#pragma once
#include <cstdint>
namespace rrpc {void voice_observe(uint32_t);void voice_tick();int voice_next(int,int,int seat=0);const char*voice_name(int);void voice_preview(int voice,int seat);}
