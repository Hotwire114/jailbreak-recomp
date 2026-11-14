"""Small generic GPU changes, generated against the pinned runtime.
No original or translated game functions are included. LGPL-2.1-or-later.
"""
def extend(source, query_reads=False):
    # query_reads (Smooth reprojection: foreground policy, motion clock,
    # ordered VRAM reads, vertex source identities) was removed in 0.15.
    # Native 60 FPS makes frame interpolation unnecessary.
    old='((s_tex_filter==2 && untracked ? 0 : s_tex_filter) |'
    # HD UI (edge-directed magnification) for 2D sprites/rects and for the
    # exact-placed HUD and text triangles (s_rr_tri, exact_hud below): an
    # italic glyph drawn as two triangles is sampled like the upright glyph
    # drawn as a rectangle beside it.
    # Outside races every 2D textured polygon (no GTE provenance) is UI too:
    # frontend pictures and panels drawn as quads that are not plain rectangles.
    # Lettering tagged by native_ui (s_rr_text, exact_hud) takes the text mode
    # (native_ui.cpp rr_ui_text_filter; traced distance-field atlas slot,
    # rr_text_filter below) whenever HD UI is on.
    # Menu title banners take the HD font's banner mode (rr_banner_filter).
    new=('(((s_rr_bf = rr_banner_filter(texpage, us, vs, lim)) ? s_rr_bf : (s_rr_text || rr_hd_untagged(texpage, clut_x, clut_y, us, vs, lim)) && (s_rr_tf = rr_text_filter(texpage, clut_x, clut_y))>=0 ? s_rr_tf : (((lim && !s_pc_valid && !s_pq_valid) || s_rr_tri || (!lim && !s_pc_valid && !s_pq_valid && !rr_ui_in_race()))'
         ' && rr_ui_texture_filter()>=0 ? rr_ui_texture_filter() : (s_tex_filter==2 && untracked ? 0 : s_tex_filter))) |')
    if source.count(old)!=1:raise ValueError('GPU filter interface changed')
    source='extern int rr_ui_texture_filter(void);\nextern int rr_ui_in_race(void);\nextern int rr_fmv_wide(void);\nextern int rr_ui_text_filter(void);\n'+source.replace(old,new)
    old='static void gl_swap_with_osd(void) {'
    new=old+'''
        { int ww=0,wh=0,w=0,h=0,x=0,y=0,dw=0,dh=0;const uint32_t*px=0;
          SDL_GL_GetDrawableSize(s_win,&ww,&wh);
          if(rr_ui_preview(ww,wh,&px,&w,&h,&x,&y,&dw,&dh))gl_draw_osd_image(px,w,h,dw,dh,x,y,ww,wh); }
    '''
    if source.count(old)!=1:raise ValueError('Native preview interface changed')
    source='extern int rr_ui_preview(int,int,const unsigned int**,int*,int*,int*,int*,int*,int*);\n'+source.replace(old,new)
    # A retained real composition is not an actual present. The debug ring
    # must observe the owned direct swap, including generated/held-real frames,
    # rather than capturing the composition that fg_keep_real defers.
    old='if (s_ctx) present_image_ring_capture_gl();'
    if source.count(old)!=1:raise ValueError('Presented-image capture interface changed')
    source=source.replace(old,'if (s_ctx && s_pt_on) present_image_ring_capture_gl();')
    old='static void gl_window_swap(void) { SDL_GL_SwapWindow(s_win); }'
    new='''static void gl_window_swap(void) {
    #ifndef PSX_NO_DEBUG_TOOLS
        if (s_ctx && !s_pt_on) present_image_ring_capture_gl();
    #endif
        SDL_GL_SwapWindow(s_win);
    }'''
    if source.count(old)!=1:raise ValueError('Owned window swap interface changed')
    source=source.replace(old,new)
    source=async_probe(source)
    source=image_uploads(source)
    source=sharp_present(source)
    source=text_sdf(source)
    return record_trim(shadow_smoothing(post_effects(prompt_glyphs(exact_hud(source)))))

# Fewer render-thread records per triangle (PROGRESS-0.15-speed.md). The emulation thread
# records every GPU call for the render thread; in a 16-rider race that recording was ~9% of
# the emulation thread. Two cuts, neither changes what is drawn:
# - gl_renderer_note_wide_triangle_recovery is a diagnostic counter (the framework says so)
#   but recorded one command per triangle. It now counts on the emulation side when recording.
# - The precise / perspective / depth triangle setters record "off" for almost every
#   triangle. Render-side state persists until the next setter, and everything else only
#   clears it (draw consumption, resets), so an "off" right after an "off" of the same state
#   cannot change anything: it is not recorded. Any "on", and any direct (unrecorded) call,
#   resets the memory of the last record.
def record_trim(source):
    def sub(old, new, what):
        nonlocal source
        if source.count(old) != 1: raise ValueError(what + ' interface changed')
        source = source.replace(old, new)
    sub('    if (rth_record_mode()) { RTH_REC(RTH_WIDE_RECOVERY, 0, recovered); return; }\n',
        '    if (rth_record_mode()) { if (recovered) ++s_wide_triangle_recovery_count; return; }   /* RR record_trim: diagnostic, counted here */\n',
        'Wide triangle recovery note')
    sub('static void rtb_set_precise_triangle(int en, int32_t x0, int32_t y0, int32_t x1, int32_t y1,\n'
        '                                     int32_t x2, int32_t y2) {\n'
        '    RTH_DIRECT_OR(RTH_REC(RTH_PRECISE, 0, en, x0, y0, x1, y1, x2, y2));\n',
        '/* RR record_trim: 1 while the last recorded setter of that state was "off". */\n'
        'static int s_rr_rec_off_pc, s_rr_rec_off_pq, s_rr_rec_off_pz;\n'
        'static void rtb_set_precise_triangle(int en, int32_t x0, int32_t y0, int32_t x1, int32_t y1,\n'
        '                                     int32_t x2, int32_t y2) {\n'
        '    if (rth_record_mode()) {\n'
        '        if (!en && s_rr_rec_off_pc) return;\n'
        '        s_rr_rec_off_pc = !en;\n'
        '        RTH_REC(RTH_PRECISE, 0, en, x0, y0, x1, y1, x2, y2); return;\n'
        '    }\n'
        '    s_rr_rec_off_pc = 0;\n',
        'Precise triangle setter')
    sub('static void rtb_set_depth_triangle(int en, float z0, float z1, float z2) {\n'
        '    if (rth_record_mode()) {\n'
        '        int32_t v[4] = { en, 0, 0, 0 };\n',
        'static void rtb_set_depth_triangle(int en, float z0, float z1, float z2) {\n'
        '    if (rth_record_mode()) {\n'
        '        if (!en && s_rr_rec_off_pz) return;\n'
        '        s_rr_rec_off_pz = !en;\n'
        '        int32_t v[4] = { en, 0, 0, 0 };\n',
        'Depth triangle setter')
    sub('        rth_rec_ints(RTH_DEPTH, 0, 4, v);\n'
        '        return;\n'
        '    }\n'
        '    glb_set_depth_triangle(en, z0, z1, z2);\n',
        '        rth_rec_ints(RTH_DEPTH, 0, 4, v);\n'
        '        return;\n'
        '    }\n'
        '    s_rr_rec_off_pz = 0;\n'
        '    glb_set_depth_triangle(en, z0, z1, z2);\n',
        'Depth triangle direct path')
    sub('static void rtb_set_perspective_triangle(int en, float q0, float q1, float q2) {\n'
        '    if (rth_record_mode()) {\n'
        '        int32_t v[4] = { en, 0, 0, 0 };\n',
        'static void rtb_set_perspective_triangle(int en, float q0, float q1, float q2) {\n'
        '    if (rth_record_mode()) {\n'
        '        if (!en && s_rr_rec_off_pq) return;\n'
        '        s_rr_rec_off_pq = !en;\n'
        '        int32_t v[4] = { en, 0, 0, 0 };\n',
        'Perspective triangle setter')
    sub('        rth_rec_ints(RTH_PERSP, 0, 4, v);\n'
        '        return;\n'
        '    }\n'
        '    glb_set_perspective_triangle(en, q0, q1, q2);\n',
        '        rth_rec_ints(RTH_PERSP, 0, 4, v);\n'
        '        return;\n'
        '    }\n'
        '    s_rr_rec_off_pq = 0;\n'
        '    glb_set_perspective_triangle(en, q0, q1, q2);\n',
        'Perspective triangle direct path')
    # Graphics worker (gpu_geometry.py gpu_worker): a sync point hit on the worker thread
    # (rt_acquire) makes the GL context current THERE, and the frame boundary's release
    # runs on the emulation thread, so the context stayed on the worker and the render
    # thread waited for it for ever (boot hang). The worker hands it back itself before
    # it reports its queue empty.
    sub('int gl_renderer_render_thread_active(void) { return s_rth_on; }\n',
        'int gl_renderer_render_thread_active(void) { return s_rth_on; }\n'
        'void gl_renderer_worker_release(void) { if (s_rth_on && rt_held()) gl_rth_release(); }\n',
        'Worker context release')
    return source

# Traced lettering (HD UI text mode 6). native_ui.cpp rr_text_sdf_build traces
# the owner's anti-aliased font page into a distance-field atlas (fill and
# outline distances and levels, K=4 per texel) the first time a tagged text
# primitive samples that page with that CLUT; four pages are kept (2x2 slots of
# one RGBA8 texture, 16 MB).
# Any upload, draw, fill or copy over the page or its CLUT marks the slot
# stale; the next use rehashes the page and rebuilds only if it changed. Pages
# whose CLUT is not a level ramp fall back to the HD UI mode. The slot rides in
# the primitive's filter bits 5-6, so text from several pages shares a batch.
RR_TEXT_SDF = r'''
extern int rr_text_sdf_build(const uint16_t *vram, int px, int py, int cx, int cy, uint8_t *out, int k);
extern int rr_text_hd_apply(const uint16_t *vram, int px, int py, int cx, int cy, uint8_t *out, int k,
                            uint32_t *done, int *rect);
extern uint32_t rr_text_hd_serial(void);
void psx_mod_counter_add(const char *name, uint32_t delta);
enum { RR_SDF_K = 4, RR_SDF_SLOTS = 4, RR_SDF_W = 256 * RR_SDF_K };
/* hd: learned HD-font glyphs already drawn into buf (rr_text_hd_apply). */
typedef struct { int used, ok, stale, x, y, cx, cy; uint64_t hash; uint32_t last, hd; uint8_t *buf; } RrSdfSlot;
static RrSdfSlot s_rr_sdf[RR_SDF_SLOTS];
static GLuint s_rr_sdf_tex;
static GLint s_uRrSdf = -1;
static uint32_t s_rr_sdf_clock;
static int s_rr_tf;
/* Menu title banners (native_ui.cpp rr_banner_build): the 336x32 picture at
 * VRAM 512,480, recognised by its fingerprint, is replaced by one HD image
 * (premultiplied RGBA, 8 pixels per texel) that the banner sprites sample. */
extern uint64_t rr_banner_hash(const uint16_t *vram, int x, int y, int w, int h);
extern const char *rr_banner_text(uint64_t hash);
extern int rr_banner_build(const uint16_t *vram, int x, int y, int w, int h, const char *text, uint8_t *out, int s);
enum { RR_BAN_X = 512, RR_BAN_Y = 480, RR_BAN_W = 336, RR_BAN_H = 32, RR_BAN_S = 8 };
static struct { int stale, ok; uint64_t hash; } s_rr_ban = { 1, 0, 0 };
static GLuint s_rr_ban_tex;
static uint8_t *s_rr_ban_buf;
static GLint s_uRrBanner = -1, s_uRrBannerRect = -1;
static int s_rr_bf;
static void rr_sdf_touch(int x0, int y0, int x1, int y1) {
    if (x1 >= RR_BAN_X && x0 < RR_BAN_X + RR_BAN_W && y1 >= RR_BAN_Y && y0 < RR_BAN_Y + RR_BAN_H) s_rr_ban.stale = 1;
    for (int i = 0; i < RR_SDF_SLOTS; i++) {
        RrSdfSlot *s = &s_rr_sdf[i];
        if (!s->used || s->stale) continue;
        if ((x1 >= s->x && x0 <= s->x + 63 && y1 >= s->y && y0 <= s->y + 255) ||
            (x1 >= s->cx && x0 <= s->cx + 15 && y1 >= s->cy && y0 <= s->cy)) s->stale = 1;
    }
}
static uint64_t rr_sdf_hash(int x, int y, int cx, int cy) {
    uint64_t h = 14695981039346656037ull;
    for (int r = 0; r < 256; r++) {
        const uint16_t *p = s_vram + (size_t)(y + r) * VRAM_W + x;
        for (int c = 0; c < 64; c++) { h ^= p[c]; h *= 1099511628211ull; }
    }
    const uint16_t *p = s_vram + (size_t)cy * VRAM_W + cx;
    for (int c = 0; c < 16; c++) { h ^= p[c]; h *= 1099511628211ull; }
    return h;
}
static int rr_sdf_ready(void) {
    static int failed;
    if (s_rr_sdf_tex) return 1;
    if (failed) return 0;
    glGenTextures(1, &s_rr_sdf_tex);
    p_glActiveTexture(PSXGL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, s_rr_sdf_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2 * RR_SDF_W, 2 * RR_SDF_W, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    p_glActiveTexture(PSXGL_TEXTURE0);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &s_rr_sdf_tex); s_rr_sdf_tex = 0; failed = 1; return 0;
    }
    return 1;
}
/* Upload atlas pixels [x0,x1) x [y0,y1) of a slot's buffer. Queued text may
 * sample this slot, so it is drawn first. */
static void rr_sdf_upload(int slot, const uint8_t *buf, int x0, int y0, int x1, int y1) {
    flush_flat_batch(); flush_tex_batch(); hiw_flush_queue();
    p_glActiveTexture(PSXGL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, s_rr_sdf_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, RR_SDF_W);
    glTexSubImage2D(GL_TEXTURE_2D, 0, (slot & 1) * RR_SDF_W + x0, (slot >> 1) * RR_SDF_W + y0, x1 - x0, y1 - y0,
                    GL_RGBA, GL_UNSIGNED_BYTE, buf + ((size_t)y0 * RR_SDF_W + x0) * 4);
    glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    p_glActiveTexture(PSXGL_TEXTURE0);
}
/* HD font glyphs learned since the slot was last brought up to date. */
static void rr_sdf_hd(RrSdfSlot *s) {
    if (!s->ok || s->hd == rr_text_hd_serial()) return;
    int r[4];
    if (rr_text_hd_apply(s_vram, s->x, s->y, s->cx, s->cy, s->buf, RR_SDF_K, &s->hd, r))
        rr_sdf_upload((int)(s - s_rr_sdf), s->buf, r[0], r[1], r[2], r[3]);
}
static int rr_sdf_slot(int x, int y, int cx, int cy) {
    RrSdfSlot *s = NULL, *victim = NULL;
    for (int i = 0; i < RR_SDF_SLOTS; i++) {
        RrSdfSlot *t = &s_rr_sdf[i];
        if (t->used && t->x == x && t->y == y && t->cx == cx && t->cy == cy) { s = t; break; }
        if (!victim || (victim->used && (!t->used || t->last < victim->last))) victim = t;
    }
    const uint32_t now = ++s_rr_sdf_clock;
    if (s && !s->stale) { s->last = now; rr_sdf_hd(s); return s->ok ? (int)(s - s_rr_sdf) : -1; }
    if (!rr_sdf_ready()) return -1;
    const uint64_t h = rr_sdf_hash(x, y, cx, cy);
    if (s && s->hash == h) { s->stale = 0; s->last = now; rr_sdf_hd(s); return s->ok ? (int)(s - s_rr_sdf) : -1; }
    if (!s) s = victim;
    if (!s->buf && !(s->buf = (uint8_t *)malloc((size_t)RR_SDF_W * RR_SDF_W * 4))) return -1;
    const int slot = (int)(s - s_rr_sdf);
    const uint64_t t0 = SDL_GetPerformanceCounter();
    const int ok = rr_text_sdf_build(s_vram, x, y, cx, cy, s->buf, RR_SDF_K);
    s->used = 1; s->ok = ok; s->stale = 0; s->x = x; s->y = y; s->cx = cx; s->cy = cy; s->hash = h; s->last = now;
    s->hd = 0;
    if (ok) {
        int r[4];
        rr_text_hd_apply(s_vram, x, y, cx, cy, s->buf, RR_SDF_K, &s->hd, r);
        rr_sdf_upload(slot, s->buf, 0, 0, RR_SDF_W, RR_SDF_W);
    }
    psx_mod_counter_add(ok ? "rr.text.sdf_builds" : "rr.text.sdf_rejected", 1);
    if (getenv("RR_SDF_TRACE"))
        fprintf(stderr, "rr text sdf: page %d,%d clut %d,%d slot %d ok %d %.2f ms\n", x, y, cx, cy, slot, ok,
                (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
    return ok ? slot : -1;
}
/* Lettering drawn outside the text routine (the menus' "Select / Help" line):
 * a 4-bit primitive whose texel box native_ui has already paired with a
 * character (rr_text_hd_known) is drawn with the HD font too. */
extern int rr_text_hd_known(int tpage, int clut, int u, int v, int w, int h);
static int rr_hd_untagged(uint16_t texpage, int cx, int cy, const int *us, const int *vs, const int *lim) {
    if (((texpage >> 7) & 3) != 0 || rr_ui_text_filter() != 6) return 0;
    int u0, v0, u1, v1;
    if (lim) { u0 = lim[0]; v0 = lim[1]; u1 = lim[2] + 1; v1 = lim[3] + 1; }
    else {
        u0 = u1 = us[0]; v0 = v1 = vs[0];
        for (int i = 1; i < 3; i++) {
            if (us[i] < u0) u0 = us[i]; if (us[i] > u1) u1 = us[i];
            if (vs[i] < v0) v0 = vs[i]; if (vs[i] > v1) v1 = vs[i];
        }
    }
    return rr_text_hd_known(texpage, ((cy & 511) << 6) | ((cx >> 4) & 63), u0, v0, u1 - u0, v1 - v0);
}
/* Banner mode (7) for a 15-bit primitive inside the banner picture while it is
 * a known banner (rebuilt when anything writes over it). */
static int rr_banner_filter(uint16_t texpage, const int *us, const int *vs, const int *lim) {
    if (((texpage >> 7) & 3) != 2 || rr_ui_text_filter() != 6) return 0;
    int u0, v0, u1, v1;
    if (lim) { u0 = lim[0]; v0 = lim[1]; u1 = lim[2] + 1; v1 = lim[3] + 1; }
    else {
        u0 = u1 = us[0]; v0 = v1 = vs[0];
        for (int i = 1; i < 3; i++) {
            if (us[i] < u0) u0 = us[i]; if (us[i] > u1) u1 = us[i];
            if (vs[i] < v0) v0 = vs[i]; if (vs[i] > v1) v1 = vs[i];
        }
    }
    const int px = (texpage & 15) * 64, py = ((texpage >> 4) & 1) * 256;
    if (px + u0 < RR_BAN_X || py + v0 < RR_BAN_Y || px + u1 > RR_BAN_X + RR_BAN_W || py + v1 > RR_BAN_Y + RR_BAN_H) return 0;
    flush_cpu_upload();   /* a pending upload of the picture marks it stale first */
    if (s_rr_ban.stale) {
        const uint64_t h = rr_banner_hash(s_vram, RR_BAN_X, RR_BAN_Y, RR_BAN_W, RR_BAN_H);
        s_rr_ban.stale = 0;
        if (h != s_rr_ban.hash) {
            s_rr_ban.hash = h; s_rr_ban.ok = 0;
            const char *text = rr_banner_text(h);
            const int W = RR_BAN_W * RR_BAN_S, H = RR_BAN_H * RR_BAN_S;
            if (text && !s_rr_ban_buf) s_rr_ban_buf = (uint8_t *)malloc((size_t)W * H * 4);
            if (text && s_rr_ban_buf &&
                rr_banner_build(s_vram, RR_BAN_X, RR_BAN_Y, RR_BAN_W, RR_BAN_H, text, s_rr_ban_buf, RR_BAN_S)) {
                flush_flat_batch(); flush_tex_batch(); hiw_flush_queue();
                p_glActiveTexture(PSXGL_TEXTURE0 + 4);
                if (!s_rr_ban_tex) {
                    glGenTextures(1, &s_rr_ban_tex);
                    glBindTexture(GL_TEXTURE_2D, s_rr_ban_tex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                }
                glBindTexture(GL_TEXTURE_2D, s_rr_ban_tex);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, 0);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_rr_ban_buf);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                p_glActiveTexture(PSXGL_TEXTURE0);
                s_rr_ban.ok = 1;
            }
            if (getenv("RR_SDF_TRACE"))
                fprintf(stderr, "rr banner: %016llx %s\n", (unsigned long long)h, s_rr_ban.ok ? text : "(original)");
        }
    }
    return s_rr_ban.ok ? 7 : 0;
}
/* Filter for lettering tagged by native_ui: 6 | slot << 5 when its 4-bit page
 * traces as a coverage font, otherwise the HD UI mode. */
static int rr_text_filter(uint16_t texpage, int cx, int cy) {
    const int f = rr_ui_text_filter();
    if (f != 6) return f;
    if (((texpage >> 7) & 3) != 0) return rr_ui_texture_filter();
    flush_cpu_upload();   /* pending uploads mark the page stale first */
    const int s = rr_sdf_slot((texpage & 15) * 64, ((texpage >> 4) & 1) * 256, cx & 1008, cy & 511);
    return s < 0 ? rr_ui_texture_filter() : 6 | (s << 5);
}
'''
def text_sdf(source):
    def sub(old,new,what):
        nonlocal source
        if source.count(old)!=1:raise ValueError(what+' changed')
        source=source.replace(old,new)
    sub('    s_uHdTexture = p_glGetUniformLocation(s_tex_prog, "u_hd_texture");\n',
        '    s_uHdTexture = p_glGetUniformLocation(s_tex_prog, "u_hd_texture");\n'
        '    s_uRrSdf = p_glGetUniformLocation(s_tex_prog, "u_rr_sdf");\n'
        '    s_uRrBanner = p_glGetUniformLocation(s_tex_prog, "u_rr_banner");\n'
        '    s_uRrBannerRect = p_glGetUniformLocation(s_tex_prog, "u_rr_banner_rect");\n','Texture uniform interface')
    sub('    glBindTexture(GL_TEXTURE_2D, hd ? hd : s_up_tex); p_glUniform1i(s_uHdTexture, 2);\n',
        '    glBindTexture(GL_TEXTURE_2D, hd ? hd : s_up_tex); p_glUniform1i(s_uHdTexture, 2);\n'
        '    p_glActiveTexture(PSXGL_TEXTURE0 + 3);\n'
        '    glBindTexture(GL_TEXTURE_2D, s_rr_sdf_tex); if (s_uRrSdf >= 0) p_glUniform1i(s_uRrSdf, 3);\n'
        '    p_glActiveTexture(PSXGL_TEXTURE0 + 4);\n'
        '    glBindTexture(GL_TEXTURE_2D, s_rr_ban_tex); if (s_uRrBanner >= 0) p_glUniform1i(s_uRrBanner, 4);\n'
        '    if (s_uRrBannerRect >= 0) p_glUniform4f(s_uRrBannerRect, RR_BAN_X, RR_BAN_Y, RR_BAN_W, RR_BAN_H);\n',
        'Textured resource binding interface')
    # Menu title banners (mode 7) sample the HD banner image instead.
    sub('    "  } else if (fmode >= 3 && fmode <= 6) {\\n"\n',
        '    "  } else if (fmode == 7) {\\n"\n'
        '    "    vec4 bc = rr_banner(uv, stp);\\n"\n'
        '    "    if (bc.a < 0.5) discard;\\n"\n'
        '    "    rgb = bc.rgb;\\n"\n'
        '    "  } else if (fmode >= 3 && fmode <= 6) {\\n"\n','HD UI banner interface')
    # Edges anti-aliased into the background, like a font drawn at the output
    # resolution: traced lettering joins the renderer's dual-source batch (the
    # semi key 4 its HLE tier uses for opaque + modes 0/1/3 in painter order),
    # where each fragment sets how much of the background stays (rr_cov). Under
    # mask checking (or blend mode 2) lettering keeps its own batch key and the
    # shader cuts the edge hard instead.
    sub('        const int batch_semi = batch_class.key;\n'
        '        const int isolate = batch_class.isolate;\n',
        '        int batch_semi = batch_class.key;\n'
        '        int isolate = batch_class.isolate;\n'
        '        if (((filter & 15) == 6 || (filter & 15) == 7) && !s_mask_check && semi != 2) { batch_semi = 4; isolate = 0; }\n',
        'Textured batch key interface')
    sub('    "    if (v_semi == 1) rgb *= 0.5; else if (v_semi == 4) rgb *= 0.25;\\n"\n'
        '    "  }\\n"\n'
        '    "  frag = vec4(rgb, (stp == 1 || u_maskset == 1) ? 1.0 : 0.0);\\n"\n',
        '    "    if (v_semi == 1) rgb *= 0.5; else if (v_semi == 4) rgb *= 0.25;\\n"\n'
        '    "  }\\n"\n'
        '    "  if (rr_cov < 1.0) { rgb *= rr_cov; dst_factor = 1.0 - rr_cov + rr_cov * dst_factor; }\\n"\n'
        '    "  frag = vec4(rgb, (stp == 1 || u_maskset == 1) ? 1.0 : 0.0);\\n"\n',
        'Texture blend output interface')
    return source

