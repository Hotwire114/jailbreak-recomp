"""Allow a title to identify its own non-video frontend, and lay out the race
HUD correctly in widescreen: every widget keeps its screen edge and its 4:3
shape at sub-pixel precision, the minimap's clip follows its frame, and the
HUD stays laid out behind the pause menu.
The runtime's generic MDEC heuristic otherwise mistakes animated menu art for
full-screen video. No game instructions or assets. LGPL-2.1-or-later.
"""

# Exact HUD placement. auto_ui_squash keeps a widget's shape by squashing it
# about its screen-edge anchor, but GP0 coordinates are whole native pixels:
# every edge snapped to the 384-wide grid, so a 6-pixel glyph came out 4 or 5
# pixels wide at 16:9 (3 or 4 at 21:9), the digits of one readout were spaced
# unevenly and 2-pixel minimap dots lost half their width. The transform now
# also records the exact squashed position of the packet it moved; the draw
# hands that to the renderer as a 16.16 override (gpu_presentation.py) for the
# rectangle or the triangles the packet draws. The integer position is still
# what the GPU state, dirty tracking and the software mirror use. Overrides
# carry RR_HUD_MARK in y (8192 px, outside any real coordinate) so the
# renderer can tell them from the GTE's sub-pixel triangles.
EXACT_RECORD = r'''
typedef struct {
    int n, rect;
    uint32_t src, cmd;
    int32_t rx[4], ry[4];   /* packet vertex before the squash */
    double ex[4], ey[4];    /* exact squashed vertex, before the draw offset */
} RrHudExact;
static RrHudExact rr_hx;
#define RR_HUD_MARK 0x20000000
/* Lettering (native_ui text tags): the renderer draws it with the HD UI text
 * reconstruction. Far from RR_HUD_MARK, so off-screen (negative) y stays in range. */
#define RR_TEXT_MARK 0x40000000
static int rr_text_flag;   /* the packet being drawn is tagged text */
static double rr_ui_xd(int32_t x, int32_t ax, double s) {
    return ax + (double)(x - ax) * ws_xnum / ws_xden * (s < 1.0 ? s : 1.0);
}
static double rr_ui_yd(int32_t y, int32_t ay, double s) {
    return s >= 1.0 ? (double)y : ay + (double)(y - ay) * s;
}
static void rr_hx_begin(int rect) {
    rr_hx.n = 0;
    rr_hx.rect = rect;
    rr_hx.src = gp0_cmd_source_addr;
    rr_hx.cmd = gp0_cmd_buf[0];
}
static void rr_hx_add(int32_t x, int32_t y, double ex, double ey) {
    if (rr_hx.n >= 4) return;
    rr_hx.rx[rr_hx.n] = x; rr_hx.ry[rr_hx.n] = y;
    rr_hx.ex[rr_hx.n] = ex; rr_hx.ey[rr_hx.n] = ey;
    rr_hx.n++;
}
static int rr_hx_current(void) {
    return rr_hx.n && rr_hx.src == gp0_cmd_source_addr && rr_hx.cmd == gp0_cmd_buf[0];
}
static int32_t rr_fix(double v) { return (int32_t)lround(v * 65536.0); }
/* ws_projection_hud_transform, recording the exact position. */
static int rr_hx_projection(int32_t *vx, const int32_t *vy, int n) {
    int32_t a;
    if (!ws_projection_hud_anchor(&a)) return 0;
    rr_hx_begin(0);
    for (int i = 0; i < n; i++) {
        rr_hx_add(vx[i], vy[i], rr_ui_xd(vx[i], a, 1.0), vy[i]);
        vx[i] = ws_scale_about(vx[i], a);
    }
    return 1;
}
/* The auto-UI vertex squash, recording the exact position. */
static void rr_hx_ui(int32_t *vx, int32_t *vy, int n, const WsUiPrepassItem *it, double s) {
    const int32_t d = ws_shared_ui_delta();
    rr_hx_begin(0);
    for (int i = 0; i < n; i++) {
        rr_hx_add(vx[i], vy[i], rr_ui_xd(vx[i], it->group.anchor, s) + d,
                  rr_ui_yd(vy[i], it->anchor_y, s));
        vx[i] = ws_ui_x(vx[i], it->group.anchor, s) + d;
        vy[i] = ws_ui_y(vy[i], it->anchor_y, s);
    }
}
static void rr_hx_rect(int32_t x, int32_t y, int w, int h, int32_t a, int32_t ay,
                       double s, int32_t d) {
    rr_hx_begin(1);
    rr_hx_add(x, y, rr_ui_xd(x, a, s) + d, rr_ui_yd(y, ay, s));
    rr_hx_add(x + w, y + h, rr_ui_xd(x + w, a, s) + d, rr_ui_yd(y + h, ay, s));
}
/* A large panel admitted from the race overlay list (overlay_ranks). */
static int rr_big_admitted(void) {
    if (!rr_ui_race_active()) return 0;
    const WsUiPrepassItem *it = ws_auto_ui_item();
    return it && it->rr_big;
}
'''

# A race frame is three DMA lists: the sky (a few buckets of polygons), the
# world (~1300 buckets) and the 2D overlay (a dozen buckets: the HUD in one,
# and while paused the pause panel and its text in the buckets above). Only the
# overlay is UI. It is the small table that contains GP0 sprites/rectangles,
# which the GTE never produces; the sky and world lists admit nothing during a
# race (the prepass used to take the sky's front bucket as UI too).
# The prepass took only the frontmost populated bucket as UI, so with the pause
# menu up the HUD behind it was left at its 4:3 coordinates and stretched. In
# the overlay every bucket is UI: admit them all, including large panels (the
# pause box; a full-width strip is still never squashed), and group each bucket
# on its own so the pause panel does not merge with the HUD boxes its corners
# overlap.
GROUP_BY_RANK = r'''
static void rr_group_by_rank(WsUiGroupItem *groups, uint32_t count) {
    static WsUiGroupItem sub[WS_UI_PREPASS_MAX];
    static uint32_t idx[WS_UI_PREPASS_MAX];
    static uint8_t done[WS_UI_PREPASS_MAX];
    memset(done, 0, count);
    for (uint32_t i = 0; i < count; i++) {
        if (done[i]) continue;
        const uint16_t r = ws_ui_prepass[i].ot_rank;
        uint32_t n = 0;
        for (uint32_t j = i; j < count; j++) {
            if (done[j] || ws_ui_prepass[j].ot_rank != r) continue;
            idx[n] = j; sub[n] = groups[j]; done[j] = 1; n++;
        }
        ws_ui_group_assign(sub, n, ws_disp_w(), ws_auto_ui_dense, ws_auto_ui_in_place);
        for (uint32_t k = 0; k < n; k++) {
            groups[idx[k]].anchor = sub[k].anchor;
            groups[idx[k]].root = idx[sub[k].root < n ? sub[k].root : k];
        }
    }
}
'''


