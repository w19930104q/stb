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
   printf("count=%d\n",v->seek_index_count);
   stb_vorbis_int64 r=stb_vorbis_seek_pushdata(v,330750);
   printf("seek result=%lld err=%d active=%d target=%lld\n",(long long)r,stb_vorbis_get_error(v),v->push_seek_active,(long long)v->push_seek_target);
   return 0;
}
