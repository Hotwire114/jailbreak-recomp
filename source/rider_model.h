/* 3D rider preview (Rider look pages, launcher and game): the owner's own rider
 * and bike models, textures, riding animation and skeleton, read from their
 * verified disc and drawn by a small software renderer. Included once, by
 * owner_assets.c. Independently authored; LGPL-2.1-or-later.
 *
 * Nothing from the game is stored here, only readers for its formats, written
 * from what the game's own routines do (PROGRESS-0.17-rider-preview.md):
 *  - /DATA/BBLEVEL1.GEO  RMD3 models (file order = race model table: 0-2 bikes,
 *                        3 DeSades body, 4 Kaffe body); LOD 0 = first DOD3.
 *  - /DATA/BBLEVEL1.TEX  PBNK rider palettes + LECT 128x60 8-bit textures.
 *  - /DATA/ANIMTBL1.PSX  DMD3 animations; block 11 is the riding loop.
 *  - /RASHCDG.BIN        the race overlay's part tables (0x800CC790 entries,
 *                        0x800CC854 type records), read as data.
 * DOD3: +0x18 u16 parts, +0x24 vertex list, +0x28 normals, +0x30 per-vertex
 * normal index; DPD3 part: +0x0E u16 count, +0x10 u32 first vertex, +0x14 face
 * list; face (20 bytes): u0 v0, u16 A (bits 11-13 corners, bit 7 the shared
 * wheel texture), u1 v1, u16 B, u2 v2 u3 v3, u16 index[4] (cyclic order). */
#include <math.h>

#define RM_PARTS 24
typedef struct RmModel {
 unsigned nv,np,nn,nvn;const unsigned char*verts,*normals,*vnorm;
 struct {unsigned first,count,nf;const unsigned char*faces;} part[RM_PARTS];
} RmModel;
typedef struct RmLink {unsigned char part,parent,joint,bone;} RmLink;
static struct {
 unsigned char*geo,*anim,*tables;unsigned geo_size;int ready;
 RmModel rider[2],bike;unsigned bike_tex;     /* LECT id of the bike's texture */
 RmLink skel[16],bikeskel[8];int nbike;int seat_joint;
 short frames[8][71];int nframes;
} rm;

static int rm_model(const unsigned char*b,unsigned size,int index,RmModel*m){
 memset(m,0,sizeof *m);unsigned p=0;int n=-1;
 while(p+16<=size){
  if(!memcmp(b+p,"RMD3",4)){n++;p+=16;continue;}
  if(memcmp(b+p,"DOD3",4))return 0;
  unsigned dsize=le32(b+p+4),np=le16(b+p+0x18),vl=le32(b+p+0x24);if(dsize<0x34||dsize>size-p||np>RM_PARTS||vl+4>dsize)return 0;
  unsigned q=p+dsize;
  if(n==index){
   m->nv=le32(b+p+vl);if(m->nv>1024||vl+4+m->nv*8>dsize)return 0;m->verts=b+p+vl+4;m->np=np;
   unsigned nl=le32(b+p+0x28),il=le32(b+p+0x30);
   if(nl&&nl+4<=dsize){m->nn=le32(b+p+nl);if(m->nn<=1024&&nl+4+m->nn*8<=dsize)m->normals=b+p+nl+4;else m->nn=0;}
   if(il&&il+4<=dsize){m->nvn=le32(b+p+il);if(m->nvn==m->nv&&il+4+m->nvn*2<=dsize)m->vnorm=b+p+il+4;else m->nvn=0;}
  }
  for(unsigned g=0;g<np;g++){
   if(q+0x18>size||memcmp(b+q,"DPD3",4))return 0;unsigned psize=le32(b+q+4),fl=le32(b+q+0x14);if(psize<0x18||psize>size-q||fl+4>psize)return 0;
   if(n==index){unsigned nf=le32(b+q+fl);if(nf>512||fl+4+nf*20>psize)return 0;m->part[g].count=le16(b+q+0x0E);m->part[g].first=le32(b+q+0x10);m->part[g].nf=nf;m->part[g].faces=b+q+fl+4;
    if(m->part[g].first+m->part[g].count>m->nv)return 0;}
   q+=psize;}
  if(n==index)return m->np>0;
  if(q+32<=size&&!memcmp(b+q,"BBD3",4))q+=le32(b+q+4)>=8&&le32(b+q+4)<=size-q?le32(b+q+4):32;
  p=q;
 }
 return 0;
}
static void rm_vertex(const RmModel*m,unsigned i,double v[3]){const unsigned char*p=m->verts+(size_t)i*8;v[0]=(short)le16(p);v[1]=(short)le16(p+2);v[2]=(short)le16(p+4);}