def overlay_ranks(source):
    source=_sub(source,'    uint8_t  enclosed_only;\n','    uint8_t  enclosed_only;\n    uint8_t  rr_big;\n',what='Prepass item')
    source=_sub(source,
        '    if ((min_x <= X + W / 64 && max_x >= X + W - W / 64) ||\n'
        '        (width > W / 2 && height > H / 4)) {\n'
        '        ws_ui_reject.too_big++;',
        '    const int rr_big = width > W / 2 && height > H / 4;\n'
        '    if ((min_x <= X + W / 64 && max_x >= X + W - W / 64) ||\n'
        '        (rr_big && !rr_ui_race_active())) {\n'
        '        ws_ui_reject.too_big++;',what='Prepass size gate')
    source=_sub(source,'    item->enclosed_only = (uint8_t)enclosed_only;\n',
                '    item->enclosed_only = (uint8_t)enclosed_only;\n    item->rr_big = (uint8_t)rr_big;\n',what='Prepass item fill')
    source=_sub(source,
        '    uint16_t max_rank = 0xFFFFu;\n',
        '    uint16_t max_rank = 0xFFFFu;\n    int rr_sprites = 0;\n',what='Prepass walk state')
    source=_sub(source,
        '                uint8_t op = (uint8_t)(first >> 24);\n',
        '                uint8_t op = (uint8_t)(first >> 24);\n'
        '                if (op >= 0x60u && op <= 0x7Fu) rr_sprites = 1;\n',what='Prepass walk')
    source=_sub(source,
        '    static uint8_t keep[WS_UI_PREPASS_MAX];\n'
        '    for (uint32_t i = 0; i < ws_ui_prepass_count; i++)\n'
        '        keep[i] = ws_ui_prepass[i].ot_rank == max_rank &&\n'
        '                  !ws_ui_prepass[i].enclosed_only;\n',
        '    const int rr_race = rr_ui_race_active();\n'
        '    const int rr_overlay = rr_race && rank < 16u && rr_sprites;\n'
        '    static uint8_t keep[WS_UI_PREPASS_MAX];\n'
        '    for (uint32_t i = 0; i < ws_ui_prepass_count; i++)\n'
        '        keep[i] = (ws_ui_prepass[i].ot_rank == max_rank || rr_overlay) &&\n'
        '                  (rr_overlay || !rr_race) &&\n'
        '                  !ws_ui_prepass[i].enclosed_only &&\n'
        '                  (!ws_ui_prepass[i].rr_big || rr_overlay);\n',what='Prepass rank filter')
    source=_sub(source,
        '            if (keep[i] || it->enclosed_only || it->ot_rank != backing_rank) continue;',
        '            if (keep[i] || it->enclosed_only || it->rr_big || it->ot_rank != backing_rank) continue;',
        what='Prepass backing filter')
    source=_sub(source,
        '        if (!it->enclosed_only || it->ot_rank != max_rank) continue;',
        '        if (!it->enclosed_only || (it->ot_rank != max_rank && !rr_overlay)) continue;',
        what='Prepass enclosed filter')
    source=_sub(source,
        '    ws_ui_group_assign(groups, ws_ui_prepass_count, ws_disp_w(),\n'
        '                       ws_auto_ui_dense, ws_auto_ui_in_place);\n',
        '    if (rr_overlay) rr_group_by_rank(groups, ws_ui_prepass_count);\n'
        '    else ws_ui_group_assign(groups, ws_ui_prepass_count, ws_disp_w(),\n'
        '                            ws_auto_ui_dense, ws_auto_ui_in_place);\n',what='Prepass grouping')
    anchor='void gpu_ws_prepass_linked_list(uint32_t start_addr) {'
    source=_sub(source,anchor,GROUP_BY_RANK+'\n'+anchor,what='Prepass interface')
    # The transforms' own size gates let an admitted panel through.
    source=_sub(source,
        '    if ((min_x <= X && max_x >= X + W && min_y <= 0 && max_y >= H) ||\n'
        '        (width > W / 2 && height > H / 4))\n'
        '        return 0;',
        '    if ((min_x <= X && max_x >= X + W && min_y <= 0 && max_y >= H) ||\n'
        '        (width > W / 2 && height > H / 4 && !rr_big_admitted()))\n'
        '        return 0;',what='Quad size gate')
    source=_sub(source,
        '    if ((*x <= X && *x + *w >= X + W && *y <= 0 && *y + *h >= H) ||\n'
        '        (*w > W / 2 && *h > H / 4))\n'
        '        return 0;',
        '    if ((*x <= X && *x + *w >= X + W && *y <= 0 && *y + *h >= H) ||\n'
        '        (*w > W / 2 && *h > H / 4 && !rr_big_admitted()))\n'
        '        return 0;',what='Rect size gate')
    return source

