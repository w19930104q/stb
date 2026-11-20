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
   printf("T=%ld off=%lld\n",target,(long long)off);
   pos=off; int fr=0;
   while(fr<25){
      int64 loc=p->current_loc;int cv=p->current_loc_valid;
      float**o;int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(p,fb+pos,(int)(fl-pos),&ch,&o,&s);
      if(s>0){long start=p->current_loc-s;
        printf("fr%2d OUT s=%4d frame_start=%ld frame_end=%ld %s\n",fr,s,start,(long)p->current_loc,
          (start<=target&&target<p->current_loc)?"<-- CONTAINS TARGET":"");
        fr++;}
      pos+=u;if(pos>=fl)break;
   }
   return 0;
}