/* DMD3 animation (race overlay: setup 0x8005E1D8, decoder 0x8005E5A4, bit reader 0x8005E558). */
static int rm_decode(const unsigned char*b,unsigned size,short out[8][71],int*nframes){
 if(size<0x18||memcmp(b,"DMD3",4))return 0;unsigned flags=b[0xD],bones=b[0xF],nfr=le16(b+0x10),total=le32(b+4);
 if(bones!=17||!nfr||total>size||total<0x18)return 0;if(nfr>8)nfr=8;
 unsigned full=le16(b+0x10);   /* tables hold every frame */
 *nframes=(int)nfr;memset(out,0,sizeof(short)*8*71);unsigned a=0x18;int left=(int)total-0x18;
 if(flags&1){for(int c=0;c<3;c++){if(a+2*full>total)return 0;for(unsigned f=0;f<nfr;f++)out[f][c]=(short)le16(b+a+2*f);a+=2*full;left-=2*(int)full;}}
 if(flags&2){
  static const unsigned char escape[8]={0,1,1,3,8,16,32,64};
  for(int c=3;left>=3&&c<71;c++){
   if(a+4>total)return 0;int L=(short)le16(b+a);unsigned w1=le16(b+a+2);int v=w1&0x7FFF;int acc=v&0x4000?v-0x8000:v;
   if(L<1||a+(unsigned)L*2+2>total)return 0;
   if(!(w1&0x8000)){for(unsigned f=0;f<nfr;f++)out[f][c]=(short)(acc<<6);}
   else{
    if(L<3)return 0;unsigned scale=le16(b+a+4)&0x7FFF,nbits=(le16(b+a+6)&0x7FFF)>>8;if(!nbits||nbits>7)return 0;
    unsigned ptr=a+6,bit=8,end=a+(unsigned)L*2+2;int run=0,last=0;
    #define RM_READ(dst) do{unsigned hw0=ptr+2<=end?le16(b+ptr):0,hw1=ptr+4<=end?le16(b+ptr+2):0;uint32_t x=((hw0<<16|hw1)<<bit);dst=(int)((x>>(32-nbits))&0xFF);unsigned t=bit+nbits;ptr+=(t>>3)&0x1E;bit=t&0xF;}while(0)
    out[0][c]=(short)((acc*(int)scale)>>9);
    for(unsigned f=1;f<nfr;f++){int code;
     if(run>0){code=last;run--;}
     else{RM_READ(code);if(code==escape[nbits]){int x=0;run=0;for(int guard=0;guard<64;guard++){RM_READ(x);if(x==(1<<nbits)-1){run+=x;continue;}break;}run+=4+x;RM_READ(code);last=code;run--;}}
     int s=code&0x80?code-256:code;if((s>>(nbits-1))&1)s-=1<<nbits;acc+=s<<4;out[f][c]=(short)((acc*(int)scale)>>9);}
    #undef RM_READ
   }
   a+=(unsigned)L*2+2;left-=(L+1)*2;}
 }
 return 1;
}
/* The overlay's part tables: links (kind != 3) of a model type, in drawing
 * order. A type's entries run to the next type's first one; the last type (3)
 * gives its own count. */
