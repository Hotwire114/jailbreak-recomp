/* Display options backed by the renderer's own settings, shared by the
 * launcher (C: launcher.ini [display], game.toml [video]) and the runtime
 * (C++: pc_services.cpp, which applies the live ones from the in-game PC
 * menu's Display page). Step 0 is always the original game's behaviour.
 * LGPL-2.1-or-later. Original author code. */
#ifndef RR_GFX_OPTIONS_H
#define RR_GFX_OPTIONS_H

/* [display] antialiasing. FXAA / FXAA HQ: the renderer's post-process pass
 * on the finished picture (game.toml antialiasing_mode; GL_POST_AA_* 1 / 2).
 * Supersample 2x / 4x: the game is drawn 2 or 4 times taller than Render
 * resolution, at most rr_ss_max_scale x 240 lines (3840), and the renderer
 * scales the picture down to the window. */
enum { rr_aa_steps = 5, rr_ss_max_scale = 16 };
static inline const char *rr_aa_name(int i){static const char *const n[]={"Off","FXAA","FXAA HQ","Supersample 2x","Supersample 4x"};return n[i>=0&&i<rr_aa_steps?i:0];}
static inline int rr_aa_post(int i){return i==1||i==2?i:0;}
static inline int rr_aa_factor(int i){return i==3?2:i==4?4:1;}
/* Render resolution steps ([display] resolution 0-4) in lines. */
static inline int rr_render_lines(int r){static const int l[]={240,720,1080,1440,2160};return l[r>=0&&r<5?r:1];}
/* Lines drawn for a Render resolution step and anti-aliasing choice, and the
 * internal scale (multiple of the PS1's 240 lines) that draws them. Without
 * supersampling this is the 0.15 table: 1, 3, 5, 6, 9. */
static inline int rr_render_scale(int r,int aa){int s=(rr_render_lines(r)*rr_aa_factor(aa)+239)/240;return s>rr_ss_max_scale?rr_ss_max_scale:s;}
static inline int rr_render_target(int r,int aa){int t=rr_render_lines(r)*rr_aa_factor(aa);return t>rr_ss_max_scale*240?rr_ss_max_scale*240:t;}

/* [display] texture_correction: the PS1's wobbling geometry and warping
 * textures (pc_services.cpp apply_correction). On: sub-pixel positions where
 * the GTE's projection can be matched by screen position (0.15's step, light).
 * Full: the PGXP value tracking in the game code as well, which corrects far
 * more of the road, scenery and riders (fast_timing.py PGXP_GATED), at a CPU
 * cost measured in PROGRESS-0.15-geometry-correction.md. Older files hold 0/1,
 * which read the same. rr_tc_heavy: the menus show the heavy anti-aliasing's
 * warning for that step. */
enum { rr_tc_steps = 3 };
/* Shown as "Precise vertices" Off / On (4 Nov 2026): On is Full, built by default
 * now (RR_PGXP_SPLIT / RR_TEXTURE_CORRECTION_FULL). The old On step fixed ~2% of
 * vertices and is no longer offered: a saved 1 reads as Off (rr_tc_load). */
enum { rr_tc_offered = 3 };
static inline const char *rr_tc_name(int i){return i==2?"On":"Off";}
static inline int rr_tc_load(int i){return i==2?2:0;}
static inline int rr_tc_step(int i){return i==2?0:2;}   /* the switch: Off <-> On (Full) */
static inline int rr_tc_heavy(int i){return i==2;}
/* xBR texture filter together with Supersample 2x/4x: xBR's edge analysis then runs on every
 * supersampled pixel. Measured 18 Oct 2026 (i9-12900KF, RTX 5070, owner's settings, 16 riders,
 * PROGRESS-0.15-lag.md): 105 frames/s of capacity, against 160 with either one off, so 120 FPS
 * falls back to 60. The menus show the heavy warning on both rows. */
static inline int rr_xbr_ss_heavy(int filter,int aa){return filter==2&&aa>=3;}

