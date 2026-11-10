/* Bounded decoding from a locally installed, verified owner disc. No assets
 * are embedded or downloaded. LGPL-2.1-or-later. */
#include "disc_reader.h"
#include "mdec_image.h"
#include "owner_assets.h"
static Image images[RR_ART_COUNT];
static Image titles[32];static int title_count;   /* /DATA/FE/TITLES.STR, in file order (menu_titles.cpp) */
static void load_fonts(Disc*,Entry);
static void load_bars(Disc*,Entry);
static void load_look_textures(Disc*,Entry);
static Image demo_cards[7];   /* race ids 31-37: 0 the old route map, 1-6 the demo courses */
static unsigned char*look_tex;static unsigned look_size;
static void rm_free(void);static void rm_load(Disc*,Entry);   /* rider_model.h: the 3D rider preview */
static int want_models;   /* the game only (its fallback preview); the launcher has no Rider look page */
void rr_art_want_models(int on){want_models=on;}
void rr_art_close(void){rm_free();free(look_tex);look_tex=NULL;look_size=0;for(int i=0;i<7;i++){free(demo_cards[i].pixels);memset(demo_cards+i,0,sizeof demo_cards[i]);}for(int i=0;i<RR_ART_COUNT;i++){free(images[i].pixels);memset(images+i,0,sizeof images[i]);}for(int i=0;i<32;i++){free(titles[i].pixels);memset(titles+i,0,sizeof titles[i]);}title_count=0;}
static void load_titles(Disc*d,Entry root){
 Entry e;if(!find_file(d,root,"/DATA/FE/TITLES.STR",&e)||e.size>256*1024)return;unsigned char*b=read_entry(d,e);if(!b)return;
 for(size_t p=0;p+24<=e.size&&title_count<32&&!memcmp(b+p,"MDEC",4);){unsigned z=be32(b+p+4);if(z<24||z>e.size-p)break;if(!decode_mdec(b+p,z,titles+title_count))break;title_count++;p+=z;}
 free(b);
}
/* Loading cards of race ids 31-37 (the demo courses and race 31's old route
 * map): /DATA/FE/FSLOAD.TCM holds one MDEC frame per 1P race id, indexed
 * {size, offset} after a count word. */
