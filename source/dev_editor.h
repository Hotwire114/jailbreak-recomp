/* Developer page: a no-code editor for tuning.ini, in the launcher's own
 * style. LGPL-2.1-or-later. Original author code. Included by launcher.c.
 *
 * Shown on the main menu after Ctrl+Shift+D. Every change goes into a draft
 * (tn_draft); the launcher and the game keep using the saved file (tn_live)
 * until Save. Save writes tuning.ini (the old one is kept as tuning.ini.bak)
 * and adds one dated, plain-English line per change to tuning-changes.log.
 * Undo steps back through the draft; Discard drops it. Leaving with unsaved
 * changes asks first.
 *
 * Pages: 30 Developer, 31 launcher pages, 32 one page's items, 33 one item,
 * 34 level-of-detail steps, 35 changes from the built-in values, 36 the
 * Display presets, 37 one preset's values. An item's page also picks its
 * picture (a still from the player's own disc). */
#include "report_zip.h"
enum {DEV_MENUS=31,DEV_PAGE_ITEMS=32,DEV_ITEM=33,DEV_LOD=34,DEV_CHANGES=35,DEV_PRESETS=36,DEV_PRESET=37};
/* tn_base: the saved file the draft started from (unsaved = base vs draft). */
static RrTuning tn_draft,tn_base,tn_defaults_cache;static int dev_draft_ready,dev_sel_page,dev_sel_item,dev_sel_preset;
#define DEV_UNDO 24
static RrTuning*dev_undo[DEV_UNDO];static char dev_undo_what[DEV_UNDO][96];static int dev_undo_n;
static void dev_adjust(int dir);
static const RrTuning*tn_builtin(void){static int ready;if(!ready){tn_defaults(&tn_defaults_cache);ready=1;}return &tn_defaults_cache;}
static void dev_sync(void){if(!dev_draft_ready){tn_draft=tn_base=tn_live;dev_draft_ready=1;}}
static int dev_dirty(void){return dev_draft_ready&&tn_changes(&tn_base,&tn_draft)>0;}
static void dev_undo_clear(void){for(int i=0;i<dev_undo_n;i++)free(dev_undo[i]);dev_undo_n=0;}
/* Before each change: remember the draft for Undo. */
static void dev_push(const char*what){
    dev_sync();if(dev_undo_n==DEV_UNDO){free(dev_undo[0]);memmove(dev_undo,dev_undo+1,sizeof dev_undo[0]*(DEV_UNDO-1));memmove(dev_undo_what,dev_undo_what+1,sizeof dev_undo_what[0]*(DEV_UNDO-1));dev_undo_n--;}
    RrTuning*c=malloc(sizeof *c);if(!c)return;*c=tn_draft;dev_undo[dev_undo_n]=c;snprintf(dev_undo_what[dev_undo_n],96,"%s",what);dev_undo_n++;
}
/* The saved file changed (by hand, the game closing): an untouched draft follows it. */
static void dev_tuning_reloaded(void){if(dev_draft_ready&&!dev_dirty()){tn_draft=tn_base=tn_live;dev_undo_clear();}}
static void wide(const char*s,wchar_t*out,size_t cap){if(!MultiByteToWideChar(CP_UTF8,0,s,-1,out,(int)cap))out[0]=0;out[cap-1]=0;}
/* One line of a possibly multi-line text, for a value column. */
static void first_line(const char*s,wchar_t*out,size_t cap){char t[80];size_t n=0;while(s[n]&&s[n]!='\n'&&n<sizeof t-4){t[n]=s[n];n++;}t[n]=0;if(s[n])strcat(t,"...");wide(t,out,cap);}