# HD UI for pictures the game writes straight into the frame buffer. The
# loading-screen course map and the animated menu pictures are MDEC images that
# the game LoadImages column by column (16-pixel strips) into the displayed
# buffer; no primitive ever samples them, so the high-resolution surface only
# received each texel as an S x S block. Large 15-bit uploads inside the display
# area are now also drawn upscaled (clamped Catmull-Rom: smooth, no ringing, no
# blocks) from the uploaded image itself. A 1x mask tracks which texels still
# hold uploaded image data (any draw, fill or copy over them clears it), so the
# filter reaches across strip borders (no seams) and never touches drawn content.
# Each S x S block's first sample is the texel itself, so the native readback of
# the surface (pack: the block's top-left sample) is unchanged. Only the owner's
# disc data is filtered at run time; nothing is stored or shipped.
IMAGE_UPLOADS = r'''
void psx_mod_counter_add(const char *name, uint32_t delta);
static uint8_t s_rr_img[VRAM_W * VRAM_H];
static uint8_t s_rr_img_tile[(VRAM_W / 32) * (VRAM_H / 32)];
static int s_rr_img_tiles;
static GLuint s_rr_img_tex, s_rr_img_prog;
static GLint s_rr_img_uSrc = -1, s_rr_img_uOwn = -1, s_rr_img_uScale = -1, s_rr_img_uPass = -1;
static const char *RR_IMG_FS =
    "#version 330\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_src;   /* uploaded image texels (RGBA8, a = bit15) */\n"
    "uniform sampler2D u_own;   /* 1 = texel still holds uploaded image data */\n"
    "uniform int u_scale;\n"
    "uniform int u_stp_pass;\n"
    "vec4 cr(float t){ float t2=t*t, t3=t2*t;\n"
    "  return vec4(-0.5*t3+t2-0.5*t, 1.5*t3-2.5*t2+1.0, -1.5*t3+2.0*t2+0.5*t, 0.5*t3-0.5*t2); }\n"
    "void main(){\n"
    "  ivec2 f = ivec2(gl_FragCoord.xy), c = f / u_scale;\n"
    "  if (texelFetch(u_own, c, 0).r < 0.5) discard;\n"
    "  vec4 e = texelFetch(u_src, c, 0);\n"
    "  bool stp = e.a >= 0.5;\n"
    "  if (u_stp_pass == 1 && stp) discard;\n"
    "  if (u_stp_pass == 2 && !stp) discard;\n"
    "  vec2 t = vec2(f - c * u_scale) / float(u_scale);   /* 0: the texel itself */\n"
    "  vec4 wx = cr(t.x), wy = cr(t.y);\n"
    "  vec3 sum = vec3(0.0), lo = e.rgb, hi = e.rgb;\n"
    "  ivec2 top = textureSize(u_src, 0) - 1;\n"
    "  for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) {\n"
    "    ivec2 q = clamp(c + ivec2(i - 1, j - 1), ivec2(0), top);\n"
    "    vec3 s = texelFetch(u_own, q, 0).r > 0.5 ? texelFetch(u_src, q, 0).rgb : e.rgb;\n"
    "    sum += s * (wx[i] * wy[j]);\n"
    "    if (i >= 1 && i <= 2 && j >= 1 && j <= 2) { lo = min(lo, s); hi = max(hi, s); }\n"
    "  }\n"
    "  frag = vec4(clamp(sum, lo, hi), stp ? 1.0 : 0.0);\n"
    "}\n";
/* Mark (v=1) or clear (v=0) image texels. Tiles of 32x32 skip clears where no
 * image was ever marked, so race frames pay nothing. */
static void rr_img_mark(int x0, int y0, int x1, int y1, uint8_t v) {
    rr_sdf_touch(x0, y0, x1, y1);   /* every upload, draw, fill and copy passes here */
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > VRAM_W - 1) x1 = VRAM_W - 1; if (y1 > VRAM_H - 1) y1 = VRAM_H - 1;
    if (x1 < x0 || y1 < y0 || (!v && !s_rr_img_tiles)) return;
    for (int ty = y0 >> 5; ty <= y1 >> 5; ty++)
        for (int tx = x0 >> 5; tx <= x1 >> 5; tx++) {
            uint8_t *t = &s_rr_img_tile[ty * (VRAM_W / 32) + tx];
            if (!v && !*t) continue;
            const int ax = x0 > tx * 32 ? x0 : tx * 32, bx = x1 < tx * 32 + 31 ? x1 : tx * 32 + 31;
            const int ay = y0 > ty * 32 ? y0 : ty * 32, by = y1 < ty * 32 + 31 ? y1 : ty * 32 + 31;
            for (int y = ay; y <= by; y++) memset(&s_rr_img[(size_t)y * VRAM_W + ax], v, (size_t)(bx - ax + 1));
            if (v && !*t) { *t = 1; s_rr_img_tiles++; }
            else if (!v && ax == tx * 32 && bx == tx * 32 + 31 && ay == ty * 32 && by == ty * 32 + 31) {
                *t = 0; s_rr_img_tiles--;
            }
        }
}
static void rr_img_clear(int x0, int y0, int x1, int y1) { rr_img_mark(x0, y0, x1, y1, 0); }
static int rr_img_ready(void) {
    static int failed;
    if (s_rr_img_prog) return 1;
    if (failed) return 0;
    s_rr_img_prog = build_program(BLIT_VS, RR_IMG_FS);
    if (!s_rr_img_prog) { failed = 1; return 0; }
    s_rr_img_uSrc = p_glGetUniformLocation(s_rr_img_prog, "u_src");
    s_rr_img_uOwn = p_glGetUniformLocation(s_rr_img_prog, "u_own");
    s_rr_img_uScale = p_glGetUniformLocation(s_rr_img_prog, "u_scale");
    s_rr_img_uPass = p_glGetUniformLocation(s_rr_img_prog, "u_stp_pass");
    glGenTextures(1, &s_rr_img_tex);
    glBindTexture(GL_TEXTURE_2D, s_rr_img_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x8229 /* GL_R8 */, VRAM_W, VRAM_H, 0, 0x1903 /* GL_RED */,
                 GL_UNSIGNED_BYTE, NULL);
    return 1;
}
/* A picture: a large rect inside the display columns (texture pages, CLUTs and
 * small sprite patches stay as they are). */
static int rr_img_picture(int x0, int y0, int x1, int y1) {
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    return w >= 8 && h >= 8 && w * h >= 256 && x1 < 640;
}
/* Menu pictures in a stretched widescreen screen (native_ui.cpp
 * rr_ui_picture_at): their uploads skip the plain blit and are redrawn
 * narrowed about the picture's centre (rr_pic_draw), so a picture keeps its
 * original proportions like the labels and titles. The sides of the stretched
 * panel keep what the pass drew there. */
extern int rr_ui_picture_at(int, int, int, int, int *, int *, int *, int *, float *);
static uint8_t rr_pic_skip[UP_RECTS_MAX];
typedef struct { int x0, y0, x1, y1; float f; } RrPic;
static RrPic rr_pics[8];
static int rr_npics;
static GLuint s_rr_pic_prog;
static GLint s_rr_pic_u[7];
static const char *RR_PIC_FS =
    "#version 330\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_src;\n"
    "uniform int u_scale;\n"
    "uniform int u_stp_pass;\n"
    "uniform vec4 u_reg;     /* picture rect, VRAM px, x1/y1 exclusive */\n"
    "uniform float u_cx;     /* narrowing anchor: the picture's centre */\n"
    "uniform float u_f;      /* narrowing factor (3/4 at 16:9, 4/7 at 21:9) */\n"
    "uniform int u_smooth;   /* HD UI: clamped Catmull-Rom, else nearest */\n"
    "vec4 cr(float t){ float t2=t*t, t3=t2*t;\n"
    "  return vec4(-0.5*t3+t2-0.5*t, 1.5*t3-2.5*t2+1.0, -1.5*t3+2.0*t2+0.5*t, 0.5*t3-0.5*t2); }\n"
    "void main(){\n"
    "  vec2 sp = gl_FragCoord.xy / float(u_scale);\n"
    "  sp.x = u_cx + (sp.x - u_cx) / u_f;\n"
    "  if (sp.x < u_reg.x || sp.x >= u_reg.z) discard;\n"
    "  ivec2 lo = ivec2(u_reg.xy), hi = ivec2(u_reg.zw) - 1;\n"
    "  vec4 e = texelFetch(u_src, clamp(ivec2(floor(sp)), lo, hi), 0);\n"
    "  bool stp = e.a >= 0.5;\n"
    "  if (u_stp_pass == 1 && stp) discard;\n"
    "  if (u_stp_pass == 2 && !stp) discard;\n"
    "  vec3 rgb = e.rgb;\n"
    "  if (u_smooth != 0) {\n"
    "    vec2 p = sp - 0.5; ivec2 b = ivec2(floor(p)); vec2 t = p - vec2(b);\n"
    "    vec4 wx = cr(t.x), wy = cr(t.y);\n"
    "    vec3 sum = vec3(0.0), mn = vec3(1.0), mx = vec3(0.0);\n"
    "    for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) {\n"
    "      vec3 s = texelFetch(u_src, clamp(b + ivec2(i - 1, j - 1), lo, hi), 0).rgb;\n"
    "      sum += s * (wx[i] * wy[j]);\n"
    "      if (i >= 1 && i <= 2 && j >= 1 && j <= 2) { mn = min(mn, s); mx = max(mx, s); }\n"
    "    }\n"
    "    rgb = clamp(sum, mn, mx);\n"
    "  }\n"
    "  frag = vec4(rgb, stp ? 1.0 : 0.0);\n"
    "}\n";
static void rr_pic_classify(const DirtyRect *rects, int nrects) {
    rr_npics = 0;
    for (int i = 0; i < nrects; i++) {
        RrPic p;
        rr_pic_skip[i] = 0;
        if (s_hr_scale < 1 || !rr_ui_picture_at(rects[i].x0, rects[i].y0, rects[i].x1, rects[i].y1,
                                                &p.x0, &p.y0, &p.x1, &p.y1, &p.f)) continue;
        rr_pic_skip[i] = 1;
        int k = 0;
        while (k < rr_npics && (rr_pics[k].x0 != p.x0 || rr_pics[k].y0 != p.y0 ||
                                rr_pics[k].x1 != p.x1 || rr_pics[k].y1 != p.y1)) k++;
        if (k == rr_npics && rr_npics < 8) rr_pics[rr_npics++] = p;
    }
}
static void rr_pic_draw(void) {
    if (!rr_npics) return;
    if (!s_rr_pic_prog) {
        static int failed;
        if (failed) return;
        s_rr_pic_prog = build_program(BLIT_VS, RR_PIC_FS);
        if (!s_rr_pic_prog) { failed = 1; return; }
        const char *names[7] = { "u_src", "u_scale", "u_stp_pass", "u_reg", "u_cx", "u_f", "u_smooth" };
        for (int i = 0; i < 7; i++) s_rr_pic_u[i] = p_glGetUniformLocation(s_rr_pic_prog, names[i]);
    }
    const int S = s_hr_scale;
    hr_begin(0);
    glDisable(GL_BLEND);
    p_glUseProgram(s_rr_pic_prog);
    p_glActiveTexture(PSXGL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_up_tex);
    p_glUniform1i(s_rr_pic_u[0], 0);
    p_glUniform1i(s_rr_pic_u[1], S);
    p_glUniform1i(s_rr_pic_u[6], rr_ui_texture_filter() >= 0 && S > 1);
    p_glBindVertexArray(s_blit_vao);
    p_glBindBuffer(PSXGL_ARRAY_BUFFER, s_blit_vbo);
    for (int k = 0; k < rr_npics; k++) {
        const RrPic *p = &rr_pics[k];
        p_glUniform4f(s_rr_pic_u[3], (float)p->x0, (float)p->y0, (float)p->x1, (float)p->y1);
        p_glUniform1f(s_rr_pic_u[4], 0.5f * (float)(p->x0 + p->x1));
        p_glUniform1f(s_rr_pic_u[5], p->f);
        glScissor(p->x0 * S, p->y0 * S, (p->x1 - p->x0) * S, (p->y1 - p->y0) * S);
        const float fx0 = (float)p->x0, fy0 = (float)p->y0, fx1 = (float)p->x1, fy1 = (float)p->y1;
        const float verts[12] = { fx0, fy0, fx1, fy0, fx0, fy1, fx1, fy0, fx0, fy1, fx1, fy1 };
        p_glBufferData(PSXGL_ARRAY_BUFFER, sizeof verts, verts, PSXGL_STREAM_DRAW);
        plain_stencil(0); p_glUniform1i(s_rr_pic_u[2], 1); glDrawArrays(GL_TRIANGLES, 0, 6);
        plain_stencil(1); p_glUniform1i(s_rr_pic_u[2], 2); glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    hr_end();
    psx_mod_counter_add("rr.ui.narrowed_pictures", (uint32_t)rr_npics);
}
static void rr_img_uploads(const DirtyRect *rects, int nrects) {
    const int on = rr_ui_texture_filter() >= 0 && s_hr_scale > 1;
    int any = 0;
    for (int i = 0; i < nrects; i++) {
        const int pic = on && !rr_pic_skip[i] && rr_img_picture(rects[i].x0, rects[i].y0, rects[i].x1, rects[i].y1);
        rr_img_mark(rects[i].x0, rects[i].y0, rects[i].x1, rects[i].y1, pic ? 0xFF : 0);   /* R8: 255 = 1.0 */
        any |= pic;
    }
    if (!any || !rr_img_ready()) return;
    const int S = s_hr_scale;
    p_glActiveTexture(PSXGL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, s_rr_img_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, VRAM_W);
    for (int i = 0; i < nrects; i++) {
        if (rr_pic_skip[i] || !rr_img_picture(rects[i].x0, rects[i].y0, rects[i].x1, rects[i].y1)) continue;
        int x0 = rects[i].x0 - 2, y0 = rects[i].y0 - 2, x1 = rects[i].x1 + 2, y1 = rects[i].y1 + 2;
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
        if (x1 > VRAM_W - 1) x1 = VRAM_W - 1; if (y1 > VRAM_H - 1) y1 = VRAM_H - 1;
        glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x1 - x0 + 1, y1 - y0 + 1, 0x1903, GL_UNSIGNED_BYTE,
                        &s_rr_img[(size_t)y0 * VRAM_W + x0]);
    }
    glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    hr_begin(0);
    glDisable(GL_BLEND);
    p_glUseProgram(s_rr_img_prog);
    p_glActiveTexture(PSXGL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_up_tex);
    p_glUniform1i(s_rr_img_uSrc, 0);
    p_glUniform1i(s_rr_img_uOwn, 1);
    p_glUniform1i(s_rr_img_uScale, S);
    p_glBindVertexArray(s_blit_vao);
    p_glBindBuffer(PSXGL_ARRAY_BUFFER, s_blit_vbo);
    for (int i = 0; i < nrects; i++) {
        if (rr_pic_skip[i] || !rr_img_picture(rects[i].x0, rects[i].y0, rects[i].x1, rects[i].y1)) continue;
        /* Two texels around the rect too: neighbouring strips re-filter their
         * edge with this strip's texels (the mask keeps drawn content out). */
        int x0 = rects[i].x0 - 2, y0 = rects[i].y0 - 2, x1 = rects[i].x1 + 2, y1 = rects[i].y1 + 2;
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
        if (x1 > VRAM_W - 1) x1 = VRAM_W - 1; if (y1 > VRAM_H - 1) y1 = VRAM_H - 1;
        glScissor(x0 * S, y0 * S, (x1 - x0 + 1) * S, (y1 - y0 + 1) * S);
        const float fx0 = (float)x0, fy0 = (float)y0, fx1 = (float)(x1 + 1), fy1 = (float)(y1 + 1);
        const float verts[12] = { fx0, fy0, fx1, fy0, fx0, fy1, fx1, fy0, fx0, fy1, fx1, fy1 };
        p_glBufferData(PSXGL_ARRAY_BUFFER, sizeof verts, verts, PSXGL_STREAM_DRAW);
        plain_stencil(0); p_glUniform1i(s_rr_img_uPass, 1); glDrawArrays(GL_TRIANGLES, 0, 6);
        plain_stencil(1); p_glUniform1i(s_rr_img_uPass, 2); glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    p_glActiveTexture(PSXGL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, 0);
    p_glActiveTexture(PSXGL_TEXTURE0);
    hr_end();
    psx_mod_counter_add("rr.ui.upscaled_uploads", 1);
}
'''
RR_VID = r'''
vec4 rr_vid(vec2 uv){
  vec4 c = bicubic(uv);
  vec2 ts = 1.0 / u_tex_size;
  vec2 b = (floor(uv * u_tex_size - 0.5) + 0.5) * ts;
  vec4 t00 = texture(u_tex, b), t10 = texture(u_tex, b + vec2(ts.x, 0.0));
  vec4 t01 = texture(u_tex, b + vec2(0.0, ts.y)), t11 = texture(u_tex, b + ts);
  vec4 lo = min(min(t00, t10), min(t01, t11)), hi = max(max(t00, t10), max(t01, t11));
  vec4 blur = 0.25 * (texture(u_tex, uv + vec2(-0.75, -0.75) * ts) + texture(u_tex, uv + vec2(0.75, -0.75) * ts)
                    + texture(u_tex, uv + vec2(-0.75, 0.75) * ts) + texture(u_tex, uv + vec2(0.75, 0.75) * ts));
  return clamp(c + 0.5 * (c - blur), lo, hi);
}
'''

