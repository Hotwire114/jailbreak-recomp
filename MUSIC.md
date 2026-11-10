# Custom music — 0.12

Import your own WAV files from Options → Audio → Add WAV tracks in the native game or launcher. Your source files are preserved. Copies live beside the installed game in `music`, outside runtime rebuilds. You can also copy WAV files into that folder before starting. The library accepts up to 128 files, at most 64 MB each. Duplicate imports preserve the existing file. Invalid or unsupported tracks fall back to the original soundtrack.

Choose Music: Custom, then use Left/Right on the track row to browse. The selected track loops during a race; automatic playlists, shuffle and MP3/FLAC decoding are not implemented. Music: Original restores the stock soundtrack. Your source and selected filename persist in launcher.ini. Original startup mode bypasses the feature.

The original Game options → Jukebox also displays the imported library. Select/Back switches Original/Custom; F7 opens the import picker; Up/Down moves through custom entries; Left/Right changes pages of five; Square previews/chooses a track; Cross accepts and leaves using the original flow. Returning to Original shows all five original bands/songs and their original controls. An imported filename may be truncated or use '?' for letters unavailable in the original font; the full Unicode filename remains intact on disk.

In races, custom audio follows the original Noise menu's music volume and pauses without advancing its position. The sound output extension recognizes the game's dry streamed stereo music pair by voice, buffer spacing, matching rate and envelope. It filters that pair's audible contribution and mixes resampled 44.1 kHz stereo PCM; native decoding, registers, envelopes, capture/IRQ timing and reverb processing continue. Unrecognized streams and music pairs configured to feed guest reverb RAM pass through. Jukebox preview uses a bounded SDL output queue which closes when leaving the screen.

Tests cover known PCM gain/looping, silence at zero music volume, pause, unchanged voice registers, original-source bypass and unrecognized-stream bypass. An actual copied Stadium Park race produced custom samples, excluded paused time, and displayed/previewed tracks in the original Jukebox with zero interpreted instructions. The file-picker UI, every song/track/mode, long listening sessions and physical controls still need user testing.

No music is included in the public package. WAV loading/resampling uses the pinned SDL implementation; see the [SDL WAV API](https://wiki.libsdl.org/SDL3/SDL_LoadWAV).