/* Not offered: the renderer's anisotropic filtering (texture_lod), depth
 * buffer (pgxp_depth_buffer) and CRT colour model (crt_filter). The first two
 * act only on PGXP-tracked triangles (Texture correction Full) and were not
 * retried with it; the colour model only reaches the software scanout, not
 * the OpenGL present (PROGRESS-0.15-graphics.md). */

/* [display] scanlines: dark gaps between the PS1's picture lines, applied by
 * the renderer when the picture is shown (game.toml scanlines /
 * scanline_strength). The renderer fades them in with the output scale. */
enum { rr_scanline_steps = 4 };
static inline const char *rr_scanline_name(int i){static const char *const n[]={"Off","Light","Medium","Strong"};return n[i>=0&&i<rr_scanline_steps?i:0];}
static inline float rr_scanline_strength(int i){return i==1?.25f:i==2?.5f:i==3?.75f:0.f;}

/* [display] filter: texture filter. Sharp: the PS1's nearest texels; Smooth:
 * bilinear; xBR: the renderer's edge-directed magnifier (game.toml
 * texture_filtering "xbr", renderer mode 3: xBR level 2 for CLUT art, clamped
 * Catmull-Rom for 15-bit pictures, nearest below 4/3 magnification). */
enum { rr_filter_steps = 3 };
static inline const char *rr_filter_name(int i){static const char *const n[]={"Sharp","Smooth","xBR"};return n[i>=0&&i<rr_filter_steps?i:0];}
static inline int rr_filter_mode(int i){return i==1?1:i==2?3:0;}
static inline const char *rr_filter_toml(int i){static const char *const n[]={"nearest","bilinear","xbr"};return n[i>=0&&i<rr_filter_steps?i:0];}

/* [display] bloom / sharpen / colour / vignette: post effects on the finished
 * race picture (gpu_presentation.py post_effects), each Off / Light / Strong.
 * Races only, and never on the HUD: the renderer keeps a mask of the race
 * overlay's sprites and rectangles and leaves those pixels as drawn. */
enum { rr_post_steps = 3, rr_post_effects = 4 };
static inline const char *rr_post_name(int i){static const char *const n[]={"Off","Light","Strong"};return n[i>=0&&i<rr_post_steps?i:0];}
static inline const char *rr_post_key(int e){static const char *const n[]={"bloom","sharpen","colour","vignette"};return n[e>=0&&e<rr_post_effects?e:0];}
static inline const char *rr_post_label(int e){static const char *const n[]={"Bloom","Sharpening","Colour","Vignette"};return n[e>=0&&e<rr_post_effects?e:0];}
/* Measured above ~1 ms a frame at 1440p (PROGRESS-0.15-post-effects.md): the
 * menus show the heavy anti-aliasing's warning for these steps. */
static inline int rr_post_heavy(int e,int step){(void)e;(void)step;return 0;}

/* [display] shadow: Shadow smoothing. The bikes' and riders' shadows (flat
 * subtractive quads) get soft edges: Light / Strong blur them by about 0.8 /
 * 1.6 PS1 pixels (gpu_presentation.py shadow_smoothing). Races only. */
enum { rr_shadow_steps = 3 };
static inline const char *rr_shadow_name(int i){static const char *const n[]={"Off","Light","Strong"};return n[i>=0&&i<rr_shadow_steps?i:0];}
static inline int rr_shadow_heavy(int step){(void)step;return 0;}   /* measured, PROGRESS-0.15-post-effects.md */

/* [display] draw_distance: how far away riders, bikes and traffic are still
 * drawn, in percent of the game's own cull distance (pc_services.cpp
 * apply_draw). Multiples of 50 keep every scaled value exact. */
enum { rr_draw_steps = 4 };
static inline const char *rr_draw_name(int i){static const char *const n[]={"Original","1.5x","2x","3x"};return n[i>=0&&i<rr_draw_steps?i:0];}
static inline int rr_draw_percent(int i){return i==1?150:i==2?200:i==3?300:100;}
#endif