# Race HUD editor (hud_editor.cpp, the F2 editor's "Race HUD" page): the
# owner's X/Y offset and size per widget, applied where the exact HUD position
# goes to the renderer (the override below), so in every aspect, after the
# widescreen squash. The integer packet position, the CPU-side clip tests, the
# software mirror and dirty tracking stay the game's own; only the drawn image
# moves. Packets count only while the race's 2D overlay list is drawn (its
# few ordering-table buckets and sprites, as overlay_ranks) and edits exist,
# so with no edits nothing here runs and 4:3 output is unchanged.
# A packet's widget is found from its 4:3 packet coordinates (before any
# squash) within the current view (the last full-view drawing area, so each
# split-screen view's HUD is placed in its own view): inside the minimap clip
# (the small drawing area of the overlay); else left third: speed box (upper
# half) or your bars and name (lower half); right third: position box or the
# opponent's bars and name; middle third: centre messages (not while paused:
# that is the pause menu). Large panels are never moved. Each widget is
# scaled about its corner at the screen edge (the centre group about its
# middle), taken from the widget's extent in the previous overlay of the same
# view, and moved by the offset in pixels of the 240-line picture (x narrowed
# by the widescreen squash, so an offset is the same on screen in any aspect).
# The minimap clip moves with its widget.
HUD_EDIT = r'''
extern int rr_hud_edit_live(void);
extern int rr_hud_edit_get(int g, double *dx, double *dy, double *s);
extern int rr_hud_edit_paused(void);
extern int rr_ui_in_race(void);
extern int rr_showroom_hud_off(void);    /* showroom.cpp: the Rider look showroom draws no HUD at all */
enum { RR_HE_SPEED, RR_HE_POS, RR_HE_MAP, RR_HE_YOU, RR_HE_THEM, RR_HE_CENTRE, RR_HE_N };
static int rr_he_on;                     /* the race overlay list is being drawn, edits exist */
static uint32_t rr_he_serial;            /* bumps per overlay list */
static int32_t rr_he_view[4] = { 0, 0, 383, 239 };   /* last full-view drawing area (inclusive) */
static int32_t rr_he_map[4];
static uint32_t rr_he_map_serial;
static int rr_he_map_ok, rr_he_small_area;
typedef struct {
    int32_t key[4];
    uint32_t serial, used;
    double cur[RR_HE_N][4], last[RR_HE_N][4];
    uint8_t has_cur[RR_HE_N], has_last[RR_HE_N];
} RrHeSlot;
static RrHeSlot rr_he_slots[4];
static uint32_t rr_he_use;
static int32_t rr_he_sx(uint32_t v) { return ((int32_t)(v << 21)) >> 21; }
/* The packet being drawn: its box in 4:3 VRAM coordinates. */
static int rr_he_packet_box(int32_t b[4]) {
    const uint32_t op = gp0_cmd_buf[0] >> 24;
    int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    if (op >= 0x20u && op <= 0x3Fu) {
        const int n = (op & 8u) ? 4 : 3, stride = 1 + ((op & 4u) ? 1 : 0) + ((op & 0x10u) ? 1 : 0);
        for (int i = 0; i < n; i++) {
            const uint32_t w = gp0_cmd_buf[1 + i * stride];
            const int32_t x = rr_he_sx(w), y = rr_he_sx(w >> 16);
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    } else if (op >= 0x60u && op <= 0x7Fu) {
        const uint32_t w = gp0_cmd_buf[1];
        int sw, sh;
        x0 = rr_he_sx(w); y0 = rr_he_sx(w >> 16);
        switch ((op >> 3) & 3u) {
        case 0: { const uint32_t wh = gp0_cmd_buf[(op & 4u) ? 3 : 2]; sw = (int)(wh & 0x3FFu); sh = (int)((wh >> 16) & 0x1FFu); break; }
        case 1: sw = sh = 1; break;
        case 2: sw = sh = 8; break;
        default: sw = sh = 16; break;
        }
        x1 = x0 + sw; y1 = y0 + sh;
    } else return 0;
    b[0] = x0 + draw_offset_x; b[2] = x1 + draw_offset_x;
    b[1] = y0 + draw_offset_y; b[3] = y1 + draw_offset_y;
    return 1;
}
static int rr_he_group(const int32_t b[4]) {
    const int32_t W = ws_disp_w(), H = ws_disp_h();
    if (b[2] - b[0] > W / 2 && b[3] - b[1] > H / 4) return -1;   /* panels, fades */
    const double cx = (b[0] + b[2]) * 0.5, cy = (b[1] + b[3]) * 0.5;
    if (rr_he_map_ok && rr_he_serial - rr_he_map_serial <= 8u &&
        cx >= rr_he_map[0] - 2 && cx <= rr_he_map[2] + 3 &&
        cy >= rr_he_map[1] - 2 && cy <= rr_he_map[3] + 3) return RR_HE_MAP;
    const double fx = (cx - rr_he_view[0]) / (double)(rr_he_view[2] - rr_he_view[0] + 1);
    const double fy = (cy - rr_he_view[1]) / (double)(rr_he_view[3] - rr_he_view[1] + 1);
    if (fx < 1.0 / 3.0) return fy < 0.5 ? RR_HE_SPEED : RR_HE_YOU;
    if (fx > 2.0 / 3.0) return fy < 0.5 ? RR_HE_POS : RR_HE_THEM;
    return rr_hud_edit_paused() ? -1 : RR_HE_CENTRE;
}
/* The current view's widget extents (this overlay and the previous one). */
static RrHeSlot *rr_he_slot(void) {
    RrHeSlot *s = NULL, *old = &rr_he_slots[0];
    for (int i = 0; i < 4; i++) {
        if (!memcmp(rr_he_slots[i].key, rr_he_view, sizeof rr_he_view) && rr_he_slots[i].used) { s = &rr_he_slots[i]; break; }
        if (rr_he_slots[i].used < old->used) old = &rr_he_slots[i];
    }
    if (!s) { s = old; memset(s, 0, sizeof *s); memcpy(s->key, rr_he_view, sizeof rr_he_view); s->serial = rr_he_serial; }
    s->used = ++rr_he_use;
    if (s->serial != rr_he_serial) {
        for (int g = 0; g < RR_HE_N; g++) {
            if (s->has_cur[g]) { memcpy(s->last[g], s->cur[g], sizeof s->cur[g]); s->has_last[g] = 1; }
            s->has_cur[g] = 0;
        }
        s->serial = rr_he_serial;
    }
    return s;
}
/* Anchor of widget g: its corner at the screen edge, the centre group's middle. */
static int rr_he_anchor(const RrHeSlot *sl, int g, double *ax, double *ay) {
    const double *a = sl->has_last[g] ? sl->last[g] : sl->has_cur[g] ? sl->cur[g] : NULL;
    if (!a) return 0;
    switch (g) {
    case RR_HE_SPEED: *ax = a[0]; *ay = a[1]; break;
    case RR_HE_POS: case RR_HE_MAP: *ax = a[2]; *ay = a[1]; break;
    case RR_HE_YOU: *ax = a[0]; *ay = a[3]; break;
    case RR_HE_THEM: *ax = a[2]; *ay = a[3]; break;
    default: *ax = (a[0] + a[2]) * 0.5; *ay = (a[1] + a[3]) * 0.5; break;
    }
    return 1;
}
static void rr_he_apply(int g, const double *dxys, double ax, double ay, double *x, double *y, int n) {
    const double xf = ws_active() && ws_xden > 0 ? (double)ws_xnum / (double)ws_xden : 1.0;
    for (int i = 0; i < n; i++) {
        x[i] = ax + (x[i] - ax) * dxys[2] + dxys[0] * xf;
        y[i] = ay + (y[i] - ay) * dxys[2] + dxys[1];
    }
    (void)g;
}
/* Edits the n final points (VRAM coordinates) of the packet being drawn. */
static int rr_he_points(double *x, double *y, int n) {
    if (!rr_he_on) return 0;
    if (rr_showroom_hud_off()) {   /* every piece of the race's 2D overlay, off screen */
        for (int i = 0; i < n; i++) { x[i] = -4000.0; y[i] = -4000.0; }
        return 1;
    }
    int32_t b[4];
    if (!rr_he_packet_box(b)) return 0;
    const int g = rr_he_group(b);
    if (g < 0) return 0;
    RrHeSlot *sl = rr_he_slot();
    double *c = sl->cur[g];
    for (int i = 0; i < n; i++) {
        if (!sl->has_cur[g]) { c[0] = c[2] = x[i]; c[1] = c[3] = y[i]; sl->has_cur[g] = 1; continue; }
        if (x[i] < c[0]) c[0] = x[i];
        if (x[i] > c[2]) c[2] = x[i];
        if (y[i] < c[1]) c[1] = y[i];
        if (y[i] > c[3]) c[3] = y[i];
    }
    double e[3], ax, ay;
    if (!rr_hud_edit_get(g, &e[0], &e[1], &e[2]) || !rr_he_anchor(sl, g, &ax, &ay)) return 0;
    rr_he_apply(g, e, ax, ay, x, y, n);
    return 1;
}
/* Drawing area changes (rr_set_draw_area): the raw area picks the view or the
 * minimap clip ... */
static void rr_he_note_area(int l, int t, int r, int b) {
    const int32_t W = ws_disp_w(), H = ws_disp_h(), w = r - l + 1, h = b - t + 1;
    rr_he_small_area = 0;
    if (w >= W / 2 && h >= H / 3) {
        rr_he_view[0] = l; rr_he_view[1] = t; rr_he_view[2] = r; rr_he_view[3] = b;
    } else if (rr_he_on && w > 0 && h > 0 && w < W / 2 && h < H / 2) {
        rr_he_map[0] = l; rr_he_map[1] = t; rr_he_map[2] = r; rr_he_map[3] = b;
        rr_he_map_serial = rr_he_serial; rr_he_map_ok = 1; rr_he_small_area = 1;
    }
}
/* ... and the minimap clip the renderer gets moves with the minimap. */
static void rr_he_area(int *l, int *t, int *r, int *b) {
    if (!rr_he_on || !rr_he_small_area) return;
    double e[3], ax, ay;
    if (!rr_hud_edit_get(RR_HE_MAP, &e[0], &e[1], &e[2])) return;
    if (!rr_he_anchor(rr_he_slot(), RR_HE_MAP, &ax, &ay)) { ax = *r + 1; ay = *t; }
    double x[2] = { (double)*l, (double)*r + 1 }, y[2] = { (double)*t, (double)*b + 1 };
    rr_he_apply(RR_HE_MAP, e, ax, ay, x, y, 2);
    *l = (int)floor(x[0] + 1e-6); *r = (int)ceil(x[1] - 1e-6) - 1;
    *t = (int)floor(y[0] + 1e-6); *b = (int)ceil(y[1] - 1e-6) - 1;
    if (*l < 0) *l = 0;
    if (*t < 0) *t = 0;
    if (*r > 1023) *r = 1023;
    if (*b > 511) *b = 511;
}
'''
# Before each linked list is drawn: is it the race's 2D overlay?
HUD_EDIT_LIST = r'''
static void rr_he_list(uint32_t start_addr) {
    rr_he_on = 0;
    if (!rr_ui_in_race() || (!rr_hud_edit_live() && !rr_showroom_hud_off())) return;
    uint32_t addr = psx_mod_gpu_dma_resolve_address(start_addr);
    int empty = 0, sprites = 0;
    for (uint32_t k = 0; k < 4096u; k++) {
        const uint32_t header = psx_read_word(addr), n = header >> 24;
        if (!n) { if (++empty > 16) return; }
        else if (!sprites) {
            const uint32_t base = psx_mod_gpu_dma_resolve_address(addr + 4u);
            for (uint32_t off = 0; off < n;) {
                const uint8_t op = (uint8_t)(psx_read_word(psx_mod_gpu_dma_resolve_address(base + off * 4u)) >> 24);
                const int count = gp0_command_word_count(op);
                if (count <= 0 || (op >= 0xA0u && op <= 0xBFu)) break;
                if (op >= 0x60u && op <= 0x7Fu) { sprites = 1; break; }
                off += (uint32_t)count;
            }
        }
        const uint32_t next = header & 0xFFFFFFu;
        if (next == 0xFFFFFFu) break;
        addr = psx_mod_gpu_dma_resolve_address(next);
    }
    if (sprites) { rr_he_on = 1; rr_he_serial++; }
}
'''

