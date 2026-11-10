/* Owner-disc artwork only. Independently authored; LGPL-2.1-or-later. */
#pragma once
#include <stdint.h>
#include <windows.h>
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct RrArt {unsigned w,h;const uint32_t *pixels;} RrArt;
/* Artwork kinds, decoded from the owner's disc only (none is shipped). */
enum {RR_ART_BACKGROUND,RR_ART_CONTROLLER,RR_ART_RIDERS,RR_ART_TROPHY,RR_ART_CHOPPER,RR_ART_PAD,RR_ART_MUSIC,RR_ART_LOGO,RR_ART_CARD,RR_ART_BAR,RR_ART_BAR_LIT,
      RR_ART_STOPWATCH,RR_ART_WRECK,RR_ART_TAUNT,RR_ART_WARRANT,RR_ART_STONE_LOGO,RR_ART_SPORT_BIKE,RR_ART_RACE_BIKE,RR_ART_MAP,RR_ART_SKULLS,RR_ART_JAILED,
      /* Unused beta loading cards of FSLOAD.TCM (UNUSED-CONTENT.md 1b): the demo
         courses 32-37 and race 31's old-style route map. */
      RR_ART_MEADOWS,RR_ART_COUNTRYSIDE,RR_ART_ALPINE,RR_ART_RASH_CITY,RR_ART_CACTUS_HILLS,RR_ART_COAST,RR_ART_BETA_MAP,
      /* More of the game's own art (FE .STR frames): the six Jail Break riders and a
         gang, the cops and cells of the busted screens, Spaz's story, a mugshot. */
      RR_ART_SWITCH,RR_ART_STACKS,RR_ART_BRICK,RR_ART_CHOPS,RR_ART_KAT,RR_ART_DRU,RR_ART_GANG,
      RR_ART_COP,RR_ART_DONUT_COP,RR_ART_LADY_COP,RR_ART_HELMET_COP,RR_ART_CAGED,RR_ART_GRABBED,
      RR_ART_FREE_SPAZ,RR_ART_SPAZ_CALL,RR_ART_SPAZ_ESCORT,RR_ART_SPAZ_BAR,RR_ART_MUGSHOT,RR_ART_COUNT};
/* Validates the supported executable before decoding any artwork. */
int rr_art_open(const wchar_t *disc);
/* Also read the rider and bike models (rr_look_preview3d) at the next rr_art_open. */
void rr_art_want_models(int on);
const RrArt *rr_art_get(int kind);
void rr_art_close(void);
int rr_owner_voice_next(const wchar_t *disc,int current,int seat);
/* The owner's taunt catalogue (DATA/AUDTAUNT.STR), read once after validating the executable. */
const unsigned char *rr_owner_taunts(const wchar_t *disc,unsigned *size);
/* Taunt voice characters (launcher.ini [audio] voice1/voice2; 0 = original). */
const char *rr_voice_name(int voice);
/* The first voice with the same character (rider names that share one voice). */
int rr_voice_canonical(int voice);
int rr_voice_role(int voice,int seat,unsigned *kind,unsigned *variant);
int rr_voice_next_role(const unsigned char *catalogue,unsigned size,int current,int seat,int step);
int rr_owner_voice_step(const wchar_t *disc,int current,int seat,int step);
/* Voice preview: the n-th catalogue clip of a voice character (wrapping over the
 * sets that hold it). adpcm points into the catalogue; attr = the clip's three
 * attribute words (attr[1] >> 16 = ADSR1, attr[2] & 0xFFFF = ADSR2, attr[2] >> 16 = pitch). */
int rr_voice_clip(const unsigned char *catalogue,unsigned size,int voice,int seat,unsigned n,const unsigned char **adpcm,unsigned *bytes,uint32_t attr[3]);
/* PS1 SPU ADPCM to 16-bit PCM, up to the end-flag frame; returns samples written. */
unsigned rr_adpcm_decode(const unsigned char *adpcm,unsigned bytes,short *out,unsigned cap);
/* The race soundtrack (/DATA/ALBUM.ALB, owner's disc): bytes from offset; 0 = unavailable. */
unsigned rr_owner_album_read(const wchar_t *disc,unsigned offset,unsigned char *out,unsigned n);
/* Rider look presets (launcher.ini [riders] paint1/2, tag1/2, outfit1/2, colour1/2; 0 = original).
 * kind: 0 bike paint, 1 name tag colour, 2 rider outfit, 3 rider colours. */
enum {RR_LOOK_PAINT,RR_LOOK_TAG,RR_LOOK_OUTFIT,RR_LOOK_COLOUR,RR_LOOK_KINDS};
int rr_look_count(int kind);
const char *rr_look_name(int kind,int value);
int rr_paint_hue(int value);          /* degrees, -1 = original */
uint32_t rr_tag_rgb(int value);       /* 0xRRGGBB, 0 = original */
/* An outfit's race model table entry, texture set, the rank's palette slot and a
 * rider profile id of that rank (whose build is used when one is in the race); 0 = original. */
int rr_outfit_look(int value,int *model,int *set,int *slot,int *profile);
int rr_colour_slot(int value);        /* the race palette slot (0-28) copied to the player, -1 = original */
/* The outfit's texture sheet in the chosen colours, fitted into w x h 0xAARRGGBB; 0 if unavailable. */
int rr_look_preview(int outfit,int colour,uint32_t *rgb,unsigned w,unsigned h);
/* 3D rider preview (rider_model.h): the rider on the bike, turned yaw degrees,
 * ss = supersampling 1-3. 0 when the disc's models could not be read. */
int rr_look_preview3d(int outfit,int colour,int paint,double yaw,uint32_t *rgb,unsigned w,unsigned h,int ss);
int rr_original_text(HDC,const wchar_t*,int,int,int,int,int,int,COLORREF);
/* One BS-v2 still the game holds in RAM (menu_titles.cpp); pixels 0xRRGGBB, free() them. */
int rr_mdec_decode(const unsigned char *data,size_t size,unsigned *w,unsigned *h,uint32_t **pixels);
/* The title banners of /DATA/FE/TITLES.STR in file order, decoded by rr_art_open. */
int rr_title_count(void);
/* The demo courses' beta loading cards (race ids 32-37, FSLOAD.TCM), decoded by rr_art_open; 384x240. */
const RrArt *rr_demo_card(int index);
const RrArt *rr_title_get(int index);
#ifdef __cplusplus
}
#endif