static void load_demo_cards(Disc*d,Entry root){
 Entry e;if(!find_file(d,root,"/DATA/FE/FSLOAD.TCM",&e)||e.size>2*1024*1024)return;unsigned char*b=read_entry(d,e);if(!b)return;
 unsigned n=e.size>=4?le32(b):0;
 for(int i=0;i<7;i++){unsigned k=30+i;if(n>64||k>=n||4+8*(k+1)>e.size)break;unsigned z=le32(b+4+8*k),o=le32(b+8+8*k);
  if(o<4+8*n||z<24||o>e.size||z>e.size-o||memcmp(b+o,"MDEC",4))continue;decode_mdec(b+o,z,demo_cards+i);}
 free(b);
}
const RrArt*rr_demo_card(int i){return i>=0&&i<6&&demo_cards[i+1].pixels?(const RrArt*)(demo_cards+i+1):NULL;}
int rr_title_count(void){return title_count;}
const RrArt*rr_title_get(int i){return i>=0&&i<title_count?(const RrArt*)(titles+i):NULL;}
static int frame(Disc*d,Entry root,const char*path,unsigned n,Image*out){
 Entry e;if(!find_file(d,root,path,&e))return 0;unsigned char*b=read_entry(d,e);if(!b)return 0;size_t p=0;
 while(n--){if(p+16>e.size||memcmp(b+p,"MDEC",4))goto fail;unsigned z=be32(b+p+4);if(z<24||z>e.size-p)goto fail;p+=z;}
 {int ok=p<e.size&&decode_mdec(b+p,e.size-p,out);free(b);return ok;}
fail:free(b);return 0;
}
int rr_art_open(const wchar_t*path){
 rr_art_close();Disc d={0};Entry root,e;unsigned char*b=NULL;char hash[65];int valid=0;
 if(open_disc(path,&d,&root)&&find_file(&d,root,"/SLUS_010.53",&e)){
 b=read_entry(&d,e);valid=b&&e.size>=2048&&!memcmp(b,"PS-X EXE",8)&&sha256(b,e.size,hash)&&!strcmp(hash,"52a3e28b2f97e49d8122eb4f510041e64dce046fd8fa9d4cdc49f9e64ff66a7b");}
 free(b);
 /* Kinds (owner_assets.h RR_ART_*): menu stills from the owner's /DATA/FE,
  * the n-th MDEC frame of each stream; 9 / 10 are the FEMISC bars. */
 static const struct {const char*path;unsigned frame;} stills[RR_ART_COUNT]={{"BGRND2",0},{"OPTIONS",0},{"ALIAS",0},{"TROPHY",0},{"BIKES",0},{"CNTRLLR",0},{"OPTIONS",12},{"RR_LOGO",14},{"OPTIONS",22},{NULL,0},{NULL,0},
  {"MODETT",0},{"WRECK",0},{"KAF_WIN",0},{"FIVEO",0},{"MODES",0},{"MODECNR",0},{"MODEH2HS",0},{"SSCOURSE",0},{"MODEH2H",0},{"JAILED",0},   /* beta cards: demo_cards */
  [RR_ART_SWITCH]={"ALIAS",2},{"ALIAS",3},{"ALIAS",4},{"ALIAS",5},{"ALIAS",6},{"ALIAS",7},{"ALIAS",1},
  {"BUST",5},{"BUST",6},{"BUST",7},{"BUST",9},{"BUST",4},{"BUST",2},
  {"MISCRES",1},{"MISCRES",6},{"MISCRES",7},{"MISCRES",5},{"FIVEO",3}};
 if(valid){load_fonts(&d,root);load_bars(&d,root);load_titles(&d,root);load_demo_cards(&d,root);load_look_textures(&d,root);if(want_models)rm_load(&d,root);
 for(int i=0;i<RR_ART_COUNT;i++)if(stills[i].path){char resource[80];snprintf(resource,sizeof resource,"/DATA/FE/%s.STR",stills[i].path);frame(&d,root,resource,stills[i].frame,images+i);}}
 if(d.fp)fclose(d.fp);return valid;
}
const RrArt*rr_art_get(int kind){
 if(kind>=RR_ART_MEADOWS&&kind<=RR_ART_BETA_MAP){const Image*c=demo_cards+(kind==RR_ART_BETA_MAP?0:kind-RR_ART_MEADOWS+1);return (const RrArt*)c;}
 return kind>=0&&kind<RR_ART_COUNT?(const RrArt*)(images+kind):NULL;}
int rr_mdec_decode(const unsigned char*data,size_t size,unsigned*w,unsigned*h,uint32_t**pixels){Image im={0};if(!decode_mdec(data,size,&im))return 0;*w=im.w;*h=im.h;*pixels=im.pixels;return 1;}
/* Read one sound catalogue only after validating the owner's executable. */
const unsigned char*rr_owner_taunts(const wchar_t*path,unsigned*size){
 static unsigned char*bytes;static unsigned length;if(size)*size=0;if(bytes){if(size)*size=length;return bytes;}
 Disc d={0};Entry root,e;unsigned char*b=NULL;char hash[65];int valid=0;
 if(open_disc(path,&d,&root)&&find_file(&d,root,"/SLUS_010.53",&e)){b=read_entry(&d,e);valid=b&&sha256(b,e.size,hash)&&!strcmp(hash,"52a3e28b2f97e49d8122eb4f510041e64dce046fd8fa9d4cdc49f9e64ff66a7b");}free(b);
 if(valid&&find_file(&d,root,"/DATA/AUDTAUNT.STR",&e)&&e.size<=4*1024*1024){bytes=read_entry(&d,e);if(bytes)length=e.size;}if(d.fp)fclose(d.fp);if(size)*size=length;return bytes;
}
/* The race soundtrack album, /DATA/ALBUM.ALB on the owner's disc (63.5 MB,
 * read in pieces, never stored): raw PS1 ADPCM, 8 KB of the left channel then
 * 8 KB of the right, about 16 kHz (the game plays it on SPU voices 23 / 22 at
 * pitch 0x5CE in one-player races; found by matching their ring buffers). The
 * game plays no music in two-player races; custom_music.cpp plays this there.
 * Returns the bytes read (0: no disc / past the end). Not thread safe. */
