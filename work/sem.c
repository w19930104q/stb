#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long flen=ftell(f); fseek(f,0,SEEK_SET);
   unsigned char*buf=malloc(flen); if(fread(buf,1,flen,f)!=(size_t)flen){} fclose(f);
   int err,used; stb_vorbis*p=stb_vorbis_open_pushdata(buf,(int)flen,&used,&err,NULL);
   long pos=used; int fr=0;
   // sequential: print current_loc before/after each output frame
   while(fr<8){
      int64 loc_before=p->current_loc; int valid=p->current_loc_valid;
      float**o;int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(p,buf+pos,(int)(flen-pos),&ch,&o,&s);
      if(s>0){printf("frame %d: loc_before=%lld valid=%d samples=%d loc_after=%lld  (after-before=%lld)\n",
         fr,(long long)loc_before,valid,s,(long long)p->current_loc,(long long)(p->current_loc-loc_before));fr++;}
      pos+=u; if(pos>=flen)break;
   }
   return 0;
}
