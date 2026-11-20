#define STB_VORBIS_NO_STDALLOC
#include "../stb_vorbis.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char**argv){
   FILE*fh=fopen(argv[1],"rb");fseek(fh,0,SEEK_END);long fl=ftell(fh);fseek(fh,0,SEEK_SET);
   unsigned char*file=malloc(fl);if(fread(file,1,fl,fh)!=(size_t)fl){}fclose(fh);
   /* generous fixed pool: decoder state + index region (index will not use it,
      but ensure no overflow into pool while decoding/seeking) */
   int poolbytes = 6*1024*1024;
   char*pool=(char*)malloc(poolbytes);
   stb_vorbis_alloc a; a.alloc_buffer=pool; a.alloc_buffer_length_in_bytes=poolbytes;
   int err=0,used=0;
   stb_vorbis*v=stb_vorbis_open_pushdata(file,(int)fl,&used,&err,&a);
   if(!v){printf("open fail err=%d\n",err);return 1;}
   long pos=used,total=0; int frames=0;
   for(;;){float**o;int ch=0,s=0;
      int u=stb_vorbis_decode_frame_pushdata(v,file+pos,(int)(fl-pos),&ch,&o,&s);
      if(u==0&&s==0)break; pos+=u; if(s>0){total+=s;frames++;} if(pos>=fl)break;}
   printf("fixedbuf decode: %ld samples %d frames, index_count=%d cap=%d\n",
          total,frames,v->seek_index_count,v->seek_index_cap);
   /* seek must fail gracefully (empty index), never crash */
   stb_vorbis_int64 r=stb_vorbis_seek_pushdata(v,total/2);
   printf("seek in fixed-buffer mode returned %lld (expect -1), error=%d\n",
          (long long)r, stb_vorbis_get_error(v));
   /* offset API still works */
   printf("byte offset=%lld\n",(long long)stb_vorbis_get_pushdata_byte_offset(v));
   stb_vorbis_close(v);
   free(pool);free(file);
   printf("fixedbuf OK\n");
   return 0;
}
