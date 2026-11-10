/* Native Windows disc/asset probe. Independently written; includes no game data. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct {FILE *fp;uint64_t sectors;unsigned stride;uint32_t volume;} Disc;
typedef struct {uint32_t lba,size;unsigned flags;} Entry;
typedef struct {unsigned w,h;uint32_t *pixels;} Image;
static char error_text[240];

static uint16_t le16(const unsigned char *b){return (uint16_t)(b[0]|b[1]<<8);}
static uint32_t le32(const unsigned char *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static uint32_t be32(const unsigned char *b){return (uint32_t)b[3]|(uint32_t)b[2]<<8|(uint32_t)b[1]<<16|(uint32_t)b[0]<<24;}
static int fail(const char *s){snprintf(error_text,sizeof(error_text),"%s",s);return 0;}
static int sector(Disc *d,uint32_t lba,unsigned char *out){
    unsigned char raw[2352];unsigned off;
    if(lba>=d->sectors||_fseeki64(d->fp,(int64_t)lba*d->stride,SEEK_SET)||fread(raw,1,d->stride,d->fp)!=d->stride)return fail("Sector is truncated or out of bounds");
    if(d->stride==2048){memcpy(out,raw,2048);return 1;}
    if(raw[0]||raw[11])return fail("Invalid CD sync");
    for(unsigned i=1;i<11;i++)if(raw[i]!=255)return fail("Invalid CD sync");
    if(raw[15]==1)off=16;
    else if(raw[15]==2){if(memcmp(raw+16,raw+20,4)||raw[18]&32)return fail("Unsupported XA Form2 or invalid subheader");off=24;}
    else return fail("Invalid raw sector mode");
    memcpy(out,raw+off,2048);return 1;
}
static int parse_record(const unsigned char *r,unsigned length,Entry *e){
    if(length<34||r[0]!=length||33u+r[32]>length||r[1]||r[26]||r[27])return fail("Invalid or unsupported ISO record");
    e->lba=le32(r+2);e->size=le32(r+10);e->flags=r[25];
    if(e->lba!=be32(r+6)||e->size!=be32(r+14))return fail("ISO endian copies disagree");
    if(e->flags&128)return fail("Multi-extent ISO is unsupported");return 1;
}
static unsigned char *read_entry(Disc *d,Entry e){
    if(e.size>32*1024*1024u||(uint64_t)e.lba+((uint64_t)e.size+2047)/2048>d->volume){fail("Resource too large or outside volume");return NULL;}
    unsigned char *data=(unsigned char*)malloc(e.size?e.size:1),block[2048];
    if(!data){fail("Out of memory");return NULL;}
    for(uint32_t p=0;p<e.size;p+=2048){if(!sector(d,e.lba+p/2048,block)){free(data);return NULL;}unsigned n=e.size-p;if(n>2048)n=2048;memcpy(data+p,block,n);}
    return data;
}
static int find_file(Disc *d,Entry root,const char *path,Entry *out){
    if(*path=='/')path++;
    const char *slash=strchr(path,'/');size_t name_len=slash?(size_t)(slash-path):strlen(path);
    if(!name_len||name_len>255||!(root.flags&2)||root.size>16*1024*1024)return fail("Invalid resource path or directory");
    unsigned char *data=read_entry(d,root);if(!data)return 0;
    for(uint32_t pos=0;pos<root.size;){
        unsigned len=data[pos];if(!len){pos=(pos/2048+1)*2048;continue;}
        if(pos%2048+len>2048||pos+len>root.size){free(data);return fail("Invalid directory boundary");}
        Entry e;if(!parse_record(data+pos,len,&e)){free(data);return 0;}
        unsigned n=data[pos+32];const unsigned char *nm=data+pos+33;for(unsigned j=0;j<n;j++)if(nm[j]==';'){n=j;break;}
        if(n==name_len&&_strnicmp((const char*)nm,path,n)==0){free(data);if(slash)return find_file(d,e,slash+1,out);*out=e;return 1;}
        pos+=len;
    }
    free(data);return fail("Resource was not found on disc");
}
static int open_disc(const wchar_t *path,Disc *d,Entry *root){
    memset(d,0,sizeof(*d));d->fp=_wfopen(path,L"rb");if(!d->fp)return fail("Cannot open disc file");
    if(_fseeki64(d->fp,0,SEEK_END))return fail("Cannot seek disc");int64_t bytes=_ftelli64(d->fp);unsigned char pvd[2048];
    for(unsigned stride=2048;stride<=2352;stride+=304){
        if(bytes<=0||(uint64_t)bytes%stride)continue;d->stride=stride;d->sectors=(uint64_t)bytes/stride;
        if(sector(d,16,pvd)&&pvd[0]==1&&!memcmp(pvd+1,"CD001\1",6)){
            d->volume=le32(pvd+80);
            if(d->volume!=be32(pvd+84)||d->volume>d->sectors||le16(pvd+128)!=2048||pvd[130]!=8||pvd[131]!=0)return fail("Invalid ISO dimensions");
            return parse_record(pvd+156,pvd[156],root);
        }
    }
    return fail("No supported ISO9660 volume; select extracted single-track BIN or ISO");
}
static int sha256(const unsigned char *data,unsigned size,char *hex){
    unsigned char digest[32];BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)return fail("SHA256 provider failed");
    NTSTATUS status=BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0);
    if(status>=0)status=BCryptHashData(hash,(PUCHAR)data,size,0);
    if(status>=0)status=BCryptFinishHash(hash,digest,sizeof(digest),0);
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)return fail("SHA256 failed");for(unsigned i=0;i<32;i++)sprintf(hex+i*2,"%02x",digest[i]);return 1;
}
static inline int save_bmp(const wchar_t *path,Image *image){
    FILE *fp=_wfopen(path,L"wb");if(!fp)return fail("Cannot write snapshot");
    BITMAPFILEHEADER fh={0};BITMAPINFOHEADER ih={0};fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(ih);fh.bfSize=fh.bfOffBits+image->w*image->h*4;
    ih.biSize=sizeof(ih);ih.biWidth=(LONG)image->w;ih.biHeight=-(LONG)image->h;ih.biPlanes=1;ih.biBitCount=32;ih.biCompression=BI_RGB;ih.biSizeImage=image->w*image->h*4;
    int ok=fwrite(&fh,sizeof(fh),1,fp)==1&&fwrite(&ih,sizeof(ih),1,fp)==1&&fwrite(image->pixels,ih.biSizeImage,1,fp)==1;
    if(fclose(fp))ok=0;return ok?1:fail("Snapshot write failed");
}
