/* Road Rash Jailbreak launcher motion. LGPL-2.1-or-later. Original author code.
 *
 * Animation is composed from frames already in memory, not re-rendered: every
 * state change still renders one full frame (launcher_skin.h render()), and the
 * movement from the frame on screen to the new one is built from cached pixels,
 * so a tick costs a copy of the changed region and nothing runs once it settles.
 *
 *  - pages: a slanted speed wipe with a soft gold edge (back wipes the other way)
 *  - rows: the highlight glides to the new row; rows recolour as it passes
 *  - anything else that changed (help card, values, footer, notices): a short
 *    crossfade of that region
 *  - entrance: header, then rows sliding in one by one, then the card; the
 *    setup screen's logo, button and note rise in
 *  - every few seconds while the launcher has focus: a glint across the
 *    selected bar (or across our logo and the button before setup)
 *  - progress: the bar eases to each new value and only the bar is redrawn
 *
 * Nothing animates when Windows "Animation effects" is off, with
 * [launcher] animation=0 in launcher.ini, or while the game runs; glints also
 * wait for the launcher to be the foreground window. */
#ifndef SPI_GETCLIENTAREAANIMATION
#define SPI_GETCLIENTAREAANIMATION 0x1042
#endif
#define MO_FADES 8
#define MO_WIPE_MS 300.0
typedef struct MoRect {int x,y,w,h;} MoRect;
typedef struct MoFade {MoRect r;double t0,dur;} MoFade;
typedef struct MoItem {MoRect r;double start,dur,dx,dy;} MoItem;   /* entrance: a region sliding into place */
static struct {
    HBITMAP frame_bmp,shown_bmp;uint32_t*frame,*shown,*old;   /* target render, on screen, screen when the last change arrived */
    int w,h,valid;
    uint32_t*l0,*l1;MoRect lr;unsigned lkey;int lvalid;         /* the bar column with every row inactive / active */
    uint32_t*toast_base;MoRect toast_r;int toast_ok;            /* frame pixels under the notice bar */
    int page,selected,list_first,installed,toast;               /* what the target shows */
    int wipe,wipe_dir;double wipe_t0;
    int band;double band_t0,band_dur,band_from,band_to;
    MoFade fade[MO_FADES];int fades;
    int intro,intro_pending;double intro_t0;MoItem items[24];int item_count;double intro_end;
    int glint,glint_logo;double glint_t0;MoRect glint_r;
    int timer;double clock;int preview;
} mo={.intro_pending=1,.clock=-1};
static double mo_now(void){if(mo.clock>=0)return mo.clock;static LARGE_INTEGER f;LARGE_INTEGER c;if(!f.QuadPart)QueryPerformanceFrequency(&f);QueryPerformanceCounter(&c);return c.QuadPart*1000.0/f.QuadPart;}
static double mo_clamp01(double t){return t<0?0:t>1?1:t;}
static double ease_out(double t){t=mo_clamp01(t);double u=1-t;return 1-u*u*u;}
static double ease_inout(double t){t=mo_clamp01(t);return t<.5?4*t*t*t:1-pow(-2*t+2,3)/2;}
static int mo_game_running(void){DWORD code;return game_process&&GetExitCodeProcess(game_process,&code)&&code==STILL_ACTIVE;}
static int mo_enabled(void){
    if(mo.preview)return 1;
    static int ini_on=-1;if(ini_on<0)ini_on=GetPrivateProfileIntW(L"launcher",L"animation",1,ini)!=0;if(!ini_on)return 0;
    BOOL on=TRUE;if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&on,0)&&!on)return 0;   /* Settings > Accessibility > Animation effects */
    return !mo_game_running();
}
/* ---- Rectangles (device pixels, from the skin's layout) ------------------- */
static MoRect mo_clip(MoRect r){const int W=ui.s.w,H=ui.s.h;if(r.x<0){r.w+=r.x;r.x=0;}if(r.y<0){r.h+=r.y;r.y=0;}if(r.x+r.w>W)r.w=W-r.x;if(r.y+r.h>H)r.h=H-r.y;if(r.w<0)r.w=0;if(r.h<0)r.h=0;return r;}
static MoRect mo_rect(double x,double y,double w,double h){MoRect r={P(x),P(y),0,0};r.w=P(x+w)-r.x;r.h=P(y+h)-r.y;return mo_clip(r);}
static MoRect mo_union(MoRect a,MoRect b){if(!a.w||!a.h)return b;if(!b.w||!b.h)return a;int x0=a.x<b.x?a.x:b.x,y0=a.y<b.y?a.y:b.y,x1=a.x+a.w>b.x+b.w?a.x+a.w:b.x+b.w,y1=a.y+a.h>b.y+b.h?a.y+a.h:b.y+b.h;return (MoRect){x0,y0,x1-x0,y1-y0};}
enum {MR_HEADER,MR_LIST,MR_CARD,MR_TOAST,MR_FOOTER,MR_SETUP,MR_COUNT};
static MoRect mo_region(int i){
    double top=ui.by+list_top(),fy=footer_y();
    switch(i){
    case MR_HEADER:return mo_rect(0,0,ui.lw,top-6);
    case MR_LIST:return mo_rect(ui.cx+44,top-6,556,fy-50-(top-6));
    case MR_CARD:return mo_rect(ui.cx+598,top-6,634,ui.by+150+456+12-(top-6));
    case MR_TOAST:return mo_rect(ui.cx+50,fy-50,1182,48);
    case MR_FOOTER:return mo_rect(0,fy,ui.lw,ui.lh-fy);
    default:return mo_rect(0,0,ui.lw,fy-50);   /* the setup screen above its notice bar */
    }
}
static double mo_row_y(int r){return ui.by+list_top()+(r-list_first)*list_pitch();}
static int mo_differs(const uint32_t*a,const uint32_t*b,MoRect r){for(int y=r.y;y<r.y+r.h;y++)if(memcmp(a+(size_t)y*mo.w+r.x,b+(size_t)y*mo.w+r.x,(size_t)r.w*4))return 1;return 0;}
static void mo_copy(uint32_t*dst,const uint32_t*src,MoRect r){for(int y=r.y;y<r.y+r.h;y++)memcpy(dst+(size_t)y*mo.w+r.x,src+(size_t)y*mo.w+r.x,(size_t)r.w*4);}
/* d = a..b by t (0..256 fixed point), per channel. */
static uint32_t mo_lerp(uint32_t a,uint32_t b,unsigned t){if(t>=256)return b;if(!t)return a;unsigned it=256-t;return (((a>>16&255)*it+(b>>16&255)*t)>>8)<<16|(((a>>8&255)*it+(b>>8&255)*t)>>8)<<8|((a&255)*it+(b&255)*t)>>8;}
static uint32_t mo_add(uint32_t p,uint32_t c,unsigned a){unsigned r=(p>>16&255)+((c>>16&255)*a>>8),g=(p>>8&255)+((c>>8&255)*a>>8),b=(p&255)+((c&255)*a>>8);return (r>255?255:r)<<16|(g>255?255:g)<<8|(b>255?255:b);}
/* ---- List layers: the same rows drawn all inactive and all active --------- */
static unsigned mo_list_key(void){
    unsigned h=2166136261u;
#define MO_MIX(v) (h=(h^(unsigned)(v))*16777619u)
    MO_MIX(page);MO_MIX(list_first);MO_MIX(mo.w);MO_MIX(mo.h);MO_MIX(art_generation);MO_MIX(online_ready);
    int n=row_count(),v=list_visible();
    for(int i=list_first;i<n&&i<list_first+v;i++){wchar_t l[160],val[160];int k=launcher_row(i,l,val,160);MO_MIX(k);MO_MIX(row_warn);
        for(const wchar_t*p=l;*p;p++)MO_MIX(*p);for(const wchar_t*p=val;*p;p++)MO_MIX(*p);
        int st,pos,safe;if(k==ROW_SLIDER&&row_slider(i,&st,&pos,&safe)){MO_MIX(st);MO_MIX(pos);MO_MIX(safe);}}
#undef MO_MIX
    return h;
}
static int mo_layers(void){
    unsigned key=mo_list_key();if(mo.lvalid&&key==mo.lkey)return 1;mo.lvalid=0;
    int n=row_count(),v=list_visible(),rows=n-list_first<v?n-list_first:v;if(rows<=0||!bg_px||bg_w!=mo.w||bg_h!=mo.h)return 0;
    MoRect r=mo_rect(ui.cx+54,ui.by+list_top()-2,LIST_W+10,(rows-1)*list_pitch()+list_height()+10);if(!r.w||!r.h)return 0;
    free(mo.l0);free(mo.l1);mo.l0=malloc((size_t)r.w*r.h*4);mo.l1=malloc((size_t)r.w*r.h*4);uint32_t*tmp=malloc((size_t)mo.w*mo.h*4);
    if(!mo.l0||!mo.l1||!tmp){free(tmp);return 0;}
    Surface saved=ui.s;int sa=arrow_row;double sy=arrow_y,sh=arrow_h,sl=arrow_lx,sr=arrow_rx;
    for(int m=0;m<2;m++){memcpy(tmp,bg_px,(size_t)mo.w*mo.h*4);ui.s=(Surface){tmp,mo.w,mo.h};list_force=m;draw_list();GdiFlush();
        uint32_t*dst=m?mo.l1:mo.l0;for(int y=0;y<r.h;y++)memcpy(dst+(size_t)y*r.w,tmp+(size_t)(r.y+y)*mo.w+r.x,(size_t)r.w*4);}
    list_force=-1;ui.s=saved;arrow_row=sa;arrow_y=sy;arrow_h=sh;arrow_lx=sl;arrow_rx=sr;free(tmp);
    mo.lr=r;mo.lkey=key;mo.lvalid=1;return 1;
}
/* ---- Effects -------------------------------------------------------------- */
static void mo_fade(MoRect r,double dur,double now){
    if(dur<=0||!r.w||!r.h)return;
    for(int i=0;i<mo.fades;i++)if(!memcmp(&mo.fade[i].r,&r,sizeof r)){mo.fade[i].t0=now;mo.fade[i].dur=dur;return;}
    if(mo.fades<MO_FADES)mo.fade[mo.fades++]=(MoFade){r,now,dur};
}
static double mo_band_now(double now){return mo.band_from+(mo.band_to-mo.band_from)*ease_out((now-mo.band_t0)/mo.band_dur);}
static void mo_item(MoRect r,double start,double dur,double dx,double dy){if(mo.item_count<24&&r.w&&r.h){mo.items[mo.item_count++]=(MoItem){r,start,dur,dx,dy};if(start+dur>mo.intro_end)mo.intro_end=start+dur;}}
static void mo_start_intro(double now){
    mo.intro=1;mo.intro_t0=now;mo.item_count=0;mo.intro_end=0;mo.wipe=mo.band=mo.fades=mo.glint=0;
    if(!installed){double b[4];find_rect(b);
        mo_item(mo_rect(ui.cx+280,ui.by+86,720,340),0,560,0,26);
        mo_item(mo_rect(b[0]-4,b[1]-4,b[2]+12,b[3]+12),200,420,0,16);
        mo_item(mo_rect(ui.cx+170,b[1]+b[3]+12,940,ui.lh-58-(b[1]+b[3]+12)),340,420,0,0);
        mo_item(mo_region(MR_TOAST),340,300,0,0);return;}
    mo_item(mo_region(MR_HEADER),0,320,0,0);
    int n=row_count(),v=list_visible(),rows=n-list_first<v?n-list_first:v;double top=ui.by+list_top(),last=top-6;
    for(int i=0;i<rows;i++){double y=top+i*list_pitch();mo_item(mo_rect(ui.cx+44,y-3,556,list_pitch()),70+45*i,330,-40,0);last=y-3+list_pitch();}
    MoRect lr=mo_region(MR_LIST);double below=(lr.y+lr.h)/ui.k-last;if(below>0)mo_item(mo_rect(ui.cx+44,last,556,below),70+45*rows,330,0,0);
    mo_item(mo_region(MR_CARD),150,420,0,18);
    mo_item(mo_region(MR_TOAST),230,300,0,0);mo_item(mo_region(MR_FOOTER),230,300,0,0);
}
static void mo_start_glint(double now){
    if(!installed){mo.glint_r=mo_rect(ui.cx+280,ui.by+86,720,340);mo.glint_logo=1;}
    else{int n=row_count();if(selected<list_first||selected>=n||selected>=list_first+list_visible())return;
        mo.glint_r=mo_rect(ui.cx+56,mo_row_y(selected),LIST_W,list_height());mo.glint_logo=0;}
    if(mo.glint_r.w&&mo.glint_r.h){mo.glint=1;mo.glint_t0=now;}
}
/* ---- Composition ---------------------------------------------------------- */
static void mo_blend_rect(MoRect r,const uint32_t*src,int sdx,int sdy,unsigned t){   /* shown = shown..src(shifted) by t/256 */
    for(int y=r.y;y<r.y+r.h;y++){int sy=y-sdy;uint32_t*out=mo.shown+(size_t)y*mo.w;
        if(sy<0||sy>=mo.h)continue;const uint32_t*in=src+(size_t)sy*mo.w;
        for(int x=r.x;x<r.x+r.w;x++){int sx=x-sdx;if(sx<0||sx>=mo.w)continue;out[x]=mo_lerp(out[x],in[sx],t);}}
}
static int mo_compose_intro(double now){
    double t=now-mo.intro_t0;size_t px=(size_t)mo.w*mo.h;
    if(t>=mo.intro_end||!bg_px||bg_w!=mo.w||bg_h!=mo.h){mo.intro=0;memcpy(mo.shown,mo.frame,px*4);return 0;}
    memcpy(mo.shown,bg_px,px*4);
    for(int i=0;i<mo.item_count;i++){MoItem*it=mo.items+i;double p=ease_out((t-it->start)/it->dur);if(p<=0)continue;
        int dx=P(it->dx*(1-p)),dy=P(it->dy*(1-p));MoRect r=it->r;r.x+=dx;r.y+=dy;r=mo_clip(r);
        /* a moving piece: clip its source to its own rectangle so neighbours never smear in */
        MoRect s=it->r;if(r.x-dx<s.x){r.w-=s.x-(r.x-dx);r.x=s.x+dx;}if(r.y-dy<s.y){r.h-=s.y-(r.y-dy);r.y=s.y+dy;}
        if(r.x+r.w-dx>s.x+s.w)r.w=s.x+s.w+dx-r.x;if(r.y+r.h-dy>s.y+s.h)r.h=s.y+s.h+dy-r.y;
        if(r.w>0&&r.h>0)mo_blend_rect(r,mo.frame,dx,dy,(unsigned)(p*256));}
    return 1;
}
static void mo_compose_wipe(double now){
    const double t=ease_inout((now-mo.wipe_t0)/MO_WIPE_MS),F=fmax(8,46*ui.k),G=fmax(6,30*ui.k),slant=.22*mo.h,e=-F-G+(mo.w+2*F+2*G+slant)*t;
    for(int y=0;y<mo.h;y++){const double ey=e-slant*(double)y/mo.h;uint32_t*out=mo.shown+(size_t)y*mo.w;const uint32_t*nw=mo.frame+(size_t)y*mo.w,*od=mo.old+(size_t)y*mo.w;
        for(int x=0;x<mo.w;x++){const double d=(mo.wipe_dir>0?x:mo.w-1-x)-ey;uint32_t c;
            if(d<=-F)c=nw[x];else if(d>=0)c=od[x];else c=mo_lerp(nw[x],od[x],(unsigned)((d+F)/F*256));
            const double g=fabs(d+F*.5)/G;if(g<1){const double k=(1-g)*(1-g);c=mo_add(c,RGBX(255,214,120),(unsigned)(110*k));}
            out[x]=c;}}
}
static void mo_compose_glint(double now,int*active){
    const double dur=mo.glint_logo?950:720,t=(now-mo.glint_t0)/dur;MoRect r=mo.glint_r;
    if(t>=1){mo.glint=0;mo_copy(mo.shown,mo.frame,r);return;}
    *active=1;const double bw=fmax(r.h*.55,18*ui.k),slant=r.h*.7,c=-bw-slant+(r.w+2*bw+slant)*ease_inout(t),peak=mo.glint_logo?120:88;
    for(int y=r.y;y<r.y+r.h;y++){const double cy=c+slant*(1-(double)(y-r.y)/r.h);uint32_t*out=mo.shown+(size_t)y*mo.w;const uint32_t*fr=mo.frame+(size_t)y*mo.w,*bg=bg_px&&bg_w==mo.w?bg_px+(size_t)y*mo.w:NULL;
        for(int x=r.x;x<r.x+r.w;x++){const double d=fabs(x-r.x-cy);out[x]=fr[x];if(d>=bw)continue;double k=(1-d/bw);k*=k*peak;
            if(mo.glint_logo){if(!bg)continue;uint32_t a=fr[x],b=bg[x];int cov=abs((int)(a>>16&255)-(int)(b>>16&255))+abs((int)(a>>8&255)-(int)(b>>8&255))+abs((int)(a&255)-(int)(b&255));k*=cov>=120?1:cov/120.0;}
            out[x]=mo_add(out[x],RGBX(255,246,226),(unsigned)k);}}
}
/* Builds the screen for "now"; returns 1 while anything still moves. */
static int mo_compose(double now,MoRect*dirty){
    MoRect full={0,0,mo.w,mo.h};*dirty=(MoRect){0,0,0,0};if(!mo.valid)return 0;int active=0;
    if(mo.intro){*dirty=full;return mo_compose_intro(now);}
    if(mo.wipe){*dirty=full;if(now-mo.wipe_t0>=MO_WIPE_MS){mo.wipe=0;memcpy(mo.shown,mo.frame,(size_t)mo.w*mo.h*4);return 0;}mo_compose_wipe(now);return 1;}
    /* 1. every animated region starts from the target */
    if(mo.band)*dirty=mo_union(*dirty,mo.lr);
    for(int i=0;i<mo.fades;i++)*dirty=mo_union(*dirty,mo.fade[i].r);
    if(dirty->w)mo_copy(mo.shown,mo.frame,*dirty);
    /* 2. the gliding highlight: active rows inside the band, inactive outside */
    if(mo.band){double t=(now-mo.band_t0)/mo.band_dur;
        if(t>=1||!mo.lvalid)mo.band=0;
        else{active=1;MoRect r=mo.lr;int by0=P(mo_band_now(now)),by1=by0+P(list_height()),bx0=P(ui.cx+56),bx1=P(ui.cx+56+LIST_W);
            for(int y=0;y<r.h;y++){int Y=r.y+y;uint32_t*out=mo.shown+(size_t)Y*mo.w+r.x;const uint32_t*a=mo.l0+(size_t)y*r.w,*b=mo.l1+(size_t)y*r.w;
                if(Y<by0||Y>=by1){memcpy(out,a,(size_t)r.w*4);continue;}
                for(int x=0;x<r.w;x++){int X=r.x+x;out[x]=X>=bx0&&X<bx1?b[x]:a[x];}}}}
    /* 3. crossfades from what was on screen */
    for(int i=0;i<mo.fades;){MoFade*f=mo.fade+i;double t=(now-f->t0)/f->dur;
        if(t>=1){*f=mo.fade[--mo.fades];continue;}
        active=1;MoRect r=f->r;unsigned k=(unsigned)(ease_out(t)*256);
        for(int y=r.y;y<r.y+r.h;y++){uint32_t*out=mo.shown+(size_t)y*mo.w;const uint32_t*od=mo.old+(size_t)y*mo.w;for(int x=r.x;x<r.x+r.w;x++)out[x]=mo_lerp(od[x],out[x],k);}
        i++;}
    /* 4. light glint */
    if(mo.glint){*dirty=mo_union(*dirty,mo.glint_r);mo_compose_glint(now,&active);}
    return active;
}
/* ---- A new full render arrived (WM_PAINT or the preview) ------------------- */
static void mo_drop_bitmaps(void){if(mo.shown_bmp)DeleteObject(mo.shown_bmp);mo.shown_bmp=NULL;mo.shown=NULL;free(mo.old);mo.old=NULL;mo.lvalid=0;mo.toast_ok=0;}
static int mo_frame_arrived(HBITMAP bmp,uint32_t*px,int w,int h){
    const double now=mo_now();const int enabled=mo_enabled();
    if(!mo.valid||w!=mo.w||h!=mo.h||!mo.shown){
        mo_drop_bitmaps();mo.w=w;mo.h=h;BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof bi.bmiHeader;bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
        mo.shown_bmp=CreateDIBSection(NULL,&bi,DIB_RGB_COLORS,(void**)&mo.shown,NULL,0);mo.old=malloc((size_t)w*h*4);
        if(!mo.shown_bmp||!mo.old){mo_drop_bitmaps();if(mo.frame_bmp)DeleteObject(mo.frame_bmp);mo.frame_bmp=bmp;mo.frame=px;mo.valid=0;return 0;}
        if(mo.frame_bmp)DeleteObject(mo.frame_bmp);mo.frame_bmp=bmp;mo.frame=px;mo.valid=1;
        mo.wipe=mo.band=mo.fades=mo.glint=0;memcpy(mo.shown,px,(size_t)w*h*4);
        if(mo.intro_pending&&enabled){mo.intro_pending=0;mo_start_intro(now);mo_compose(now,&(MoRect){0});}
        mo.intro_pending=0;goto record;}
    {uint32_t*prev=mo.frame;HBITMAP prev_bmp=mo.frame_bmp;mo.frame=px;mo.frame_bmp=bmp;
    if(!enabled){mo.intro=mo.wipe=mo.band=mo.fades=mo.glint=0;memcpy(mo.shown,px,(size_t)w*h*4);DeleteObject(prev_bmp);goto record;}
    memcpy(mo.old,mo.shown,(size_t)w*h*4);   /* fades start from exactly what is on screen */
    if(installed!=mo.installed){DeleteObject(prev_bmp);mo_start_intro(now);goto record;}   /* the menus after setup */
    if(mo.intro){DeleteObject(prev_bmp);goto record;}                                    /* the entrance carries on to the new target */
    if(page!=mo.page){mo.wipe=1;mo.wipe_t0=now;mo.wipe_dir=parent_of(mo.page)==page?-1:1;mo.band=mo.fades=mo.glint=0;DeleteObject(prev_bmp);goto record;}
    for(int i=0;i<mo.fades;i++)mo.fade[i].t0=now;   /* restarted from the new snapshot */
    if(mo.glint)mo.glint=0;
    if(!installed){if(mo_differs(prev,px,mo_region(MR_SETUP)))mo_fade(mo_region(MR_SETUP),180,now);}
    else{
        if(selected!=mo.selected&&list_first==mo.list_first&&mo_layers()){
            double from=mo.band?mo_band_now(now):mo_row_y(mo.selected),to=mo_row_y(selected),rows=fabs(to-from)/list_pitch();
            mo.band=1;mo.band_t0=now;mo.band_from=from;mo.band_to=to;mo.band_dur=fmin(240,110+38*rows);}
        else if(mo_differs(prev,px,mo_region(MR_LIST)))mo_fade(mo_region(MR_LIST),list_first!=mo.list_first?140:120,now);
        if(mo_differs(prev,px,mo_region(MR_CARD)))mo_fade(mo_region(MR_CARD),page==29?0:180,now);   /* the room chat: typing stays instant */
        if(mo_differs(prev,px,mo_region(MR_HEADER)))mo_fade(mo_region(MR_HEADER),170,now);
        if(mo_differs(prev,px,mo_region(MR_FOOTER)))mo_fade(mo_region(MR_FOOTER),130,now);}
    if(!busy&&mo_differs(prev,px,mo_region(MR_TOAST)))mo_fade(mo_region(MR_TOAST),toast_painted!=mo.toast?240:150,now);
    DeleteObject(prev_bmp);
    memcpy(mo.shown,px,(size_t)w*h*4);}   /* what does not move shows the new frame at once; the effects compose over it */
record:
    mo.page=page;mo.selected=selected;mo.list_first=list_first;mo.installed=installed;mo.toast=toast_painted;
    return 1;
}
/* ---- Window side ------------------------------------------------------------ */
static void mo_present(HDC dc,MoRect r){if(!r.w||!r.h||!mo.shown_bmp)return;HDC mem=CreateCompatibleDC(dc);HGDIOBJ o=SelectObject(mem,mo.shown_bmp);BitBlt(dc,r.x,r.y,r.w,r.h,mem,r.x,r.y,SRCCOPY);SelectObject(mem,o);DeleteDC(mem);}
static void mo_run_timer(int on){if(!mainwindow)return;if(on&&!mo.timer){SetTimer(mainwindow,3,15,NULL);mo.timer=1;}else if(!on&&mo.timer){KillTimer(mainwindow,3);mo.timer=0;}}
/* WM_PAINT: render the state, start whatever moves, show it. */
static void mo_paint(HDC dc){
    void*pixels;HBITMAP bmp=render(dc,&pixels);if(!bmp)return;
    if(!mo_frame_arrived(bmp,pixels,render_width,render_height)){HDC mem=CreateCompatibleDC(dc);HGDIOBJ o=SelectObject(mem,bmp);BitBlt(dc,0,0,render_width,render_height,mem,0,0,SRCCOPY);SelectObject(mem,o);DeleteDC(mem);return;}
    MoRect d;int active=mo_compose(mo_now(),&d);mo_present(dc,(MoRect){0,0,mo.w,mo.h});mo_run_timer(active);
}
/* Timer 3 (15 ms) while something moves: compose and show only what changed. */
static void mo_tick(void){
    MoRect d;int active=mo_compose(mo_now(),&d);
    if(d.w&&d.h){HDC dc=GetDC(mainwindow);mo_present(dc,d);ReleaseDC(mainwindow,dc);}
    mo_run_timer(active);
}
/* Timer 4 (a few seconds): an occasional glint while the launcher has focus. */
static void mo_glint_timer(void){
    if(!mo.valid||mo.timer||!mo_enabled()||busy||binding>=0||page==29||GetForegroundWindow()!=mainwindow||IsIconic(mainwindow)||!IsWindowVisible(mainwindow))return;
    mo_start_glint(mo_now());if(mo.glint)mo_run_timer(1);
}
/* The notice bar's pixels under the bar itself, saved by render(), so progress
 * updates redraw only the bar instead of the whole window. */