unsigned rr_owner_album_read(const wchar_t*path,unsigned offset,unsigned char*out,unsigned n){
 static Disc d;static Entry e;static int state;   /* 0 not opened, 1 ready, -1 unavailable */
 if(!state){Entry root;state=-1;if(open_disc(path,&d,&root)&&find_file(&d,root,"/DATA/ALBUM.ALB",&e)&&e.size>=0x10000)state=1;else if(d.fp){fclose(d.fp);d.fp=NULL;}}
 if(state<0||offset>=e.size)return 0;if(n>e.size-offset)n=e.size-offset;
 unsigned char block[2048];unsigned got=0;
 while(got<n){unsigned at=offset+got,o=at%2048,k=2048-o;if(k>n-got)k=n-got;if(!sector(&d,e.lba+at/2048,block))break;memcpy(out+got,block+o,k);got+=k;}
 return got;
}
/* Taunt voices. A catalogue block's kind is a character, not a speaker: the
 * twenty numbered sets hold different lines for the same characters. Kind 6 is
 * Player 1's rider, 0x16 Player 2's; AI riders speak the kind their profile
 * selects (Lt Bull 0x20, Lt Tasha 0x21, Gr Mongo 0x50 and Gr Jork 0x51, the
 * second variant of the 0x20/0x21 blocks; the gangs share kind 4 / 5, the
 * variants of the kind 4 block; cops 7). launcher.ini [audio] voice1/voice2
 * hold an index into this table, 0 = the race's own voice. */
/* The disc has six AI voices; the other riders share them as the game itself
 * assigns them (speech code 0x8001A9CC: a profile whose kind has no block
 * falls back to kind 4 + (profile & 3), the gang voice). Every rider is listed
 * by name; the first ten keep their old index. */
static const struct {const char*name;unsigned char kind,variant;} voices[]={{"Original",0,0},
 {"Lt Tasha",0x21,0},{"Lt Bull",0x20,0},{"Gr Jork",0x21,1},{"Gr Mongo",0x20,1},{"Gator's gang",4,0},{"Aldente's gang",4,1},{"Cop",7,2},{"Player 1 rider",6,2},{"Player 2 rider",0x16,0},
 {"Cp Draco",4,0},{"Cp Helmut",4,1},{"Gator",4,0},{"Aldente",4,1},{"Zed",4,0},{"Sergio",4,1},{"Tiny",4,0},{"Huang",4,1},{"Scar",4,0},{"Scooter",4,1},{"Kahura",4,1}};
int rr_voice_canonical(int v){int n=(int)(sizeof voices/sizeof*voices);if(v<=0||v>=n)return 0;for(int i=1;i<v;i++)if(voices[i].kind==voices[v].kind&&voices[i].variant==voices[v].variant)return i;return v;}
const char*rr_voice_name(int v){return v>0&&v<(int)(sizeof voices/sizeof*voices)?voices[v].name:voices[0].name;}
/* variant 2: both variants of the block are the same character. */
int rr_voice_role(int v,int seat,unsigned*kind,unsigned*variant){if(v<=0||v>=(int)(sizeof voices/sizeof*voices)||voices[v].kind==(seat?0x16u:6u))return 0;if(kind)*kind=voices[v].kind;if(variant)*variant=voices[v].variant;return 1;}
int rr_voice_next_role(const unsigned char*b,unsigned size,int current,int seat,int step){int n=(int)(sizeof voices/sizeof*voices);if(!b)return 0;
 for(int i=0;i<n;i++){current=((current+step)%n+n)%n;unsigned kind;if(!current)return 0;if(!rr_voice_role(current,seat,&kind,NULL))continue;for(unsigned p=0;p+0x4000<=size;p+=0x4000)if((le32(b+p)&0xffff00ffu)==(0xa0000000u|kind)&&b[p+1]>=0x81)return current;}return 0;}