static int rm_links(const unsigned char*t,int type,RmLink*out,int cap){
 const unsigned char*rec=t+0xC4+3*type;unsigned first=rec[0],count=type<3?(unsigned)rec[3]-first:rec[1];int n=0;
 if(first+count>48)return 0;
 for(unsigned i=first;i<first+count&&n<cap;i++){uint32_t w=le32(t+4*i);if((w&3)==3)continue;
  out[n++]=(RmLink){(unsigned char)(w>>18&31),(unsigned char)(w>>13&31),(unsigned char)(w>>10&7),(unsigned char)(w>>23&31)};}
 return n;
}
static void rm_load(Disc*d,Entry root){
 Entry e;free(rm.geo);free(rm.anim);free(rm.tables);memset(&rm,0,sizeof rm);
 if(!find_file(d,root,"/DATA/BBLEVEL1.GEO",&e)||e.size>1024*1024||!(rm.geo=read_entry(d,e)))return;rm.geo_size=e.size;
 if(!rm_model(rm.geo,e.size,3,rm.rider)||!rm_model(rm.geo,e.size,4,rm.rider+1)||!rm_model(rm.geo,e.size,0,&rm.bike))return;
 if(rm.rider[0].np!=17||rm.rider[1].np!=17||rm.bike.np>6)return;
 if(!find_file(d,root,"/DATA/ANIMTBL1.PSX",&e)||e.size>1024*1024||!(rm.anim=read_entry(d,e)))return;
 {unsigned p=0;for(int k=0;k<11&&p+8<=e.size;k++){if(memcmp(rm.anim+p,"DMD3",4))return;unsigned z=le32(rm.anim+p+4);if(z<0x18||z>e.size-p)return;p+=z;}
  if(p+8>e.size||!rm_decode(rm.anim+p,e.size-p,rm.frames,&rm.nframes))return;}
 if(!find_file(d,root,"/RASHCDG.BIN",&e)||e.size<0x71300||e.size>1024*1024)return;
 {unsigned char*o=read_entry(d,e);if(!o)return;rm.tables=malloc(0x100);if(rm.tables)memcpy(rm.tables,o+(0x800CC790-0x8005B5E8),0x100);free(o);if(!rm.tables)return;}
 if(rm_links(rm.tables,0,rm.skel,16)!=16)return;
 for(int i=0;i<16;i++)if(rm.skel[i].part>=17||rm.skel[i].parent>=17||rm.skel[i].bone>=17)return;
 RmLink bl[8];int n=rm_links(rm.tables,3,bl,8);rm.seat_joint=-1;
 for(int i=0;i<n;i++){if(bl[i].parent>=rm.bike.np)return;if(bl[i].part<rm.bike.np)rm.bikeskel[rm.nbike++]=bl[i];else if(bl[i].parent==0)rm.seat_joint=bl[i].joint;}
 if(rm.seat_joint<0)return;
 /* The bike's texture: the first bike LECT (kind 2) of the TEX file, for bike 0. */
 for(unsigned p=0x3014;p+568+128*60<=look_size;p++)if(!memcmp(look_tex+p,"LECT",4)&&look_tex[p+12]==2&&le32(look_tex+p+4)==0x2038){rm.bike_tex=look_tex[p+16];break;}
 rm.ready=rm.bike_tex!=0;
}
static void rm_free(void){free(rm.geo);free(rm.anim);free(rm.tables);memset(&rm,0,sizeof rm);}
static const unsigned char*rm_lect(unsigned id,const unsigned char**pal){
 for(unsigned p=0x3014;p+568+128*60<=look_size;p++)if(!memcmp(look_tex+p,"LECT",4)&&le32(look_tex+p+4)==0x2038&&look_tex[p+16]==id){if(pal)*pal=look_tex+p+0x38;return look_tex+p+568;}
 return NULL;
}

typedef struct {double m[3][3],t[3];} RmXf;
static void rm_quat(const short*q,double m[3][3]){double x=q[0],y=q[1],z=q[2],w=q[3],n=x*x+y*y+z*z+w*w,s=n>0?2/n:0;
 m[0][0]=1-s*(y*y+z*z);m[0][1]=s*(x*y-z*w);m[0][2]=s*(x*z+y*w);m[1][0]=s*(x*y+z*w);m[1][1]=1-s*(x*x+z*z);m[1][2]=s*(y*z-x*w);m[2][0]=s*(x*z-y*w);m[2][1]=s*(y*z+x*w);m[2][2]=1-s*(x*x+y*y);}
static void rm_mul(const double a[3][3],const double b[3][3],double o[3][3]){double r[3][3];for(int i=0;i<3;i++)for(int j=0;j<3;j++)r[i][j]=a[i][0]*b[0][j]+a[i][1]*b[1][j]+a[i][2]*b[2][j];memcpy(o,r,sizeof r);}
static void rm_apply(const RmXf*x,const double v[3],double o[3]){for(int i=0;i<3;i++)o[i]=x->m[i][0]*v[0]+x->m[i][1]*v[1]+x->m[i][2]*v[2]+x->t[i];}