static void mo_save_toast_base(void){
    MoRect r=mo_region(MR_TOAST);size_t n=(size_t)r.w*r.h;static size_t cap;mo.toast_ok=0;if(!n)return;
    if(n>cap){free(mo.toast_base);mo.toast_base=malloc(n*4);cap=mo.toast_base?n:0;}if(!mo.toast_base)return;
    GdiFlush();for(int y=0;y<r.h;y++)memcpy(mo.toast_base+(size_t)y*r.w,ui.s.px+(size_t)(r.y+y)*ui.s.w+r.x,(size_t)r.w*4);mo.toast_r=r;mo.toast_ok=1;
}
static int mo_toast_refresh(void){
    if(!mo.valid||!mo.toast_ok||!mainwindow||!toast_visible()||!toast_painted)return 0;
    RECT c;GetClientRect(mainwindow,&c);if(c.right!=mo.w||c.bottom!=mo.h)return 0;
    Surface saved=ui.s;ui_layout(mo.w,mo.h);MoRect r=mo.toast_r;if(memcmp(&r,&(MoRect){0},sizeof r)==0)return 0;
    for(int y=0;y<r.h;y++)memcpy(mo.frame+(size_t)(r.y+y)*mo.w+r.x,mo.toast_base+(size_t)y*r.w,(size_t)r.w*4);
    ui.s=(Surface){mo.frame,mo.w,mo.h};draw_toast();GdiFlush();ui.s=saved;
    if(!mo.intro&&!mo.wipe){mo_copy(mo.shown,mo.frame,r);HDC dc=GetDC(mainwindow);mo_present(dc,r);ReleaseDC(mainwindow,dc);}
    return 1;
}
/* Eased progress: the bar slides to each reported value instead of jumping. */
static double mo_eased_progress(double target){
    static double shown=-1,last;const double now=mo_now();
    if(shown<0||target<shown-.5||!mo_enabled()){shown=target;last=now;return shown;}
    shown+=(target-shown)*(1-exp(-(now-last)/220.0));last=now;if(fabs(target-shown)<.05)shown=target;return shown;
}

