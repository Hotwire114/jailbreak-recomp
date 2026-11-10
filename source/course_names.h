/* Game strings for save listings. LGPL-2.1-or-later. Independently authored.
 * Read at run time from the owner's installed disc (DATA/FE/FESTRING.LOC, the
 * game's own string table); no game text is included here. Include
 * disc_reader.h first. Course race ids and string indices: TRACK-MODDING.md
 * 1.2 (code mapping at 0x800720B4): 1P race r -> 405+6(r-1); 2P race r ->
 * 789+6i, i = r-1 for r <= 19, r-20 above. */
#ifndef RR_COURSE_NAMES_H
#define RR_COURSE_NAMES_H
/* String <index> of the frontend table, as printable ASCII (Latin-1 letters
 * become '?'); 0 when missing. The table is read once. */
static int rr_fe_string(const wchar_t *disc,int index,char *out,size_t cap){
    static unsigned char *table;static unsigned size;static int tried;
    if(!out||!cap)return 0;out[0]=0;
    if(!tried&&disc){Disc d;Entry root,e;tried=1;
        if(open_disc(disc,&d,&root)&&find_file(&d,root,"/DATA/FE/FESTRING.LOC",&e)){table=read_entry(&d,e);size=table?e.size:0;}
        if(d.fp)fclose(d.fp);}
    if(!table||size<0x28||memcmp(table,"LOCH",4)||memcmp(table+0x14,"LOCL",4)||index<0)return 0;
    unsigned count=le32(table+0x20);
    if((unsigned)index>=count||0x24+4*(size_t)index+4>size)return 0;
    unsigned off=0x14+le32(table+0x24+4*index);if(off>=size)return 0;
    size_t n=0;while(off+n<size&&table[off+n]&&n+1<cap){unsigned char c=table[off+n];out[n]=c>=32&&c<127?(char)c:'?';n++;}
    while(n&&out[n-1]==' ')n--;out[n]=0;return n>0;
}
static int rr_course_name(const wchar_t *disc,int race,int players,char *out,size_t cap){
    int index;
    if(players==2){if(race<1||race>55||(race>18&&race<38))return 0;index=789+6*(race<=19?race-1:race-20);}
    else{if(race<1||race>64)return 0;index=405+6*(race-1);}
    return rr_fe_string(disc,index,out,cap);
}
#endif
