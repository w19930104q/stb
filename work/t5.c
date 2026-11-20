#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long flen=ftell(f); fseek(f,0,SEEK_SET);
   unsigned char*buf=malloc(flen); if(fread(buf,1,flen,f)!=(size_t)flen){} fclose(f);
   int err,used; stb_vorbis*p=stb_vorbis_open_pushdata(buf,(int)flen,&used,&err,NULL);
   long pos=used;
   for(;;){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(p,buf+pos,(int)(flen-pos),&ch,&o,&s);
      if(u==0&&s==0)break;pos+=u;if(pos>=flen)break;}
   stb_vorbis_int64 off=stb_vorbis_seek_pushdata(p,0);
   printf("off=%lld first_decode=%d page_crc_tests=%d\n",(long long)off,p->first_decode,p->page_crc_tests);
   pos=off; int fr=0;
   while(fr<10){
      int64 loc=p->current_loc; int fd=p->first_decode, cv=p->current_loc_valid;
      float**o;int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(p,buf+pos,(int)(flen-pos),&ch,&o,&s);
      printf("fr%d: in loc=%lld fd=%d cv=%d -> s=%d outloc=%lld fd_after=%d prevlen=%d u=%d\n",
        fr,(long long)loc,fd,cv,s,(long long)p->current_loc,p->first_decode,p->previous_length,u);
      pos+=u;fr++;if(pos>=flen)break;
   }
   return 0;
}
