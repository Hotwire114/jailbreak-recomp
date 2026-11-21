/* Linux launcher only (included by launcher.c): the text box that stands in for the Windows EDIT
 * dialogs (rider names, custom cheat codes, a report's note). It is drawn over the current
 * launcher screen in the launcher's own look (HD font, panel colours) and runs its own message
 * loop until Enter / Esc. LGPL-2.1-or-later. Original author code. */
typedef struct LinuxPrompt {
    const wchar_t *title, *caption; wchar_t *buf; size_t cap; int max_chars, multiline;
} LinuxPrompt;
static void linux_prompt_paint(const LinuxPrompt *p){
    HDC wdc=GetDC(mainwindow);void*px=NULL;HBITMAP bmp=render(wdc,&px);
    if(!bmp){ReleaseDC(mainwindow,wdc);return;}
    /* render() leaves ui.s on its image and ui laid out for the window: draw on top of it. */
    fill(0,0,ui.lw,ui.lh,C_INK,175);
    const double bw=760,bh=p->multiline?380:250,bx=ui.cx+(1280-bw)/2,by=ui.by+(720-bh)/2-20;
    fill(bx,by,bw,bh,C_PANEL,250);outline(bx,by,bw,bh,C_STEEL,140);fill(bx,by,bw,4,RGBX(207,22,46),255);
    text_shadow(p->title,bx+28,by+20,bw-56,40,30,FW_SEMIBOLD,C_GOLD,DT_LEFT|DT_TOP|DT_SINGLELINE);
    text(p->caption,bx+28,by+66,bw-56,44,17,FW_NORMAL,C_STEEL,DT_LEFT|DT_TOP|DT_WORDBREAK);
    const double fx=bx+28,fy=by+116,fw=bw-56,fh=p->multiline?190:52;
    fill(fx,fy,fw,fh,C_INK,255);outline(fx,fy,fw,fh,C_GOLD,200);
    size_t n=wcslen(p->buf);wchar_t*shown=malloc((n+2)*sizeof(wchar_t));
    if(shown){wcscpy(shown,p->buf);shown[n]=L'_';shown[n+1]=0;   /* the caret */
        text(shown,fx+14,fy+10,fw-28,fh-20,22,FW_NORMAL,C_WHITE,DT_LEFT|DT_TOP|(p->multiline?DT_WORDBREAK:DT_SINGLELINE));free(shown);}
    text(p->multiline?L"Ctrl+Enter: OK     Enter: new line     Ctrl+V: paste     Esc: cancel":L"Enter: OK     Ctrl+V: paste     Esc: cancel",
         fx,fy+fh+18,fw,30,16,FW_NORMAL,C_DIM,DT_LEFT|DT_TOP|DT_SINGLELINE);
    HDC mem=CreateCompatibleDC(wdc);HGDIOBJ old=SelectObject(mem,bmp);BitBlt(wdc,0,0,render_width,render_height,mem,0,0,SRCCOPY);
    SelectObject(mem,old);DeleteDC(mem);DeleteObject(bmp);ReleaseDC(mainwindow,wdc);
}
static void linux_prompt_add(LinuxPrompt*p,const wchar_t*s){
    size_t n=wcslen(p->buf);
    for(;*s;s++){wchar_t c=*s;if(c==L'\r')continue;if(c==L'\n'&&!p->multiline)continue;if(c==L'\t')c=L' ';
        if(c!=L'\n'&&(c<32||c==127))continue;if((int)n>=p->max_chars||n+1>=p->cap)break;p->buf[n++]=c;p->buf[n]=0;}
}
int rr_linux_prompt(HWND owner,const wchar_t*title,const wchar_t*caption,wchar_t*text_io,size_t cap,int max_chars,int multiline){
    (void)owner;if(!text_io||cap<2||!mainwindow)return 0;
    LinuxPrompt p={title?title:L"",caption?caption:L"",calloc(cap,sizeof(wchar_t)),cap,max_chars>0?max_chars:(int)cap-1,multiline};
    if(!p.buf)return 0;linux_prompt_add(&p,text_io);
    MSG held[32];int nheld=0,result=-1;   /* worker messages (WM_APP+n) wait for the box to close */
    linux_prompt_paint(&p);MSG m;
    while(result<0&&GetMessageW(&m,NULL,0,0)>0){
        if(m.hwnd!=mainwindow){TranslateMessage(&m);DispatchMessageW(&m);continue;}
        int redraw=0;
        switch(m.message){
        case WM_CHAR:{wchar_t s[2]={(wchar_t)m.wParam,0};if(m.wParam>=32){linux_prompt_add(&p,s);redraw=1;}break;}
        case WM_KEYDOWN:case WM_SYSKEYDOWN:{
            const int ctrl=GetKeyState(VK_CONTROL)<0;size_t n=wcslen(p.buf);
            if(m.wParam==VK_ESCAPE)result=0;
            else if(m.wParam==VK_RETURN){if(p.multiline&&!ctrl){wchar_t nl[2]={L'\n',0};linux_prompt_add(&p,nl);redraw=1;}else result=1;}
            else if(m.wParam==VK_BACK&&n){p.buf[n-1]=0;redraw=1;}
            else if(m.wParam=='V'&&ctrl){wchar_t*c=rr_linux_clipboard_text();if(c){linux_prompt_add(&p,c);free(c);redraw=1;}}
            break;}
        case WM_PAINT:case WM_SIZE:redraw=1;break;
        case WM_CLOSE:result=0;PostMessageW(mainwindow,WM_CLOSE,0,0);break;
        case WM_TIMER:case WM_MOUSEMOVE:case WM_MOUSELEAVE:case WM_MOUSEWHEEL:case WM_LBUTTONDOWN:case WM_XBUTTONDOWN:break;
        default:if(m.message>=WM_APP&&nheld<32)held[nheld++]=m;break;
        }
        if(redraw)linux_prompt_paint(&p);
    }
    if(result==1){wcsncpy(text_io,p.buf,cap-1);text_io[cap-1]=0;}
    free(p.buf);
    for(int i=0;i<nheld;i++)PostMessageW(held[i].hwnd,held[i].message,held[i].wParam,held[i].lParam);
    InvalidateRect(mainwindow,NULL,FALSE);
    return result==1;
}