# Rectangles: send the exact box of the packet being drawn, when the integer
# rectangle is the one its transform produced (within a pixel); a HUD edit
# (HUD_EDIT) moves it from there, or from the integer box.
EXACT_RECTS = r'''
/* The HUD clip as moved by rr_set_draw_area (CLIP_FOLLOWS_HUD). */
static int rr_area_on;
static int32_t rr_area_l, rr_area_r;
static int rr_hx_rect_begin(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    const int32_t mark = rr_text_flag ? RR_TEXT_MARK : RR_HUD_MARK;
    double px[2] = { (double)x, (double)(x + w) }, py[2] = { (double)y, (double)(y + h) };
    int exact = 0;
    if (rr_hx_current()) {
        double x0 = rr_hx.ex[0], x1 = x0, y0 = rr_hx.ey[0], y1 = y0;
        for (int i = 1; i < rr_hx.n; i++) {
            if (rr_hx.ex[i] < x0) x0 = rr_hx.ex[i];
            if (rr_hx.ex[i] > x1) x1 = rr_hx.ex[i];
            if (rr_hx.ey[i] < y0) y0 = rr_hx.ey[i];
            if (rr_hx.ey[i] > y1) y1 = rr_hx.ey[i];
        }
        x0 += draw_offset_x; x1 += draw_offset_x;
        y0 += draw_offset_y; y1 += draw_offset_y;
        if (fabs(x - x0) > 1.01 || fabs(x + w - x1) > 1.01 ||
            fabs(y - y0) > 1.01 || fabs(y + h - y1) > 1.01) {
            if (!rr_he_on) return 0;
        } else {
            px[0] = x0; px[1] = x1; py[0] = y0; py[1] = y1; exact = 1;
        }
    }
    const int edited = rr_he_points(px, py, 2);
    /* Unedited: the exact box, or untransformed lettering in its own box. */
    if (!exact && !edited && (rr_hx_current() || !rr_text_flag)) return 0;
    gr_set_precise_triangle(1, rr_fix(px[0]), rr_fix(py[0]) + mark,
                            rr_fix(px[1]), rr_fix(py[1]) + mark, INT32_MIN, INT32_MIN);
    return 1;
}
static void rr_hx_rect_end(int on) {
    if (on) gr_set_precise_triangle(0, 0, 0, 0, 0, 0, 0);
}
static void rr_draw_flat_rect(int x, int y, int w, int h, uint16_t c) {
    const int on = rr_hx_rect_begin(x, y, w, h);
    gr_draw_flat_rect(x, y, w, h, c);
    rr_hx_rect_end(on);
}
static void rr_draw_textured_rect(int x, int y, int w, int h, int u, int v,
                                  uint16_t cx, uint16_t cy, uint16_t tp) {
    const int on = rr_hx_rect_begin(x, y, w, h);
    gr_draw_textured_rect(x, y, w, h, u, v, cx, cy, tp);
    rr_hx_rect_end(on);
}
static void rr_draw_textured_rect_scaled(int x, int y, int w, int h, int u0, int v0,
                                         int u1, int v1, uint16_t cx, uint16_t cy,
                                         uint16_t tp) {
    const int on = rr_hx_rect_begin(x, y, w, h);
    gr_draw_textured_rect_scaled(x, y, w, h, u0, v0, u1, v1, cx, cy, tp);
    rr_hx_rect_end(on);
}
#define gr_draw_flat_rect rr_draw_flat_rect
#define gr_draw_textured_rect rr_draw_textured_rect
#define gr_draw_textured_rect_scaled rr_draw_textured_rect_scaled
'''

# Triangles (polygon HUD pieces: glyph quads, minimap dots): the exact position
# of each vertex, matched by its packet coordinates.
EXACT_TRIANGLES = r'''
static int rr_hx_triangle(int i0, int i1, int i2, const int32_t vx[3], const int32_t vy[3]) {
    int32_t fx[3], fy[3];
    double ex[3], ey[3];
    const int rec = rr_hx_current() && !rr_hx.rect;
    int exact = 0;
    if (rec) {   /* the transform's exact position of each vertex */
        const int idx[3] = { i0, i1, i2 };
        exact = 1;
        for (int k = 0; k < 3 && exact; k++) {
            int32_t raw_x, raw_y;
            parse_vertex(gp0_cmd_buf[idx[k]], &raw_x, &raw_y);
            int j = 0;
            while (j < rr_hx.n && (rr_hx.rx[j] != raw_x || rr_hx.ry[j] != raw_y)) j++;
            if (j == rr_hx.n) { exact = 0; break; }
            ex[k] = rr_hx.ex[j] + draw_offset_x; ey[k] = rr_hx.ey[j] + draw_offset_y;
            if (fabs(vx[k] - ex[k]) > 1.01 || fabs(vy[k] - ey[k]) > 1.01) exact = 0;
        }
        if (!exact && !rr_he_on) return 0;
    }
    if (!exact) for (int k = 0; k < 3; k++) { ex[k] = vx[k]; ey[k] = vy[k]; }
    const int edited = rr_he_points(ex, ey, 3);   /* HUD_EDIT */
    /* Unedited: the exact position, or untransformed lettering (text mark). */
    if (!exact && !edited && (rec || !rr_text_flag)) return 0;
    for (int k = 0; k < 3; k++) {
        fx[k] = rr_fix(ex[k]);
        fy[k] = rr_fix(ey[k]) + (rr_text_flag ? RR_TEXT_MARK : RR_HUD_MARK);
    }
    gr_set_precise_triangle(1, fx[0], fy[0], fx[1], fy[1], fx[2], fy[2]);
    return 1;
}
'''

# The race minimap draws its rider dots inside a drawing-area clip that matches
# its frame sprite (Stadium Park: x 339..362, y 21..68 of a 384x240 display).
# auto_ui_squash moves the frame and the dots towards the screen edge; the clip
# has to move with them, or the dots are drawn outside the frame or cut off.
# The clip takes the anchor of the HUD piece it overlaps most, so it follows
# whichever edge, centre or proportional size that widget was given, and covers
# every pixel the exact frame touches.
CLIP_FOLLOWS_HUD = r'''
static int32_t rr_overlap(int32_t a0, int32_t a1, int32_t b0, int32_t b1) {
    int32_t lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
    return hi > lo ? hi - lo : 0;
}
static void rr_set_draw_area(void) {
    int l = (int)draw_area_left, t = (int)draw_area_top;
    int r = (int)draw_area_right, b = (int)draw_area_bottom;
    rr_area_on = 0;
    rr_he_note_area(l, t, r, b);
    if (rr_ui_race_active() && ws_auto_ui_squash && ws_active() &&
        r >= l && b >= t && ws_ui_prepass_count) {
        const int32_t x0 = l - draw_offset_x, x1 = r + 1 - draw_offset_x;
        const int32_t y0 = t - draw_offset_y, y1 = b + 1 - draw_offset_y;
        const int32_t X = ws_disp_x(), W = ws_disp_w();
        const WsUiPrepassItem *best = NULL;
        int64_t best_area = 0;
        if (x1 - x0 < W / 2) {
            for (uint32_t i = 0; i < ws_ui_prepass_count; i++) {
                const WsUiPrepassItem *it = &ws_ui_prepass[i];
                const int32_t ix0 = X + it->group.x;
                const int64_t area =
                    (int64_t)rr_overlap(x0, x1, ix0, ix0 + it->group.width) *
                    rr_overlap(y0, y1, it->y, it->y + it->h);
                if (area > best_area) { best = it; best_area = area; }
            }
        }
        if (best) {
            const double s = ws_auto_ui_size();
            const int32_t a = best->group.anchor, d = ws_shared_ui_delta();
            l = draw_offset_x + (int)floor(rr_ui_xd(x0, a, s) + d + 1e-6);
            r = draw_offset_x + (int)ceil(rr_ui_xd(x1, a, s) + d - 1e-6) - 1;
            t = draw_offset_y + (int)floor(rr_ui_yd(y0, best->anchor_y, s) + 1e-6);
            b = draw_offset_y + (int)ceil(rr_ui_yd(y1, best->anchor_y, s) - 1e-6) - 1;
            if (l < 0) l = 0;
            if (t < 0) t = 0;
            if (r > 1023) r = 1023;
            if (b > 511) b = 511;
            rr_area_on = 1; rr_area_l = l; rr_area_r = r;
        }
    }
    rr_he_area(&l, &t, &r, &b);   /* HUD_EDIT: the renderer's clip only */
    gr_set_draw_area(l, t, r, b);
}
'''


