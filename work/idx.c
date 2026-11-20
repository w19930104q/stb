#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long flen=ftell(f); fseek(f,0,SEEK_SET);
   unsigned char*buf=malloc(flen); if(fread(buf,1,flen,f)!=(size_t)flen){} fclose(f);
   int err,used; stb_vorbis*v=stb_vorbis_open_pushdata(buf,(int)flen,&used,&err,NULL);
   long pos=used;
   for(;;){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(v,buf+pos,(int)(flen-pos),&ch,&o,&s);
      if(u==0&&s==0)break;pos+=u;if(pos>=flen)break;}
   stb_vorbis*p=stb_vorbis_open_memory(buf,(int)flen,&err,NULL);
   printf("total samples (pull): %u\n",stb_vorbis_stream_length_in_samples(p));
   printf("index entries: %d\n",v->seek_index_count);
   for(int i=0;i<v->seek_index_count;i++)
      printf("  off=%ld gran=%ld\n",(long)v->seek_index[i].byte_offset,(long)v->seek_index[i].granule_position);
   return 0;
}
