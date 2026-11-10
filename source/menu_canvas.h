/* Shared high-resolution steel/blue/orange menu layout. Independent geometry
 * and host text; original preview art comes only from the installed disc.
 * Logical 1280x720, rendered at the actual host pixel density. LGPL-2.1+. */
#pragma once
#include <windows.h>
#include "owner_assets.h"
typedef struct RrMenuView {wchar_t title[64],labels[8][160],help[512],footer[160];int count,selected,art;} RrMenuView;
static void rr_box(HDC dc,int x,int y,int w,int h,COLORREF c){RECT r={x,y,x+w,y+h};HBRUSH b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
static void rr_text(HDC dc,const wchar_t*s,int x,int y,int w,int h,int size,int bold,COLORREF c,const wchar_t*face){if(rr_original_text(dc,s,x,y,w,h,size,!wcscmp(face,L"Impact"),c))return;HFONT f=CreateFontW(-size,0,0,0,bold,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,face);HGDIOBJ prev=SelectObject(dc,f);SetTextColor(dc,c);SetBkMode(dc,TRANSPARENT);RECT r={x,y,x+w,y+h};DrawTextW(dc,s,-1,&r,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,prev);DeleteObject(f);}
static void rr_image(HDC dc,const RrArt*a,int x,int y,int w,int h){if(!a||!a->pixels||!a->w||!a->h)return;BITMAPINFO b={0};b.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);b.bmiHeader.biWidth=a->w;b.bmiHeader.biHeight=-(LONG)a->h;b.bmiHeader.biPlanes=1;b.bmiHeader.biBitCount=32;double k=(double)w/a->w;if(k>(double)h/a->h)k=(double)h/a->h;int dw=(int)(a->w*k),dh=(int)(a->h*k);SetStretchBltMode(dc,HALFTONE);StretchDIBits(dc,x+(w-dw)/2,y+(h-dh)/2,dw,dh,0,0,a->w,a->h,a->pixels,&b,DIB_RGB_COLORS,SRCCOPY);}
static HBITMAP rr_menu_bitmap(HDC compatible,int w,int h,const RrMenuView*v,void**pixels){
 if(w<1||h<1||w>7680||h>4320)return NULL;BITMAPINFO b={0};b.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);b.bmiHeader.biWidth=w;b.bmiHeader.biHeight=-h;b.bmiHeader.biPlanes=1;b.bmiHeader.biBitCount=32;
 HBITMAP bitmap=CreateDIBSection(compatible,&b,DIB_RGB_COLORS,pixels,NULL,0);if(!bitmap)return NULL;uint32_t*p=(uint32_t*)*pixels;const RrArt*background=rr_art_get(0);
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){uint32_t c=background&&background->pixels?background->pixels[(size_t)(y*background->h/h)*background->w+x*background->w/w]:0x091337;p[y*w+x]=(((c>>16&255)*3/5)<<16)|(((c>>8&255)*3/5)<<8)|((c&255)*3/5);}
 HDC dc=CreateCompatibleDC(compatible);HGDIOBJ old=SelectObject(dc,bitmap);SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,1280,720,NULL);SetViewportExtEx(dc,w,h,NULL);
 COLORREF steel=RGB(179,197,220),gold=RGB(255,190,37),white=RGB(240,242,250);
 rr_text(dc,v->title,52,39,1180,85,62,FW_BOLD,RGB(1,4,13),L"Impact");rr_text(dc,v->title,48,35,1180,85,62,FW_BOLD,steel,L"Impact");
 for(int i=0;i<v->count&&i<8;i++){int y=140+i*58;const RrArt*bar=rr_art_get(i==v->selected?10:9);if(bar&&bar->pixels)rr_image(dc,bar,49,y,466,50);else{rr_box(dc,49,y,466,50,steel);rr_box(dc,51,y+2,462,46,RGB(2,15,53));}rr_text(dc,v->labels[i],70,y+9,424,34,27,FW_NORMAL,i==v->selected?white:gold,L"Tahoma");}

 // Help and artwork occupy disjoint right-hand rectangles. All text clips to
 // its own box, including multiline descriptions and long imported filenames.
 rr_text(dc,v->help,575,145,635,144,25,FW_NORMAL,white,L"Tahoma");rr_box(dc,571,314,641,291,steel);rr_box(dc,573,316,637,287,RGB(2,15,53));rr_image(dc,rr_art_get(v->art),581,324,621,271);
 rr_text(dc,v->footer,52,662,1160,40,23,FW_NORMAL,steel,L"Tahoma");SelectObject(dc,old);DeleteDC(dc);return bitmap;
}