/* ---- Text box (modal, in the style of the rider-name dialog) ------------- */
typedef struct TextPrompt {HWND edit;int accepted;int multiline;wchar_t*text;size_t cap;} TextPrompt;
static LRESULT CALLBACK text_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_NCCREATE){SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);return DefWindowProcW(window,msg,wp,lp);}
    TextPrompt*p=(TextPrompt*)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(msg==WM_COMMAND&&p){if(LOWORD(wp)==IDOK){GetWindowTextW(p->edit,p->text,(int)p->cap);p->accepted=1;DestroyWindow(window);return 0;}if(LOWORD(wp)==IDCANCEL){DestroyWindow(window);return 0;}}
    if(msg==WM_CLOSE){DestroyWindow(window);return 0;}return DefWindowProcW(window,msg,wp,lp);
}
/* Edits `text` (cap characters, \n line breaks). Returns 1 on OK. */
static int prompt_text(const wchar_t*title,const wchar_t*caption,wchar_t*text,size_t cap,int multiline){
    WNDCLASSW wc={0};wc.lpfnWndProc=text_proc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"JailbreakTextPrompt";wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    TextPrompt state={0};state.multiline=multiline;state.cap=cap;
    wchar_t*buffer=calloc(cap+cap/2+8,sizeof(wchar_t));if(!buffer)return 0;
    {size_t o=0;for(const wchar_t*s=text;*s&&o+2<cap+cap/2;s++){if(*s==L'\n')buffer[o++]=L'\r';buffer[o++]=*s;}buffer[o]=0;}
    state.text=buffer;state.cap=cap+cap/2+4;
    RECT r;GetWindowRect(mainwindow,&r);UINT dpi=window_dpi(mainwindow);
#define D(v) MulDiv(v,dpi,96)
    int W=D(multiline?640:500),H=D(multiline?380:230),eh=multiline?D(210):D(35);
    HWND dialog=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,title,WS_CAPTION|WS_SYSMENU,r.left+(r.right-r.left-W)/2,r.top+(r.bottom-r.top-H)/2,W,H,mainwindow,NULL,wc.hInstance,&state);if(!dialog){free(buffer);return 0;}
    HWND label=CreateWindowW(L"STATIC",caption,WS_CHILD|WS_VISIBLE,D(22),D(16),W-D(60),D(40),dialog,NULL,wc.hInstance,NULL);
    state.edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",buffer,WS_CHILD|WS_VISIBLE|WS_TABSTOP|(multiline?ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL:ES_AUTOHSCROLL),D(22),D(60),W-D(60),eh,dialog,(HMENU)100,wc.hInstance,NULL);
    SendMessageW(state.edit,EM_LIMITTEXT,(WPARAM)(cap-1+(multiline?cap/4:0)),0);
    int by=D(60)+eh+D(18);
    HWND ok=CreateWindowW(L"BUTTON",L"OK",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,W-D(250),by,D(110),D(34),dialog,(HMENU)IDOK,wc.hInstance,NULL),cancel=CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,W-D(130),by,D(90),D(34),dialog,(HMENU)IDCANCEL,wc.hInstance,NULL);
    HFONT font=CreateFontW(-D(18),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");
#undef D
    HWND widgets[]={label,state.edit,ok,cancel};for(int i=0;i<4;i++)SendMessageW(widgets[i],WM_SETFONT,(WPARAM)font,TRUE);
    EnableWindow(mainwindow,FALSE);ShowWindow(dialog,SW_SHOW);SetFocus(state.edit);SendMessageW(state.edit,EM_SETSEL,0,-1);MSG msg;
    while(IsWindow(dialog)&&GetMessageW(&msg,NULL,0,0)>0){
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN&&(!multiline||GetKeyState(VK_CONTROL)<0)){SendMessageW(dialog,WM_COMMAND,IDOK,0);continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_TAB){HWND next=GetNextDlgTabItem(dialog,GetFocus(),GetKeyState(VK_SHIFT)<0);if(next)SetFocus(next);continue;}
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    EnableWindow(mainwindow,TRUE);SetForegroundWindow(mainwindow);DeleteObject(font);
    if(state.accepted){size_t o=0;for(const wchar_t*s=buffer;*s&&o+1<cap;s++)if(*s!=L'\r')text[o++]=*s;text[o]=0;}
    free(buffer);return state.accepted;
}
/* Asks for a new text and stores it (UTF-8) if the game font can show it. */
static int dev_edit_text(const wchar_t*title,const wchar_t*caption,char*field,size_t cap,int lines,const char*what){
    wchar_t w[TN_HELP+8];wide(field,w,TN_HELP+8);if(!prompt_text(title,caption,w,cap,lines))return 0;
    char u[TN_HELP*3+8];if(!WideCharToMultiByte(CP_UTF8,0,w,-1,u,sizeof u,NULL,NULL))return 0;
    char clean[TN_HELP+8];int r=tn_clean_text(clean,cap,u,lines);
    if(r!=1){MessageBoxW(mainwindow,r<0?L"That text is too long.":L"The text is empty, or uses letters the game's font does not have (plain English letters, numbers and punctuation work).",title,MB_OK|MB_ICONWARNING);return 0;}
    if(!strcmp(clean,field))return 0;
    dev_push(what);tn_copy(field,cap,clean);return 1;
}

