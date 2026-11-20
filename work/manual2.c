#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
static uint8 *fb; static long fl; static float *ref[8]; static long rn; static int chn;
static void buildref(void){int err;stb_vorbis*v=stb_vorbis_open_memory(fb,(int)fl,&err,NULL);
 chn=stb_vorbis_get_info(v).channels;long cap=stb_vorbis_stream_length_in_samples(v)+65536,n=0;
 for(int c=0;c<chn;c++)ref[c]=malloc(cap*4);
 for(;;){float**o;int ch;int s=stb_vorbis_get_frame_float(v,&ch,&o);if(s<=0)break;
  for(int c=0;c<ch;c++)memcpy(ref[c]+n,o[c],s*4);n+=s;}rn=n;stb_vorbis_close(v);}
static void arm(stb_vorbis*p,long target,int idx){
   p->previous_length=0;p->page_crc_tests=0;p->discard_samples_deferred=0;p->current_loc_valid=FALSE;
   p->first_decode=FALSE;p->samples_output=0;p->channel_buffer_start=0;p->channel_buffer_end=0;
   p->next_seg=-1;p->valid_bits=0;p->packet_bytes=0;p->bytes_in_seg=0;p->eof=0;
   p->push_seek_active=TRUE;p->push_seek_target=target;
   int64 result=p->seek_index[idx].byte_offset;
   p->push_block=NULL;p->push_block_offset=result;p->push_byte_offset=result;
}
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);fl=ftell(f);fseek(f,0,SEEK_SET);
   fb=malloc(fl);if(fread(fb,1,fl,f)!=(size_t)fl){}fclose(f);buildref();
   int err,used;stb_vorbis*p=stb_vorbis_open_pushdata(fb,(int)fl,&used,&err,NULL);
   long pos=used;for(;;){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(p,fb+pos,(int)(fl-pos),&ch,&o,&s);
     if(u==0&&s==0)break;pos+=u;if(pos>=fl)break;}
   long target=atol(argv[2]); int wantidx=atoi(argv[3]);
   arm(p,target,wantidx);
   pos=p->seek_index[wantidx].byte_offset;
   float got[65536];long gn=0;
   while(gn<4096){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(p,fb+pos,(int)(fl-pos),&ch,&o,&s);
     pos+=u;if(s>0){memcpy(got+gn,o[0],s*4);gn+=s;}if(pos>=fl)break;}
   long bk=-1;int bm=0;for(long k=target-6000;k<target+3000;k++){if(k<0||k+60>rn)continue;int m=0;for(int i=0;i<60;i++)if(got[i]==ref[0][k+i])m++;else break;if(m>bm){bm=m;bk=k;}}
   printf("idx%d gran=%ld target=%ld -> align %ld delta %ld match %d got0=%.9g\n",wantidx,
     (long)p->seek_index[wantidx].granule_position,target,bk,bk-target,bm,got[0]);
   return 0;
}
