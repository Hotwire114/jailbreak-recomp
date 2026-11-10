/* Road Rash Jailbreak launcher: unpacks the verified build-tool archive.
 * Independently authored; LGPL-2.1-or-later. Written from the ZIP application
 * note and RFC 1951 (deflate), so first-run setup needs no PowerShell, no
 * Windows shell and no tar.exe (none of which exist under Wine).
 *
 * Supports what the toolchain archive uses: stored and deflated entries,
 * ZIP64 sizes and offsets. Every name is checked (no absolute paths, drive
 * letters, backslashes or ".." parts) and must land inside the destination.
 * The archive itself has already passed its size and SHA-256 check. */
#ifndef RR_ZIP_EXTRACT_H
#define RR_ZIP_EXTRACT_H

/* ---- Inflate ----------------------------------------------------------- */
typedef struct {short count[16],symbol[288];} ZxHuff;
typedef struct {const unsigned char*in;size_t inlen,inpos;unsigned long bitbuf;int bitcnt,err;unsigned char*out;size_t outlen,outpos;} ZxInf;
static int zx_bits(ZxInf*s,int need){
    unsigned long val=s->bitbuf;
    while(s->bitcnt<need){if(s->inpos>=s->inlen){s->err=1;return 0;}val|=(unsigned long)s->in[s->inpos++]<<s->bitcnt;s->bitcnt+=8;}
    s->bitbuf=val>>need;s->bitcnt-=need;return (int)(val&((1UL<<need)-1));
}
static int zx_decode(ZxInf*s,const ZxHuff*h){
    int code=0,first=0,index=0;
    for(int len=1;len<=15;len++){code|=zx_bits(s,1);if(s->err)return -1;int count=h->count[len];
        if(code-count<first)return h->symbol[index+(code-first)];index+=count;first+=count;first<<=1;code<<=1;}
    s->err=1;return -1;
}
/* Canonical Huffman code from code lengths; returns <0 for an over-subscribed set. */
static int zx_construct(ZxHuff*h,const short*length,int n){
    short offs[16];for(int len=0;len<16;len++)h->count[len]=0;
    for(int sym=0;sym<n;sym++)h->count[length[sym]]++;
    if(h->count[0]==n)return 0;
    int left=1;for(int len=1;len<16;len++){left<<=1;left-=h->count[len];if(left<0)return left;}
    offs[1]=0;for(int len=1;len<15;len++)offs[len+1]=(short)(offs[len]+h->count[len]);
    for(int sym=0;sym<n;sym++)if(length[sym])h->symbol[offs[length[sym]]++]=(short)sym;
    return left;
}
static void zx_codes(ZxInf*s,const ZxHuff*lencode,const ZxHuff*distcode){
    static const short lbase[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const short lext[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const short dbase[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const short dext[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    for(;;){int sym=zx_decode(s,lencode);if(s->err)return;
        if(sym<256){if(s->outpos>=s->outlen){s->err=1;return;}s->out[s->outpos++]=(unsigned char)sym;}
        else if(sym==256)return;
        else{sym-=257;if(sym>=29){s->err=1;return;}size_t len=(size_t)lbase[sym]+(size_t)zx_bits(s,lext[sym]);
            int d=zx_decode(s,distcode);if(s->err||d<0||d>=30){s->err=1;return;}size_t dist=(size_t)dbase[d]+(size_t)zx_bits(s,dext[d]);
            if(s->err||dist>s->outpos||len>s->outlen-s->outpos){s->err=1;return;}
            for(size_t k=0;k<len;k++){s->out[s->outpos]=s->out[s->outpos-dist];s->outpos++;}}
    }
}
static int zx_inflate(const unsigned char*in,size_t inlen,unsigned char*out,size_t outlen){
    ZxInf s={in,inlen,0,0,0,0,out,outlen,0};static ZxHuff flen,fdist;static int fixed_ready;
    if(!fixed_ready){short l[288];int i=0;for(;i<144;i++)l[i]=8;for(;i<256;i++)l[i]=9;for(;i<280;i++)l[i]=7;for(;i<288;i++)l[i]=8;zx_construct(&flen,l,288);
        for(i=0;i<30;i++)l[i]=5;zx_construct(&fdist,l,30);fixed_ready=1;}
    int last;
    do{last=zx_bits(&s,1);int type=zx_bits(&s,2);if(s.err)return 0;
        if(type==0){s.bitbuf=0;s.bitcnt=0;if(s.inpos+4>s.inlen)return 0;
            unsigned len=s.in[s.inpos]|s.in[s.inpos+1]<<8,nlen=s.in[s.inpos+2]|s.in[s.inpos+3]<<8;s.inpos+=4;
            if(len!=(~nlen&0xffff)||len>s.inlen-s.inpos||len>s.outlen-s.outpos)return 0;
            memcpy(s.out+s.outpos,s.in+s.inpos,len);s.inpos+=len;s.outpos+=len;}
        else if(type==1)zx_codes(&s,&flen,&fdist);
        else if(type==2){static const short order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};short lengths[320];ZxHuff lencode,distcode;
            int nlen=zx_bits(&s,5)+257,ndist=zx_bits(&s,5)+1,ncode=zx_bits(&s,4)+4;if(s.err||nlen>286||ndist>30)return 0;
            int i=0;for(;i<ncode;i++)lengths[order[i]]=(short)zx_bits(&s,3);for(;i<19;i++)lengths[order[i]]=0;
            if(zx_construct(&lencode,lengths,19)!=0)return 0;
            for(i=0;i<nlen+ndist;){int sym=zx_decode(&s,&lencode);if(s.err)return 0;
                if(sym<16)lengths[i++]=(short)sym;
                else{short len=0;int rep;if(sym==16){if(!i)return 0;len=lengths[i-1];rep=3+zx_bits(&s,2);}else if(sym==17)rep=3+zx_bits(&s,3);else rep=11+zx_bits(&s,7);
                    if(s.err||i+rep>nlen+ndist)return 0;while(rep--)lengths[i++]=len;}}
            if(lengths[256]==0)return 0;
            int err=zx_construct(&lencode,lengths,nlen);if(err<0||(err>0&&nlen-lencode.count[0]!=1))return 0;
            err=zx_construct(&distcode,lengths+nlen,ndist);if(err<0||(err>0&&ndist-distcode.count[0]!=1))return 0;
            zx_codes(&s,&lencode,&distcode);}
        else return 0;
        if(s.err)return 0;
    }while(!last);
    return s.outpos==s.outlen;
}

/* ---- ZIP --------------------------------------------------------------- */
static unsigned zx16(const unsigned char*p){return p[0]|p[1]<<8;}
static unsigned zx32(const unsigned char*p){return p[0]|p[1]<<8|p[2]<<16|(unsigned)p[3]<<24;}
static unsigned long long zx64(const unsigned char*p){return zx32(p)|(unsigned long long)zx32(p+4)<<32;}
/* A safe relative name ("a/b/c"); 0 if it could leave the destination. */
static int zx_safe_name(const char*n){
    if(!n[0]||n[0]=='/'||strchr(n,'\\')||strchr(n,':'))return 0;
    for(const char*p=n;*p;){const char*e=strchr(p,'/');size_t k=e?(size_t)(e-p):strlen(p);if(k==2&&p[0]=='.'&&p[1]=='.')return 0;if(!e)break;p=e+1;}
    return 1;
}
/* Creates every folder of an extended-length ("\\?\C:\...") path; the shell's
 * helpers stop at 260 characters, and toolchain files nest deeper than that. */
static void zx_mkdirs(wchar_t*path){
    for(wchar_t*p=path+7;*p;p++)if(*p==L'\\'){*p=0;CreateDirectoryW(path,NULL);*p=L'\\';}
    CreateDirectoryW(path,NULL);
}
/* Unpacks archive into dest (an absolute path, created as needed). progress(done,total) may be NULL.
 * Returns 1 on success; on failure writes a reason into why. */
static int zip_extract(const wchar_t*archive,const wchar_t*dest,void(*progress)(unsigned,unsigned),char*why,size_t why_cap){
    int ok=0;HANDLE file=CreateFileW(archive,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL),map=NULL;const unsigned char*z=NULL;
    snprintf(why,why_cap,"the archive could not be opened");
    if(file==INVALID_HANDLE_VALUE)return 0;
    LARGE_INTEGER size;if(!GetFileSizeEx(file,&size)||size.QuadPart<22)goto done;
    map=CreateFileMappingW(file,NULL,PAGE_READONLY,0,0,NULL);if(!map)goto done;
    z=(const unsigned char*)MapViewOfFile(map,FILE_MAP_READ,0,0,0);if(!z)goto done;
    {const size_t n=(size_t)size.QuadPart;size_t eocd=0;int found=0;
     for(size_t p=n-22;;p--){if(zx32(z+p)==0x06054b50){eocd=p;found=1;break;}if(p==0||n-p>65557)break;}
     snprintf(why,why_cap,"the archive is not a valid zip");if(!found)goto done;
     unsigned long long entries=zx16(z+eocd+10),cd=zx32(z+eocd+16);
     if((entries==0xffff||cd==0xffffffffu)&&eocd>=20&&zx32(z+eocd-20)==0x07064b50){unsigned long long z64=zx64(z+eocd-20+8);
         if(z64+56>n||zx32(z+z64)!=0x06064b50)goto done;entries=zx64(z+z64+32);cd=zx64(z+z64+48);}
     size_t p=(size_t)cd;
     for(unsigned long long e=0;e<entries;e++){
        if(p+46>n||zx32(z+p)!=0x02014b50)goto done;
        unsigned method=zx16(z+p+10),nl=zx16(z+p+28),xl=zx16(z+p+30),cl=zx16(z+p+32);
        unsigned long long csize=zx32(z+p+20),usize=zx32(z+p+24),local=zx32(z+p+42);
        if(p+46+nl+xl+cl>n)goto done;
        for(size_t x=p+46+nl;x+4<=p+46+nl+xl;){unsigned id=zx16(z+x),len=zx16(z+x+2);if(x+4+len>p+46+nl+xl)break;
            if(id==1){size_t q=x+4;if(usize==0xffffffffu&&q+8<=x+4+len){usize=zx64(z+q);q+=8;}if(csize==0xffffffffu&&q+8<=x+4+len){csize=zx64(z+q);q+=8;}if(local==0xffffffffu&&q+8<=x+4+len)local=zx64(z+q);}
            x+=4+len;}
        char name[1024];if(nl>=sizeof name)goto done;memcpy(name,z+p+46,nl);name[nl]=0;p+=46+nl+xl+cl;
        if(!zx_safe_name(name)){snprintf(why,why_cap,"unsafe name in the archive: %s",name);goto done;}
        wchar_t wname[1024],path[32768];if(!MultiByteToWideChar(CP_UTF8,0,name,-1,wname,1024))goto done;
        for(wchar_t*c=wname;*c;c++)if(*c==L'/')*c=L'\\';
        swprintf(path,32768,L"\\\\?\\%ls\\%ls",dest,wname);
        {size_t k=wcslen(path);while(k&&path[k-1]==L'\\')path[--k]=0;}
        if(nl&&name[nl-1]=='/'){zx_mkdirs(path);continue;}
        {wchar_t dir[32768];wcscpy(dir,path);wchar_t*slash=wcsrchr(dir,L'\\');if(slash){*slash=0;zx_mkdirs(dir);}}
        if(local+30>n||zx32(z+local)!=0x04034b50)goto done;
        size_t data=(size_t)local+30+zx16(z+local+26)+zx16(z+local+28);
        if(data>n||csize>n-data||usize>0x7fffffffull)goto done;
        unsigned char*out=(unsigned char*)malloc(usize?(size_t)usize:1);if(!out){snprintf(why,why_cap,"out of memory");goto done;}
        int good=method==0?(csize==usize&&(memcpy(out,z+data,(size_t)usize),1)):method==8?zx_inflate(z+data,(size_t)csize,out,(size_t)usize):0;
        if(!good){free(out);snprintf(why,why_cap,"could not unpack %s",name);goto done;}
        HANDLE w=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);DWORD wrote=0;
        int saved=w!=INVALID_HANDLE_VALUE&&WriteFile(w,out,(DWORD)usize,&wrote,NULL)&&wrote==(DWORD)usize;if(w!=INVALID_HANDLE_VALUE)CloseHandle(w);free(out);
        if(!saved){snprintf(why,why_cap,"could not write %s (disk full?)",name);goto done;}
        if(progress&&(e%64==0||e+1==entries))progress((unsigned)(e+1),(unsigned)entries);
     }
     ok=1;why[0]=0;}
done:
    if(z)UnmapViewOfFile(z);if(map)CloseHandle(map);CloseHandle(file);return ok;
}
#endif