def _glsl(text):
    """GLSL block -> lines of the C string literal the shaders are written as."""
    out = ''
    for line in text.strip('\n').split('\n'):
        out += '    "' + line.replace('\\', '\\\\').replace('"', '\\"') + '\\n"\n'
    return out

# HD UI for lettering and CLUT art: xBR level 2 (Hyllian's xBR-lv2, MIT; the
# runtime's own simplified pass only cut 45-degree corners). Level 2 also
# rounds 2:1 and 1:2 slopes, so curves in the owner's font (S, o, a) lose their
# staircase. Transparency takes part in the edge test (a transparent texel is
# far from every colour), so a glyph's silhouette follows the smoothed edge
# instead of the texel grid; the cut stays binary (PS1 cutout, no blur).
# Colours mix premultiplied, so a transparent neighbour never darkens an edge.
# Evaluated per output pixel at any scale: the blend band is one output pixel.
XBR_LV2 = r'''
float rr_y(int r){ return r == 0 ? -400.0 : dot(col5(r), vec3(0.299, 0.587, 0.114)) * 48.0; }
vec4 rr_pm(int r){ return r == 0 ? vec4(0.0) : vec4(col5(r), 1.0); }
int rr_t(ivec2 p, int x, int y){ return stable_texel(p + ivec2(x, y)); }
vec4 rr_wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h){
  return abs(a-b) + abs(a-c) + abs(d-e) + abs(d-f) + 4.0*abs(g-h);
}
vec4 rr_eq(vec4 a, vec4 b){ return vec4(lessThan(abs(a-b), vec4(15.0))); }
vec4 rr_xbr2(vec2 uv, float dl, int E, out int stp){
  ivec2 p = ivec2(floor(uv)); vec2 fp = fract(uv);
  int A1 = rr_t(p,-1,-2), B1 = rr_t(p,0,-2), C1 = rr_t(p,1,-2);
  int A0 = rr_t(p,-2,-1), A = rr_t(p,-1,-1), B = rr_t(p,0,-1), C = rr_t(p,1,-1), C4 = rr_t(p,2,-1);
  int D0 = rr_t(p,-2,0),  D = rr_t(p,-1,0),                     F = rr_t(p,1,0),  F4 = rr_t(p,2,0);
  int G0 = rr_t(p,-2,1),  G = rr_t(p,-1,1),  H = rr_t(p,0,1),  I = rr_t(p,1,1),  I4 = rr_t(p,2,1);
  int G5 = rr_t(p,-1,2), H5 = rr_t(p,0,2), I5 = rr_t(p,1,2);
  vec4 b = vec4(rr_y(B), rr_y(D), rr_y(H), rr_y(F));
  vec4 c = vec4(rr_y(C), rr_y(A), rr_y(G), rr_y(I));
  vec4 e = vec4(rr_y(E));
  vec4 d = b.yzwx, f = b.wxyz, g = c.zwxy, h = b.zwxy, i = c.wxyz;
  vec4 i4 = vec4(rr_y(I4), rr_y(C1), rr_y(A0), rr_y(G5));
  vec4 i5 = vec4(rr_y(I5), rr_y(C4), rr_y(A1), rr_y(G0));
  vec4 h5 = vec4(rr_y(H5), rr_y(F4), rr_y(B1), rr_y(D0));
  vec4 f4 = h5.yzwx;
  ivec4 rb = ivec4(B, D, H, F), rc = ivec4(C, A, G, I), re = ivec4(E);
  ivec4 rd = rb.yzwx, rf = rb.wxyz, rg = rc.zwxy, rh = rb.zwxy;
  vec4 lv0 = vec4(notEqual(re, rf)) * vec4(notEqual(re, rh));
  vec4 lv1 = lv0 * clamp((1.0 - rr_eq(f, b)) * (1.0 - rr_eq(h, d))
                         + rr_eq(e, i) * (1.0 - rr_eq(f, i4)) * (1.0 - rr_eq(h, i5))
                         + rr_eq(e, g) + rr_eq(e, c), 0.0, 1.0);
  vec4 lv2l = vec4(notEqual(re, rg)) * vec4(notEqual(rd, rg));
  vec4 lv2u = vec4(notEqual(re, rc)) * vec4(notEqual(rb, rc));
  vec4 wd1 = rr_wd(e, c, g, i, h5, f4, h, f), wd2 = rr_wd(h, d, i5, f, i4, b, e, i);
  vec4 edri = vec4(lessThanEqual(wd1, wd2)) * lv0;
  vec4 edr  = vec4(lessThan(wd1, wd2)) * lv1;
  vec4 edrl = vec4(lessThanEqual(2.0*abs(f-g), abs(h-c))) * lv2l * edr;
  vec4 edru = vec4(greaterThanEqual(abs(f-g), 2.0*abs(h-c))) * lv2u * edr;
  vec4 fx  = vec4(1.0,-1.0,-1.0,1.0)*fp.y + vec4(1.0,1.0,-1.0,-1.0)*fp.x;
  vec4 fxl = vec4(1.0,-1.0,-1.0,1.0)*fp.y + vec4(0.5,2.0,-0.5,-2.0)*fp.x;
  vec4 fxu = vec4(1.0,-1.0,-1.0,1.0)*fp.y + vec4(2.0,0.5,-2.0,-0.5)*fp.x;
  vec4 d4 = vec4(dl), dL = vec4(0.5*dl, dl, 0.5*dl, dl), dU = dL.yxwz;
  vec4 Co = vec4(1.5,0.5,-0.5,0.5), Cx = vec4(1.0,1.0,-0.5,0.0), Cy = vec4(2.0,0.0,-1.0,0.5);
  vec4 fx45i = edri * clamp((fx + d4 - Co - 0.25)/(2.0*d4), 0.0, 1.0);
  vec4 fx45  = edr  * clamp((fx + d4 - Co)/(2.0*d4), 0.0, 1.0);
  vec4 fx30  = edrl * clamp((fxl + dL - Cx)/(2.0*dL), 0.0, 1.0);
  vec4 fx60  = edru * clamp((fxu + dU - Cy)/(2.0*dU), 0.0, 1.0);
  vec4 m = max(max(fx30, fx60), max(fx45, fx45i));
  bvec4 px = lessThanEqual(abs(e-f), abs(e-h));
  int nx = px.x ? F : H, ny = px.y ? B : F, nz = px.z ? D : B, nw = px.w ? H : D;
  vec4 pe = rr_pm(E);
  vec4 r1 = mix(mix(pe, rr_pm(nx), m.x), rr_pm(nz), m.z);
  vec4 r2 = mix(mix(pe, rr_pm(ny), m.y), rr_pm(nw), m.w);
  vec4 d1 = abs(pe - r1), d2 = abs(pe - r2);
  bool use2 = d2.r + d2.g + d2.b + 3.0*d2.a > d1.r + d1.g + d1.b + 3.0*d1.a;
  vec4 r = use2 ? r2 : r1;
  int dom = E;
  float best = 0.5;
  if (use2) { if (m.y >= best) { dom = ny; best = m.y; } if (m.w >= best) dom = nw; }
  else      { if (m.x >= best) { dom = nx; best = m.x; } if (m.z >= best) dom = nz; }
  if (dom == 0) dom = E;
  stp = (dom >> 15) & 1;
  return vec4(r.a > 0.0 ? r.rgb / r.a : vec3(0.0), r.a);
}
vec4 rr_cubic_a(vec2 uv, int E, out int stp){
  vec2 p = uv - 0.5; ivec2 b = ivec2(floor(p)); vec2 f = p - vec2(b);
  vec4 wx = rr_cr4(f.x), wy = rr_cr4(f.y);
  vec4 sum = vec4(0.0); vec3 lo = vec3(1.0), hi = vec3(0.0);
  int dom = E; float best = -1.0;
  for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) {
    int r = stable_texel(b + ivec2(i - 1, j - 1));
    float w = wx[i] * wy[j];
    sum += rr_pm(r) * w;
    if (i >= 1 && i <= 2 && j >= 1 && j <= 2 && r != 0) {
      lo = min(lo, col5(r)); hi = max(hi, col5(r));
      if (w > best) { best = w; dom = r; }
    }
  }
  stp = (dom >> 15) & 1;
  return vec4(sum.a > 0.001 ? clamp(sum.rgb / sum.a, lo, hi) : vec3(0.0), sum.a);
}
vec4 rr_text_vec(vec2 uv, int E, out int stp){
  vec2 p = uv - 0.5; ivec2 b = ivec2(floor(p)); vec2 f = p - vec2(b);
  vec4 wx = rr_cr4(f.x), wy = rr_cr4(f.y);
  int r[16]; float llo = 1e9, lhi = -1e9;
  for (int k = 0; k < 16; k++) {
    r[k] = stable_texel(b + ivec2(k % 4 - 1, k / 4 - 1));
    if (r[k] != 0) { float l = dot(col5(r[k]), vec3(0.299, 0.587, 0.114)); llo = min(llo, l); lhi = max(lhi, l); }
  }
  /* Per texel, relative to the brightest (fill) texel around: fill (>= 85%),
   * anti-aliasing (40-85%: partial coverage of the fill colour, so it shapes
   * the edge instead of painting grey), or a real dark outline (< 40%). */
  float A = 0.0, T = 0.0, wf = 0.0, wo = 0.0, best = -1.0;
  vec3 cf = vec3(0.0), co = vec3(0.0);
  int dom = E;
  for (int k = 0; k < 16; k++) {
    if (r[k] == 0) continue;
    int i = k % 4, j = k / 4;
    float w = wx[i] * wy[j], wp = max(w, 0.0);
    vec3 c = col5(r[k]);
    float rel = lhi > 0.02 ? dot(c, vec3(0.299, 0.587, 0.114)) / lhi : 1.0;
    float cov = 1.0, t = 1.0;
    if (rel < 0.40) t = 0.0;
    else if (rel < 0.85) cov = rel;
    A += w * cov; T += w * cov * t;
    if (rel >= 0.85) { cf += wp * c; wf += wp; }
    else if (rel < 0.40) { co += wp * c; wo += wp; }
    if (w > best) { best = w; dom = r[k]; }
  }
  stp = (dom >> 15) & 1;
  if (A < 0.5) return vec4(0.0);
  float tf = T / A;
  float aa = clamp(fwidth(tf), 0.002, 0.5);
  vec3 fill = wf > 0.0 ? cf / wf : col5(dom);
  vec3 outl = wo > 0.0 ? co / wo : fill;
  return vec4(mix(outl, fill, smoothstep(0.5 - aa, 0.5 + aa, tf)), 1.0);
}
uniform sampler2D u_rr_sdf;   /* traced lettering atlas (text_sdf): RB distances, GA levels */
float rr_cov = 1.0;           /* traced lettering edge coverage, blended in the dual-source pass */
/* Atlas lettering (HD font or traced), drawn like a font rendered at the output
 * resolution: the fill level inside the fill outline; then the ring level out
 * to the outer edge (outlined fonts: their outline ring; HD font on coverage
 * pages: a thin dark edge) and for outlined fonts their half-strength dark
 * shadow rim beyond that;
 * the outer edge anti-aliased over one output pixel (px: texels per pixel)
 * into the background. Without the dual-source pass (mask checking) the edge
 * is cut hard at its centre. The sample clamps to the glyph's own texel box
 * and needs a glyph texel of this primitive around it, so a neighbouring glyph
 * in the page never shows. Colours come from the primitive's own CLUT. */
vec4 rr_text_sdf(vec2 uv, float px, out int stp){
  stp = 0; rr_cov = 1.0;
  int slot = (v_filter >> 5) & 3;
  vec2 q = clamp(uv, vec2(v_limits.xy), vec2(v_limits.zw) + 0.999);
  vec2 f = vec2(float(slot & 1), float(slot >> 1)) * 1024.0 + q * 4.0;
  vec2 d = texture(u_rr_sdf, f / 2048.0).rb * (255.0 / 63.5) - 128.0 / 63.5;   /* +-2 texels */
  float w = max(px, 0.02);
  float a = clamp(d.y / w + 0.5, 0.0, 1.0), s = clamp((d.y + 0.7) / w + 0.5, 0.0, 1.0);
  if (u_semimode != 4) { a = a >= 0.5 ? 1.0 : 0.0; s = 0.0; }
  if (a <= 0.0 && s <= 0.0) return vec4(0.0);
  /* G: fill level, bit 6 an HD font glyph (its shape need not follow the
   * original texels), bit 7 an outlined font (shadow rim). A: ring level. */
  vec4 lv = texelFetch(u_rr_sdf, ivec2(floor(f)), 0) * 255.0 + 0.5;
  int g = int(lv.g);
  if ((g & 64) == 0) {
    ivec2 b = ivec2(floor(q - 0.5));
    if (stable_texel(b) == 0 && stable_texel(b + ivec2(1, 0)) == 0 &&
        stable_texel(b + ivec2(0, 1)) == 0 && stable_texel(b + ivec2(1, 1)) == 0) return vec4(0.0);
  }
  int cf = palette_at(v_clut.x + (g & 15), v_clut.y), cl = palette_at(v_clut.x + int(lv.a), v_clut.y);
  if (cf == 0) return vec4(0.0);
  vec3 fill = col5(cf), rgb = fill;
  float t = clamp(d.x / w + 0.5, 0.0, 1.0);
  stp = (cf >> 15) & 1;
  if (t < 1.0 && cl != cf) {
    rgb = mix(col5(cl), fill, t);
    if (t < 0.5) stp = (cl >> 15) & 1;
  }
  if (g >= 128) {
    /* The original's semi-transparent shadow rim (its lowest levels, drawn
     * at half strength) 0.7 texel beyond the outline, under it. */
    float sa = 0.5 * s * (1.0 - a);
    vec3 dark = col5(palette_at(v_clut.x + 2, v_clut.y));
    rgb = (rgb * a + dark * sa) / max(a + sa, 1e-4);
    a += sa;
  }
  if (a <= 0.0) return vec4(0.0);
  rr_cov = a;
  return vec4(rgb, 1.0);
}
uniform sampler2D u_rr_banner;   /* HD menu title banner (text_sdf), premultiplied */
uniform vec4 u_rr_banner_rect;   /* its picture in VRAM: x, y, w, h */
/* Menu title banner: the HD image in place of the banner picture, blended over
 * the backdrop in the dual-source pass (cut at half coverage without it). */
vec4 rr_banner(vec2 uv, out int stp){
  stp = 0; rr_cov = 1.0;
  vec4 c = texture(u_rr_banner, (vec2(v_tpage) + uv - u_rr_banner_rect.xy) / u_rr_banner_rect.zw);
  if (u_semimode != 4) return c.a >= 0.5 ? vec4(c.rgb / c.a, 1.0) : vec4(0.0);
  if (c.a < 0.004) return vec4(0.0);
  rr_cov = c.a;
  return vec4(c.rgb / c.a, 1.0);
}
'''

