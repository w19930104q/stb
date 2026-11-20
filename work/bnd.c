#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
static uint8 *fb; static long fl; static float *ref[8]; static long rn; static int chn;
static void buildref(void){
   int err; stb_vorbis*v=stb_vorbis_open_memory(fb,(int)fl,&err,NULL);
   chn=stb_vorbis_get_info(v).channels; long cap=stb_vorbis_stream_length_in_samples(v)+65536,n=0;
   for(int c=0;c<chn;c++)ref[c]=malloc(cap*4);
   for(;;){float**o;int ch;int s=stb_vorbis_get_frame_float(v,&ch,&o);if(s<=0)break;
      for(int c=0;c<ch;c++)memcpy(ref[c]+n,o[c],s*4);n+=s;}
   rn=n; stb_vorbis_close(v);
}
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); fl=ftell(f); fseek(f,0,SEEK_SET);
   fb=malloc(fl); if(fread(fb,1,fl,f)!=(size_t)fl){} fclose(f); buildref();
   int err,used; stb_vorbis*p=stb_vorbis_open_pushdata(fb,(int)fl,&used,&err,NULL);
   long pos=used;
   for(;;){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(p,fb+pos,(int)(fl-pos),&ch,&o,&s);
      if(u==0&&s==0)break;pos+=u;if(pos>=fl)break;}
   long target=atol(argv[2]);
   stb_vorbis_int64 off=stb_vorbis_seek_pushdata(p,target);
   // which index entry?
   int sel=-1; for(int i=0;i<p->seek_index_count;i++)if(p->seek_index[i].byte_offset==off){sel=i;break;}
   printf("target=%ld off=%lld sel=%d",target,(long long)off,sel);
   if(sel>=0)printf(" start=%ld gran=%ld",(long)p->seek_index[sel].sample_start,(long)p->seek_index[sel].granule_position);
   printf("\n");
   pos=off; float got[65536]; long gn=0;
   while(gn<4096){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(p,fb+pos,(int)(fl-pos),&ch,&o,&s);
      pos+=u; if(s>0){memcpy(got+gn,o[0],s*4);gn+=s;} if(pos>=fl)break;}
   long bestk=-1;int bm=0;
   for(long k=target-3000;k<target+3000;k++){if(k<0||k+60>rn)continue;int m=0;for(int i=0;i<60;i++)if(got[i]==ref[0][k+i])m++;else break;if(m>bm){bm=m;bestk=k;}}
   printf("  best align %ld delta %ld match %d; got0=%.9g ref[t]=%.9g\n",bestk,bestk-target,bm,got[0],ref[0][target]);
   return 0;
}
