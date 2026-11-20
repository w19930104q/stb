#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
#define CAP 262144
#define CHUNK 4096
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);long fl=ftell(f);fseek(f,0,SEEK_SET);
   unsigned char*file=malloc(fl);if(fread(file,1,fl,f)!=(size_t)fl){}fclose(f);
   int err,used;stb_vorbis*v=stb_vorbis_open_pushdata(file,(int)fl,&used,&err,NULL);
   long pos=used;
   for(;;){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(v,file+pos,(int)(fl-pos),&ch,&o,&s);
     if(u==0&&s==0)break;pos+=u;if(pos>=fl)break;}
   long T=330750;
   stb_vorbis_int64 off=stb_vorbis_seek_pushdata(v,T);
   pos=off; long buflen=0; unsigned char buf[CAP];
   for(int k=0;k<12;k++){
      if(buflen<CAP && pos+buflen<fl){long want=CAP-buflen,avail=fl-(pos+buflen),n=want<avail?want:avail;if(n>CHUNK)n=CHUNK;
         memcpy(buf+buflen,file+pos+buflen,n);buflen+=n;}
      float**o;int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(v,buf,(int)buflen,&ch,&o,&s);
      stb_vorbis_int64 lo=stb_vorbis_get_pushdata_byte_offset(v);
      printf("call %d: used=%d s=%d liboff=%lld expected=%ld %s\n",k,u,s,(long long)lo,pos+u,lo==pos+u?"OK":"MISMATCH");
      if(u>0){memmove(buf,buf+u,buflen-u);buflen-=u;pos+=u;}
   }
   return 0;
}