# Text tags that last as long as their packet (native_ui.cpp). The framework's
# HUD tags expire two VBlanks after they are written. Races build and draw a
# frame within one VBlank, but the frontend, the results screen and loading
# build a label once and redraw the same packets for several VBlanks, so the
# tag was honoured on some frames and not on others: the label flipped between
# its proportioned and its stretched shape ("text jumps and stretches"). A
# packet whose words are unchanged is the same label, so these tags are matched
# by address and packet fingerprint and live for ten seconds.
#   own=0: race text, anchored at a screen edge or centre (edge -1/0/1), the
#          same role the HUD tag carries;
#   own=1: frontend/results text, narrowed about its own anchor (the x the
#          game laid the label out from), so it stays where the game put it in
#          its stretched box and keeps the original glyph proportions.
TEXT_TAGS = r'''
extern int rr_ui_in_race(void);
void psx_mod_counter_add(const char *name, uint32_t delta);
#define RR_TEXT_TAGS 4096u
#define RR_TEXT_FRESH 600u
typedef struct {
    uint32_t addr, frame;
    double anchor;
    uint8_t used, own;
    WsPrepassPacketGuard guard;
} RrTextTag;
static RrTextTag rr_text_tags[RR_TEXT_TAGS];
void rr_ws_tag_text(uint32_t prim, int own, double anchor) {
    uint32_t words[12], count = 0;
    if ((prim & 3u) || prim > UINT32_MAX - 4u ||
        !ws_hud_command_words(prim + 4u, words, &count)) return;
    const uint32_t addr = GPU_RAM_KEY(prim + 4u), now = (uint32_t)s_frame_count;
    uint32_t idx = (addr >> 2) & (RR_TEXT_TAGS - 1u), victim = idx;
    for (uint32_t i = 0; i < 8u; i++) {
        const uint32_t j = (idx + i) & (RR_TEXT_TAGS - 1u);
        if (!rr_text_tags[j].used || rr_text_tags[j].addr == addr) { victim = j; break; }
        if (now - rr_text_tags[j].frame > now - rr_text_tags[victim].frame) victim = j;
    }
    RrTextTag *t = &rr_text_tags[victim];
    t->used = 1; t->addr = addr; t->frame = now; t->own = own ? 1 : 0;
    t->anchor = own ? anchor : (anchor < 0 ? -1.0 : anchor > 0 ? 1.0 : 0.0);
    t->guard = ws_prepass_packet_guard(words, count);
}
static int rr_text_why;   /* last miss: 0 absent, 1 expired, 2 packet changed */
static const RrTextTag *rr_text_lookup(void) {
    rr_text_why = 0;
    if (gp0_cmd_source_addr == 0xFFFFFFFFu || gp0_words_needed <= 0 || gp0_words_needed > 12)
        return NULL;
    const uint32_t addr = GPU_RAM_KEY(gp0_cmd_source_addr & 0x1FFFFFFFu), now = (uint32_t)s_frame_count;
    const uint32_t idx = (addr >> 2) & (RR_TEXT_TAGS - 1u);
    for (uint32_t i = 0; i < 8u; i++) {
        const RrTextTag *t = &rr_text_tags[(idx + i) & (RR_TEXT_TAGS - 1u)];
        if (!t->used || t->addr != addr) continue;
        if (t->frame > now || now - t->frame > RR_TEXT_FRESH) { rr_text_why = 1; return NULL; }
        if (!ws_prepass_packet_matches(&t->guard, gp0_cmd_buf, (uint32_t)gp0_words_needed)) {
            rr_text_why = 2; return NULL;
        }
        return t;
    }
    return NULL;
}
'''

# Frontend/results text: the glyph quad narrowed about its label's own anchor,
# at sub-pixel precision (the exact-HUD override, like the race HUD).
TEXT_QUAD = r'''
static int rr_text_quad(int32_t vx[4], const int32_t vy[4]) {
    const RrTextTag *t = rr_text_lookup();
    rr_text_flag = t != NULL;   /* lettering, in every aspect */
    {   /* RR_TEXT_TRACE: one line per textured quad lookup (diagnostics) */
        static int on = -1, lines, ylo, yhi;
        if (on < 0) {   /* RR_TEXT_TRACE=ylo,yhi limits it to one band */
            const char *e = getenv("RR_TEXT_TRACE");
            on = e != NULL; ylo = -4096; yhi = 4096;
            if (e) sscanf(e, "%d,%d", &ylo, &yhi);
        }
        if (on && lines < 3000 && vy[0] >= ylo && vy[0] <= yhi) {
            lines++;
            fprintf(stderr, "TEXT f=%u src=%08X xy=%d,%d tag=%d own=%d a=%.1f why=%d w0=%08X\n", (unsigned)s_frame_count,
                    gp0_cmd_source_addr, vx[0], vy[0], t != NULL, t ? t->own : -1, t ? t->anchor : 0.0,
                    rr_text_why, gp0_cmd_buf[0]);
        }
    }
    if (!t || !t->own || rr_ui_in_race() || !ws_active()) return 0;
    rr_hx_begin(0);
    for (int i = 0; i < 4; i++) {
        const double ex = t->anchor + (double)(vx[i] - t->anchor) * ws_xnum / ws_xden;
        rr_hx_add(vx[i], vy[i], ex, vy[i]);
        vx[i] = (int32_t)floor(ex + 0.5);
    }
    psx_mod_counter_add("rr.ui.text_quads", 1);
    return 1;
}
'''


# Frontend title lettering ("MAIN MENU:", "TIME TRIALS:" ...) is one image drawn
# as 256-wide sprites in the top band. The stretched frontend widened it
# while the labels kept their proportions. Each title row narrows about its own
# left edge: the leftmost sprite of that row and palette, learned from the
# previous frame (the halves arrive right half first). Menu bars start at
# y 40+ and stay stretched, so labels stay inside them. Races are untouched.
TITLE_RECTS = r'''
/* Menu pictures (native_ui.cpp rr_ui_picture_at) are narrowed in the renderer
 * about their centre. Each pass the game first draws a sprite on the same
 * rectangle (a still of the picture from texture VRAM); left full-width, its
 * outer columns showed beside the narrowed picture ("|" left of the ROAD RASH
 * panel, the handcuff rims). A sprite lying on a current picture narrows about
 * the same centre by the same factor, so the sides show the menu backdrop. */
extern int rr_ui_picture_at(int, int, int, int, int *, int *, int *, int *, float *);
static int rr_picture_rect(int32_t *x0, int32_t y0, int w, int h) {
    if (rr_ui_in_race() || !ws_active() || w < 16 || h < 16) return 0;
    int rx0, ry0, rx1, ry1;
    float f;
    const int ax = *x0 + draw_offset_x, ay = y0 + draw_offset_y;
    if (!rr_ui_picture_at(ax, ay, ax + w - 1, ay + h - 1, &rx0, &ry0, &rx1, &ry1, &f)) return 0;
    const int32_t a = (rx0 + rx1) / 2 - draw_offset_x;
    rr_hx_rect(*x0, y0, w, h, a, 0, 1.0, 0);
    *x0 = ws_scale_about(*x0, a);
    psx_mod_counter_add("rr.ui.picture_sprites", 1);
    return 1;
}
typedef struct { uint32_t key, frame; int32_t min_now, min_last; } RrTitleRow;
static RrTitleRow rr_title_rows[8];
static int rr_title_rect(int32_t *x0, int32_t y0, int w, int h, uint16_t clut) {
    if (rr_ui_in_race() || !ws_active()) return 0;
    /* Any texture depth: the title banners are 15-bit images. Full-screen
     * backdrops (240 tall) and the picture panels (y 111) fall outside. */
    if (y0 >= 32 || y0 < -8 || h < 20 || h > 72 || w < 32 ||
        w >= (int)ws_disp_w() * 9 / 10) return 0;
    const uint32_t key = ((uint32_t)clut << 16) | (uint32_t)(y0 & 0xFFFF);
    const uint32_t now = (uint32_t)s_frame_count;
    RrTitleRow *r = NULL, *old = &rr_title_rows[0];
    for (int i = 0; i < 8; i++) {
        if (rr_title_rows[i].key == key && rr_title_rows[i].frame) { r = &rr_title_rows[i]; break; }
        if (rr_title_rows[i].frame < old->frame) old = &rr_title_rows[i];
    }
    if (!r) { r = old; r->key = key; r->frame = now; r->min_now = *x0; r->min_last = INT32_MAX; }
    if (r->frame != now) {
        r->min_last = now - r->frame <= 8u ? r->min_now : INT32_MAX;
        r->min_now = *x0; r->frame = now;
    } else if (*x0 < r->min_now) r->min_now = *x0;
    const int32_t a = r->min_last < *x0 ? r->min_last : *x0;
    rr_hx_rect(*x0, y0, w, h, a, 0, 1.0, 0);
    *x0 = ws_scale_about(*x0, a);
    psx_mod_counter_add("rr.ui.title_rects", 1);
    return 1;
}
'''