# Mode 3 (HD UI): full-colour (15-bit) pictures take the clamped Catmull-Rom,
# lettering and CLUT art xBR level 2; below 4/3 magnification nearest texels.
HD_UI_BRANCH = r'''
    int raw = stable_texel(ivec2(floor(uv)));
    vec2 fw3 = abs(dx) + abs(dy);
    float fwm = max(fw3.x, fw3.y);
    if (v_depth == 2 && fwm < 1.0) {
      if (raw == 0) discard;
      stp = (raw >> 15) & 1;
      rgb = rr_cubic(uv, raw);
    } else if (fwm < 0.75) {
      vec4 xr = fmode == 6 ? ((v_twin & 1023) == 0 ? rr_text_sdf(uv, fwm, stp) : rr_cubic_a(uv, raw, stp))
              : fmode == 5 ? rr_text_vec(uv, raw, stp) : fmode == 4 ? rr_cubic_a(uv, raw, stp) : rr_xbr2(uv, fwm, raw, stp);
      if (xr.a < 0.5) discard;
      rgb = xr.rgb;
    } else {
      if (raw == 0) discard;
      stp = (raw >> 15) & 1;
      rgb = col5(raw);
    }
'''

def image_uploads(source):
    def sub(old,new,what):
        nonlocal source
        if source.count(old)!=1:raise ValueError(what+' changed')
        source=source.replace(old,new)
    # HD UI in the texture shader: CLUT (4/8-bit) art and lettering keep the
    # edge-directed xBR; 15-bit full-colour pictures (menu backdrops, photos,
    # movie frames used as sprites) are photographic, where xBR leaves nearest
    # blocks, so they take the same clamped Catmull-Rom as the uploads above.
    # Transparent texels lend the centre colour; taps clamp to the sprite's own
    # UV bounds, so neighbouring atlas images never bleed in.
    sub('    "  return mix(xbr_c(E), xbr_c(px), a);\\n"\n    "}\\n"\n',
        '    "  return mix(xbr_c(E), xbr_c(px), a);\\n"\n    "}\\n"\n'
        '    "vec4 rr_cr4(float t){ float t2=t*t, t3=t2*t;\\n"\n'
        '    "  return vec4(-0.5*t3+t2-0.5*t, 1.5*t3-2.5*t2+1.0, -1.5*t3+2.0*t2+0.5*t, 0.5*t3-0.5*t2); }\\n"\n'
        '    "vec3 rr_cubic(vec2 uv, int e_raw){\\n"\n'
        '    "  vec2 p = uv - 0.5; ivec2 b = ivec2(floor(p)); vec2 f = p - vec2(b);\\n"\n'
        '    "  vec4 wx = rr_cr4(f.x), wy = rr_cr4(f.y);\\n"\n'
        '    "  vec3 e = col5(e_raw), sum = vec3(0.0), lo = e, hi = e;\\n"\n'
        '    "  for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) {\\n"\n'
        '    "    int r = stable_texel(b + ivec2(i - 1, j - 1));\\n"\n'
        '    "    vec3 s = r != 0 ? col5(r) : e;\\n"\n'
        '    "    sum += s * (wx[i] * wy[j]);\\n"\n'
        '    "    if (i >= 1 && i <= 2 && j >= 1 && j <= 2) { lo = min(lo, s); hi = max(hi, s); }\\n"\n'
        '    "  }\\n"\n'
        '    "  return clamp(sum, lo, hi);\\n"\n'
        '    "}\\n"\n','xBR shader interface')
    sub('    "  return clamp(sum, lo, hi);\\n"\n    "}\\n"\n',
        '    "  return clamp(sum, lo, hi);\\n"\n    "}\\n"\n'+_glsl(XBR_LV2),'xBR level 2 interface')
    sub('    "  } else if (fmode == 3) {\\n"\n'
        '    "    /* The nearest texel stays the PS1 one (floor of the unshifted uv). */\\n"\n'
        '    "    int raw = stable_texel(ivec2(floor(uv)));\\n"\n'
        '    "    if (raw == 0) discard;\\n"\n'
        '    "    stp = (raw >> 15) & 1;\\n"\n'
        '    "    rgb = xbr_sample(uv, raw, abs(dx) + abs(dy));\\n"\n',
        '    "  } else if (fmode >= 3 && fmode <= 6) {\\n"\n'+_glsl(HD_UI_BRANCH),'HD UI sampling interface')
    # FMVs (24-bit frames, CPU present path): with HD UI on, the runtime's own
    # bicubic movie reconstruction (u_sharp 2) instead of nearest, which it
    # used whenever anti-aliasing was off (Filter: Sharp) or no [video]
    # fmv_filter was set. Its measurements: two thirds less staircase than
    # nearest at the same gradient sharpness.
    # Sharpened bicubic for movies (u_sharp 4): the Catmull-Rom reconstruction
    # plus half its difference from a 1.5-texel box, clamped to the 2x2 source
    # texels around the sample, so edges gain contrast without halos.
    sub('    "  if (u_sharp == 2) { c = bicubic(uv); }\\n"\n',
        '    "  if (u_sharp == 2) { c = bicubic(uv); }\\n"\n'
        '    "  else if (u_sharp == 4) { c = rr_vid(uv); }\\n"\n','Present sampling modes')
    sub('    "  return r;\\n"\n    "}\\n"\n    PSX_SCANLINE_FUNC\n',
        '    "  return r;\\n"\n    "}\\n"\n'+_glsl(RR_VID)+'    PSX_SCANLINE_FUNC\n','Present bicubic interface')
    sub('    int filt_mode = linear ? fmv_filter_mode()\n                           : -1;',
        '    int filt_mode = rr_ui_texture_filter() >= 0 ? 4 : linear ? fmv_filter_mode()\n                           : -1;',
        'FMV present interface')
    # Movies may fill the widescreen window (launcher Display > Movies:
    # Widescreen); otherwise 24-bit frames keep their 4:3 box.
    old=('void gl_renderer_present(const uint32_t *pixels, int src_w, int src_h, int linear,\n'
         '                         int force_4_3, int content_w) {\n')
    sub(old,old+'    if (force_4_3 && s_present_d24 && rr_fmv_wide()) force_4_3 = 0;\n','FMV aspect interface')
    # MDEC video carries one colour per 2x2 pixels: with HD UI the runtime's
    # chroma smoothing removes the colour blocks around sharp edges.
    sub('    const int chroma = s_fmv_chroma && s_present_d24;',
        '    const int chroma = (s_fmv_chroma || rr_ui_texture_filter() >= 0) && s_present_d24;','FMV chroma interface')
    anchor='static void flush_cpu_upload(void) {\n'
    sub(anchor,RR_TEXT_SDF+IMAGE_UPLOADS+anchor,'CPU upload interface')
    sub('    hr_end();\n    hiw_mirror_uploads(rects, nrects);',
        '    hr_end();\n    rr_img_uploads(rects, nrects);\n    rr_pic_draw();\n    hiw_mirror_uploads(rects, nrects);','Upload blit interface')
    sub('    uint64_t draw_t0 = diag ? SDL_GetPerformanceCounter() : 0;\n    hr_begin(0);\n',
        '    uint64_t draw_t0 = diag ? SDL_GetPerformanceCounter() : 0;\n    rr_pic_classify(rects, nrects);\n    hr_begin(0);\n',
        'Upload classify interface')
    sub('        glScissor(x * s_hr_scale, y * s_hr_scale, w * s_hr_scale, h * s_hr_scale);\n'
        '        float fx0 = (float)x, fy0 = (float)y, fx1 = (float)(x + w), fy1 = (float)(y + h);\n',
        '        if (rr_pic_skip[i]) continue;   /* menu picture: rr_pic_draw */\n'
        '        glScissor(x * s_hr_scale, y * s_hr_scale, w * s_hr_scale, h * s_hr_scale);\n'
        '        float fx0 = (float)x, fy0 = (float)y, fx1 = (float)(x + w), fy1 = (float)(y + h);\n',
        'Upload blit loop interface')
    sub('    rect_add(&s_pack_dirty, x0, y0, x1, y1);\n    if (s_out_scale > 1) rect_add(&s_stencil_stale, x0, y0, x1, y1);',
        '    rect_add(&s_pack_dirty, x0, y0, x1, y1);\n    rr_img_clear(x0, y0, x1, y1);\n'
        '    if (s_out_scale > 1) rect_add(&s_stencil_stale, x0, y0, x1, y1);','Primitive dirty interface')
    sub('static void glb_fill_rect(int x,int y,int w,int h,uint16_t c){\n',
        'static void glb_fill_rect(int x,int y,int w,int h,uint16_t c){\n    rr_img_clear(x, y, x + w - 1, y + h - 1);\n','Fill interface')
    sub('static void glb_copy_rect(int sx,int sy,int dx,int dy,int w,int h){\n',
        'static void glb_copy_rect(int sx,int sy,int dx,int dy,int w,int h){\n    rr_img_clear(dx, dy, dx + w - 1, dy + h - 1);\n','Copy interface')
    return source

# Presentation with Filter: Sharp and HD UI on. The internal image (e.g. 720
# lines) rarely maps to the window at a whole multiple (1080: 1.5x), and nearest
# sampling then shows some rows and columns twice as thick as their neighbours:
# uneven glyph strokes. Sharp-bilinear keeps every texel flat and confines the
# blend to one output pixel at texel borders (the framework's own u_sharp 1).
def sharp_present(source):
    old=('    } else {\n'
         '        present_set_sharp(0, 0, 0, 0, 0);\n'
         '    }\n'
         '    /* Scanline at the native line grid.')
    new=('''    } else if (!linear && rr_ui_texture_filter() >= 0 && s_present_uSharp >= 0 && w > 0 && h > 0 &&
               (lw % (w * (src_scale > 1 ? src_scale : 1)) || lh % (h * (src_scale > 1 ? src_scale : 1))) &&
               lw > w * (src_scale > 1 ? src_scale : 1)) {
        const int S = src_scale > 1 ? src_scale : 1;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p_glUniform1i(s_present_uSharp, 1);
        if (s_present_uTexSize >= 0) p_glUniform2f(s_present_uTexSize, tex_w * (float)S, tex_h * (float)S);
        if (s_present_uSharpScale >= 0)
            p_glUniform2f(s_present_uSharpScale, (float)lw / (float)(w * S), (float)lh / (float)(h * S));
    } else {
        present_set_sharp(0, 0, 0, 0, 0);
    }
    /* Scanline at the native line grid.''')
    if source.count(old)!=1:raise ValueError('Present sampling interface changed')
    return source.replace(old,new)

