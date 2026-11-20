#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// raw ogg page parser
int main(int argc,char**argv){
   FILE*f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);long fl=ftell(f);fseek(f,0,SEEK_SET);
   unsigned char*b=malloc(fl);if(fread(b,1,fl,f)!=(size_t)fl){}fclose(f);
   int pg=0; long i=0;
   while(i+27<fl){
      if(memcmp(b+i,"OggS",4)==0){
         unsigned char ver=b[i+4], hdr=b[i+5]; long gran=(long)b[i+6]|((long)b[i+7]<<8)|((long)b[i+8]<<16)|((long)b[i+9]<<24)|((long)b[i+10]<<32)|((long)b[i+11]<<40)|((long)b[i+12]<<48)|((long)b[i+13]<<56);
         int nseg=b[i+26]; long pay=0; int last255=-1;
         for(int s=0;s<nseg;s++){pay+=b[i+27+s]; if(b[i+27+s]<255)last255=s;}
         int cont=(hdr&1);
         printf("page %2d off=%6ld gran=%8ld nseg=%3d cont=%d hdr=%d pay=%ld\n",pg++,i,gran,nseg,cont,hdr,pay);
         i+=27+nseg+pay;
      } else i++;
   }
   return 0;
}