/* ---- Save, discard, import, export -------------------------------------- */
static void dev_save(void){
    dev_sync();if(!dev_dirty()){notice(L"NOTHING TO SAVE");return;}
    int n=tn_changes(&tn_base,&tn_draft);
    if(!tn_save(tuning_path,&tn_draft)){notice(L"TUNING.INI COULD NOT BE SAVED — CHECK THE INSTALLATION FOLDER");return;}
    wchar_t log[32768];join(log,store,L"tuning-changes.log");tn_log_changes(log,&tn_live,&tn_draft,"launcher's Developer page");
    tn_live=tn_base=tn_draft;tn_refresh();tuning_stamp=file_stamp(tuning_path);dev_undo_clear();
    if(model_detail>=tn_live.lod_n){model_detail=tn_live.lod_n-1;save_settings();}
    wchar_t text[160];swprintf(text,160,L"SAVED %d CHANGE%ls — THE LAUNCHER USES THEM NOW, THE GAME FROM ITS NEXT START",n,n==1?L"":L"S");notice(text);
}
static void dev_discard(void){tn_draft=tn_base=tn_live;dev_draft_ready=1;dev_undo_clear();notice(L"UNSAVED CHANGES DISCARDED");}
/* Leaving the Developer page (or the launcher): save, discard or stay. */
static int dev_leave_ok(void){
    if(!dev_dirty())return 1;
    int a=MessageBoxW(mainwindow,L"You have unsaved changes to the launcher menus.\n\nSave them now?\n\nYes saves them, No throws them away, Cancel goes back to the editor.",L"Developer",MB_YESNOCANCEL|MB_ICONQUESTION);
    if(a==IDCANCEL)return 0;if(a==IDYES)dev_save();else dev_discard();return 1;
}
static int file_dialog(wchar_t*path,int save,const wchar_t*title){
    OPENFILENAMEW d={0};d.lStructSize=sizeof d;d.hwndOwner=mainwindow;d.lpstrFilter=L"Tuning file (*.ini)\0*.ini\0All files\0*.*\0";d.lpstrFile=path;d.nMaxFile=32768;d.lpstrTitle=title;d.lpstrDefExt=L"ini";
    d.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);return save?GetSaveFileNameW(&d):GetOpenFileNameW(&d);
}
static void dev_import(void){
    wchar_t path[32768]=L"";if(!file_dialog(path,0,L"Import a tuning file"))return;
    RrTuning*t=malloc(sizeof *t);if(!t)return;TniLog*log=calloc(1,sizeof *log);if(!log){free(t);return;}
    if(!tn_load(path,t,log)){notice(L"THAT FILE COULD NOT BE READ");free(t);free(log);return;}
    dev_push("import");tn_draft=*t;
    if(log->count){wchar_t*w=calloc(sizeof log->text+256,sizeof(wchar_t));if(w){swprintf(w,sizeof log->text+256,L"Imported. %d line%ls could not be used and keep their built-in values:\n\n%hs",log->count,log->count==1?L"":L"s",log->text);MessageBoxW(mainwindow,w,L"Import a tuning file",MB_OK|MB_ICONINFORMATION);free(w);}}
    notice(L"IMPORTED INTO THE EDITOR — SAVE CHANGES TO USE IT");free(t);free(log);
}
static void dev_export(void){
    wchar_t path[32768]=L"tuning.ini";if(!file_dialog(path,1,L"Export the tuning file"))return;
    dev_sync();notice(tn_save(path,&tn_draft)?L"TUNING FILE EXPORTED (WITH ANY UNSAVED CHANGES)":L"THE TUNING FILE COULD NOT BE EXPORTED");
}

/* ---- Bug report ------------------------------------------------------------ */
/* The launcher window as drawn now, as a PNG (WIC). */
static int save_png(const wchar_t*path,const uint32_t*px,int w,int h){
    HRESULT init=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);if(FAILED(init))return 0;
    IWICImagingFactory*f=NULL;IWICStream*s=NULL;IWICBitmapEncoder*e=NULL;IWICBitmapFrameEncode*fr=NULL;int ok=0;WICPixelFormatGUID fmt=GUID_WICPixelFormat32bppBGR;
    if(FAILED(CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void**)&f)))goto done;
    if(FAILED(IWICImagingFactory_CreateStream(f,&s))||FAILED(IWICStream_InitializeFromFilename(s,path,GENERIC_WRITE)))goto done;
    if(FAILED(IWICImagingFactory_CreateEncoder(f,&GUID_ContainerFormatPng,NULL,&e))||FAILED(IWICBitmapEncoder_Initialize(e,(IStream*)s,WICBitmapEncoderNoCache)))goto done;
    if(FAILED(IWICBitmapEncoder_CreateNewFrame(e,&fr,NULL))||FAILED(IWICBitmapFrameEncode_Initialize(fr,NULL)))goto done;
    if(FAILED(IWICBitmapFrameEncode_SetSize(fr,(UINT)w,(UINT)h))||FAILED(IWICBitmapFrameEncode_SetPixelFormat(fr,&fmt)))goto done;
    if(FAILED(IWICBitmapFrameEncode_WritePixels(fr,(UINT)h,(UINT)w*4,(UINT)w*h*4,(BYTE*)px))||FAILED(IWICBitmapFrameEncode_Commit(fr))||FAILED(IWICBitmapEncoder_Commit(e)))goto done;
    ok=1;