def title_rects(source):
    anchor = 'static void gp0_exec_textured_rect(void) {'
    source = _sub(source, anchor, TITLE_RECTS + '\n' + anchor, what='Textured rect interface')
    source = _sub(source,
        '    if (ws_active() && w > 0) {\n'
        '        int32_t ws_ax;\n'
        '        if (ws_tagged_anchor(&ws_ax)) {\n'
        '            x0 = ws_scale_about(x0, ws_ax);\n'
        '            ws_w = (int)ws_scale_len(w);\n'
        '        } else {\n',
        '    if (ws_active() && w > 0) {\n'
        '        int32_t ws_ax;\n'
        '        if (rr_picture_rect(&x0, y0, w, h) || rr_title_rect(&x0, y0, w, h, clut)) {\n'
        '            ws_w = (int)ws_scale_len(w);\n'
        '        } else if (ws_tagged_anchor(&ws_ax)) {\n'
        '            x0 = ws_scale_about(x0, ws_ax);\n'
        '            ws_w = (int)ws_scale_len(w);\n'
        '        } else {\n', what='Textured rect transform')
    return source


# Menu picture panels. Each menu screen draws one full-screen backdrop (two
# 256x240 sprites) whose bottom-right panel frame surrounds the picture slot
# (230,111 256x112). The picture narrows to its 4:3 proportions in widescreen,
# so in the stretched frame it left empty bands at both sides. The backdrop is
# drawn in three column pieces instead: the panel's columns narrow about the
# picture centre by the same factor, and the plain backdrop left and right of it
# widens to meet them (about 1.15x / 1.26x on the left at 16:9 / 21:9). Pieces
# span the full height, so no backdrop line breaks at a row.
PANEL_BACKDROP = r'''
extern int rr_ui_panel(int, int, uint32_t, int *, int *, int *, int *);
static int rr_panel_backdrop(int32_t x0, int32_t y0, int w, int h, int u0, int v0,
                             uint16_t clut_x, uint16_t clut_y, uint32_t color24,
                             int semi_trans, int raw_texture) {
    if (rr_ui_in_race() || !ws_active() || w < 64 || h < 200 || y0 > 8) return 0;
    int rx0, ry0, rx1, ry1;
    /* The packet's address tells native_ui which menu page this frame draws. */
    const uint32_t src = gp0_cmd_source_addr == 0xFFFFFFFFu ? 0u : gp0_cmd_source_addr & 0x1FFFFFFFu;
    if (!rr_ui_panel(y0 + draw_offset_y, y0 + draw_offset_y + h, src, &rx0, &ry0, &rx1, &ry1)) return 0;
    /* The frame's bevel lies 2-4 px outside the slot. */
    const int32_t c = (rx0 + rx1) / 2 - draw_offset_x;
    const int32_t pl = rx0 - draw_offset_x - 6, pr = rx1 - draw_offset_x + 6;
    const int32_t lo = ws_disp_x(), hi = ws_disp_x() + ws_disp_w();
    if (!(lo < pl && pr < hi)) return 0;
    const double f = (double)ws_xnum / ws_xden;
    const double gl = c + (pl - c) * f, gr = c + (pr - c) * f;
    const int32_t sa[3] = {INT32_MIN, pl, pr}, sb[3] = {pl, pr, INT32_MAX};
    setup_textured_draw(color24, semi_trans, raw_texture);
    for (int k = 0; k < 3; k++) {
        const int32_t a = x0 > sa[k] ? x0 : sa[k], b = x0 + w < sb[k] ? x0 + w : sb[k];
        if (b <= a) continue;
        double ea, eb;
        if (k == 0) { ea = lo + (a - lo) * (gl - lo) / (pl - lo); eb = lo + (b - lo) * (gl - lo) / (pl - lo); }
        else if (k == 1) { ea = c + (a - c) * f; eb = c + (b - c) * f; }
        else { ea = gr + (a - pr) * (hi - gr) / (hi - pr); eb = gr + (b - pr) * (hi - gr) / (hi - pr); }
        const int32_t ia = (int32_t)lround(ea), ib = (int32_t)lround(eb);
        if (ib <= ia) continue;
        const int32_t x = ia + draw_offset_x, y = y0 + draw_offset_y;
        if (draw_area_out_rect(x, y, ib - ia, h)) continue;
        rr_hx_begin(1);
        rr_hx_add(ia, y0, ea, y0);
        rr_hx_add(ib, y0 + h, eb, y0 + h);
        gr_draw_textured_rect_scaled(x, y, ib - ia, h, u0 + (a - x0), v0, u0 + (b - x0), v0 + h,
                                     clut_x, clut_y, current_texpage());
    }
    psx_mod_counter_add("rr.ui.panel_backdrops", 1);
    return 1;
}
'''


def panel_backdrop(source):
    anchor = 'static void gp0_exec_textured_rect(void) {'
    source = _sub(source, anchor, PANEL_BACKDROP + '\n' + anchor, what='Backdrop rect interface')
    old = '    /* Widescreen: tagged sprite parts squash around their projected anchor;\n'
    source = _sub(source, old,
        '    if (rr_panel_backdrop(x0, y0, w, h, u0, v0, clut_x, clut_y, color24,\n'
        '                          semi_trans, raw_texture)) return;\n' + old,
        what='Backdrop rect transform')
    return source


def text_tags(source):
    anchor = 'static int ws_explicit_hud_anchor(int *out_anchor) {'
    source = _sub(source, anchor, TEXT_TAGS + '\n' + anchor, what='HUD anchor interface')
    # Race text whose two-VBlank HUD tag expired keeps its role while its
    # packet is unchanged.
    source = _sub(source,
        '        &anchor);\n'
        '    ws_tag_stats_note_lookup(&ws_hud_tag_stats, result);\n',
        '        &anchor);\n'
        '    if (result != WS_HUD_ANCHOR_HIT) {\n'
        '        const RrTextTag *t = rr_text_lookup();\n'
        '        if (t && !t->own) { anchor = (int)t->anchor; result = WS_HUD_ANCHOR_HIT; }\n'
        '    }\n'
        '    ws_tag_stats_note_lookup(&ws_hud_tag_stats, result);\n', what='HUD anchor lookup')
    anchor = 'static uint32_t ws_auto_ui_group_key_words(const uint32_t *words,'
    source = _sub(source, anchor, TEXT_QUAD + '\n' + anchor, what='Text quad interface')
    source = _sub(source,
        '    ws_auto_ui_transform_quad(vx, vy);\n    ws_nw_backdrop_stretch_quad(vx, vy);',
        '    if (!rr_text_quad(vx, vy)) ws_auto_ui_transform_quad(vx, vy);\n'
        '    ws_nw_backdrop_stretch_quad(vx, vy);', what='Textured quad transform')
    return source