/* ---- --preview-motion: frames of a scripted visit, for review ---------------- */
/* Script words (';'-separated): intro, hold MS, down, up, open PAGE, back,
 * glint, build A B MS (setup progress from A to B percent). Frames are written
 * raw (BGRA, top-down) to stdout ("-") or as numbered BMPs (a pattern with %d). */
static int mo_out_frame(const wchar_t*pattern,int*index){
    if(!wcscmp(pattern,L"-")){DWORD n;HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE);return WriteFile(out,mo.shown,(DWORD)((size_t)mo.w*mo.h*4),&n,NULL)&&n==(DWORD)((size_t)mo.w*mo.h*4);}
    wchar_t path[32768];swprintf(path,32768,pattern,(*index)++);Image im={(unsigned)mo.w,(unsigned)mo.h,mo.shown};return save_bmp(path,&im);
}
static int mo_preview_render(void){HDC dc=GetDC(NULL);void*px;HBITMAP b=render(dc,&px);ReleaseDC(NULL,dc);return b&&mo_frame_arrived(b,px,render_width,render_height);}
static int mo_preview_run(const wchar_t*script,const wchar_t*pattern,double fps){
    mo.preview=1;mo.clock=0;int index=0;const double step=1000.0/fps;wchar_t buf[1024];wcsncpy(buf,script,1023);buf[1023]=0;
    mo.intro_pending=wcsncmp(buf,L"intro",5)==0;if(!mo_preview_render())return 0;
    for(wchar_t*tok=buf,*next;tok&&*tok;tok=next){next=wcschr(tok,L';');if(next)*next++=0;
        double hold=0,a=0,b=0,ms=0;int n=row_count();
        if(!wcscmp(tok,L"intro")){}
        else if(swscanf(tok,L"hold %lf",&hold)==1){}
        else if(!wcscmp(tok,L"down")){if(n)selected=(selected+1)%n;list_scroll();mo_preview_render();}
        else if(!wcscmp(tok,L"up")){if(n)selected=(selected+n-1)%n;list_scroll();mo_preview_render();}
        else if(swscanf(tok,L"open %lf",&a)==1){if(nav_depth<16){nav_pages[nav_depth]=page;nav_rows[nav_depth]=selected;nav_depth++;}page=(int)a;selected=list_first=0;mo_preview_render();}
        else if(!wcscmp(tok,L"back")){int p=parent_page();if(nav_depth&&nav_pages[nav_depth-1]==p)selected=nav_rows[--nav_depth];else selected=0;page=p;list_first=0;mo_preview_render();}
        else if(!wcscmp(tok,L"glint")){mo_start_glint(mo.clock);}
        else if(swscanf(tok,L"build %lf %lf %lf",&a,&b,&ms)==3){
            busy=2;wcscpy(status_text,L"PREPARING YOUR LOCAL NATIVE BUILD — THIS MAY TAKE A FEW MINUTES");status_tick=1;
            for(double t=0;t<=ms;t+=step){build_percent=(int)(a+(b-a)*fmin(1,t/(ms*.6)));mo_preview_render();   /* reports arrive in steps; the bar eases */
                MoRect d;mo_compose(mo.clock,&d);if(!mo_out_frame(pattern,&index))return 0;mo.clock+=step;}
            busy=0;status_tick=0;mo_preview_render();
            continue;}
        MoRect d;double end=mo.clock+hold;int active;
        do{active=mo_compose(mo.clock,&d);if(!mo_out_frame(pattern,&index))return 0;mo.clock+=step;}while(active||mo.clock<end);
    }
    return 1;
}
