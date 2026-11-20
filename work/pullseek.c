#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stb_vorbis.c"
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);long fl=ftell(f);fseek(f,0,SEEK_SET);
   unsigned char*b=malloc(fl);if(fread(b,1,fl,f)!=(size_t)fl){}fclose(f);
   int err; stb_vorbis*v=stb_vorbis_open_memory(b,(int)fl,&err,NULL);
   long target=atol(argv[2]);
   int ok=stb_vorbis_seek(v,(unsigned)target);
   printf("pull seek %ld ok=%d loc=%d fileoff=%u\n",target,ok,stb_vorbis_get_sample_offset(v),stb_vorbis_get_file_offset(v));
   float**o;int ch;int s=stb_vorbis_get_frame_float(v,&ch,&o);
   printf("first frame s=%d loc=%d\n",s,stb_vorbis_get_sample_offset(v));
   return 0;
}
