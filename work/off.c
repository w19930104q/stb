#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);long fl=ftell(f);fseek(f,0,SEEK_SET);
   unsigned char*b=malloc(fl);if(fread(b,1,fl,f)!=(size_t)fl){}fclose(f);
   int err,used;stb_vorbis*v=stb_vorbis_open_pushdata(b,(int)fl,&used,&err,NULL);
   long pos=used;
   for(;;){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(v,b+pos,(int)(fl-pos),&ch,&o,&s);
     if(u==0&&s==0)break;pos+=u;if(pos>=fl)break;}
   long T=330750;
   stb_vorbis_int64 off=stb_vorbis_seek_pushdata(v,T);
   printf("seek off=%lld liboff=%lld\n",(long long)off,(long long)stb_vorbis_get_pushdata_byte_offset(v));
   pos=off;
   for(int k=0;k<6;k++){
      float**o;int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(v,b+pos,(int)(fl-pos),&ch,&o,&s);
      printf("call %d: used=%d s=%d liboff=%lld expected=%ld\n",k,u,s,(long long)stb_vorbis_get_pushdata_byte_offset(v),pos+u);
      pos+=u;
   }
   return 0;
}
