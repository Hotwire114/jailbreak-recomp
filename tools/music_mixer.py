"""Generate an owner-local generic mixer extension from the pinned framework.
No guest game instructions are read, copied or replaced by this helper.
"""
def extend(source):
    gain='''                    int16_t vr = chan_volume(voice_reg(v, 1), &sweep_voice_env[v][1]);'''
    tap='audio_trace_pcm(AUDIO_TAP_SPU_OUT, out_stereo, frames);'
    if source.count(gain)!=1 or source.count(tap)!=2:
        raise ValueError('Pinned mixer changed; custom music requires review')
    entry='static void spu_render_block(int16_t* out_stereo, int frames) {'
    if source.count(entry)!=1:raise ValueError('Pinned mixer entry changed')
    source=source.replace(entry,entry+'\n    const unsigned rr_music_mask = rr_music_voice_mask();')
    source=source.replace(gain,gain+'\n                    if(v<24 && (rr_music_mask & (1u<<v))) vl=vr=0;')
    source=source.replace(tap,'rr_music_mix(out_stereo, frames);\n        '+tap)
    return ('/* Owner-local presentation mixer, generated from generic runtime. */\n'
            'extern unsigned rr_music_voice_mask(void);\n'
            'extern void rr_music_mix(short*,int);\n'+source)

if __name__=='__main__':
    import pathlib,sys
    pathlib.Path(sys.argv[2]).write_text(extend(pathlib.Path(sys.argv[1]).read_text()))
