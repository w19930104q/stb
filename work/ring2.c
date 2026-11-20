#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
#define CAP 262144
#define CHUNK 4096
static unsigned char*file;static long fl;
static stb_vorbis*open_drain(void){
   int err,used;stb_vorbis*v=stb_vorbis_open_pushdata(file,(int)fl,&used,&err,NULL);
   long pos=used;
   for(;;){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(v,file+pos,(int)(fl-pos),&ch,&o,&s);
     if(u==0&&s==0)break;pos+=u;if(pos>=fl)break;}
   return v;
}
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);fl=ftell(f);fseek(f,0,SEEK_SET);
   file=malloc(fl);if(fread(file,1,fl,f)!=(size_t)fl){}fclose(f);
   stb_vorbis*v=open_drain();
   long targets[]={330750,165375,496125,1000,0,652500,220500};
   for(int t=0;t<7;t++){
      long T=targets[t];
      stb_vorbis_int64 off=stb_vorbis_seek_pushdata(v,T);
      long pos=off,buflen=0;unsigned char buf[CAP];int bad=0;
      for(int k=0;k<400 && !bad;k++){
         if(buflen<CAP&&pos+buflen<fl){long want=CAP-buflen,avail=fl-(pos+buflen),n=want<avail?want:avail;if(n>CHUNK)n=CHUNK;
            memcpy(buf+buflen,file+pos+buflen,n);buflen+=n;}
         if(buflen==0)break;
         float**o;int ch=0,s=0;
         int u=stb_vorbis_decode_frame_pushdata(v,buf,(int)buflen,&ch,&o,&s);
         stb_vorbis_int64 lo=stb_vorbis_get_pushdata_byte_offset(v);
         if(lo!=-1 && lo!=pos+u){printf("T=%ld MISMATCH k=%d used=%d liboff=%lld expected=%ld crc=%d\n",
            T,k,u,(long long)lo,pos+u,v->page_crc_tests);bad=1;}
         if(u>0){memmove(buf,buf+u,buflen-u);buflen-=u;pos+=u;}
      }
      if(!bad)printf("T=%ld OK\n",T);
   }
   return 0;
}