int rr_owner_voice_next(const wchar_t*path,int current,int seat){unsigned size=0;const unsigned char*b=rr_owner_taunts(path,&size);return rr_voice_next_role(b,size,current,seat,1);}
int rr_owner_voice_step(const wchar_t*path,int current,int seat,int step){unsigned size=0;const unsigned char*b=rr_owner_taunts(path,&size);return rr_voice_next_role(b,size,current,seat,step<0?-1:1);}
/* A catalogue block: id at +0 (0xA000ssKK, set ss >= 0x81, kind KK), clip
 * entries at +0x28 + (+0x38), count in the low byte at +0x2C, 16 bytes each:
 * three attribute words and the ADPCM offset from +0x60. */
int rr_voice_clip(const unsigned char*b,unsigned size,int voice,int seat,unsigned n,const unsigned char**adpcm,unsigned*bytes,uint32_t attr[3]){
 unsigned kind,variant,count=0;if(!b||!rr_voice_role(voice,seat,&kind,&variant))return 0;
 for(unsigned p=0;p+0x4000<=size;p+=0x4000)if((le32(b+p)&0xffff00ffu)==(0xa0000000u|kind)&&b[p+1]>=0x81)count++;
 if(!count)return 0;unsigned want=n%count;
 for(unsigned p=0;p+0x4000<=size;p+=0x4000){if((le32(b+p)&0xffff00ffu)!=(0xa0000000u|kind)||b[p+1]<0x81||want--)continue;
  unsigned entries=b[p+0x2c],table=le32(b+p+0x38),v=variant==2?(n/count)&1:variant;if(!entries||entries>2||table<0x14||table+0x28+entries*16>0x60)return 0;if(v>=entries)v=0;
  const unsigned char*e=b+p+0x28+table+v*16;unsigned start=0x60+le32(e+12),end=start;if(start%16||start>=0x4000)return 0;
  while(end+16<=0x4000&&!(b[p+end+1]&1))end+=16;if(end+16>0x4000)return 0;
  *adpcm=b+p+start;*bytes=end+16-start;for(int i=0;i<3;i++)attr[i]=le32(e+4*i);return 1;}
 return 0;
}
unsigned rr_adpcm_decode(const unsigned char*a,unsigned bytes,short*out,unsigned cap){
 static const int k0[5]={0,60,115,98,122},k1[5]={0,0,-52,-55,-60};int h1=0,h2=0;unsigned n=0;
 for(unsigned p=0;p+16<=bytes&&n+28<=cap;p+=16){int shift=a[p]&15,f=a[p]>>4;if(f>4)f=4;if(shift>12)shift=9;
  for(int i=0;i<28;i++){int s=(int16_t)(((a[p+2+i/2]>>((i&1)*4))&15)<<12)>>shift;s+=(h1*k0[f]+h2*k1[f]+32)>>6;if(s>32767)s=32767;if(s<-32768)s=-32768;out[n++]=(short)s;h2=h1;h1=s;}
  if(a[p+1]&1)break;}
 return n;
}
/* Rider look presets. Paint recolours the player's bike palette to a hue
 * (rider_enhancements.cpp); the name tag tints the player's race name; an
 * outfit gives the player that rider's model, texture and palette. Outfits
 * are named by the riders' voice characters (profile ids as in rr_voice_role's
 * table: Lt Tasha 0x21, Lt Bull 0x20, Gr Jork 0x51, Gr Mongo 0x50). */