# Widescreen cloud layer. The race sky (builder 0x800644F4 in the race
# overlay) is a chain of GTE-projected raw-texture quads (0x2C-0x2F, colour 0)
# of one 256-texel cloud panorama: texture rows v 192..254, u segments
# 0/64/128/192 (the last ends at 251), drawn right to left, each strip's right
# edge the previous strip's left edge. The game always builds 7 strips, which
# span about guest x -120..+420 depending on the heading: enough for its 384-
# wide 4:3 view. Widescreen shows guest x beyond that (16:9: -64..448) and the
# chain end moves with every turn of the road, so the sky edge jumped about as
# bare gradient. When a chain stops short of the widescreen view, continue it
# with the same panorama: each added strip repeats the end strip's edges, grown
# by the end strip's own growth ratio (the cylinder widens towards the edges),
# with the next texture segment, exactly what more game strips would draw.
# Widescreen races only; 4:3 output is unchanged. RR_SKY_EXTEND=0 turns it off.
SKYLINE = r'''
void psx_mod_counter_add(const char* name, uint32_t delta);
typedef struct { uint32_t w[9]; } RrSkyStrip;
static RrSkyStrip rr_sky_l, rr_sky_l2, rr_sky_r, rr_sky_r2;
static int rr_sky_n;
static int rr_sky_is_strip(const uint32_t *b) {
    /* Colour 0 raw texture: world quads that happen to use the same texel
     * window carry a colour. */
    if ((b[0] & 0xFCFFFFFFu) != 0x2C000000u) return 0;
    const int u0 = b[2] & 0xFF, u1 = b[4] & 0xFF, u2 = b[6] & 0xFF, u3 = b[8] & 0xFF;
    return (u0 & 63) == 0 && u1 == (u0 == 192 ? 251 : u0 + 63) && u2 == u0 && u3 == u1 &&
           ((b[2] >> 8) & 0xFF) == 192 && ((b[4] >> 8) & 0xFF) == 192 &&
           ((b[6] >> 8) & 0xFF) == 254 && ((b[8] >> 8) & 0xFF) == 254;
}
static double rr_sky_x(uint32_t w) { int32_t x, y; parse_vertex(w, &x, &y); return x; }
static double rr_sky_y(uint32_t w) { int32_t x, y; parse_vertex(w, &x, &y); return y; }
static uint32_t rr_sky_xy(double x, double y) {
    int32_t ix = (int32_t)lround(x), iy = (int32_t)lround(y);
    if (ix < -1024) ix = -1024; if (ix > 1023) ix = 1023;
    if (iy < -1024) iy = -1024; if (iy > 1023) iy = 1023;
    return ((uint32_t)(iy & 0x7FF) << 16) | (uint32_t)(ix & 0x7FF);
}
static void rr_sky_uv(RrSkyStrip *s, int u0) {
    const int u1 = u0 == 192 ? 251 : u0 + 63;
    s->w[2] = (s->w[2] & 0xFFFF0000u) | (192u << 8) | (uint32_t)u0;
    s->w[4] = (s->w[4] & 0xFFFF0000u) | (192u << 8) | (uint32_t)u1;
    s->w[6] = (254u << 8) | (uint32_t)u0;
    s->w[8] = (254u << 8) | (uint32_t)u1;
}
/* Width growth from the strip next to the end towards the end, kept sane. */
static double rr_sky_growth(const RrSkyStrip *end, const RrSkyStrip *next, int have_next) {
    if (!have_next) return 1.0;
    const double we = fabs(rr_sky_x(end->w[3]) - rr_sky_x(end->w[1]));
    const double wn = fabs(rr_sky_x(next->w[3]) - rr_sky_x(next->w[1]));
    double g = wn > 0.5 ? we / wn : 1.0;
    return g < 1.0 ? 1.0 : g > 1.6 ? 1.6 : g;
}
static void rr_sky_draw(const RrSkyStrip *s) {
    uint32_t save[9];
    const uint32_t src = gp0_cmd_source_addr;
    const int hx = rr_hx.n;
    memcpy(save, gp0_cmd_buf, sizeof save);
    memcpy(gp0_cmd_buf, s->w, sizeof s->w);
    gp0_cmd_source_addr = 0xFFFFFFFFu;   /* no packet identity: no tags, no exact HUD */
    rr_hx.n = 0;
    gp0_exec_textured_quad();
    rr_hx.n = hx;
    gp0_cmd_source_addr = src;
    memcpy(gp0_cmd_buf, save, sizeof save);
}
static void rr_sky_flush(void) {
    static int on = -1;
    if (on < 0) { const char *e = getenv("RR_SKY_EXTEND"); on = !(e && e[0] == '0'); }
    const int n = rr_sky_n, have2 = n >= 2;
    rr_sky_n = 0;
    /* A skyline is 6-7 strips; never extend a stray pair. */
    if (n < 3 || !on) return;
    if (!rr_ui_race_active() || !ws_active() || ws_xnum <= 0 || ws_xnum >= ws_xden) {
        psx_mod_counter_add("rr.sky.chain_not_wide", 1); return;
    }
    psx_mod_counter_add("rr.sky.chains", 1);
    /* Guest x the widescreen view shows: the 4:3 view widened about its centre. */
    const double w = (double)ws_disp_w(), c = (double)ws_disp_x() + w / 2.0;
    const double half = w / 2.0 * (double)ws_xden / (double)ws_xnum + 8.0;
    const double lo = c - half - draw_offset_x, hi = c + half - draw_offset_x;
    int added = 0;
    RrSkyStrip e = rr_sky_l;   /* left end: TL w1, TR w3, BL w5, BR w7 */
    double g = rr_sky_growth(&rr_sky_l, &rr_sky_l2, have2);
    for (int k = 0; k < 6; k++) {
        const double tlx = rr_sky_x(e.w[1]), tly = rr_sky_y(e.w[1]), trx = rr_sky_x(e.w[3]), try_ = rr_sky_y(e.w[3]);
        const double blx = rr_sky_x(e.w[5]), bly = rr_sky_y(e.w[5]), brx = rr_sky_x(e.w[7]), bry = rr_sky_y(e.w[7]);
        if ((tlx < blx ? tlx : blx) <= lo || trx - tlx <= 0.5) break;
        RrSkyStrip s = e;
        s.w[3] = e.w[1]; s.w[7] = e.w[5];
        s.w[1] = rr_sky_xy(tlx + (tlx - trx) * g, tly + (tly - try_) * g);
        s.w[5] = rr_sky_xy(blx + (blx - brx) * g, bly + (bly - bry) * g);
        rr_sky_uv(&s, ((int)(e.w[2] & 0xFF) + 192) & 0xFF);
        rr_sky_draw(&s); added++;
        e = s;
    }
    e = rr_sky_r;
    g = rr_sky_growth(&rr_sky_r, &rr_sky_r2, have2);
    for (int k = 0; k < 6; k++) {
        const double tlx = rr_sky_x(e.w[1]), tly = rr_sky_y(e.w[1]), trx = rr_sky_x(e.w[3]), try_ = rr_sky_y(e.w[3]);
        const double blx = rr_sky_x(e.w[5]), bly = rr_sky_y(e.w[5]), brx = rr_sky_x(e.w[7]), bry = rr_sky_y(e.w[7]);
        if ((trx > brx ? trx : brx) >= hi || trx - tlx <= 0.5) break;
        RrSkyStrip s = e;
        s.w[1] = e.w[3]; s.w[5] = e.w[7];
        s.w[3] = rr_sky_xy(trx + (trx - tlx) * g, try_ + (try_ - tly) * g);
        s.w[7] = rr_sky_xy(brx + (brx - blx) * g, bry + (bry - bly) * g);
        rr_sky_uv(&s, ((int)(e.w[2] & 0xFF) + 64) & 0xFF);
        rr_sky_draw(&s); added++;
        e = s;
    }
    if (added) psx_mod_counter_add("rr.sky.extended_strips", (uint32_t)added);
}
/* Before each GP0 command: collect a strip chain, extend it when it ends. Any
 * command other than a NOP closes the chain first, so the added strips are
 * drawn with the texture window, texpage and mask state the game's own strips
 * used (keeping the chain open across E1-E6 drew wrong-state strips). */
static void rr_sky_note(uint8_t opcode) {
    if ((opcode & 0xFCu) == 0x2Cu && rr_sky_is_strip(gp0_cmd_buf)) {
        if (rr_sky_n && gp0_cmd_buf[3] == rr_sky_l.w[1] && gp0_cmd_buf[7] == rr_sky_l.w[5]) {
            if (rr_sky_n == 1) memcpy(rr_sky_r2.w, gp0_cmd_buf, sizeof rr_sky_r2.w);
            rr_sky_l2 = rr_sky_l;
            memcpy(rr_sky_l.w, gp0_cmd_buf, sizeof rr_sky_l.w);
            rr_sky_n++;
            return;
        }
        rr_sky_flush();
        memcpy(rr_sky_l.w, gp0_cmd_buf, sizeof rr_sky_l.w);
        rr_sky_r = rr_sky_l;
        rr_sky_n = 1;
        return;
    }
    if (opcode == 0x00u) return;
    if (rr_sky_n) rr_sky_flush();
}
'''

