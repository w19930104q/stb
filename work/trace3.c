#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long flen=ftell(f); fseek(f,0,SEEK_SET);
   unsigned char*buf=malloc(flen); if(fread(buf,1,flen,f)!=(size_t)flen){} fclose(f);
   int err,used; stb_vorbis*p=stb_vorbis_open_pushdata(buf,(int)flen,&used,&err,NULL);
   long pos=used;
   for(;;){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(p,buf+pos,(int)(flen-pos),&ch,&o,&s);
      if(u==0&&s==0)break;pos+=u;if(pos>=flen)break;}
   long target=atol(argv[2]);
   stb_vorbis_int64 off=stb_vorbis_seek_pushdata(p,target);
   pos=off; int fr=0;
   while(fr<40){
      float**o;int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(p,buf+pos,(int)(flen-pos),&ch,&o,&s);
      if(s>0){printf("frame %d: FIRST OUTPUT samples=%d loc=%lld active=%d\n",fr,s,(long long)p->current_loc,p->push_seek_active);
         printf("  output[0][0]=%.9g\n",o[0][0]); break;}
      pos+=u; fr++; if(pos>=flen)break;
   }
   return 0;
}