# Every race frame the game StoreImages a 32x1 strip at VRAM (588,504) right
# after DrawOTag (RECT 0x8005B398, into a buffer it alternates per frame); in
# split screen the RECT is (652,376). A VRAM read under the render thread is a
# full sync: the emulation thread waits for every queued draw to reach the GPU
# and back, 1-3.5 ms a frame, more with the whole pack on screen, and it
# serialises the two threads (split screen at 120 FPS could not hold 120). During
# races that strip is served from a copy the render thread refreshes
# asynchronously (gl_renderer_fbo_peek_deferred), one frame behind, which the
# game's own double-buffering already allows for. The sun-flare test's pixel
# reads of the drawn frame get the same treatment through a 32x32 window (24 Oct:
# 47 syncs a second of 2.6-5.6 ms each, 28% of the game thread at 120 FPS in the
# pack). Every other read keeps the exact sync.
ASYNC_PROBE = r'''
extern int rr_ui_in_race(void);
enum { RR_PROBE_W = 32, RR_WIN = 64 };
static const int s_rr_probe_xy[2][2] = { { 588, 504 }, { 652, 376 } };   /* one player, split screen */
static uint16_t s_rr_probe[2][2][RR_PROBE_W];
static int s_rr_probe_cur[2] = { -1, -1 };   /* buffer being served per strip; -1 = none yet */
static uint32_t s_rr_tick;                   /* race frames, counted at the strip's first pixel */
/* Pixels of the drawn frame (the sun-flare test, race overlay 0x800B74F0, reads
 * one or two pixels at the sun every frame): served from a 32x32 window around
 * the last read, copied asynchronously in that frame's command order, so a read
 * this frame gets what a synced read last frame would have. Outside the window
 * (the sun moved far, or the first read) the read syncs and a new window is
 * queued. RR_ASYNC_PIXEL=0 syncs every read (A/B). */
/* RR_VREAD_TRACE=1 (dev): which read path takes the context (a real sync), by site and the last pixel, every 2 s. */
static void rr_sync_note(int site, int x, int y) {
    static int on = -1; static unsigned n[8]; static int lx[8], ly[8]; static uint64_t last;
    if (on < 0) { const char *e = getenv("RR_VREAD_TRACE"); on = e && e[0] == '1'; }
    if (!on || site < 0 || site > 7 || rt_held()) return;
    n[site]++; lx[site] = x; ly[site] = y;
    const uint64_t now = SDL_GetTicks();
    if (now - last > 2000) {
        last = now;
        for (int i = 0; i < 8; i++) if (n[i]) fprintf(stderr, "rr sync: site %d syncs=%u (last %d,%d)\n", i, n[i], lx[i], ly[i]);
        fflush(stderr);
    }
}/* Four windows in a ring, one queued a frame: the game draws into two buffers in
 * turn, so the pixel it tests sits in the other buffer every other frame and is
 * served from the window of two frames ago (always landed by then). */
enum { RR_WINS = 8, RR_WIN_PER_FRAME = 3 };
static uint16_t s_rr_win[RR_WINS][RR_WIN * RR_WIN];
static int s_rr_win_x[RR_WINS], s_rr_win_y[RR_WINS], s_rr_win_ok[RR_WINS], s_rr_win_on = -1;
static uint32_t s_rr_win_tick[RR_WINS], s_rr_win_q_tick = ~0u;
static int s_rr_win_next, s_rr_win_queued;   /* ring slot to fill next; windows queued this frame */
static uint16_t rr_frame_pixel(int x, int y) {
    if (s_rr_win_on < 0) { const char *e = getenv("RR_ASYNC_PIXEL"); s_rr_win_on = !(e && e[0] == '0'); }
    if (!s_rr_win_on) { rr_sync_note(1, x, y); GL_RT_SYNC("vram_read"); return glb_vram_read(x, y); }
    int best = -1;
    for (int i = 0; i < RR_WINS; i++)   /* the newest window at least two frames old (landed) that holds the pixel */
        if (s_rr_win_ok[i] && s_rr_tick - s_rr_win_tick[i] >= 2u && x >= s_rr_win_x[i] && x < s_rr_win_x[i] + RR_WIN &&
            y >= s_rr_win_y[i] && y < s_rr_win_y[i] + RR_WIN && (best < 0 || s_rr_win_tick[i] - s_rr_win_tick[best] < 0x80000000u))
            best = i;
    uint16_t v;
    if (best >= 0) v = s_rr_win[best][(y - s_rr_win_y[best]) * RR_WIN + (x - s_rr_win_x[best])];
    else { rr_sync_note(2, x, y); GL_RT_SYNC("vram_read"); v = glb_vram_read(x, y); }
    /* A window around this pixel for the frames to come, unless one queued this
     * frame already holds it (the test reads more than one point a frame). */
    if (s_rr_win_q_tick != s_rr_tick) { s_rr_win_q_tick = s_rr_tick; s_rr_win_queued = 0; }
    int covered = 0;
    for (int i = 0; i < RR_WINS; i++)
        if (s_rr_win_ok[i] && s_rr_win_tick[i] == s_rr_tick && x >= s_rr_win_x[i] + 4 && x < s_rr_win_x[i] + RR_WIN - 4 &&
            y >= s_rr_win_y[i] + 4 && y < s_rr_win_y[i] + RR_WIN - 4) covered = 1;
    if (!covered && s_rr_win_queued < RR_WIN_PER_FRAME) {
        const int b = s_rr_win_next; s_rr_win_next = (s_rr_win_next + 1) % RR_WINS; s_rr_win_queued++;
        int x0 = x - RR_WIN / 2, y0 = y - RR_WIN / 2;
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
        if (x0 > VRAM_W - RR_WIN) x0 = VRAM_W - RR_WIN; if (y0 > VRAM_H - RR_WIN) y0 = VRAM_H - RR_WIN;
        s_rr_win_ok[b] = 0;
        if (gl_renderer_fbo_peek_deferred(x0, y0, RR_WIN, RR_WIN, s_rr_win[b])) {
            s_rr_win_x[b] = x0; s_rr_win_y[b] = y0; s_rr_win_tick[b] = s_rr_tick; s_rr_win_ok[b] = 1;
        }
    }
    return v;
}
static uint16_t rtb_vram_read(int x, int y) {
    for (int k = 0; k < 2; k++) {
        const int px = s_rr_probe_xy[k][0], py = s_rr_probe_xy[k][1];
        if (y != py || x < px || x >= px + RR_PROBE_W || !rr_ui_in_race()) continue;
        if (x == px) {
            s_rr_tick++;
            if (s_rr_probe_cur[k] < 0) {
                rr_sync_note(3, px, py);
                GL_RT_SYNC("vram_read");
                for (int i = 0; i < RR_PROBE_W; i++) s_rr_probe[k][0][i] = s_rr_probe[k][1][i] = glb_vram_read(px + i, py);
                s_rr_probe_cur[k] = 0;
            } else {
                s_rr_probe_cur[k] ^= 1;     /* the copy queued last frame */
            }
            if (!gl_renderer_fbo_peek_deferred(px, py, RR_PROBE_W, 1, s_rr_probe[k][s_rr_probe_cur[k] ^ 1]))
                s_rr_probe_cur[k] = -1;     /* not recording: next request syncs */
        }
        if (s_rr_probe_cur[k] >= 0) return s_rr_probe[k][s_rr_probe_cur[k]][x - px];
        rr_sync_note(4, x, y); GL_RT_SYNC("vram_read"); return glb_vram_read(x, y);
    }
    if (!rr_ui_in_race()) {
        if (s_rr_probe_cur[0] >= 0 || s_rr_probe_cur[1] >= 0) s_rr_probe_cur[0] = s_rr_probe_cur[1] = -1;
        for (int i = 0; i < RR_WINS; i++) s_rr_win_ok[i] = 0;
    } else if (y < 480 && s_rr_tick) return rr_frame_pixel(x, y);   /* the drawn frames (display area) */
    rr_sync_note(5, x, y); GL_RT_SYNC("vram_read"); return glb_vram_read(x, y);
}
'''
def async_probe(source):
    old='static uint16_t rtb_vram_read(int x, int y) { GL_RT_SYNC("vram_read"); return glb_vram_read(x, y); }'
    if source.count(old)!=1:raise ValueError('VRAM read interface changed')
    return source.replace(old,ASYNC_PROBE)

# Exact HUD placement (gpu_geometry.py rr_hx). gpu.c sends the squashed HUD
# position of the next primitive as a sub-pixel override whose y carries
# RR_HUD_MARK: x2 == INT32_MIN marks a rectangle (x0,y0)-(x1,y1), anything else
# a triangle. HUD triangles stay "untracked" for texture filtering, like the
# sprites they sit with; rectangles keep the sprite filter (HD UI) and only take
# the position.
EXACT_HUD_STATE = r'''
static int s_rr_rect, s_rr_tri, s_rr_rv, s_rr_text;
static float s_rr_r[4], s_rr_vx[3], s_rr_vy[3];
static void rr_rect_vertices(int second) {
    const float x0 = s_rr_r[0], y0 = s_rr_r[1], x1 = s_rr_r[2], y1 = s_rr_r[3];
    const float vx[2][3] = { { x0, x1, x0 }, { x1, x0, x1 } };
    const float vy[2][3] = { { y0, y0, y1 }, { y0, y1, y1 } };
    for (int i = 0; i < 3; i++) { s_rr_vx[i] = vx[second][i]; s_rr_vy[i] = vy[second][i]; }
}
'''
def exact_hud(source):
    def sub(old,new,what):
        nonlocal source
        if source.count(old)!=1:raise ValueError(what+' changed')
        source=source.replace(old,new)
    old=('static void glb_set_precise_triangle(int enabled,\n'
         '                                     int32_t x0,int32_t y0, int32_t x1,int32_t y1,\n'
         '                                     int32_t x2,int32_t y2) {\n')
    sub(old,old+'''    s_rr_rect = s_rr_tri = s_rr_text = 0;
    if (enabled && y0 >= 0x10000000) {         /* Road Rash HUD (RR_HUD_MARK) or lettering (RR_TEXT_MARK) */
        const int32_t mark = y0 >= 0x38000000 ? 0x40000000 : 0x20000000;
        s_rr_text = mark == 0x40000000;
        if (x2 == INT32_MIN) {
            s_rr_rect = 1;
            s_rr_r[0] = (float)x0 / 65536.0f; s_rr_r[1] = (float)(y0 - mark) / 65536.0f;
            s_rr_r[2] = (float)x1 / 65536.0f; s_rr_r[3] = (float)(y1 - mark) / 65536.0f;
            enabled = 0; x0 = y0 = x1 = y1 = x2 = y2 = 0;
        } else {
            s_rr_tri = 1;
            y0 -= mark; y1 -= mark; y2 -= mark;
        }
    }
''','Precise triangle interface')
    sub('static inline void precise_consumed(void) { s_pc_valid = 0;',
        'static inline void precise_consumed(void) { s_rr_tri = 0; s_rr_text = 0; s_pc_valid = 0;','Precise consume interface')
    sub('    const int untracked=(lim && !s_projected_uv_valid) || (!s_pc_valid && !s_pq_valid);',
        '    const int untracked=(lim && !s_projected_uv_valid) || (!s_pc_valid && !s_pq_valid) || s_rr_tri;',
        'Texture tracking interface')
    sub('            vp[0] = s_pc_valid ? s_pc_x[i] : (float)xs[i];\n'
        '            vp[1] = s_pc_valid ? s_pc_y[i] : (float)ys[i];',
        '            vp[0] = s_rr_rv ? s_rr_vx[i] : s_pc_valid ? s_pc_x[i] : (float)xs[i];\n'
        '            vp[1] = s_rr_rv ? s_rr_vy[i] : s_pc_valid ? s_pc_y[i] : (float)ys[i];','Textured vertex interface')
    sub('    gpu_triangle(x,   y,   c, x+w, y,   c, x,   y+h, c, semi);\n'
        '    gpu_triangle(x+w, y,   c, x,   y+h, c, x+w, y+h, c, semi);',
        '''    const int rr = s_rr_rect && !overlay;
    s_rr_rect = 0;
    for (int k = 0; k < 2; k++) {
        if (rr) {
            rr_rect_vertices(k);
            for (int i = 0; i < 3; i++) { s_pc_x[i] = s_rr_vx[i]; s_pc_y[i] = s_rr_vy[i]; }
            s_pc_valid = 1;
        }
        if (!k) gpu_triangle(x,   y,   c, x+w, y,   c, x,   y+h, c, semi);
        else    gpu_triangle(x+w, y,   c, x,   y+h, c, x+w, y+h, c, semi);
    }
    if (rr) s_pc_valid = 0;''','Flat rect interface')
    sub('    gpu_textured_triangle(xs1,ys1,us1,vs1,col,tp,clut_x,clut_y,s_mod_raw,semi,lim);\n',
        '''    const int rr = s_rr_rect;
    s_rr_rect = 0;
    if (rr) { rr_rect_vertices(0); s_rr_rv = 1; }
    gpu_textured_triangle(xs1,ys1,us1,vs1,col,tp,clut_x,clut_y,s_mod_raw,semi,lim);
    if (rr) rr_rect_vertices(1);
''','Textured rect interface')
    sub('    gpu_textured_triangle(xs2,ys2,us2,vs2,col,tp,clut_x,clut_y,s_mod_raw,semi,lim);\n',
        '    gpu_textured_triangle(xs2,ys2,us2,vs2,col,tp,clut_x,clut_y,s_mod_raw,semi,lim);\n    s_rr_rv = 0; s_rr_text = 0;\n',
        'Textured rect interface')
    return EXACT_HUD_STATE+source

# Menu prompt glyphs (mode 8; controls_screen.cpp rr_prompt_build). The menus'
# "X : Select / O : Help" icons are 14x10 sprites from one 15-bit strip at VRAM
# 512,368 (146x10; the Up/Down icon, e.g. "Move Highlight" in the main menu's
# Help, is 14x12, so the replaced rectangle is 146x12). While that strip is the
# known one, the sprites sample an HD image of the player's PC bindings instead
# (keys or controller buttons, 8 pixels per texel), anti-aliased into the
# background like the title banners. Independent of HD UI / HD font. Anything
# written over the strip rebuilds it, and so does a new rr_prompt_serial. Each icon is recognised on its own:
# after a race the gaps hold race textures and Left/Right is overwritten; only sprites inside a recognised
# icon's box are replaced, and the others are looked at again every 250 ms
# (Player 1 switched keyboard / controller).
RR_PROMPTS = r'''
extern int rr_prompt_build(const uint16_t *vram, int x, int y, int w, int h, uint8_t *out, int s);
extern unsigned rr_prompt_serial(void);
extern int rr_prompt_cells(const uint16_t *vram, int x, int y);
enum { RR_PG_X = 512, RR_PG_Y = 368, RR_PG_W = 146, RR_PG_H = 12, RR_PG_S = 8 };
static struct { int stale, ok; unsigned serial; uint64_t retry; } s_rr_pg = { 1, 0, 0, 0 };   /* ok: recognised icons, bit per cell */
static GLuint s_rr_pg_tex;
static uint8_t *s_rr_pg_buf;
static GLint s_uRrGlyphs = -1, s_uRrGlyphRect = -1;
static void rr_prompt_touch(int x0, int y0, int x1, int y1) {
    if (x1 >= RR_PG_X && x0 < RR_PG_X + RR_PG_W && y1 >= RR_PG_Y && y0 < RR_PG_Y + RR_PG_H) s_rr_pg.stale = 1;
}
static int rr_prompt_filter(uint16_t texpage, const int *us, const int *vs, const int *lim) {
    if (((texpage >> 7) & 3) != 2) return 0;
    int u0, v0, u1, v1;
    if (lim) { u0 = lim[0]; v0 = lim[1]; u1 = lim[2] + 1; v1 = lim[3] + 1; }
    else {
        u0 = u1 = us[0]; v0 = v1 = vs[0];
        for (int i = 1; i < 3; i++) {
            if (us[i] < u0) u0 = us[i]; if (us[i] > u1) u1 = us[i];
            if (vs[i] < v0) v0 = vs[i]; if (vs[i] > v1) v1 = vs[i];
        }
    }
    const int px = (texpage & 15) * 64, py = ((texpage >> 4) & 1) * 256;
    if (px + u0 < RR_PG_X || py + v0 < RR_PG_Y || px + u1 > RR_PG_X + RR_PG_W || py + v1 > RR_PG_Y + RR_PG_H) return 0;
    flush_cpu_upload();   /* a pending upload of the strip marks it stale first */
    const unsigned serial = rr_prompt_serial();
    const uint64_t now = SDL_GetTicks();
    /* Not built yet: look again, at most every 250 ms. The strip can be written
     * GPU-side first (copy/upload pipeline), so the CPU copy checked here may
     * lag the write that marked it stale; giving up after one look kept the
     * PlayStation icons for the rest of the session (after a race). */
    /* Some icons not recognised: look again (cheap fingerprints), rebuild when that changes. */
    if (!s_rr_pg.stale && serial == s_rr_pg.serial && s_rr_pg.ok != 0x3F && now >= s_rr_pg.retry) {
        s_rr_pg.retry = now + 250;
        if (rr_prompt_cells(s_vram, RR_PG_X, RR_PG_Y) != s_rr_pg.ok) s_rr_pg.stale = 1;
    }
    if (s_rr_pg.stale || serial != s_rr_pg.serial) {
        const int W = RR_PG_W * RR_PG_S, H = RR_PG_H * RR_PG_S;
        s_rr_pg.stale = 0; s_rr_pg.ok = 0; s_rr_pg.serial = serial;
        if (!s_rr_pg_buf) s_rr_pg_buf = (uint8_t *)malloc((size_t)W * H * 4);
        const int r = s_rr_pg_buf ? rr_prompt_build(s_vram, RR_PG_X, RR_PG_Y, RR_PG_W, RR_PG_H, s_rr_pg_buf, RR_PG_S) : 0;
        if (r != 0x3F) s_rr_pg.retry = now + 250;   /* icons not recognised or pads not set up yet */
        if (r < 0) s_rr_pg.stale = 1;   /* asked again at the next sprite */
        if (r > 0) {
            flush_flat_batch(); flush_tex_batch(); hiw_flush_queue();
            p_glActiveTexture(PSXGL_TEXTURE0 + 5);
            if (!s_rr_pg_tex) {
                glGenTextures(1, &s_rr_pg_tex);
                glBindTexture(GL_TEXTURE_2D, s_rr_pg_tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glBindTexture(GL_TEXTURE_2D, s_rr_pg_tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_rr_pg_buf);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            p_glActiveTexture(PSXGL_TEXTURE0);
            s_rr_pg.ok = r;
        }
    }
    /* Only a sprite inside a recognised icon's box; anything else in the strip
     * (leftover race textures between the icons) keeps its own drawing. */
    const int cell = (px + u0 - RR_PG_X) / 26;
    if (cell < 0 || cell > 5 || !((s_rr_pg.ok >> cell) & 1) || px + u1 > RR_PG_X + 26 * cell + 14 || py + v1 > RR_PG_Y + (cell == 4 ? 12 : 10)) return 0;
    return 8;
}
'''
RR_PROMPTS_GLSL = r'''
uniform sampler2D u_rr_glyphs;   /* HD menu prompt icons (prompt_glyphs), premultiplied */
uniform vec4 u_rr_glyph_rect;    /* their strip in VRAM: x, y, w, h */
'''
RR_PROMPTS_BRANCH = r'''
  } else if (fmode == 8) {
    vec4 gc = texture(u_rr_glyphs, (vec2(v_tpage) + uv - u_rr_glyph_rect.xy) / u_rr_glyph_rect.zw);
    stp = 0;
    if (u_semimode != 4) { if (gc.a < 0.5) discard; }
    else { if (gc.a < 0.004) discard; rr_cov = gc.a; }
    rgb = gc.rgb / gc.a;
'''
def prompt_glyphs(source):
    def sub(old,new,what):
        nonlocal source
        if source.count(old)!=1:raise ValueError(what+' changed')
        source=source.replace(old,new)
    anchor='static void rr_sdf_touch(int x0, int y0, int x1, int y1) {\n'
    sub(anchor,RR_PROMPTS+anchor+'    rr_prompt_touch(x0, y0, x1, y1);\n','Prompt strip invalidation interface')
    sub('(s_rr_bf = rr_banner_filter(texpage, us, vs, lim)) ? s_rr_bf : ',
        '(s_rr_bf = rr_prompt_filter(texpage, us, vs, lim)) ? s_rr_bf : (s_rr_bf = rr_banner_filter(texpage, us, vs, lim)) ? s_rr_bf : ',
        'Prompt filter interface')
    sub('if (((filter & 15) == 6 || (filter & 15) == 7) && !s_mask_check && semi != 2)',
        'if (((filter & 15) == 6 || (filter & 15) == 7 || (filter & 15) == 8) && !s_mask_check && semi != 2)','Prompt batch key interface')
    sub('    s_uHdTexture = p_glGetUniformLocation(s_tex_prog, "u_hd_texture");\n',
        '    s_uHdTexture = p_glGetUniformLocation(s_tex_prog, "u_hd_texture");\n'
        '    s_uRrGlyphs = p_glGetUniformLocation(s_tex_prog, "u_rr_glyphs");\n'
        '    s_uRrGlyphRect = p_glGetUniformLocation(s_tex_prog, "u_rr_glyph_rect");\n','Prompt uniform interface')
    sub('    glBindTexture(GL_TEXTURE_2D, hd ? hd : s_up_tex); p_glUniform1i(s_uHdTexture, 2);\n',
        '    glBindTexture(GL_TEXTURE_2D, hd ? hd : s_up_tex); p_glUniform1i(s_uHdTexture, 2);\n'
        '    p_glActiveTexture(PSXGL_TEXTURE0 + 5);\n'
        '    glBindTexture(GL_TEXTURE_2D, s_rr_pg_tex); if (s_uRrGlyphs >= 0) p_glUniform1i(s_uRrGlyphs, 5);\n'
        '    if (s_uRrGlyphRect >= 0) p_glUniform4f(s_uRrGlyphRect, RR_PG_X, RR_PG_Y, RR_PG_W, RR_PG_H);\n',
        'Prompt binding interface')
    sub('    "vec4 rr_cr4(float t){ float t2=t*t, t3=t2*t;\\n"\n',
        _glsl(RR_PROMPTS_GLSL)+'    "vec4 rr_cr4(float t){ float t2=t*t, t3=t2*t;\\n"\n','Prompt shader uniform interface')
    sub('    "  } else if (fmode >= 3 && fmode <= 6) {\\n"\n',
        _glsl(RR_PROMPTS_BRANCH)+'    "  } else if (fmode >= 3 && fmode <= 6) {\\n"\n','Prompt shader branch interface')
    return source

