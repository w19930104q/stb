#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
static uint8 *fb; static long fl;
static float *ref[8]; static long rn;
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); fl=ftell(f); fseek(f,0,SEEK_SET);
   fb=malloc(fl); if(fread(fb,1,fl,f)!=(size_t)fl){} fclose(f);
   int err,used; stb_vorbis*p=stb_vorbis_open_pushdata(fb,(int)fl,&used,&err,NULL);
   long pos=used;
   for(;;){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(p,fb+pos,(int)(fl-pos),&ch,&o,&s);
      if(u==0&&s==0)break;pos+=u;if(pos>=fl)break;}
   stb_vorbis*v=stb_vorbis_open_memory(fb,(int)fl,&err,NULL);
   int chn=stb_vorbis_get_info(v).channels; long cap=stb_vorbis_stream_length_in_samples(v)+65536,n=0;
   for(int c=0;c<chn;c++)ref[c]=malloc(cap*4);
   for(;;){float**o;int ch;int s=stb_vorbis_get_frame_float(v,&ch,&o);if(s<=0)break;
      for(int c=0;c<ch;c++)memcpy(ref[c]+n,o[c],s*4);n+=s;}
   rn=n; stb_vorbis_close(v);
   long target=atol(argv[2]);
   stb_vorbis_int64 off=stb_vorbis_seek_pushdata(p,target);
   pos=off; float got[65536]; long gn=0;
   while(gn<8192){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(p,fb+pos,(int)(fl-pos),&ch,&o,&s);
      pos+=u; if(s>0){memcpy(got+gn,o[0],s*4);gn+=s;} if(pos>=fl)break;}
   // find best alignment by searching wider and reporting nearest
   long bestk=-1; int bestmatch=0;
   for(long k=target-6000;k<target+6000;k++){ if(k<0||k+50>rn)continue;
      int m=0; for(int i=0;i<50;i++)if(got[i]==ref[0][k+i])m++; else break;
      if(m>bestmatch){bestmatch=m;bestk=k;}}
   printf("target=%ld best align at %ld (delta %ld) with %d matching samples\n",target,bestk,bestk-target,bestmatch);
   return 0;
}
