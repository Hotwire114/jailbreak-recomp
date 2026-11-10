/* Independently implemented bounded BS-v2 still-image decoder.
 * Generic MPEG tables in mpeg_tables.h are LGPL-2.1-or-later.
 * This decoder is supplied under the same licence; it contains no game code.
 * Format references: FFmpeg libavcodec/mdec.c; psx-spx MDEC documentation.
 * It decodes data, never executes PlayStation CPU instructions. */
#include <math.h>
#include "mpeg_tables.h"
typedef struct {const unsigned char *bytes;size_t size,bit;int error;} Bits;
static unsigned bits(Bits *b,unsigned n){
    unsigned result=0;
    if(n>24||b->bit+n>b->size*8){b->error=1;return 0;}
    for(unsigned i=0;i<n;i++){
        size_t byte=(b->bit/8)^1u;
        if(byte>=b->size){b->error=1;return 0;}
        result=(result<<1)|((b->bytes[byte]>>(7-(b->bit&7)))&1);b->bit++;
    }return result;
}
static int signed_bits(Bits *b,unsigned n){unsigned v=bits(b,n);return v&(1u<<(n-1))?(int)v-(int)(1u<<n):(int)v;}
static int vlc(Bits *b){
    unsigned code=0;
    for(unsigned length=1;length<=16;length++){
        code=(code<<1)|bits(b,1);if(b->error)return -1;
        for(unsigned i=0;i<113;i++)if(ff_mpeg1_vlc_table[i][1]==length&&ff_mpeg1_vlc_table[i][0]==code)return (int)i;
    }b->error=1;return -1;
}
static const unsigned char scan[64]={
    0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,
    12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
static double basis[8][8];static int basis_ready;
static int decode_block(Bits *b,int q,double *out){
    double coefficient[64]={0},pass[64];
    coefficient[0]=2*signed_bits(b,10)+1024;unsigned position=0;
    for(unsigned count=0;count<64;count++){
        int symbol=vlc(b);if(symbol<0)return 0;if(symbol==112)break;
        int run,level,escape=symbol==111;
        if(escape){run=(int)bits(b,6);level=signed_bits(b,10);}
        else{run=ff_mpeg12_run[symbol];level=ff_mpeg12_level[symbol];if(bits(b,1))level=-level;}
        position+=(unsigned)run+1;
        if(b->error||position>=64)return 0;
        unsigned slot=scan[position];int value=(abs(level)*q*ff_mpeg1_default_intra_matrix[slot])>>3;
        if(escape)value=(value-1)|1;
        coefficient[slot]=level<0?-value:value;
        if(count==63)return 0;
    }
    if(b->error)return 0;
    if(!basis_ready){for(int x=0;x<8;x++)for(int u=0;u<8;u++)basis[x][u]=.5*(u?1:.7071067811865475)*cos((2*x+1)*u*3.14159265358979323846/16);basis_ready=1;}
    for(int v=0;v<8;v++)for(int x=0;x<8;x++){double sum=0;for(int u=0;u<8;u++)sum+=coefficient[v*8+u]*basis[x][u];pass[v*8+x]=sum;}
    for(int y=0;y<8;y++)for(int x=0;x<8;x++){double sum=0;for(int v=0;v<8;v++)sum+=pass[v*8+x]*basis[y][v];out[y*8+x]=sum;}
    return 1;
}
static unsigned clamp_channel(double d){return d<0?0:d>255?255:(unsigned)(d+.5);}
static unsigned read_be16(const unsigned char *p){return ((unsigned)p[0]<<8)|p[1];}
static int decode_mdec(const unsigned char *data,size_t size,Image *image){
    if(size<24||memcmp(data,"MDEC",4))return fail("Not an MDEC image container");
    unsigned length=be32(data+4),w=read_be16(data+8),h=read_be16(data+10);
    unsigned q=le16(data+20),version=le16(data+22);
    if(length<24||length>size||!w||!h||w>1024||h>1024||q>63||version!=2)return fail("Unsupported or truncated BS-v2 image");
    Bits b={data+24,length-24,0,0};
    image->pixels=(uint32_t*)calloc((size_t)w*h,4);if(!image->pixels)return fail("Cannot allocate disc artwork");image->w=w;image->h=h;
    double block[6][64];
    for(unsigned mx=0;mx<(w+15)/16;mx++)for(unsigned my=0;my<(h+15)/16;my++){
        for(unsigned n=0;n<6;n++)if(!decode_block(&b,(int)q,block[n])){free(image->pixels);memset(image,0,sizeof(*image));return fail("Invalid MDEC coefficient stream");}
        for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++){
            unsigned px=mx*16+x,py=my*16+y;if(px>=w||py>=h)continue;
            unsigned chroma=(y/2)*8+x/2,luma=2+(y/8)*2+x/8;
            double yy=block[luma][(y&7)*8+(x&7)],cr=block[0][chroma]-128,cb=block[1][chroma]-128;
            image->pixels[(size_t)py*w+px]=(clamp_channel(yy+1.402*cr)<<16)|(clamp_channel(yy-.344136*cb-.714136*cr)<<8)|clamp_channel(yy+1.772*cb);
        }
    }
    return 1;
}
