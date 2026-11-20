#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
int main(int argc,char**argv){
   (void)argc;
   FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long flen=ftell(f); fseek(f,0,SEEK_SET);
   unsigned char*buf=malloc(flen); fread(buf,1,flen,f); fclose(f);
   int err,used; stb_vorbis*v=stb_vorbis_open_pushdata(buf,(int)flen,&used,&err,NULL);
   if(!v){printf("open fail %d\n",err);return 1;}
   long pos=used, total=0; int frames=0;
   for(;;){
      float**out; int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(v,buf+pos,(int)(flen-pos),&ch,&out,&s);
      if(u==0&&s==0)break;
      pos+=u; if(s>0){total+=s;frames++;}
      if(pos>=flen)break;
   }
   printf("push sequential: %ld samples in %d frames, consumed %ld/%ld bytes\n",total,frames,pos,flen);
   stb_vorbis_close(v);
   int pc,sr; short*po; int n=stb_vorbis_decode_memory(buf,(int)flen,&pc,&sr,&po);
   printf("pull decode: %d samples, %d ch, %d Hz\n",n,pc,sr);
   free(po); free(buf);
   return 0;
}
