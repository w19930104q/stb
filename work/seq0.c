#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long flen=ftell(f); fseek(f,0,SEEK_SET);
   unsigned char*buf=malloc(flen); if(fread(buf,1,flen,f)!=(size_t)flen){} fclose(f);
   int err,used;
   // sequential push
   stb_vorbis*p=stb_vorbis_open_pushdata(buf,(int)flen,&used,&err,NULL);
   long pos=used; int fr=0;
   while(fr<3){float**o;int ch=0,s=0;int u=stb_vorbis_decode_frame_pushdata(p,buf+pos,(int)(flen-pos),&ch,&o,&s);
      if(s>0){printf("PUSH seq frame %d: s=%d o0[0]=%.9g loc=%lld\n",fr,s,o[0][0],(long long)p->current_loc);fr++;}
      pos+=u;if(pos>=flen)break;}
   stb_vorbis_close(p);
   // pull
   stb_vorbis*v=stb_vorbis_open_memory(buf,(int)flen,&err,NULL);
   fr=0; while(fr<3){float**o;int ch;int s=stb_vorbis_get_frame_float(v,&ch,&o);
      if(s>0){printf("PULL frame %d: s=%d o0[0]=%.9g\n",fr,s,o[0][0]);fr++;}}
   return 0;
}