/* Bike paint (the rule rider_enhancements.cpp applies in game): the main body
 * ramp of the palette, chosen by how much of the bike texture it covers, takes
 * the preset hue with its saturation and brightness kept. */
typedef struct {int r,g,b,v,mn;} RmShade;
static RmShade rm_shade(unsigned c){RmShade s={(int)(c&31),(int)(c>>5&31),(int)(c>>10&31),0,0};s.v=s.r>s.g?(s.r>s.b?s.r:s.b):(s.g>s.b?s.g:s.b);s.mn=s.r<s.g?(s.r<s.b?s.r:s.b):(s.g<s.b?s.g:s.b);return s;}
static int rm_coloured(RmShade s){return s.v-s.mn>=9&&(s.v-s.mn)*100>=s.v*35;}
static double rm_hue(RmShade s){double d=s.v-s.mn,h=s.v==s.r?(s.g-s.b)/d:s.v==s.g?(s.b-s.r)/d+2:(s.r-s.g)/d+4;h*=60;return h<0?h+360:h;}
static double rm_gap(double a,double b){double d=fabs(a-b);return d>180?360-d:d;}
static void rm_paint(uint16_t*pal,unsigned m,const unsigned char*tex,int hue){
 unsigned use[256]={0};for(unsigned i=0;i<128*60;i++)use[tex[i]]++;
 struct {unsigned a,b,use,col;double sx,sy;} r[128];unsigned n=0;
 for(unsigned i=0;i<m;i++){RmShade s=rm_shade(pal[i]);int start=!n;
  if(n){RmShade p=rm_shade(pal[i-1]);start=s.v>p.v+1||((pal[i]&0x7fff)==0)!=((pal[i-1]&0x7fff)==0)||(rm_coloured(s)&&rm_coloured(p)&&rm_gap(rm_hue(s),rm_hue(p))>45);}
  if(start){r[n].a=r[n].b=i;r[n].use=r[n].col=0;r[n].sx=r[n].sy=0;n++;}
  r[n-1].b=i;r[n-1].use+=use[i];if(rm_coloured(s)){double h=rm_hue(s)*3.14159265358979/180;r[n-1].col+=use[i];r[n-1].sx+=cos(h)*use[i];r[n-1].sy+=sin(h)*use[i];}}
 int best=-1;for(unsigned i=0;i<n;i++)if(r[i].b-r[i].a+1>=12&&r[i].col&&r[i].col*2>=r[i].use&&(best<0||r[i].col>r[best].col))best=(int)i;
 if(best<0)return;double h0=atan2(r[best].sy,r[best].sx)*180/3.14159265358979;if(h0<0)h0+=360;
 for(unsigned i=0;i<n;i++){double h=atan2(r[i].sy,r[i].sx)*180/3.14159265358979;if(h<0)h+=360;
  if(!((int)i==best||(r[i].b-r[i].a+1>=10&&r[i].col*2>=r[i].use&&r[i].col*10>=r[best].col*3&&rm_gap(h,h0)<=25)))continue;
  for(unsigned j=r[i].a;j<=r[i].b;j++){unsigned c=pal[j];RmShade s=rm_shade(c);if(s.v<=2||s.v==s.mn)continue;int v=s.v,mn=s.mn,f=hue%60,dd=((v-mn)*f+30)/60,t=mn+dd,q=v-dd,R,G,B;
   switch(hue/60%6){case 0:R=v;G=t;B=mn;break;case 1:R=q;G=v;B=mn;break;case 2:R=mn;G=v;B=t;break;case 3:R=mn;G=q;B=v;break;case 4:R=t;G=mn;B=v;break;default:R=v;G=mn;B=q;}
   pal[j]=(uint16_t)((c&0x8000)|R|G<<5|B<<10);}}
}

