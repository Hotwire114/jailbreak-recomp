#pragma once
#include <cstdint>
namespace rrpc {
void music_tick(bool race,bool paused);
void music_preview(bool enabled);
void music_change(int row,int direction);
void music_import();
void music_labels(char (*labels)[32],char (*help)[32],int row);
bool music_is_custom();
int music_track_count();
void music_track_label(int index,char*text,unsigned capacity);
void music_select(int index);
}
