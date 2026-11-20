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
   long target=330750;
   stb_vorbis_int64 off=stb_vorbis_seek_pushdata(p,target);
   pos=off; int fr=0;
   while(fr<20){
      float**o;int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(p,fb+pos,(int)(fl-pos),&ch,&o,&s);
      if(s>0){
        printf("OUTPUT frame: s=%d loc=%lld buf_start=%d buf_end=%d out[0]=%p chan[0]=%p\n",
          s,(long long)p->current_loc,p->channel_buffer_start,p->channel_buffer_end,(void*)o[0],(void*)p->channel_buffers[0]);
        printf("  o[0][0]=%.9g  o[0][1]=%.9g\n",o[0][0],o[0][1]);
        // which ref sample does o[0][0] equal?
        for(long k=330000;k<331000;k++)if(o[0][0]==ref[0][k]){printf("  o[0][0] == ref[%ld]\n",k);break;}
        break;
      }
      pos+=u;fr++;if(pos>=fl)break;
   }
   return 0;
}