def skyline(source):
    anchor = 'static void gp0_dispatch_command(uint8_t opcode) {\n'
    source = _sub(source, anchor, SKYLINE + '\n' + anchor, what='GP0 dispatch (sky)')
    return _sub(source, anchor + '    rr_hx.n = 0;\n', anchor + '    rr_sky_note(opcode);\n    rr_hx.n = 0;\n',
                what='GP0 dispatch (sky note)')


def _sub(source, old, new, count=1, what='interface'):
    if source.count(old) != count:
        raise ValueError(f'{what} changed')
    return source.replace(old, new)


# gpu_vram_peek without a renderer sync, for VRAM the GPU never draws into:
# CLUT rows and texture pages, which only uploads write (palettes, the bike's
# texture). gpu.c's array holds every upload at once (it is the renderer's
# emulation-side copy). rider_enhancements.cpp reads palette rows with it a
# few times a second in races: through gpu_vram_peek each read drained the
# render thread's queue, a hitch at 120 FPS (24 Oct).
VRAM_PEEK_CPU = r'''
uint16_t gpu_vram_peek_cpu(int x, int y) {
    if (x < 0 || x >= 1024 || y < 0 || y >= 512) return 0;
    return vram[(uint32_t)y * 1024u + (uint32_t)x];
}
'''


def extend(source):
    # (0.14's rr_motion_source_id vertex identity hook was removed in 0.15 with
    # the Smooth reprojection path it served.)
    old='int gpu_ws_present_native_43(void) {\n    if (!ws_engaged()) return 0;'
    # Races never show full-screen video. Recent MDEC activity (the loading
    # screen, streamed race textures) otherwise pinned race frames to a 4:3
    # pillarbox while widescreen was selected.
    source=_sub(source,old,old+'\n    if (rr_ui_wide_frontend()) return 0;\n    if (rr_ui_race_active()) return 0;',
                what='Widescreen scene interface')

    # Exact HUD placement: record in the transforms ...
    anchor='static uint32_t ws_auto_ui_group_key_words(const uint32_t *words,'
    source=_sub(source,anchor,EXACT_RECORD+'\n'+anchor,what='HUD transform interface')
    source=_sub(source,'    if (ws_projection_hud_transform(vx, n)) return 1;',
                '    if (rr_hx_projection(vx, vy, n)) return 1;',what='HUD part transform')
    source=_sub(source,'    if (ws_projection_hud_transform(vx, 4)) return 1;',
                '    if (rr_hx_projection(vx, vy, 4)) return 1;',what='HUD quad transform')
    for n in ('n','4'):
        source=_sub(source,
            f'    for (int i = 0; i < {n}; i++) {{\n'
            '        vx[i] = ws_ui_x(vx[i], it->group.anchor, s) + ws_shared_ui_delta();\n'
            '        vy[i] = ws_ui_y(vy[i], it->anchor_y, s);\n'
            '    }\n',
            f'    rr_hx_ui(vx, vy, {n}, it, s);\n',what='HUD vertex squash')
    source=_sub(source,
        '    if (ws_projection_hud_anchor(&explicit_anchor)) {\n'
        '        *x = ws_scale_about(*x, explicit_anchor);',
        '    if (ws_projection_hud_anchor(&explicit_anchor)) {\n'
        '        rr_hx_rect(*x, *y, *w, *h, explicit_anchor, 0, 1.0, 0);\n'
        '        *x = ws_scale_about(*x, explicit_anchor);',what='HUD tagged rect')
    source=_sub(source,
        '    const double s = ws_auto_ui_size();\n'
        '    if (s >= 1.0) {\n'
        '        *x = ws_scale_about(*x, it->group.anchor) + ws_shared_ui_delta();',
        '    const double s = ws_auto_ui_size();\n'
        '    rr_hx_rect(*x, *y, *w, *h, it->group.anchor, it->anchor_y, s, ws_shared_ui_delta());\n'
        '    if (s >= 1.0) {\n'
        '        *x = ws_scale_about(*x, it->group.anchor) + ws_shared_ui_delta();',what='HUD auto rect')
    # ... and apply at the draw. A record belongs to one GP0 packet.
    anchor='static int32_t draw_offset_x, draw_offset_y;\n'
    source=_sub(source,anchor,anchor+HUD_EDIT+EXACT_RECTS,what='Draw offset interface')
    anchor='static void prepare_precise_triangle(int i0, int i1, int i2,'
    source=_sub(source,anchor,EXACT_TRIANGLES+'\n'+anchor,what='Precise triangle interface')
    old='    const int geometry = gte_geometry_correction_enabled();\n    if (!geometry && !s_native_wide_projection_correction) {'
    source=_sub(source,old,'    if (rr_hx_triangle(i0, i1, i2, vx, vy)) return;\n'+old,what='Precise triangle gate')
    old='static void gp0_dispatch_command(uint8_t opcode) {\n    switch (opcode) {'
    source=_sub(source,old,old.replace('{\n','{\n    rr_hx.n = 0;\n    rr_text_flag = 0;\n',1),what='GP0 dispatch')

    # Draw-area changes go through rr_set_draw_area (the minimap clip above).
    # The CPU-side reject tests must see the moved clip too: at 21:9 the
    # squashed dots lie right of the guest's x 362 and were dropped before the
    # renderer saw them.
    source=_sub(source,
        '    *left  = (int32_t)draw_area_left;\n    *right = (int32_t)draw_area_right;\n',
        '    *left  = (int32_t)draw_area_left;\n    *right = (int32_t)draw_area_right;\n'
        '    if (rr_area_on) {\n'
        '        if (rr_area_l < *left)  *left  = rr_area_l;\n'
        '        if (rr_area_r > *right) *right = rr_area_r;\n'
        '    }\n',what='Draw area reject bounds')
    anchor='static void gp0_exec_draw_area_tl(void) {'
    source=_sub(source,anchor,CLIP_FOLLOWS_HUD+'\n'+anchor,what='Draw area interface')
    source=_sub(source,
        '    split_trace_note_draw_area();\n    gr_set_draw_area((int)draw_area_left, (int)draw_area_top,\n'
        '                     (int)draw_area_right, (int)draw_area_bottom);\n    ws_nw_sync_target();',
        '    split_trace_note_draw_area();\n    rr_set_draw_area();\n    ws_nw_sync_target();',
        count=2,what='Draw area handlers')

    # A race HUD is never a dense 2D menu. With both riders' health bars and
    # names on screen it reaches 32+ pieces, and "dense" anchored every widget
    # at the screen centre: the whole HUD jumped back to its 4:3 positions.
    old=('    ws_auto_ui_dense = ws_ui_prepass_count >= 32u && !ws_gte_game_mode_cfg &&\n'
         '        !ws_local_native_split();')
    source=_sub(source,old,old[:-1]+' && !rr_ui_race_active();',what='Dense UI interface')
    source=overlay_ranks(source)
    anchor='void gpu_ws_prepass_linked_list(uint32_t start_addr) {\n'
    source=_sub(source,anchor,HUD_EDIT_LIST+'\n'+anchor+'    rr_he_list(start_addr);   /* HUD_EDIT */\n',what='Prepass interface (HUD edit)')
    source=text_tags(source)
    source=title_rects(source)
    source=panel_backdrop(source)
    source=skyline(source)
    source=_sub(source,'uint16_t gpu_vram_peek(int x, int y) {\n',VRAM_PEEK_CPU+'uint16_t gpu_vram_peek(int x, int y) {\n',what='VRAM peek interface')
    return 'extern int rr_ui_wide_frontend(void);\nextern int rr_ui_race_active(void);\n'+source
if __name__=='__main__':
    import pathlib,sys
    pathlib.Path(sys.argv[2]).write_text(extend(pathlib.Path(sys.argv[1]).read_text()))
