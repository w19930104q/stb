#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
static uint8 *filebuf; static long filelen;
static float *ref[8]; static long refn; static int chn;
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); filelen=ftell(f); fseek(f,0,SEEK_SET);
   filebuf=malloc(filelen); if(fread(filebuf,1,filelen,f)!=(size_t)filelen){} fclose(f);
   int err; stb_vorbis*v=stb_vorbis_open_memory(filebuf,(int)filelen,&err,NULL);
   chn=stb_vorbis_get_info(v).channels; long cap=stb_vorbis_stream_length_in_samples(v)+65536,n=0;
   for(int c=0;c<chn;c++)ref[c]=malloc(cap*4);
   for(;;){float**o;int ch;int s=stb_vorbis_get_frame_float(v,&ch,&o);if(s<=0)break;
      for(int c=0;c<ch;c++)memcpy(ref[c]+n,o[c],s*4);n+=s;}
   refn=n; stb_vorbis_close(v);
   long target=atol(argv[2]);
   int used; stb_vorbis*p=stb_vorbis_open_pushdata(filebuf,(int)filelen,&used,&err,NULL);
   stb_vorbis_int64 off=stb_vorbis_seek_pushdata(p,target);
   printf("target=%ld seek off=%lld idx=%d\n",target,(long long)off,p->seek_index_count);
   long pos=off; float got[65536]; long gotn=0; int frames=0;
   while(gotn<8192){
      float**o;int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(p,filebuf+pos,(int)(filelen-pos),&ch,&o,&s);
      pos+=u;
      if(s>0){memcpy(got+gotn,o[0],s*4);gotn+=s;frames++;}
      if(pos>=filelen)break;
   }
   printf("decoded %ld samples in %d frames, current_loc=%lld\n",gotn,frames,(long long)stb_vorbis_get_sample_offset64(p));
   // where does got[0..] align in ref?
   for(long k=target-4000;k<target+4000;k++){ if(k<0||k+200>refn)continue;
      int m=1; for(int i=0;i<200;i++)if(got[i]!=ref[0][k+i]){m=0;break;}
      if(m){printf("ALIGNS at ref %ld (delta %ld)\n",k,k-target);return 0;}}
   printf("no alignment near target; got[0]=%.9g ref[target]=%.9g\n",got[0],ref[0][target]);
   return 0;
}