/* Drawing. Screen y down like the game's; light from the camera's upper left. */
typedef struct {double x,y,z,iz,u,v,l;} RmVtx;
typedef struct {const unsigned char*tex;uint32_t pal[256];int wheel;} RmMat;
static uint32_t rm_rgb(unsigned c){return 0xff000000u|((c&31)*255/31)<<16|((c>>5&31)*255/31)<<8|(c>>10&31)*255/31;}
static void rm_tri(const RmVtx*a,const RmVtx*b,const RmVtx*c,const RmMat*mt,uint32_t*px,float*zb,int W,int H){
 double den=(b->y-c->y)*(a->x-c->x)+(c->x-b->x)*(a->y-c->y);if(fabs(den)<1e-9)return;
 int x0=(int)floor(fmin(a->x,fmin(b->x,c->x))),x1=(int)ceil(fmax(a->x,fmax(b->x,c->x))),y0=(int)floor(fmin(a->y,fmin(b->y,c->y))),y1=(int)ceil(fmax(a->y,fmax(b->y,c->y)));
 if(x0<0)x0=0;if(y0<0)y0=0;if(x1>W-1)x1=W-1;if(y1>H-1)y1=H-1;
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){double px0=x+.5,py0=y+.5;
  double l1=((b->y-c->y)*(px0-c->x)+(c->x-b->x)*(py0-c->y))/den,l2=((c->y-a->y)*(px0-c->x)+(a->x-c->x)*(py0-c->y))/den,l3=1-l1-l2;
  if(l1<0||l2<0||l3<0)continue;double iz=l1*a->iz+l2*b->iz+l3*c->iz;if(iz<=0)continue;float z=(float)(1/iz);size_t k=(size_t)y*W+x;if(z>=zb[k])continue;
  double u=(l1*a->u*a->iz+l2*b->u*b->iz+l3*c->u*c->iz)/iz,v=(l1*a->v*a->iz+l2*b->v*b->iz+l3*c->v*c->iz)/iz,lt=l1*a->l+l2*b->l+l3*c->l;
  uint32_t col;
  if(mt->wheel){   /* the shared wheel sheet is not decoded yet: a tyre with a softly lit hub */
   double du=u-27,dv=v-26,r=u<56?sqrt(du*du+dv*dv)/26:1,k=r<.35?1:r>.6?0:(.6-r)/.25;
   col=0xff000000u|(uint32_t)(34+k*58)<<16|(uint32_t)(34+k*62)<<8|(uint32_t)(38+k*70);}
  else{int ui=(int)u,vi=(int)v;if(ui<0)ui=0;if(ui>127)ui=127;if(vi<0)vi=0;if(vi>59)vi=59;unsigned i=mt->tex[vi*128+ui];col=mt->pal[i];if(!col)continue;}
  int R=(int)((col>>16&255)*lt),G=(int)((col>>8&255)*lt),B=(int)((col&255)*lt);if(R>255)R=255;if(G>255)G=255;if(B>255)B=255;
  zb[k]=z;px[k]=0xff000000u|(uint32_t)R<<16|(uint32_t)G<<8|(uint32_t)B;}
}
/* The rider on the bike, yaw degrees around the vertical. ss = supersampling (1-3). */
static int rm_render(int body,unsigned set_id,int slot,int paint_hue,double yaw,uint32_t*out,unsigned w,unsigned h,int ss){
 if(!rm.ready||!look_tex||!out||!w||!h||w>2048||h>2048)return 0;if(ss<1)ss=1;if(ss>3)ss=3;
 const RmModel*rider=rm.rider+(body!=0),*bike=&rm.bike;
 /* Pose (frame 0 of the riding loop). */
 RmXf rx[17],bx[RM_PARTS];const short*fr=rm.frames[0];
 rm_quat(fr+3,rx[0].m);for(int i=0;i<3;i++)rx[0].t[i]=fr[i];
 for(int i=0;i<16;i++){const RmLink*l=rm.skel+i;double q[3][3],v[3];rm_quat(fr+3+4*l->bone,q);rm_mul(rx[l->parent].m,q,rx[l->part].m);
  rm_vertex(rider,rider->part[l->parent].first+l->joint,v);for(int k=0;k<3;k++)rx[l->part].t[k]=rx[l->parent].m[k][0]*v[0]+rx[l->parent].m[k][1]*v[1]+rx[l->parent].m[k][2]*v[2]+rx[l->parent].t[k];}
 for(unsigned p=0;p<bike->np;p++){memset(bx+p,0,sizeof bx[p]);for(int i=0;i<3;i++)bx[p].m[i][i]=1;}
 for(int i=0;i<rm.nbike;i++){double v[3];rm_vertex(bike,bike->part[rm.bikeskel[i].parent].first+rm.bikeskel[i].joint,v);for(int k=0;k<3;k++)bx[rm.bikeskel[i].part].t[k]=bx[rm.bikeskel[i].parent].t[k]+v[k];}
 double seat[3];rm_vertex(bike,bike->part[0].first+(unsigned)rm.seat_joint,seat);for(int p=0;p<17;p++)for(int k=0;k<3;k++)rx[p].t[k]+=seat[k];
 /* World positions and normals. */
 unsigned nv=rider->nv+bike->nv;double(*P)[3]=malloc(sizeof(double[3])*nv),(*N)[3]=malloc(sizeof(double[3])*nv);
 int W=(int)w*ss,H=(int)h*ss;uint32_t*px=malloc((size_t)W*H*4);float*zb=malloc((size_t)W*H*4);RmVtx*sv=malloc(sizeof(RmVtx)*nv);
 if(!P||!N||!px||!zb||!sv){free(P);free(N);free(px);free(zb);free(sv);return 0;}
 for(int m=0;m<2;m++){const RmModel*mo=m?bike:rider;const RmXf*xf=m?bx:rx;unsigned base=m?rider->nv:0;
  for(unsigned g=0;g<mo->np;g++)for(unsigned i=mo->part[g].first;i<mo->part[g].first+mo->part[g].count;i++){double v[3];rm_vertex(mo,i,v);rm_apply(xf+g,v,P[base+i]);
   double n[3]={0,-4096,0};if(mo->vnorm&&mo->normals){unsigned k=le16(mo->vnorm+2*i);if(k<mo->nn){const unsigned char*q=mo->normals+8*k;n[0]=(short)le16(q);n[1]=(short)le16(q+2);n[2]=(short)le16(q+4);}}
   for(int a=0;a<3;a++)N[base+i][a]=(xf[g].m[a][0]*n[0]+xf[g].m[a][1]*n[1]+xf[g].m[a][2]*n[2])/4096;}}
 /* Camera: around the models' centre, slightly from above. */
 double lo[3]={1e9,1e9,1e9},hi[3]={-1e9,-1e9,-1e9};for(unsigned i=0;i<nv;i++)for(int a=0;a<3;a++){if(P[i][a]<lo[a])lo[a]=P[i][a];if(P[i][a]>hi[a])hi[a]=P[i][a];}
 double c[3],rad=0;for(int a=0;a<3;a++)c[a]=(lo[a]+hi[a])/2;for(unsigned i=0;i<nv;i++){double dx=P[i][0]-c[0],dy=P[i][1]-c[1],dz=P[i][2]-c[2],r=sqrt(dx*dx+dy*dy+dz*dz);if(r>rad)rad=r;}
 const double cy=cos(yaw*3.14159265358979/180),sy=sin(yaw*3.14159265358979/180),cp=cos(.2),sp=sin(.2),dist=rad*3.4;
 const double L[3]={-.45,-.7,-.55};
 /* Camera space, then a scale and offset that fit the models' outline to the frame. */
 double mnx=1e9,mxx=-1e9,mny=1e9,mxy=-1e9;
 for(unsigned i=0;i<nv;i++){double x=P[i][0]-c[0],y=P[i][1]-c[1],z=P[i][2]-c[2],X=x*cy-z*sy,Z=x*sy+z*cy,Y=y*cp-Z*sp;Z=y*sp+Z*cp+dist;
  double nx=N[i][0]*cy-N[i][2]*sy,nz=N[i][0]*sy+N[i][2]*cy,ny=N[i][1]*cp-nz*sp;nz=N[i][1]*sp+nz*cp;double d=nx*L[0]+ny*L[1]+nz*L[2];if(d<0)d=-d*0.35;
  sv[i].x=X/Z;sv[i].y=Y/Z;sv[i].z=Z;sv[i].iz=1/Z;sv[i].l=.5+.75*d;
  if(sv[i].x<mnx)mnx=sv[i].x;if(sv[i].x>mxx)mxx=sv[i].x;if(sv[i].y<mny)mny=sv[i].y;if(sv[i].y>mxy)mxy=sv[i].y;}
 const double f=fmin(W*.90/(mxx-mnx+1e-9),H*.84/(mxy-mny+1e-9)),ox=W/2.0-(mnx+mxx)/2*f,oy=H*.47-(mny+mxy)/2*f;
 for(unsigned i=0;i<nv;i++){sv[i].x=ox+sv[i].x*f;sv[i].y=oy+sv[i].y*f;}
 /* Background: a soft vertical gradient, then a shadow ellipse under the lowest point. */
 for(int y=0;y<H;y++){double t=(double)y/H;uint32_t r=(uint32_t)(22+18*t),g=(uint32_t)(26+20*t),b=(uint32_t)(44+30*t);for(int x=0;x<W;x++){px[(size_t)y*W+x]=0xff000000u|r<<16|g<<8|b;zb[(size_t)y*W+x]=1e30f;}}
 {double sx=W/2.0,syy=oy+mxy*f-H*.02,ax=(mxx-mnx)*f*.52,ay=H*.07;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){double dx=(x-sx)/ax,dy=(y-syy)/ay,q=dx*dx+dy*dy;if(q<1){uint32_t*p=px+(size_t)y*W+x;double k=.55*(1-q);uint32_t r=(uint32_t)((*p>>16&255)*(1-k)),g=(uint32_t)((*p>>8&255)*(1-k)),b=(uint32_t)((*p&255)*(1-k));*p=0xff000000u|r<<16|g<<8|b;}}}
 /* Materials. */
 RmMat rmat={0},bmat={0},wmat={0};const unsigned char*bpal=NULL;rmat.tex=rm_lect(set_id,NULL);bmat.tex=rm_lect(rm.bike_tex,&bpal);wmat.wheel=1;
 if(!rmat.tex||!bmat.tex){free(P);free(N);free(px);free(zb);free(sv);return 0;}
 {const unsigned char*pal=look_tex+0x14+((15-slot/3)*384+(slot%3)*128)*2;for(int i=0;i<256;i++){unsigned v=i<128?(unsigned)(pal[i*2]|pal[i*2+1]<<8):0;rmat.pal[i]=(v&0x7fff)?rm_rgb(v):0;}}
 {uint16_t pal[256];for(int i=0;i<256;i++)pal[i]=(uint16_t)(bpal[i*2]|bpal[i*2+1]<<8);if(paint_hue>=0)rm_paint(pal,128,bmat.tex,paint_hue);for(int i=0;i<256;i++)bmat.pal[i]=(pal[i]&0x7fff)?rm_rgb(pal[i]):0;}
 /* Faces. */
 for(int m=0;m<2;m++){const RmModel*mo=m?bike:rider;unsigned base=m?rider->nv:0;
  for(unsigned g=0;g<mo->np;g++)for(unsigned k=0;k<mo->part[g].nf;k++){const unsigned char*fc=mo->part[g].faces+20*k;unsigned A=le16(fc+2),corners=(A>>11&7)==3?3:4;
   unsigned idx[4]={le16(fc+12),le16(fc+14),le16(fc+16),le16(fc+18)};const unsigned char uv[4][2]={{fc[0],fc[1]},{fc[4],fc[5]},{fc[8],fc[9]},{fc[10],fc[11]}};
   int ok=1;for(unsigned q=0;q<corners;q++)if(idx[q]>=mo->nv)ok=0;if(!ok)continue;
   const RmMat*mt=m?((A&0x80)?&wmat:&bmat):&rmat;RmVtx t[4];
   for(unsigned q=0;q<corners;q++){t[q]=sv[base+idx[q]];t[q].u=uv[q][0]+.5;t[q].v=uv[q][1]+.5;}
   rm_tri(t,t+1,t+2,mt,px,zb,W,H);if(corners==4)rm_tri(t,t+2,t+3,mt,px,zb,W,H);}}
 /* Down-sample. */
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){unsigned r=0,g=0,b=0;for(int j=0;j<ss;j++)for(int i=0;i<ss;i++){uint32_t p=px[(size_t)(y*ss+j)*W+x*ss+i];r+=p>>16&255;g+=p>>8&255;b+=p&255;}
  unsigned n=(unsigned)(ss*ss);out[(size_t)y*w+x]=0xff000000u|(r/n)<<16|(g/n)<<8|(b/n);}
 free(P);free(N);free(px);free(zb);free(sv);return 1;
}