# Post effects (launcher / in-game Display: Bloom, Sharpening, Colour, Vignette;
# each Off / Light / Strong, all Off by default; pc_services.cpp apply_post).
# They run on the composed race picture right after the renderer's own FXAA
# (pres_record), before hold-last capture, screenshots, the OSD and the swap,
# so like FXAA they never touch VRAM, readbacks or the game. Off: nothing below
# runs and the present is unchanged.
# Never on the HUD or menus: effects run during races only, and a mask of the
# race overlay keeps its pixels exactly as drawn. The mask is kept per VRAM
# texel on the CPU (32x32 tiles; tiles never marked cost nothing): a GP0
# sprite or rectangle (the GTE never produces those; a race frame's overlay
# list is the only one that has them), lettering tagged by native_ui and exact
# HUD triangles mark their box (a sprite only its opaque texels, read from the
# CPU copy of VRAM). Anything else clears its box: the sky and world are
# untracked polygons in this build (no PGXP), so an "overlay phase" that also
# marked untagged polygons drawn after the first sprite caught road polygons
# and left flat, effect-free patches in the picture; it was removed (HUD
# polygons that are neither rectangles nor tagged take the effects). Painter's order does the rest: each frame the
# scene clears last frame's marks before the HUD sets them again. Big overlay
# panels (pause box, fades) leave the mask alone.
# Bloom: bright parts (soft knee on the brightest channel) at a quarter of the
# picture's size, the HUD masked out, blurred twice (9-tap Gaussian over 5
# linear fetches, then at double spread) and screen-blended back.
# Sharpening: contrast-adaptive (an independent implementation of the published
# CAS idea: a negative cross lobe scaled by how much headroom the 3x3 cross
# leaves, so flat areas and strong edges are not over-sharpened, no halos).
# Colour: vibrance (saturation raised most where it is low, skin and sky stay
# natural) and a smoothstep contrast curve. Vignette: corners darkened.
# RR_POST_TIME=1 (or the test hook's "posttime 1"): glFinish-timed passes, mean
# per frame logged every 60 timed frames (measurement only; it serialises the GPU).
RR_FX_BRIGHT = r'''
#version 330
in vec2 v_uv; out vec4 o;
uniform sampler2D u_src; uniform sampler2D u_mask;
uniform vec2 u_rcp; uniform vec4 u_map; uniform vec2 u_vram; uniform int u_use_mask; uniform vec4 u_k;
float ui(vec2 uv){
  if (u_use_mask == 0) return 0.0;
  return texture(u_mask, vec2(u_map.x + uv.x * u_map.z, u_map.y + (1.0 - uv.y) * u_map.w) / u_vram).r;
}
void main(){
  vec3 s = 0.25 * (texture(u_src, v_uv + u_rcp * vec2(-1.0, -1.0)).rgb + texture(u_src, v_uv + u_rcp * vec2(1.0, -1.0)).rgb
                 + texture(u_src, v_uv + u_rcp * vec2(-1.0, 1.0)).rgb + texture(u_src, v_uv + u_rcp * vec2(1.0, 1.0)).rgb);
  float l = max(s.r, max(s.g, s.b));
  float k = clamp((l - u_k.x) / max(1.0 - u_k.x, 0.001), 0.0, 1.0);
  o = vec4(s * (k * k) * (1.0 - ui(v_uv)), 1.0);
}
'''
RR_FX_BLUR = r'''
#version 330
in vec2 v_uv; out vec4 o;
uniform sampler2D u_src; uniform vec2 u_dir;
void main(){
  vec3 c = texture(u_src, v_uv).rgb * 0.2270270;
  c += (texture(u_src, v_uv + u_dir * 1.3846154).rgb + texture(u_src, v_uv - u_dir * 1.3846154).rgb) * 0.3162162;
  c += (texture(u_src, v_uv + u_dir * 3.2307692).rgb + texture(u_src, v_uv - u_dir * 3.2307692).rgb) * 0.0702703;
  o = vec4(c, 1.0);
}
'''
RR_FX_FINAL = r'''
#version 330
in vec2 v_uv; out vec4 o;
uniform sampler2D u_src; uniform sampler2D u_bloom; uniform sampler2D u_mask;
uniform vec2 u_rcp; uniform vec4 u_map; uniform vec2 u_vram; uniform int u_use_mask;
uniform vec4 u_k;      /* bloom strength, sharpness, colour amount, vignette */
uniform float u_aspect;
float ui(vec2 uv){
  if (u_use_mask == 0) return 0.0;
  return texture(u_mask, vec2(u_map.x + uv.x * u_map.z, u_map.y + (1.0 - uv.y) * u_map.w) / u_vram).r;
}
void main(){
  vec3 c0 = texture(u_src, v_uv).rgb;
  float m = ui(v_uv);
  if (m >= 0.999) { o = vec4(c0, 1.0); return; }
  vec3 c = c0;
  if (u_k.y > 0.0) {
    vec3 a = texture(u_src, v_uv + vec2(0.0, u_rcp.y)).rgb, b = texture(u_src, v_uv - vec2(u_rcp.x, 0.0)).rgb;
    vec3 d = texture(u_src, v_uv + vec2(u_rcp.x, 0.0)).rgb, e = texture(u_src, v_uv - vec2(0.0, u_rcp.y)).rgb;
    vec3 mn = min(c, min(min(a, b), min(d, e))), mx = max(c, max(max(a, b), max(d, e)));
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(0.0001)), 0.0, 1.0));
    vec3 w = amp * (-1.0 / mix(8.0, 5.0, u_k.y));
    c = clamp((c + (a + b + d + e) * w) / (1.0 + 4.0 * w), 0.0, 1.0);
  }
  if (u_k.x > 0.0) c = 1.0 - (1.0 - c) * (1.0 - clamp(texture(u_bloom, v_uv).rgb * u_k.x, 0.0, 1.0));
  if (u_k.z > 0.0) {
    float hi = max(c.r, max(c.g, c.b)), lo = min(c.r, min(c.g, c.b));
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = clamp(mix(vec3(l), c, 1.0 + u_k.z * (1.0 - (hi - lo))), 0.0, 1.0);
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.35 * u_k.z);
  }
  if (u_k.w > 0.0) {
    vec2 q = (v_uv - 0.5) * vec2(u_aspect, 1.0);
    float r = length(q) / length(vec2(u_aspect, 1.0) * 0.5);
    c *= 1.0 - u_k.w * smoothstep(0.35, 1.0, r);
  }
  o = vec4(mix(c, c0, m), 1.0);
}
'''
def _cstr(name, glsl):
    return 'static const char *' + name + ' =\n' + _glsl(glsl).rstrip('\n') + ';\n'