done:
    if(fr)IWICBitmapFrameEncode_Release(fr);if(e)IWICBitmapEncoder_Release(e);if(s)IWICStream_Release(s);if(f)IWICImagingFactory_Release(f);CoUninitialize();return ok;
}
typedef struct DevText {char*b;size_t n,cap;} DevText;
static void dev_text_line(void*ctx,const char*line){DevText*t=ctx;size_t m=strlen(line);if(t->n+m+4>t->cap){size_t c=(t->cap+m+4)*2;char*b=realloc(t->b,c);if(!b)return;t->b=b;t->cap=c;}memcpy(t->b+t->n,line,m);t->n+=m;memcpy(t->b+t->n,"\r\n",3);t->n+=2;}
static void report_problem(void){
    wchar_t note[1024]=L"";
    if(!prompt_text(L"Report a problem",L"What went wrong? A short note helps (optional). OK saves the report.",note,1000,1))return;
    wchar_t shot[32768];join(shot,store,L"reports");ensure(shot);join(shot,store,L"reports\\launcher-screen.png");
    HDC dc=GetDC(mainwindow);void*pixels=NULL;HBITMAP bmp=render(dc,&pixels);int have=bmp&&pixels&&save_png(shot,pixels,render_width,render_height);if(bmp)DeleteObject(bmp);ReleaseDC(mainwindow,dc);
    char*u=malloc(4096);DevText ctx={0};
    if(u&&!WideCharToMultiByte(CP_UTF8,0,note,-1,u,4096,NULL,NULL))u[0]=0;
    {char head[256];wchar_t title[64];wcsncpy(title,page_title(page),63);title[63]=0;snprintf(head,sizeof head,"Mode: %s\r\nLauncher page: %ls\r\nUnsaved editor changes: %s\r\n\r\nChanged from the built-in values (tuning.ini):",enhanced_mode?"Enhanced":"Original",title,dev_dirty()?"yes":"no");dev_text_line(&ctx,head);}
    int before=(int)ctx.n;tn_diff(tn_builtin(),&tn_live,dev_text_line,&ctx);if((int)ctx.n==before)dev_text_line(&ctx,"(nothing)");
    wchar_t out[32768];int ok=rr_report_write(store,"launcher",have?shot:NULL,u?u:"",ctx.b,out,32768);
    DeleteFileW(shot);free(u);free(ctx.b);
    if(ok){wchar_t text[256];swprintf(text,256,L"REPORT SAVED: reports\\%ls",PathFindFileNameW(out));notice(text);}else notice(L"THE REPORT COULD NOT BE SAVED — CHECK THE INSTALLATION FOLDER");
}