static const struct {const char*name;short hue;} paints[]={{"Original",-1},{"Red",0},{"Orange",28},{"Yellow",54},{"Green",125},{"Blue",220},{"Purple",275},{"Pink",325}};
static const struct {const char*name;uint32_t rgb;} tags[]={{"Original",0},{"White",0xFFFFFF},{"Yellow",0xFFE628},{"Orange",0xFF8C14},{"Red",0xFF3228},{"Green",0x3CFF50},{"Cyan",0x3CE6FF},{"Pink",0xFF5AC8}};
/* Outfits by gang and rank: a rider model (race model table 0x800CE1B0 entry:
 * 3 = DeSades body, 4 = Kaffe Boys body), an outfit texture set (texture table
 * *(0x8005B2E4) entry; every BBLEVEL*.TEX loads the same outfit textures into
 * sets 8-14: LECT ids 52, 51, 4, 3, 2, 1, 56) and the palette slot the game gives
 * that rank (riders read from race savestates, 17 Oct 2026: record +1 profile
 * 0x10 Draco set 11 slot 11, 0x20 Bull 12/10, DeSades grunts 12-13/8-9; 0x11
 * Helmut 8/7, 0x21 Tasha 9/6, Kaffe grunts 9-10/4-5; police 0x12/0x22 14/12).
 * The first four keep their old index (launcher.ini outfit1/2: Lt Tasha,
 * Lt Bull, Gr Jork, Gr Mongo). */
static const struct {const char*name;unsigned char model,set,slot,profile;} outfits[]={{"Original",0,0,0,0},
 {"Kaffe Lieutenant",4,9,6,0x21},{"DeSades Lieutenant",3,12,10,0x20},{"Kaffe Grunt",4,10,4,0x51},{"DeSades Grunt",3,13,8,0x50},
 {"DeSades Captain",3,11,11,0x10},{"Kaffe Captain",4,8,7,0x11},{"Police",3,14,12,0x12}};
/* Colours: one of the game's own rider palettes (BBLEVEL*.TEX PBNK, uploaded
 * into palette slots 0-28 of every race), copied into the player's slot.
 * Slots 4-12 are the gangs' and police colours, 14-28 schemes no rider in the
 * checked races wears (UNUSED-CONTENT.md, section 6). */
static const unsigned char colour_slots[]={0,4,5,6,7,8,9,10,11,12,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28};
static const char*const colour_names[]={"Original","Kaffe 1","Kaffe 2","Kaffe Lt","Kaffe Captain","DeSades 1","DeSades 2","DeSades Lt","DeSades Captain","Police",
 "Scheme 1","Scheme 2","Scheme 3","Scheme 4","Scheme 5","Scheme 6","Scheme 7","Scheme 8","Scheme 9","Scheme 10","Scheme 11","Scheme 12","Scheme 13","Scheme 14","Scheme 15"};
int rr_look_count(int kind){return kind==RR_LOOK_PAINT?(int)(sizeof paints/sizeof*paints):kind==RR_LOOK_TAG?(int)(sizeof tags/sizeof*tags):kind==RR_LOOK_COLOUR?(int)sizeof colour_slots:(int)(sizeof outfits/sizeof*outfits);}
const char*rr_look_name(int kind,int v){if(v<0||v>=rr_look_count(kind))v=0;return kind==RR_LOOK_PAINT?paints[v].name:kind==RR_LOOK_TAG?tags[v].name:kind==RR_LOOK_COLOUR?colour_names[v]:outfits[v].name;}
int rr_paint_hue(int v){return v>0&&v<rr_look_count(RR_LOOK_PAINT)?paints[v].hue:-1;}
uint32_t rr_tag_rgb(int v){return v>0&&v<rr_look_count(RR_LOOK_TAG)?tags[v].rgb:0;}
int rr_outfit_look(int v,int*model,int*set,int*slot,int*profile){if(v<=0||v>=rr_look_count(RR_LOOK_OUTFIT))return 0;*model=outfits[v].model;*set=outfits[v].set;*slot=outfits[v].slot;*profile=outfits[v].profile;return 1;}
int rr_colour_slot(int v){return v>0&&v<rr_look_count(RR_LOOK_COLOUR)?colour_slots[v]:-1;}
/* Look preview (Rider look page): the outfit's texture sheet in the chosen
 * palette, from the owner's BBLEVEL1.TEX (PBNK = 48 palettes of 128 colours
 * as a 384x16 image, slot k at row 15 - k/3, column k%3; LECT = one 128x60
 * 8-bit texture, id at +16, pixels at +568). */