RR_POST_FX = r'''
/* ---- Road Rash post effects (gpu_presentation.py post_effects) ---- */
extern int rr_ui_in_race(void);
static int s_rr_fx[4], s_rr_fx_on;   /* bloom, sharpen, colour, vignette: 0 off, 1 light, 2 strong */
void gl_renderer_rr_post_set(int bloom, int sharpen, int colour, int vignette) {
    GL_RT_SYNC("rr_post");
    const int v[4] = { bloom, sharpen, colour, vignette };
    s_rr_fx_on = 0;
    for (int i = 0; i < 4; i++) { s_rr_fx[i] = v[i] >= 0 && v[i] <= 2 ? v[i] : 0; s_rr_fx_on |= s_rr_fx[i]; }
}
/* HUD mask, one byte per VRAM texel (255 = race overlay). */
static uint8_t s_rr_uim[VRAM_W * VRAM_H];
static uint8_t s_rr_uim_tile[(VRAM_W / 32) * (VRAM_H / 32)];
static int s_rr_uim_tiles, s_rr_sprite;
static void rr_uim_mark(int x0, int y0, int x1, int y1, uint8_t v) {
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > VRAM_W - 1) x1 = VRAM_W - 1; if (y1 > VRAM_H - 1) y1 = VRAM_H - 1;
    if (x1 < x0 || y1 < y0 || (!v && !s_rr_uim_tiles)) return;
    for (int ty = y0 >> 5; ty <= y1 >> 5; ty++)
        for (int tx = x0 >> 5; tx <= x1 >> 5; tx++) {
            uint8_t *t = &s_rr_uim_tile[ty * (VRAM_W / 32) + tx];
            if (!v && !*t) continue;
            const int ax = x0 > tx * 32 ? x0 : tx * 32, bx = x1 < tx * 32 + 31 ? x1 : tx * 32 + 31;
            const int ay = y0 > ty * 32 ? y0 : ty * 32, by = y1 < ty * 32 + 31 ? y1 : ty * 32 + 31;
            for (int y = ay; y <= by; y++) memset(&s_rr_uim[(size_t)y * VRAM_W + ax], v, (size_t)(bx - ax + 1));
            if (v && !*t) { *t = 1; s_rr_uim_tiles++; }
            else if (!v && ax == tx * 32 && bx == tx * 32 + 31 && ay == ty * 32 && by == ty * 32 + 31) {
                *t = 0; s_rr_uim_tiles--;
            }
        }
}
/* A GP0 sprite / textured rectangle marks only its opaque texels (the CPU
 * copy of VRAM holds the HUD art the game uploaded), so the scene seen
 * between letters and around icons keeps its effects; s_rr_sprite 2 tells
 * rr_uim_prim the box is done. Each texel maps linearly from the box. */
static void rr_uim_sprite(const int *xs, const int *ys, const int *us, const int *vs,
                          uint16_t texpage, int clut_x, int clut_y) {
    int a = 0, b = 0, c = 0, d = 0;   /* vertices with min / max x, min / max y */
    for (int i = 1; i < 3; i++) {
        if (xs[i] < xs[a]) a = i; if (xs[i] > xs[b]) b = i;
        if (ys[i] < ys[c]) c = i; if (ys[i] > ys[d]) d = i;
    }
    const int x0 = xs[a], x1 = xs[b], y0 = ys[c], y1 = ys[d];
    if (x1 <= x0 || y1 <= y0 || (long)(x1 - x0) * (y1 - y0) >= 160 * 120) return;
    s_rr_sprite = 2;
    const int depth = (texpage >> 7) & 3, px = (texpage & 15) * 64, py = ((texpage >> 4) & 1) * 256;
    const float du = (float)(us[b] - us[a]) / (float)(x1 - x0), dv = (float)(vs[d] - vs[c]) / (float)(y1 - y0);
    for (int y = y0 < s_area_y1 ? s_area_y1 : y0; y < y1 && y <= s_area_y2 && y < VRAM_H; y++) {
        if (y < 0) continue;
        const int v = ((int)((float)vs[c] + ((float)(y - y0) + 0.5f) * dv)) & 255;
        const uint16_t *row = s_vram + (size_t)((py + v) & (VRAM_H - 1)) * VRAM_W;
        for (int x = x0 < s_area_x1 ? s_area_x1 : x0; x < x1 && x <= s_area_x2 && x < VRAM_W; x++) {
            if (x < 0) continue;
            const int u = ((int)((float)us[a] + ((float)(x - x0) + 0.5f) * du)) & 255;
            uint16_t texel;
            if (depth == 0) texel = s_vram[(size_t)clut_y * VRAM_W + ((clut_x + ((row[(px + u / 4) & (VRAM_W - 1)] >> ((u & 3) * 4)) & 15)) & (VRAM_W - 1))];
            else if (depth == 1) texel = s_vram[(size_t)clut_y * VRAM_W + ((clut_x + ((row[(px + u / 2) & (VRAM_W - 1)] >> ((u & 1) * 8)) & 255)) & (VRAM_W - 1))];
            else texel = row[(px + u) & (VRAM_W - 1)];
            if (texel) rr_uim_mark(x, y, x, y, 0xFF);
        }
    }
}
/* Every primitive's clipped box (mark_prim_dirty). */
static void rr_uim_prim(int x0, int y0, int x1, int y1) {
    const int sprite = s_rr_sprite;
    s_rr_sprite = 0;
    if (!s_rr_fx_on || x1 < x0 || y1 < y0 || sprite == 2) return;
    const long area = (long)(x1 - x0 + 1) * (long)(y1 - y0 + 1);
    const int ui = sprite || s_rr_tri || s_rr_text;
    if (ui && area >= 160 * 120) return;              /* overlay panels, fades, the pause dim */
    rr_uim_mark(x0, y0, x1, y1, ui ? 0xFF : 0);
}
static void rr_uim_fill(int x, int y, int w, int h) {
    if (s_rr_uim_tiles) rr_uim_mark(x, y, x + w - 1, y + h - 1, 0);
}
'''+_cstr('RR_FX_BRIGHT_FS', RR_FX_BRIGHT)+_cstr('RR_FX_BLUR_FS', RR_FX_BLUR)+_cstr('RR_FX_FINAL_FS', RR_FX_FINAL)+r'''
static GLuint s_rr_fx_prog[3], s_rr_fx_src, s_rr_fx_mask, s_rr_fx_bt[2], s_rr_fx_fbo[2];
static int s_rr_fx_sw, s_rr_fx_sh, s_rr_fx_bw, s_rr_fx_bh, s_rr_fx_failed;
enum { RR_FX_U = 11 };
static GLint s_rr_fx_u[3][RR_FX_U];
static const char *const s_rr_fx_names[RR_FX_U] = { "u_src", "u_bloom", "u_mask", "u_rcp", "u_map", "u_vram",
                                                    "u_use_mask", "u_k", "u_dir", "u_uv_rect", "u_aspect" };
enum { RU_SRC, RU_BLOOM, RU_MASK, RU_RCP, RU_MAP, RU_VRAM, RU_USE, RU_K, RU_DIR, RU_UV, RU_ASPECT };
static void rr_fx_release(void) {
    memset(s_rr_fx_prog, 0, sizeof s_rr_fx_prog); s_rr_fx_src = s_rr_fx_mask = 0;
    memset(s_rr_fx_bt, 0, sizeof s_rr_fx_bt); memset(s_rr_fx_fbo, 0, sizeof s_rr_fx_fbo);
    s_rr_fx_sw = s_rr_fx_sh = s_rr_fx_bw = s_rr_fx_bh = 0; s_rr_fx_failed = 0;
}
static void rr_fx_texture(GLuint *t, int w, int h, GLint ifmt, GLenum fmt, const void *data) {
    if (!*t) glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}
static int rr_fx_ready(void) {
    if (s_rr_fx_prog[2]) return 1;
    const char *fs[3] = { RR_FX_BRIGHT_FS, RR_FX_BLUR_FS, RR_FX_FINAL_FS };
    for (int p = 0; p < 3; p++) {
        s_rr_fx_prog[p] = build_program(PRESENT_VS, fs[p]);
        if (!s_rr_fx_prog[p]) { s_rr_fx_failed = 1; return 0; }
        for (int i = 0; i < RR_FX_U; i++) s_rr_fx_u[p][i] = p_glGetUniformLocation(s_rr_fx_prog[p], s_rr_fx_names[i]);
    }
    return 1;
}
static void rr_fx_draw(int p) {
    p_glUseProgram(s_rr_fx_prog[p]);
    /* PRESENT_VS flips v; (0,1,1,0) cancels it for bottom-up copies. */
    p_glUniform4f(s_rr_fx_u[p][RU_UV], 0.0f, 1.0f, 1.0f, 0.0f);
    p_glUniform1i(s_rr_fx_u[p][RU_SRC], 0); p_glUniform1i(s_rr_fx_u[p][RU_BLOOM], 1); p_glUniform1i(s_rr_fx_u[p][RU_MASK], 2);
}
/* Pass timing (RR_POST_TIME set, or switched by the test hook): glFinish
 * around the passes, means logged every 60 timed frames. Measurement only. */
#ifndef PSX_NO_DEBUG_TOOLS
static int s_rr_fx_time = -1;
void gl_renderer_rr_post_time(int on) { GL_RT_SYNC("rr_post"); s_rr_fx_time = on ? 1 : 0; }
#else
void gl_renderer_rr_post_time(int on) { (void)on; }
#endif
#ifndef PSX_NO_DEBUG_TOOLS
static double s_rr_fx_us[3];
static uint32_t s_rr_fx_n;
static uint64_t rr_fx_clock(int on) { if (!on) return 0; glFinish(); return SDL_GetPerformanceCounter(); }
#endif
static void rr_post_apply(int path, int dx, int dy, int w, int h, int lx, int ly, int lw, int lh) {
    if (!s_rr_fx_on || path != GL_PRES_VRAM || !s_ctx || s_rr_fx_failed || !rr_ui_in_race()) return;
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(s_win, &ww, &wh);
    if (w <= 0 || h <= 0 || lx < 0 || ly < 0 || lx + lw > ww || ly + lh > wh || lw < 16 || lh < 16) return;
    if (!rr_fx_ready()) return;
#ifndef PSX_NO_DEBUG_TOOLS
    if (s_rr_fx_time < 0) s_rr_fx_time = getenv("RR_POST_TIME") != NULL;
    uint64_t t0 = rr_fx_clock(s_rr_fx_time), t1 = 0, t2 = 0;
#endif
    const GLboolean blend = glIsEnabled(GL_BLEND);
    glDisable(GL_BLEND);
    /* The picture, and the HUD mask over the displayed VRAM rect. */
    p_glActiveTexture(PSXGL_TEXTURE0);
    if (!s_rr_fx_src || s_rr_fx_sw != lw || s_rr_fx_sh != lh) {
        rr_fx_texture(&s_rr_fx_src, lw, lh, GL_RGBA8, GL_RGBA, NULL); s_rr_fx_sw = lw; s_rr_fx_sh = lh;
    } else glBindTexture(GL_TEXTURE_2D, s_rr_fx_src);
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER, 0);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, lx, ly, lw, lh);
    const int use_mask = s_rr_uim_tiles > 0;
    p_glActiveTexture(PSXGL_TEXTURE0 + 2);
    if (!s_rr_fx_mask) rr_fx_texture(&s_rr_fx_mask, VRAM_W, VRAM_H, 0x8229 /* GL_R8 */, 0x1903 /* GL_RED */, s_rr_uim);
    else glBindTexture(GL_TEXTURE_2D, s_rr_fx_mask);
    if (use_mask) {
        int x0 = dx < 0 ? 0 : dx, y0 = dy < 0 ? 0 : dy, x1 = dx + w, y1 = dy + h;
        if (x1 > VRAM_W) x1 = VRAM_W; if (y1 > VRAM_H) y1 = VRAM_H;
        if (x1 > x0 && y1 > y0) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, VRAM_W);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x1 - x0, y1 - y0, 0x1903, GL_UNSIGNED_BYTE,
                            &s_rr_uim[(size_t)y0 * VRAM_W + x0]);
            glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        }
    }
    const float map[4] = { (float)dx, (float)dy, (float)w, (float)h };
    p_glBindVertexArray(s_present_vao);
    /* Bloom: bright pass at a quarter size, blurred twice. */
    if (s_rr_fx[0]) {
        const int bw = lw / 4 > 1 ? lw / 4 : 1, bh = lh / 4 > 1 ? lh / 4 : 1;
        p_glActiveTexture(PSXGL_TEXTURE0 + 1);
        if (!s_rr_fx_bt[0] || s_rr_fx_bw != bw || s_rr_fx_bh != bh) {
            for (int i = 0; i < 2; i++) {
                rr_fx_texture(&s_rr_fx_bt[i], bw, bh, GL_RGBA8, GL_RGBA, NULL);
                if (!s_rr_fx_fbo[i]) p_glGenFramebuffers(1, &s_rr_fx_fbo[i]);
                p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_rr_fx_fbo[i]);
                p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER, PSXGL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_rr_fx_bt[i], 0);
            }
            s_rr_fx_bw = bw; s_rr_fx_bh = bh;
        }
        glViewport(0, 0, bw, bh);
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_rr_fx_fbo[0]);
        glBindTexture(GL_TEXTURE_2D, 0);   /* bt[0] is the target */
        rr_fx_draw(0);
        p_glUniform2f(s_rr_fx_u[0][RU_RCP], 1.0f / (float)lw, 1.0f / (float)lh);
        p_glUniform4f(s_rr_fx_u[0][RU_MAP], map[0], map[1], map[2], map[3]);
        p_glUniform2f(s_rr_fx_u[0][RU_VRAM], (float)VRAM_W, (float)VRAM_H);
        p_glUniform1i(s_rr_fx_u[0][RU_USE], use_mask);
        p_glUniform4f(s_rr_fx_u[0][RU_K], s_rr_fx[0] == 2 ? 0.40f : 0.55f, 0.0f, 0.0f, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        rr_fx_draw(1);
        p_glUniform1i(s_rr_fx_u[1][RU_SRC], 1);
        for (int it = 0; it < 2; it++) {
            const float spread = it ? 2.0f : 1.0f;
            p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_rr_fx_fbo[1]);
            glBindTexture(GL_TEXTURE_2D, s_rr_fx_bt[0]);
            p_glUniform2f(s_rr_fx_u[1][RU_DIR], spread / (float)bw, 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_rr_fx_fbo[0]);
            glBindTexture(GL_TEXTURE_2D, s_rr_fx_bt[1]);
            p_glUniform2f(s_rr_fx_u[1][RU_DIR], 0.0f, spread / (float)bh);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glBindTexture(GL_TEXTURE_2D, s_rr_fx_bt[0]);
    } else {
        p_glActiveTexture(PSXGL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, s_rr_fx_src);
    }
#ifndef PSX_NO_DEBUG_TOOLS
    t1 = rr_fx_clock(s_rr_fx_time);
#endif
    /* The picture through sharpening, bloom, colour and vignette, HUD kept. */
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, 0);
    glViewport(lx, ly, lw, lh);
    rr_fx_draw(2);
    static const float sharp[3] = { 0.0f, 0.4f, 0.9f }, bloom[3] = { 0.0f, 0.6f, 1.0f };
    static const float colour[3] = { 0.0f, 0.3f, 0.6f }, vignette[3] = { 0.0f, 0.25f, 0.45f };
    p_glUniform2f(s_rr_fx_u[2][RU_RCP], 1.0f / (float)lw, 1.0f / (float)lh);
    p_glUniform4f(s_rr_fx_u[2][RU_MAP], map[0], map[1], map[2], map[3]);
    p_glUniform2f(s_rr_fx_u[2][RU_VRAM], (float)VRAM_W, (float)VRAM_H);
    p_glUniform1i(s_rr_fx_u[2][RU_USE], use_mask);
    p_glUniform4f(s_rr_fx_u[2][RU_K], bloom[s_rr_fx[0]], sharp[s_rr_fx[1]], colour[s_rr_fx[2]], vignette[s_rr_fx[3]]);
    p_glUniform1f(s_rr_fx_u[2][RU_ASPECT], (float)lw / (float)lh);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    p_glBindVertexArray(0);
    p_glUseProgram(0);
    for (int u = 2; u >= 0; u--) { p_glActiveTexture(PSXGL_TEXTURE0 + u); glBindTexture(GL_TEXTURE_2D, 0); }
    if (blend) glEnable(GL_BLEND);
#ifndef PSX_NO_DEBUG_TOOLS
    if (s_rr_fx_time) {
        t2 = rr_fx_clock(1);
        const double f = 1e6 / (double)SDL_GetPerformanceFrequency();
        s_rr_fx_us[0] += (double)(t1 - t0) * f; s_rr_fx_us[2] += (double)(t2 - t1) * f;
        if (++s_rr_fx_n == 60) {
            fprintf(stderr, "rr post: %dx%d fx %d%d%d%d mask %d: copy+bloom %.1f us, final %.1f us per frame\n", lw, lh,
                    s_rr_fx[0], s_rr_fx[1], s_rr_fx[2], s_rr_fx[3], s_rr_uim_tiles, s_rr_fx_us[0] / 60.0, s_rr_fx_us[2] / 60.0);
            s_rr_fx_us[0] = s_rr_fx_us[2] = 0.0; s_rr_fx_n = 0;
        }
    }
#endif
}
'''
def post_effects(source):
    def sub(old,new,what):
        nonlocal source
        if source.count(old)!=1:raise ValueError(what+' changed')
        source=source.replace(old,new)
    anchor='/* Present sampling. u_sharp==0 is the historical behaviour'
    sub(anchor,RR_POST_FX+anchor,'Post effects anchor')
    sub('static void post_aa_apply(int lx, int ly, int lw, int lh);\nstatic void pres_record(',
        'static void post_aa_apply(int lx, int ly, int lw, int lh);\n'
        'static void rr_post_apply(int path, int dx, int dy, int w, int h, int lx, int ly, int lw, int lh);\n'
        'static void pres_record(','Present record declaration')
    sub('        post_aa_apply(lx, ly, lw, lh);\n',
        '        post_aa_apply(lx, ly, lw, lh);\n    rr_post_apply(path, dx, dy, w, h, lx, ly, lw, lh);\n','Present record post pass')
    sub('    rr_img_clear(x0, y0, x1, y1);\n    if (s_out_scale > 1)',
        '    rr_img_clear(x0, y0, x1, y1);\n    rr_uim_prim(x0, y0, x1, y1);\n    if (s_out_scale > 1)','Primitive mask interface')
    sub('    const int untracked=(lim && !s_projected_uv_valid) || (!s_pc_valid && !s_pq_valid) || s_rr_tri;\n',
        '    const int untracked=(lim && !s_projected_uv_valid) || (!s_pc_valid && !s_pq_valid) || s_rr_tri;\n'
        '    s_rr_sprite = lim != NULL;   /* GP0 sprite / textured rectangle */\n'
        '    if (lim && s_rr_fx_on && rr_ui_in_race()) rr_uim_sprite(xs, ys, us, vs, texpage, clut_x, clut_y);\n','Sprite tag interface')
    sub('        if (!k) gpu_triangle(x,   y,   c, x+w, y,   c, x,   y+h, c, semi);\n',
        '        s_rr_sprite = 1;   /* GP0 flat rectangle */\n'
        '        if (!k) gpu_triangle(x,   y,   c, x+w, y,   c, x,   y+h, c, semi);\n','Flat rect tag interface')
    sub('static void glb_fill_rect(int x,int y,int w,int h,uint16_t c){\n',
        'static void glb_fill_rect(int x,int y,int w,int h,uint16_t c){\n    rr_uim_fill(x, y, w, h);\n','Fill mask interface')
    sub('    post_aa_release();\n','    post_aa_release();\n    rr_fx_release();\n','Post release interface')
    return source

