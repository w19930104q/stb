#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
static uint8 *fb; static long fl; static float *ref[8]; static long rn;
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
   printf("target=%ld off=%lld first_decode=%d\n",target,(long long)off,p->first_decode);
   pos=off; int fr=0;
   while(fr<12){
      float**o;int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(p,fb+pos,(int)(fl-pos),&ch,&o,&s);
      if(s>0){
        printf("OUT s=%d loc=%lld o0=%.9g\n",s,(long long)p->current_loc,o[0][0]);
        for(long k=0;k<2000;k++)if(o[0][0]==ref[0][k]){printf("  matches ref[%ld]\n",k);goto done;}
        printf("  no match in ref[0..2000]\n");
        done: break;
      }
      pos+=u;fr++;if(pos>=fl)break;
   }
   return 0;
}