static void load_look_textures(Disc*d,Entry root){Entry e;if(!find_file(d,root,"/DATA/BBLEVEL1.TEX",&e)||e.size<0x3014||e.size>512*1024)return;free(look_tex);look_tex=read_entry(d,e);look_size=look_tex?e.size:0;
 if(look_tex&&(memcmp(look_tex,"KNBP",4)||le32(look_tex+4)!=0x3014)){free(look_tex);look_tex=NULL;look_size=0;}}
#include "rider_model.h"
/* The 3D preview: the outfit's rider body, texture and colours on the bike in
 * its paint, turned yaw degrees; ss = supersampling. */
int rr_look_preview3d(int outfit,int colour,int paint,double yaw,uint32_t*rgb,unsigned w,unsigned h,int ss){
 static const unsigned char set_ids[]={52,51,4,3,2,1,56};   /* texture sets 8-14 */
 int model=3,set=13,slot=8,profile=0;rr_outfit_look(outfit,&model,&set,&slot,&profile);int c=rr_colour_slot(colour);if(c>=0)slot=c;
 if(set<8||set>14||slot<0||slot>28)return 0;
 return rm_render(model==4,set_ids[set-8],slot,rr_paint_hue(paint),yaw,rgb,w,h,ss);
}
int rr_look_preview(int outfit,int colour,uint32_t*rgb,unsigned w,unsigned h){
 static const unsigned char set_ids[]={52,51,4,3,2,1,56};   /* texture sets 8-14 */
 if(!look_tex||!rgb||!w||!h)return 0;
 int model=3,set=13,slot=8,profile=0;rr_outfit_look(outfit,&model,&set,&slot,&profile);int c=rr_colour_slot(colour);if(c>=0)slot=c;
 if(set<8||set>14||slot<0||slot>28)return 0;
 const unsigned char*tex=NULL;for(unsigned p=0x3014;p+568+128*60<=look_size;p++)if(!memcmp(look_tex+p,"LECT",4)&&look_tex[p+16]==set_ids[set-8]){tex=look_tex+p+568;break;}
 if(!tex)return 0;const unsigned char*pal=look_tex+0x14+((15-slot/3)*384+(slot%3)*128)*2;
 /* The 128x60 sheet fitted whole, on black. */
 const double scale=(double)w/128<(double)h/60?(double)w/128:(double)h/60;const unsigned dw=(unsigned)(128*scale),dh=(unsigned)(60*scale),ox=(w-dw)/2,oy=(h-dh)/2;
 for(unsigned i=0;i<w*h;i++)rgb[i]=0xff000000u;
 for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){unsigned sx=(unsigned)(x/scale),sy=(unsigned)(y/scale);if(sx>127)sx=127;if(sy>59)sy=59;unsigned i=tex[sy*128+sx];if(i>127)i=0;
  unsigned v=pal[i*2]|pal[i*2+1]<<8;rgb[(oy+y)*w+ox+x]=0xff000000u|((v&31)*255/31)<<16|((v>>5&31)*255/31)<<8|(v>>10&31)*255/31;}
 return 1;
}
/* The launcher draws the owner's original FNTP glyphs and SHPP bars. */
typedef struct RrGlyph {unsigned code,w,h,x,y,advance;int ox,oy;} RrGlyph;
typedef struct RrFont {RrGlyph glyph[96];unsigned count,w,h,cap;unsigned char*alpha;} RrFont;
static RrFont fonts[2];
static unsigned char*scale2(const unsigned char*p,unsigned w,unsigned h){unsigned char*q=malloc((size_t)w*h*4);if(!q)return NULL;for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){unsigned char e=p[y*w+x],b=p[(y?y-1:y)*w+x],d=p[y*w+(x?x-1:x)],f=p[y*w+(x+1<w?x+1:x)],hh=p[(y+1<h?y+1:y)*w+x];unsigned z=y*2*w*2+x*2;q[z]=b!=hh&&d!=f&&d==b?d:e;q[z+1]=b!=hh&&d!=f&&b==f?f:e;q[z+w*2]=b!=hh&&d!=f&&d==hh?d:e;q[z+w*2+1]=b!=hh&&d!=f&&hh==f?f:e;}return q;}
static void load_font(Disc*d,Entry root,const char*path,RrFont*f){Entry e;if(!find_file(d,root,path,&e))return;unsigned char*b=read_entry(d,e);if(!b)return;if(e.size<1104||memcmp(b,"FNTP",4))goto done;unsigned n=le16(b+10),o=le32(b+28);if(n>96||32+n*11>e.size||o>e.size-16||b[o]!=0x40)goto done;unsigned w=le16(b+o+4),h=le16(b+o+6);if(!w||!h||w>512||h>512||(size_t)w*h/2>e.size-o-16)goto done;
 unsigned char*a=malloc((size_t)w*h);if(!a)goto done;for(unsigned i=0;i<w*h;i++)a[i]=((b[o+16+i/2]>>(i%2*4))&15)*17;
 unsigned char*a2=scale2(a,w,h);free(a);if(!a2)goto done;unsigned char*a4=scale2(a2,w*2,h*2);free(a2);if(!a4)goto done;free(f->alpha);memset(f,0,sizeof *f);f->alpha=a4;f->w=w*4;f->h=h*4;f->count=n;f->cap=9;
 for(unsigned i=0;i<n;i++){unsigned char*g=b+32+i*11;f->glyph[i]=(RrGlyph){le16(g),g[2],g[3],le16(g+4),le16(g+6),g[8],(signed char)g[9],(signed char)g[10]};if(le16(g)=='H')f->cap=g[3];}
done:free(b);}
static const RrGlyph*glyph(const RrFont*f,unsigned c){for(unsigned i=0;i<f->count;i++)if(f->glyph[i].code==c)return f->glyph+i;return NULL;}
int rr_original_text(HDC dc,const wchar_t*text,int x,int y,int w,int h,int size,int title,COLORREF color){
 RrFont*f=fonts+(title!=0);if(!f->alpha||!text||w<=0||h<=0)return 0;POINT p[2]={{0,0},{w,h}};LPtoDP(dc,p,2);int pw=abs(p[1].x-p[0].x),ph=abs(p[1].y-p[0].y);if(!pw||!ph||pw>7680||ph>4320)return 0;
 BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=pw;bi.bmiHeader.biHeight=-ph;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;uint32_t*bits;HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);if(!bitmap)return 0;memset(bits,0,(size_t)pw*ph*4);double sx=(double)size/f->cap*pw/w*.625,sy=(double)size/f->cap*ph/h;double cx=0,cy=0,line=(size+5.0)*ph/h;
 // Single-line labels fit as one run. Scale both axes together so a long
 // filename or option value cannot wrap into the next button or get clipped.
 if(h<=size+8&&!wcschr(text,L'\n')){
  double advance=0,left=0,right=0,top=0,bottom=f->cap;
  for(unsigned i=0;text[i];i++){const RrGlyph*g=glyph(f,text[i]<128?text[i]:'?');if(!g)continue;if(advance+g->ox<left)left=advance+g->ox;if(advance+g->ox+g->w>right)right=advance+g->ox+g->w;if(g->oy<top)top=g->oy;if(g->oy+(int)g->h>bottom)bottom=g->oy+(int)g->h;advance+=g->advance;}
  if(advance>right)right=advance;double fit=1.0;if((right-left)*sx>pw)fit=pw/((right-left)*sx);if((bottom-top)*sy*fit>ph)fit=ph/((bottom-top)*sy);sx*=fit;sy*=fit;cx=-left*sx;cy=(ph-(bottom-top)*sy)/2-top*sy;
 }

 for(unsigned i=0;text[i];i++){unsigned c=text[i];if(c=='\n'){cx=0;cy+=line;continue;}const RrGlyph*g=glyph(f,c<128?c:'?');if(!g)continue;double advance=g->advance*sx;if(c==' '){cx+=advance;continue;}
 if(i==0||text[i-1]==' '||text[i-1]=='\n'){double word=0;for(unsigned j=i;text[j]&&text[j]!=' '&&text[j]!='\n';j++){const RrGlyph*gg=glyph(f,text[j]<128?text[j]:'?');if(gg)word+=gg->advance*sx;}if(cx>0&&cx+word>pw){cx=0;cy+=line;}}
 if(cx+advance>pw&&cx>0){cx=0;cy+=line;}int gx=(int)(cx+g->ox*sx),gy=(int)(cy+g->oy*sy),gw=(int)(g->w*sx+.5),gh=(int)(g->h*sy+.5);if(cy>=ph)break;
 for(int yy=0;yy<gh;yy++)for(int xx=0;xx<gw;xx++){int dx=gx+xx,dy=gy+yy;if(dx<0||dy<0||dx>=pw||dy>=ph)continue;unsigned ax=g->x*4+(unsigned)((xx+.5)*g->w*4/gw),ay=g->y*4+(unsigned)((yy+.5)*g->h*4/gh);if(ax>=f->w||ay>=f->h)continue;unsigned a=f->alpha[ay*f->w+ax];if(!a)continue;unsigned r=GetRValue(color)*a/255,green=GetGValue(color)*a/255,b=GetBValue(color)*a/255;bits[dy*pw+dx]=a<<24|r<<16|green<<8|b;}
 cx+=advance;}
 HDC src=CreateCompatibleDC(dc);HGDIOBJ old=SelectObject(src,bitmap);BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};AlphaBlend(dc,x,y,w,h,src,0,0,pw,ph,blend);SelectObject(src,old);DeleteDC(src);DeleteObject(bitmap);return 1;
}
static void load_bars(Disc*d,Entry root){Entry e;if(!find_file(d,root,"/DATA/FE/FEMISC.PSH",&e))return;unsigned char*b=read_entry(d,e);if(!b)return;if(e.size>=16&&!memcmp(b,"SHPP",4)){unsigned n=le32(b+8);if(n<=256&&16+n*8<=e.size)for(unsigned i=0;i<n;i++){unsigned char*r=b+16+i*8;int index=!memcmp(r,"BTNN",4)?9:!memcmp(r,"BTNH",4)?10:-1;if(index<0)continue;unsigned o=le32(r+4);if(o>e.size-16||b[o]!=0x42)continue;unsigned w=le16(b+o+4),h=le16(b+o+6);if(!w||!h||w>1024||h>1024||(size_t)w*h*2>e.size-o-16)continue;Image*a=images+index;a->w=w;a->h=h;a->pixels=malloc((size_t)w*h*4);if(!a->pixels)continue;for(unsigned j=0;j<w*h;j++){unsigned c=le16(b+o+16+j*2);a->pixels[j]=((c&31)*255/31<<16)|(((c>>5)&31)*255/31<<8)|((c>>10)&31)*255/31;}}}free(b);}

static void load_fonts(Disc*d,Entry root){load_font(d,root,"/DATA/FE/BTN_FONT.PFN",fonts);load_font(d,root,"/DATA/FE/HDR_FONT.PFN",fonts+1);}
