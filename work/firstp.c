#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long flen=ftell(f); fseek(f,0,SEEK_SET);
   unsigned char*buf=malloc(flen); if(fread(buf,1,flen,f)!=(size_t)flen){} fclose(f);
   int err,used; stb_vorbis*p=stb_vorbis_open_pushdata(buf,(int)flen,&used,&err,NULL);
   printf("after open: used=%d first_audio_page_offset=%u\n",used,p->first_audio_page_offset);
   long pos=used;
   for(;;){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(p,buf+pos,(int)(flen-pos),&ch,&o,&s);
      if(u==0&&s==0)break;pos+=u;if(pos>=flen)break;}
   printf("index[0]: off=%ld start=%ld gran=%ld\n",(long)p->seek_index[0].byte_offset,
      (long)p->seek_index[0].sample_start,(long)p->seek_index[0].granule_position);
   return 0;
}