/* ---- Pages ----------------------------------------------------------------- */
static const wchar_t*dev_title(int p){
    switch(p){case DEV_MENUS:return L"LAUNCHER MENUS";case DEV_PAGE_ITEMS:return L"EDIT PAGE";case DEV_ITEM:return L"EDIT ITEM";case DEV_LOD:return L"LEVEL OF DETAIL";case DEV_CHANGES:return L"MY CHANGES";
    case DEV_PRESETS:return L"DISPLAY PRESETS";case DEV_PRESET:return L"EDIT PRESET";}
    return L"DEVELOPER";
}
static int dev_parent(int p){return p==DEV_PAGE_ITEMS?DEV_MENUS:p==DEV_ITEM?DEV_PAGE_ITEMS:p==DEV_PRESET?DEV_PRESETS:DEV_PAGE;}
/* The info card's picture: the item's (draft) picture, or the preset's. */
static int dev_art(void){
    dev_sync();if(page==DEV_ITEM){int k=tn_draft.picture[dev_sel_item];return k>=0?k:item_auto_art(dev_sel_item);}
    if(page==DEV_PRESETS)return preset_art(selected%3);if(page==DEV_PRESET)return preset_art(dev_sel_preset);
    return RR_ART_MAP;
}
typedef struct DevLines {char line[64][200];int n;} DevLines;
static void dev_collect(void*ctx,const char*line){DevLines*d=ctx;if(d->n<64)snprintf(d->line[d->n++],200,"%s",line);}
static DevLines dev_lines;
static int dev_row_count(void){
    dev_sync();
    switch(page){case DEV_PAGE:return 12;case DEV_MENUS:return TN_PAGES;case DEV_PAGE_ITEMS:return 1+tn_draft.count[dev_sel_page];case DEV_ITEM:return 7;case DEV_LOD:return tn_draft.lod_n+2;
    case DEV_PRESETS:return 3;case DEV_PRESET:return TN_PRESET_FIELDS+1;
    case DEV_CHANGES:dev_lines.n=0;tn_diff(tn_builtin(),&tn_draft,dev_collect,&dev_lines);return dev_lines.n?dev_lines.n:1;}
    return 0;
}
static int dev_row(int r,wchar_t*label,wchar_t*value,size_t cap){
    dev_sync();label[0]=value[0]=0;int kind=ROW_ACTION;
    if(page==DEV_PAGE){static const wchar_t*names[]={L"Launcher menus",L"Level of detail",L"Display presets",L"Show my changes",L"Save changes",L"Discard unsaved changes",L"Undo",L"Reset everything to defaults",L"Import a tuning file",L"Export the tuning file",L"Open the change log",L"Report a problem"};
        if(r<0||r>11)return ROW_INFO;wcsncpy(label,names[r],cap);kind=r<4?ROW_MENU:ROW_ACTION;
        int unsaved=tn_changes(&tn_base,&tn_draft);
        if(r==3){int n=tn_changes(tn_builtin(),&tn_draft);swprintf(value,cap,n?L"%d":L"None",n);}
        else if(r==4)swprintf(value,cap,unsaved?L"%d unsaved":L"Nothing to save",unsaved);
        else if(r==6){if(dev_undo_n)wide(dev_undo_what[dev_undo_n-1],value,cap);else wcsncpy(value,L"Nothing to undo",cap);}
    }else if(page==DEV_MENUS){if(r<0||r>=TN_PAGES)return ROW_INFO;wide(tn_pages[r].name,label,cap);int shown=0;for(int k=0;k<tn_draft.count[r];k++)shown+=!tn_draft.hidden[tn_draft.order[r][k]];swprintf(value,cap,L"%d items",shown);kind=ROW_MENU;}
    else if(page==DEV_PAGE_ITEMS){
        if(r==0){wcsncpy(label,L"Page title",cap);wide(tn_draft.title[dev_sel_page],value,cap);}
        else if(r-1<tn_draft.count[dev_sel_page]){int i=tn_draft.order[dev_sel_page][r-1];wide(tn_draft.label[i],label,cap);if(tn_draft.hidden[i])wcsncpy(value,L"Hidden",cap);kind=ROW_MENU;}
    }else if(page==DEV_ITEM){int i=dev_sel_item,pos=0,p=tn_where(&tn_draft,i,&pos);
        switch(r){
        case 0:wcsncpy(label,L"Name",cap);wide(tn_draft.label[i],value,cap);break;
        case 1:wcsncpy(label,L"Help text",cap);first_line(tn_draft.help[i],value,cap);break;
        case 2:wcsncpy(label,L"Position",cap);swprintf(value,cap,L"%d of %d",pos+1,p>=0?tn_draft.count[p]:0);kind=ROW_VALUE;break;
        case 3:wcsncpy(label,L"Page",cap);if(p>=0)wide(tn_pages[p].name,value,cap);kind=tn_items[i].flags&TN_FIXED?ROW_INFO:ROW_VALUE;break;
        case 4:wcsncpy(label,L"Show",cap);wcsncpy(value,tn_draft.hidden[i]?L"Hidden":L"Shown",cap);kind=tn_items[i].flags&TN_FIXED?ROW_INFO:ROW_VALUE;break;
        case 5:wcsncpy(label,L"Picture",cap);wide(tn_pictures[tn_picture_index(tn_draft.picture[i])].name,value,cap);if(value[0]>=L'a'&&value[0]<=L'z')value[0]=(wchar_t)(value[0]-32);kind=ROW_VALUE;break;
        case 6:wcsncpy(label,L"Reset this item",cap);break;}
    }else if(page==DEV_PRESETS){if(r<0||r>2)return ROW_INFO;wide(tn_preset_names[r],label,cap);kind=ROW_MENU;
    }else if(page==DEV_PRESET){int p=dev_sel_preset;
        if(r>=0&&r<TN_PRESET_FIELDS){char v[48];wide(tn_preset_fields[r].name,label,cap);tn_preset_text(&tn_draft,r,tn_draft.preset[p][r],v,sizeof v);if(v[0]>='a'&&v[0]<='z')v[0]=(char)(v[0]-32);wide(v,value,cap);kind=ROW_VALUE;}
        else wcsncpy(label,L"Reset this preset",cap);
    }else if(page==DEV_LOD){int n=tn_draft.lod_n;
        if(r==0){wcsncpy(label,L"Number of steps",cap);swprintf(value,cap,L"%d",n);kind=ROW_VALUE;}
        else if(r<n){char v[16];tn_lod_name(tn_draft.lod[r],v,sizeof v);swprintf(label,cap,L"Step %d",r+1);swprintf(value,cap,L"%hs  (%d%%)",v,tn_draft.lod[r]);kind=ROW_VALUE;}
        else if(r==n){char v[16];tn_lod_name(tn_draft.lod[tn_draft.lod_safe],v,sizeof v);wcsncpy(label,L"Recommended maximum",cap);wide(v,value,cap);kind=ROW_SLIDER;}
        else wcsncpy(label,L"Reset level of detail",cap);
    }else if(page==DEV_CHANGES){if(!dev_lines.n){wcsncpy(label,L"Nothing changed yet",cap);return ROW_INFO;}if(r>=0&&r<dev_lines.n)wide(dev_lines.line[r],label,cap);kind=ROW_INFO;}
    label[cap-1]=value[cap-1]=0;return kind;
}
static int dev_slider(int r,int*steps,int*pos,int*safe){if(page!=DEV_LOD||r!=tn_draft.lod_n)return 0;*steps=tn_draft.lod_n;*pos=*safe=tn_draft.lod_safe;return 1;}
static const wchar_t*dev_help(void){
    static wchar_t text[TN_HELP+200];int r=selected;dev_sync();
    if(page==DEV_PAGE){static const wchar_t*h[]={
        L"Names, help text, order and pages of every launcher menu item.",
        L"The level-of-detail steps on Options > Display, and the recommended maximum.",
        L"What Recommended, Classic look and High end set (Options > Display > Preset and the first-run page).",
        L"Everything that differs from the built-in values (your draft included).",
        L"Writes tuning.ini. The launcher uses it at once, the game from its next start.\nEach change is listed in tuning-changes.log.",
        L"Throws away everything changed since the last save.",
        L"Takes back the last change (unsaved changes only).",
        L"Puts every setting back to the built-in value, in the draft. Save to keep it.",
        L"Loads another tuning file into the editor. Save to use it.",
        L"Writes the editor's settings (unsaved changes included) to a file of your choice.",
        L"Opens tuning-changes.log: every saved change, with its date.",
        L"Saves a bug report: a picture of the launcher, the recent logs and your settings, with a short note, in the reports folder."};
        return r>=0&&r<12?h[r]:L"";}
    if(page==DEV_PRESETS)return L"Choose a preset to change what it sets. Players pick presets on Options > Display and on the first-run page.";
    if(page==DEV_PRESET){if(r<TN_PRESET_FIELDS)return r==8?L"Left / Right change it. \"Recommended maximum\" follows the level-of-detail page.":L"Left / Right change the value this preset gives the setting.";
        return L"This preset's values back to the built-in ones.";}
    if(page==DEV_MENUS)return L"Choose a page to rename its items, change their help text, put them in another order or move them to another page.";
    if(page==DEV_PAGE_ITEMS){if(r==0)return L"The big heading of this page. Enter changes it.";
        if(r-1<tn_draft.count[dev_sel_page]){int i=tn_draft.order[dev_sel_page][r-1];char v[TN_HELP+120];snprintf(v,sizeof v,"Enter edits this item. Left / Right move it up or down.\n\nHelp text: %s",tn_draft.help[i]);wide(v,text,sizeof text/sizeof text[0]);return text;}return L"";}
    if(page==DEV_ITEM){int i=dev_sel_item;switch(r){
        case 0:{char v[200];snprintf(v,sizeof v,"The name on the menu bar. Enter changes it.\nBuilt-in: %s",tn_items[i].label);wide(v,text,sizeof text/sizeof text[0]);return text;}
        case 1:{char v[TN_HELP+40];snprintf(v,sizeof v,"%s\n\n(Enter changes it.)",tn_draft.help[i]);wide(v,text,sizeof text/sizeof text[0]);return text;}
        case 2:return L"Left / Right move the item up or down on its page.";
        case 3:return tn_items[i].flags&TN_FIXED?L"This item always stays on its own page.":L"Left / Right move the item to another page. It goes to the end of that page.";
        case 4:return tn_items[i].flags&TN_FIXED?L"This item is always shown.":L"Hidden items are not on the menu. An item that opens a page cannot hide that page for good: it comes back if nothing else leads there.";
        case 5:return L"Left / Right choose the picture shown for this item (the card on the right shows it). Pictures come from your own game disc.\nAutomatic: the launcher's own choice.";
        case 6:return L"Name, help, picture, page and position back to the built-in values.";}}
    if(page==DEV_LOD){int n=tn_draft.lod_n;
        if(r==0)return L"How many steps the Level of detail slider has (2 to 12). Right adds a step at the end, Left removes the last one.";
        if(r<n)return L"How far away riders and bikes keep full detail at this step, as a multiple of the original game's distance. Left / Right change it in steps of 25%.";
        if(r==n)return L"The highest step that holds 60 FPS everywhere. Steps above it show a warning in red.";
        return L"Back to the built-in steps.";}
    if(page==DEV_CHANGES){if(r>=0&&r<dev_lines.n){wide(dev_lines.line[r],text,sizeof text/sizeof text[0]);return text;}return L"Your settings are all the built-in ones.";}
    return L"";
}
static void dev_move(int item,int delta){int pos,p=tn_where(&tn_draft,item,&pos);if(p<0)return;int to=pos+delta;if(to<0||to>=tn_draft.count[p])return;dev_push("move");tn_move(&tn_draft,item,p,to);}
static void dev_action(void){
    dev_sync();int r=selected;
    if(page==DEV_PAGE){
        switch(r){
        case 0:open_page(DEV_MENUS);break;case 1:open_page(DEV_LOD);break;case 2:open_page(DEV_PRESETS);break;case 3:open_page(DEV_CHANGES);break;
        case 4:dev_save();break;
        case 5:if(dev_dirty()&&MessageBoxW(mainwindow,L"Throw away the unsaved changes?",L"Developer",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)dev_discard();break;
        case 6:if(dev_undo_n){dev_undo_n--;tn_draft=*dev_undo[dev_undo_n];free(dev_undo[dev_undo_n]);notice(L"UNDONE");}else notice(L"NOTHING TO UNDO");break;
        case 7:if(MessageBoxW(mainwindow,L"Put every menu setting and the level-of-detail steps back to the built-in values?\n\nThis changes the draft only; Save to keep it.",L"Developer",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES){dev_push("reset everything");tn_draft=*tn_builtin();notice(L"EVERYTHING RESET IN THE DRAFT — SAVE CHANGES TO KEEP IT");}break;
        case 8:dev_import();break;case 9:dev_export();break;
        case 10:{wchar_t log[32768];join(log,store,L"tuning-changes.log");if(exists(log))ShellExecuteW(mainwindow,L"open",log,NULL,NULL,SW_SHOWNORMAL);else notice(L"NO CHANGES SAVED YET");break;}
        case 11:report_problem();break;}
        return;}
    if(page==DEV_PRESETS){if(r>=0&&r<3){dev_sel_preset=r;open_page(DEV_PRESET);}return;}
    if(page==DEV_PRESET){if(r<TN_PRESET_FIELDS)dev_adjust(1);else{dev_push("reset preset");memcpy(tn_draft.preset[dev_sel_preset],tn_builtin()->preset[dev_sel_preset],sizeof tn_draft.preset[0]);}return;}
    if(page==DEV_MENUS){if(r>=0&&r<TN_PAGES){dev_sel_page=r;open_page(DEV_PAGE_ITEMS);}return;}
    if(page==DEV_PAGE_ITEMS){
        if(r==0){wchar_t t[80];swprintf(t,80,L"%hs: page title",tn_pages[dev_sel_page].name);dev_edit_text(t,L"The heading shown at the top of this page:",tn_draft.title[dev_sel_page],TN_TITLE,0,"page title");}
        else if(r-1<tn_draft.count[dev_sel_page]){dev_sel_item=tn_draft.order[dev_sel_page][r-1];open_page(DEV_ITEM);}
        return;}
    if(page==DEV_ITEM){int i=dev_sel_item;
        if(r==0)dev_edit_text(L"Item name",L"The name on the menu bar (up to 47 characters):",tn_draft.label[i],TN_LABEL,0,"name");
        else if(r==1)dev_edit_text(L"Help text",L"The text in the panel on the right. Enter starts a new line; Ctrl+Enter or OK saves.",tn_draft.help[i],TN_HELP,1,"help text");
        else if(r==2)dev_move(i,1);
        else if(r>=3&&r<=5)dev_adjust(1);
        else if(r==6){const RrTuning*d=tn_builtin();dev_push("reset item");tn_copy(tn_draft.label[i],TN_LABEL,d->label[i]);tn_copy(tn_draft.help[i],TN_HELP,d->help[i]);tn_draft.hidden[i]=0;tn_draft.picture[i]=-1;
            int dpos,dp=tn_where(d,i,&dpos);tn_move(&tn_draft,i,dp,dpos);}
        return;}
    if(page==DEV_LOD&&r==tn_draft.lod_n+1){dev_push("reset level of detail");const RrTuning*d=tn_builtin();tn_draft.lod_n=d->lod_n;memcpy(tn_draft.lod,d->lod,sizeof d->lod);tn_draft.lod_safe=d->lod_safe;}
}
static void dev_adjust(int dir){
    dev_sync();int r=selected;
    if(page==DEV_PAGE_ITEMS&&r>=1&&r-1<tn_draft.count[dev_sel_page]){int i=tn_draft.order[dev_sel_page][r-1];int pos;tn_where(&tn_draft,i,&pos);if(pos+(dir>0?1:-1)>=0&&pos+(dir>0?1:-1)<tn_draft.count[dev_sel_page]){dev_move(i,dir>0?1:-1);selected+=dir>0?1:-1;}return;}
    if(page==DEV_ITEM){int i=dev_sel_item;
        if(r==2)dev_move(i,dir>0?1:-1);
        else if(r==3&&!(tn_items[i].flags&TN_FIXED)){int p=tn_where(&tn_draft,i,NULL),q=p;do q=(q+(dir>0?1:TN_PAGES-1))%TN_PAGES;while(q!=p&&tn_draft.count[q]>=TN_ROWS);
            if(q!=p){dev_push("move to another page");tn_move(&tn_draft,i,q,-1);TniLog log;memset(&log,0,sizeof log);tn_fix_layout(&tn_draft,&log);if(log.count)notice(L"THAT WOULD HIDE A PAGE FOR GOOD — THE ITEM STAYS WHERE IT WAS");}}
        else if(r==4&&!(tn_items[i].flags&TN_FIXED)){dev_push(tn_draft.hidden[i]?"show item":"hide item");tn_draft.hidden[i]=!tn_draft.hidden[i];TniLog log;memset(&log,0,sizeof log);tn_fix_layout(&tn_draft,&log);if(log.count)notice(L"THIS ITEM IS THE ONLY WAY TO ITS PAGE, SO IT STAYS SHOWN");}
        else if(r==5){int j=(tn_picture_index(tn_draft.picture[i])+(dir>0?1:TN_PICTURES-1))%TN_PICTURES;dev_push("picture");tn_draft.picture[i]=tn_pictures[j].kind;}
        return;}
    if(page==DEV_PRESET&&r>=0&&r<TN_PRESET_FIELDS){int p=dev_sel_preset,v=tn_draft.preset[p][r],steps=tn_preset_fields[r].steps;
        if(!steps){int n=tn_draft.lod_n;v=v<0?n:v;v=(v+(dir>0?1:n))%(n+1);if(v==n)v=-1;}   /* steps, then "recommended maximum" */
        else v=((v+(dir>0?1:-1))%steps+steps)%steps;
        dev_push("preset value");tn_draft.preset[p][r]=v;return;}
    if(page==DEV_LOD){int n=tn_draft.lod_n;
        if(r==0){if(dir>0&&n<TN_LOD_MAX&&tn_draft.lod[n-1]<3200){int step=tn_draft.lod[n-1]/2/25*25;if(step<25)step=25;int v=tn_draft.lod[n-1]+step;if(v>3200)v=3200;dev_push("add a step");tn_draft.lod[n]=v;tn_draft.lod_n++;}
            else if(dir<0&&n>2){dev_push("remove a step");tn_draft.lod_n--;if(tn_draft.lod_safe>=tn_draft.lod_n)tn_draft.lod_safe=tn_draft.lod_n-1;}}
        else if(r>0&&r<n){int lo=tn_draft.lod[r-1]+25,hi=r+1<n?tn_draft.lod[r+1]-25:3200,v=tn_draft.lod[r]+(dir>0?25:-25);if(v>=lo&&v<=hi){dev_push("change a step");tn_draft.lod[r]=v;}}
        else if(r==n){int k=tn_draft.lod_safe+(dir>0?1:-1);if(k>=0&&k<n){dev_push("recommended maximum");tn_draft.lod_safe=k;}}
    }
}