# Shadow smoothing (launcher / in-game Display: Off / Light / Strong, Off by
# default; pc_services.cpp apply_post -> gl_renderer_rr_shadow_set). Every bike
# and rider shadow is a set of untextured flat quads (GP0 0x2A, semi-
# transparency mode 2 = B - F, colour 0x232526, about 65 a frame), each
# bracketed by E6 3 / E6 0 (mask set + mask check, so overlapping quads darken
# a pixel once) and interleaved with other primitives at the same depth.
# With the option on, during races, a flat triangle of that kind (mode 2, one
# dark grey colour, no native-wide mirror, windowed mode or render pass) is held
# back instead of joining the flat batch. The run stays open across the
# E6 toggles between quads; the next real GPU work (a flat or textured batch
# flush, a fill, copy, upload, readback, drawing-area change or present: all of
# them pass through flush_flat_batch or the textured flush) composites it
# first, so painter order is kept.
# Compositing is incremental per drawing area and frame, which gives soft
# edges with the original's "darken once" rule even when other primitives
# split a shadow into several runs: U holds the union of all shadow triangles
# so far (R8, 2 texels per PS1 pixel), D what has already been subtracted.
# For a run: its triangles are added to U, U is blurred horizontally into T
# around the run, and the picture gets (blur(U) - D) x the shadow colour
# subtracted (vertical blur while compositing), after which D = blur(U) there.
# Coverage 1 is exactly the original B - F. Mask bits follow the original:
# with checking, pixels masked by anything else are skipped; with setting, the
# soft shadow's core (coverage >= 0.5) gets the bit (alpha, and the stencil in
# a colourless pass, plus stencil bit 1 so later runs of the same frame may
# still finish those pixels). The present, a fill or copy, or a new
# drawing area starts a new accumulation. A drawing area larger than the
# target (480x480 PS1 pixels plus margin) draws the original hard quads.
# Off: rr_shadow_take returns at once and nothing else runs.
RR_SH_VS = r'''
#version 330
layout(location=0) in vec2 a_pos;
uniform vec2 u_o; uniform vec2 u_s;
void main(){ gl_Position = vec4((a_pos - u_o) * u_s - 1.0, 0.0, 1.0); }
'''
RR_SH_COVER = r'''
#version 330
out vec4 o;
void main(){ o = vec4(1.0); }
'''
RR_SH_BLUR = r'''
#version 330
out vec4 o;
uniform sampler2D u_tex; uniform vec2 u_inv; uniform float u_sigma; uniform int u_r;
void main(){
  vec2 p = gl_FragCoord.xy; float s = 0.0, w = 0.0;
  for (int i = -10; i <= 10; i++) {
    if (abs(i) > u_r) continue;
    float k = exp(-0.5 * float(i * i) / (u_sigma * u_sigma));
    s += k * texture(u_tex, (p + vec2(float(i), 0.0)) * u_inv).r; w += k;
  }
  o = vec4(s / w);
}
'''
RR_SH_COMPOSITE = r'''
#version 330
out vec4 o;
uniform sampler2D u_tex;    /* T: union coverage, blurred horizontally */
uniform sampler2D u_done;   /* D: coverage already subtracted this frame */
uniform vec2 u_inv; uniform float u_sigma; uniform int u_r;
uniform vec2 u_org; uniform float u_k; uniform float u_hs; uniform vec4 u_col;
uniform int u_cut;   /* 0 picture, 1 mask pass (core only), 2 write D */
void main(){
  vec2 p = (gl_FragCoord.xy / u_hs - u_org) * u_k;   /* coverage texels */
  float s = 0.0, w = 0.0;
  for (int i = -10; i <= 10; i++) {
    if (abs(i) > u_r) continue;
    float k = exp(-0.5 * float(i * i) / (u_sigma * u_sigma));
    s += k * texture(u_tex, (p + vec2(0.0, float(i))) * u_inv).r; w += k;
  }
  float c = s / w;
  if (u_cut == 2) { o = vec4(c); return; }
  if (u_cut == 1 && c < 0.5) discard;
  float d = max(c - texture(u_done, p * u_inv).r, 0.0);
  o = vec4(u_col.rgb * d, c >= 0.5 ? u_col.a : 0.0);   /* u_col.a: the mask bit the original sets */
}
'''
RR_SHADOW = r'''
/* ---- Road Rash shadow smoothing (gpu_presentation.py shadow_smoothing) ---- */
static void mark_prim_dirty(const int *xs, const int *ys, int n, int textured);
static void hr_begin(int clip_to_draw_area);
static void hr_end(void);
static void apply_psx_blend(int mode);
static void mask_stencil_ex(int write_val, int check);
static int s_rr_shadow;                       /* 0 off, 1 light, 2 strong */
void gl_renderer_rr_shadow_set(int step) { GL_RT_SYNC("rr_shadow"); s_rr_shadow = step >= 0 && step <= 2 ? step : 0; }
enum { RR_SH_MAXV = 3 * 2048, RR_SH_TW = 1024, RR_SH_TH = 1024, RR_SH_K = 2, RR_SH_PAD = 5, RR_SH_MARGIN = 16 };
static float s_rr_sh_v[RR_SH_MAXV * 6];       /* GEO vertex layout: x, y, r, g, b, a */
static int s_rr_sh_n, s_rr_sh_col, s_rr_sh_flushing, s_rr_sh_mset, s_rr_sh_mcheck;   /* the run's mask-bit state */
static int s_rr_sh_hold;                      /* inside glb_set_mask_bits: the run stays open */
static int s_rr_sh_sess, s_rr_sh_area[4];     /* accumulation open for this drawing area */
static float s_rr_sh_box[4];
enum { RR_SH_U, RR_SH_T, RR_SH_D };
static GLuint s_rr_sh_prog[3], s_rr_sh_tex[3], s_rr_sh_fbo[3];
static int s_rr_sh_failed;
enum { RS_O, RS_S, RS_TEX, RS_DONE, RS_INV, RS_SIGMA, RS_R, RS_ORG, RS_K, RS_HS, RS_COL, RS_CUT, RS_U };
static GLint s_rr_sh_u[3][RS_U];
static void rr_sh_release(void) {
    memset(s_rr_sh_prog, 0, sizeof s_rr_sh_prog); memset(s_rr_sh_tex, 0, sizeof s_rr_sh_tex);
    memset(s_rr_sh_fbo, 0, sizeof s_rr_sh_fbo); s_rr_sh_failed = 0; s_rr_sh_n = 0; s_rr_sh_sess = 0;
}
static int rr_sh_ready(void) {
    if (s_rr_sh_prog[2]) return 1;
    if (s_rr_sh_failed) return 0;
    const char *fs[3] = { RR_SH_COVER_FS, RR_SH_BLUR_FS, RR_SH_COMPOSITE_FS };
    static const char *const names[RS_U] = { "u_o", "u_s", "u_tex", "u_done", "u_inv", "u_sigma", "u_r", "u_org", "u_k", "u_hs", "u_col", "u_cut" };
    for (int p = 0; p < 3; p++) {
        s_rr_sh_prog[p] = build_program(RR_SH_VS_SRC, fs[p]);
        if (!s_rr_sh_prog[p]) { s_rr_sh_failed = 1; return 0; }
        for (int i = 0; i < RS_U; i++) s_rr_sh_u[p][i] = p_glGetUniformLocation(s_rr_sh_prog[p], names[i]);
    }
    for (int i = 0; i < 3; i++) {
        glGenTextures(1, &s_rr_sh_tex[i]);
        glBindTexture(GL_TEXTURE_2D, s_rr_sh_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, 0x8229 /* GL_R8 */, RR_SH_TW, RR_SH_TH, 0, 0x1903 /* GL_RED */, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p_glGenFramebuffers(1, &s_rr_sh_fbo[i]);
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_rr_sh_fbo[i]);
        p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER, PSXGL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_rr_sh_tex[i], 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, 0);
    return 1;
}
/* The accumulation ends (present, fill, copy). */
static void rr_shadow_end_frame(void) { s_rr_sh_sess = 0; }
static void rr_sh_quad(float x0, float y0, float x1, float y1) {
    const float q[36] = { x0, y0, 0, 0, 0, 0,  x1, y0, 0, 0, 0, 0,  x0, y1, 0, 0, 0, 0,
                          x1, y0, 0, 0, 0, 0,  x0, y1, 0, 0, 0, 0,  x1, y1, 0, 0, 0, 0 };
    p_glBufferData(PSXGL_ARRAY_BUFFER, (ptrdiff_t)sizeof q, q, PSXGL_STREAM_DRAW);
}
/* The held run, drawn now (see above). */
static void rr_shadow_flush(void) {
    if (!s_rr_sh_n || s_rr_sh_flushing || s_rr_sh_hold) return;
    s_rr_sh_flushing = 1;
    const int n = s_rr_sh_n;
    s_rr_sh_n = 0;
    const float x0 = floorf(s_rr_sh_box[0]) - RR_SH_PAD, y0 = floorf(s_rr_sh_box[1]) - RR_SH_PAD;
    const float x1 = ceilf(s_rr_sh_box[2]) + RR_SH_PAD, y1 = ceilf(s_rr_sh_box[3]) + RR_SH_PAD;
    /* The picture under the soft edge changes too: dirty like a primitive. */
    { const int bx[4] = { (int)x0, (int)x1, (int)x0, (int)x1 }, by[4] = { (int)y0, (int)y0, (int)y1, (int)y1 };
      mark_prim_dirty(bx, by, 4, 0); }
    const int same = s_rr_sh_sess && s_rr_sh_area[0] == s_area_x1 && s_rr_sh_area[1] == s_area_y1 &&
                     s_rr_sh_area[2] == s_area_x2 && s_rr_sh_area[3] == s_area_y2;
    const float ox = (float)(s_area_x1 - RR_SH_MARGIN), oy = (float)(s_area_y1 - RR_SH_MARGIN);   /* coverage origin */
    const int fits = (s_area_x2 - s_area_x1 + 1 + 2 * RR_SH_MARGIN) * RR_SH_K <= RR_SH_TW &&
                     (s_area_y2 - s_area_y1 + 1 + 2 * RR_SH_MARGIN) * RR_SH_K <= RR_SH_TH;
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    if (depth) glDisable(GL_DEPTH_TEST);
    p_glActiveTexture(PSXGL_TEXTURE0);
    if (!fits || !rr_sh_ready()) {
        /* The original hard-edged draw. */
        hr_begin(1);
        apply_psx_blend(2);
        mask_stencil_ex(s_rr_sh_mset, s_rr_sh_mcheck);
        p_glUseProgram(s_geo_prog);
        p_glBindVertexArray(s_geo_vao);
        p_glBindBuffer(PSXGL_ARRAY_BUFFER, s_geo_vbo);
        p_glBufferData(PSXGL_ARRAY_BUFFER, (ptrdiff_t)(n * 6 * sizeof(float)), s_rr_sh_v, PSXGL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, n);
        hr_end();
        psx_mod_counter_add("rr.shadow.hard", 1);
    } else {
        const float sigma = (s_rr_shadow == 2 ? 1.6f : 0.8f) * RR_SH_K;
        const int r = (int)ceilf(2.5f * sigma) > 10 ? 10 : (int)ceilf(2.5f * sigma);
        const float cs[2] = { 2.0f * RR_SH_K / RR_SH_TW, 2.0f * RR_SH_K / RR_SH_TH };
        p_glBindVertexArray(s_geo_vao);
        p_glBindBuffer(PSXGL_ARRAY_BUFFER, s_geo_vbo);
        glDisable(GL_BLEND); glDisable(GL_STENCIL_TEST);
        glViewport(0, 0, RR_SH_TW, RR_SH_TH);
        glDisable(GL_SCISSOR_TEST);
        if (!same) {   /* a new accumulation: U and D empty */
            glClearColor(0.f, 0.f, 0.f, 0.f);
            p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_rr_sh_fbo[RR_SH_U]); glClear(GL_COLOR_BUFFER_BIT);
            p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_rr_sh_fbo[RR_SH_D]); glClear(GL_COLOR_BUFFER_BIT);
            s_rr_sh_sess = 1;
            s_rr_sh_area[0] = s_area_x1; s_rr_sh_area[1] = s_area_y1; s_rr_sh_area[2] = s_area_x2; s_rr_sh_area[3] = s_area_y2;
        }
        /* The run into U (overlaps stay 1). */
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_rr_sh_fbo[RR_SH_U]);
        p_glUseProgram(s_rr_sh_prog[0]);
        p_glUniform2f(s_rr_sh_u[0][RS_O], ox, oy);
        p_glUniform2f(s_rr_sh_u[0][RS_S], cs[0], cs[1]);
        p_glBufferData(PSXGL_ARRAY_BUFFER, (ptrdiff_t)(n * 6 * sizeof(float)), s_rr_sh_v, PSXGL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, n);
        /* U blurred horizontally into T, over the run's box plus the vertical
         * blur's reach. */
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_rr_sh_fbo[RR_SH_T]);
        glBindTexture(GL_TEXTURE_2D, s_rr_sh_tex[RR_SH_U]);
        p_glUseProgram(s_rr_sh_prog[1]);
        p_glUniform2f(s_rr_sh_u[1][RS_O], ox, oy);
        p_glUniform2f(s_rr_sh_u[1][RS_S], cs[0], cs[1]);
        p_glUniform1i(s_rr_sh_u[1][RS_TEX], 0);
        p_glUniform2f(s_rr_sh_u[1][RS_INV], 1.0f / RR_SH_TW, 1.0f / RR_SH_TH);
        p_glUniform1f(s_rr_sh_u[1][RS_SIGMA], sigma);
        p_glUniform1i(s_rr_sh_u[1][RS_R], r);
        rr_sh_quad(x0, y0 - RR_SH_PAD, x1, y1 + RR_SH_PAD);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        /* Subtract the new part from the picture, inside the drawing area.
         * Stencil bit 1 (unused by the renderer, which masks with 0x01) marks
         * pixels this accumulation itself masked: later runs may still add
         * to them, so the check skips only (stencil & 3) == 1, pixels masked
         * by anything else. */
        GLint wmask = 0x01;
        glGetIntegerv(GL_STENCIL_WRITEMASK, &wmask);
        hr_begin(1);
        if (!same) { glStencilMask(0x02); glClearStencil(0); glClear(GL_STENCIL_BUFFER_BIT); }
        p_glBindVertexArray(s_geo_vao);
        p_glBindBuffer(PSXGL_ARRAY_BUFFER, s_geo_vbo);
        if (s_rr_sh_mcheck) {
            glEnable(GL_STENCIL_TEST); glStencilMask(0x00);
            glStencilFunc(GL_NOTEQUAL, 1, 0x03); glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        } else glDisable(GL_STENCIL_TEST);
        glEnable(GL_BLEND);
        p_glBlendEquationSeparate(PSXGL_FUNC_REVERSE_SUBTRACT, PSXGL_FUNC_ADD);
        if (s_rr_sh_mset) p_glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        else p_glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
        p_glActiveTexture(PSXGL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, s_rr_sh_tex[RR_SH_D]);
        p_glActiveTexture(PSXGL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_rr_sh_tex[RR_SH_T]);
        p_glUseProgram(s_rr_sh_prog[2]);
        p_glUniform2f(s_rr_sh_u[2][RS_O], 0.0f, 0.0f);
        p_glUniform2f(s_rr_sh_u[2][RS_S], 2.0f / VRAM_W, 2.0f / VRAM_H);
        p_glUniform1i(s_rr_sh_u[2][RS_TEX], 0);
        p_glUniform1i(s_rr_sh_u[2][RS_DONE], 1);
        p_glUniform2f(s_rr_sh_u[2][RS_INV], 1.0f / RR_SH_TW, 1.0f / RR_SH_TH);
        p_glUniform1f(s_rr_sh_u[2][RS_SIGMA], sigma);
        p_glUniform1i(s_rr_sh_u[2][RS_R], r);
        p_glUniform2f(s_rr_sh_u[2][RS_ORG], ox, oy);
        p_glUniform1f(s_rr_sh_u[2][RS_K], (float)RR_SH_K);
        p_glUniform1f(s_rr_sh_u[2][RS_HS], (float)s_hr_scale);
        p_glUniform4f(s_rr_sh_u[2][RS_COL], s_rr_sh_v[2], s_rr_sh_v[3], s_rr_sh_v[4], s_rr_sh_mset ? 1.0f : 0.0f);
        p_glUniform1i(s_rr_sh_u[2][RS_CUT], 0);
        rr_sh_quad(x0, y0, x1, y1);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        if (s_rr_sh_mset) {
            /* Mask bit (bit 0) plus "masked by this accumulation" (bit 1):
             * with checking, only where bit 0 is still clear. */
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glEnable(GL_STENCIL_TEST); glStencilMask(0x03);
            if (s_rr_sh_mcheck) glStencilFunc(GL_NOTEQUAL, 3, 0x01); else glStencilFunc(GL_ALWAYS, 3, 0x03);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            p_glUniform1i(s_rr_sh_u[2][RS_CUT], 1);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        glStencilMask((GLuint)wmask);
        hr_end();
        /* D = blur(U) over the same box (coverage space: p = gl_FragCoord). */
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, RR_SH_TW, RR_SH_TH);
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_rr_sh_fbo[RR_SH_D]);
        p_glActiveTexture(PSXGL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, 0);   /* D is the target now */
        p_glActiveTexture(PSXGL_TEXTURE0);
        p_glBindVertexArray(s_geo_vao);
        p_glBindBuffer(PSXGL_ARRAY_BUFFER, s_geo_vbo);
        p_glUseProgram(s_rr_sh_prog[2]);
        p_glUniform2f(s_rr_sh_u[2][RS_O], ox, oy);
        p_glUniform2f(s_rr_sh_u[2][RS_S], cs[0], cs[1]);
        p_glUniform2f(s_rr_sh_u[2][RS_ORG], 0.0f, 0.0f);
        p_glUniform1f(s_rr_sh_u[2][RS_HS], (float)RR_SH_K);
        p_glUniform1i(s_rr_sh_u[2][RS_CUT], 2);
        rr_sh_quad(x0, y0, x1, y1);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindTexture(GL_TEXTURE_2D, 0);
        p_glBindVertexArray(0);
        p_glUseProgram(0);
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER, 0);
        psx_mod_counter_add("rr.shadow.runs", 1);
    }
    if (depth) glEnable(GL_DEPTH_TEST);
    s_rr_sh_flushing = 0;
}
/* gpu_geometry: hold a shadow triangle (1 = taken). */
static int rr_shadow_take(const int *xs, const int *ys, const uint16_t *cs, int n, int semi, int precise) {
    if (!s_rr_shadow || n != 3 || semi != 2) return 0;
    const int c = cs[0] & 0x7FFF;
    if ((cs[1] & 0x7FFF) != c || (cs[2] & 0x7FFF) != c) return 0;
    const int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    if (r > 6 || g > 6 || b > 6 || r - g > 1 || g - r > 1 || g - b > 1 || b - g > 1) return 0;
    /* A shadow-coloured triangle the smoothing leaves alone: counted by reason. */
    const char *why = g_wide_cur ? "rr.shadow.skip_wide" : s_hiw ? "rr.shadow.skip_window"
                    : s_pass_active ? "rr.shadow.skip_pass" : s_rr_sh_failed ? "rr.shadow.skip_failed" : !rr_ui_in_race() ? "rr.shadow.skip_not_race" : NULL;
    if (why) { psx_mod_counter_add(why, 1); return 0; }
    const int mset = s_mask_set ? 1 : 0, mcheck = s_mask_check ? 1 : 0;
    if (s_rr_sh_n && (c != s_rr_sh_col || mset != s_rr_sh_mset || mcheck != s_rr_sh_mcheck || s_rr_sh_n + 3 > RR_SH_MAXV))
        rr_shadow_flush();
    if (!s_rr_sh_n) {
        flush_flat_batch();   /* earlier flat primitives first (the run is empty: no recursion) */
        s_rr_sh_col = c; s_rr_sh_mset = mset; s_rr_sh_mcheck = mcheck;
        s_rr_sh_box[0] = s_rr_sh_box[1] = 1e9f; s_rr_sh_box[2] = s_rr_sh_box[3] = -1e9f;
    }
    for (int i = 0; i < 3; i++) {
        float *v = &s_rr_sh_v[s_rr_sh_n++ * 6];
        v[0] = precise ? s_pc_x[i] : (float)xs[i];
        v[1] = precise ? s_pc_y[i] : (float)ys[i];
        v[2] = (float)(r << 3) / 255.0f; v[3] = (float)(g << 3) / 255.0f; v[4] = (float)(b << 3) / 255.0f; v[5] = (float)mset;
        if (v[0] < s_rr_sh_box[0]) s_rr_sh_box[0] = v[0]; if (v[0] > s_rr_sh_box[2]) s_rr_sh_box[2] = v[0];
        if (v[1] < s_rr_sh_box[1]) s_rr_sh_box[1] = v[1]; if (v[1] > s_rr_sh_box[3]) s_rr_sh_box[3] = v[1];
    }
    return 1;
}
'''
def shadow_smoothing(source):
    def sub(old,new,what):
        nonlocal source
        if source.count(old)!=1:raise ValueError(what+' changed')
        source=source.replace(old,new)
    block=(_cstr('RR_SH_VS_SRC', RR_SH_VS)+_cstr('RR_SH_COVER_FS', RR_SH_COVER)+_cstr('RR_SH_BLUR_FS', RR_SH_BLUR)
           +_cstr('RR_SH_COMPOSITE_FS', RR_SH_COMPOSITE)+RR_SHADOW)
    anchor='/* Present sampling. u_sharp==0 is the historical behaviour'
    sub(anchor,block+anchor,'Shadow anchor')
    sub('static void flush_flat_batch_impl(void) {\n    if (s_fb_n == 0) return;\n',
        'static void flush_flat_batch_impl(void) {\n    rr_shadow_flush();   /* a held shadow run goes first */\n    if (s_fb_n == 0) return;\n','Flat flush shadow interface')
    sub('static void flush_tex_batch_impl(void) {\n    if (s_tb_n == 0) return;\n',
        'static void flush_tex_batch_impl(void) {\n    if (s_tb_n == 0) return;\n    rr_shadow_flush();   /* a held shadow run goes first */\n','Textured flush shadow interface')
    sub('    int gate = bd_prim_gate(xs, n, 0);\n    const int dmode = n == 3 ? pgxp_tri_depth_mode(semi) : 0;\n',
        '    if (rr_shadow_take(xs, ys, cs, n, semi, precise)) return;\n'
        '    int gate = bd_prim_gate(xs, n, 0);\n    const int dmode = n == 3 ? pgxp_tri_depth_mode(semi) : 0;\n','Flat triangle shadow interface')
    sub('static void glb_fill_rect(int x,int y,int w,int h,uint16_t c){\n',
        'static void glb_fill_rect(int x,int y,int w,int h,uint16_t c){\n    rr_shadow_flush();\n','Fill shadow interface')
    sub('static void glb_copy_rect(int sx,int sy,int dx,int dy,int w,int h){\n',
        'static void glb_copy_rect(int sx,int sy,int dx,int dy,int w,int h){\n    rr_shadow_flush();\n','Copy shadow interface')
    sub('    rr_fx_release();\n','    rr_fx_release();\n    rr_sh_release();\n','Shadow release interface')
    # The game brackets each shadow quad with E6 3 / E6 0: the mask change keeps the run open.
    sub('static void glb_set_mask_bits(int s, int c) {\n    int next_check = c ? 1 : 0;\n    if (next_check != s_mask_check) {\n',
        'static void glb_set_mask_bits(int s, int c) {\n    int next_check = c ? 1 : 0;\n    s_rr_sh_hold = 1;\n    if (next_check != s_mask_check) {\n','Mask bits shadow interface')
    sub('    s_mask_set = s ? 1 : 0;\n    s_mask_check = next_check;\n    sw_set_mask_bits(s, c);\n',
        '    s_mask_set = s ? 1 : 0;\n    s_mask_check = next_check;\n    s_rr_sh_hold = 0;\n    sw_set_mask_bits(s, c);\n','Mask bits shadow end interface')
    # A new frame (present), a fill or a copy starts a new accumulation.
    sub('    rr_post_apply(path, dx, dy, w, h, lx, ly, lw, lh);\n','    rr_post_apply(path, dx, dy, w, h, lx, ly, lw, lh);\n    rr_shadow_end_frame();\n','Present shadow interface')
    sub('static void pres_record(','static void rr_shadow_end_frame(void);\nstatic void pres_record(','Present shadow declaration')
    sub('static void glb_fill_rect(int x,int y,int w,int h,uint16_t c){\n    rr_shadow_flush();\n','static void glb_fill_rect(int x,int y,int w,int h,uint16_t c){\n    rr_shadow_flush(); rr_shadow_end_frame();\n','Fill shadow end interface')
    sub('static void glb_copy_rect(int sx,int sy,int dx,int dy,int w,int h){\n    rr_shadow_flush();\n','static void glb_copy_rect(int sx,int sy,int dx,int dy,int w,int h){\n    rr_shadow_flush(); rr_shadow_end_frame();\n','Copy shadow end interface')
    return source

# Run last, once every recipe above is defined (the public installer runs this file as a script).
if __name__=='__main__':
    import pathlib,sys
    pathlib.Path(sys.argv[2]).write_text(extend(pathlib.Path(sys.argv[1]).read_text()))
