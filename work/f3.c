#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
static uint8 *fb; static long fl;
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);fl=ftell(f);fseek(f,0,SEEK_SET);
   fb=malloc(fl);if(fread(fb,1,fl,f)!=(size_t)fl){}fclose(f);
   int err,used;stb_vorbis*p=stb_vorbis_open_pushdata(fb,(int)fl,&used,&err,NULL);
   long pos=used;for(;;){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(p,fb+pos,(int)(fl-pos),&ch,&o,&s);
     if(u==0&&s==0)break;pos+=u;if(pos>=fl)break;}
   long target=atol(argv[2]);
   stb_vorbis_int64 off=stb_vorbis_seek_pushdata(p,target);
   pos=off; int fr=0;
   while(fr<6){
      int pl=p->previous_length;
      float**o;int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(p,fb+pos,(int)(fl-pos),&ch,&o,&s);
      printf("fr%d: prevlen_in=%d raw_s=%d loc=%lld\n",fr,pl,s,(long long)p->current_loc);
      pos+=u;fr++;if(pos>=fl)break;
   }
   return 0;
}
