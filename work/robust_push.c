// robustness driver for pushdata seek: throws corrupt/truncated inputs,
// random seeks, random flushes, and malloc-failure injection at the new
// code paths. must not crash, overflow, or leak (run under sanitizers).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- malloc failure injection (same idea as tests/local_fuzz/vorbis.c)
static size_t g_alloc_count_limit;
static int fuzz_alloc_check(size_t sz)
{
   if (!g_alloc_count_limit) return 0;
   --g_alloc_count_limit;
   return 1;
}
static void *fuzz_malloc(size_t sz) { return fuzz_alloc_check(sz) ? malloc(sz) : NULL; }
static void *fuzz_realloc(void *p, size_t sz) { return fuzz_alloc_check(sz) ? realloc(p, sz) : NULL; }
static void fuzz_free(void *p) { free(p); }
#define malloc(sz) fuzz_malloc(sz)
#define realloc(p,sz) fuzz_realloc(p,sz)
#define free(p) fuzz_free(p)

#include "../stb_vorbis.c"
#undef malloc
#undef realloc
#undef free

static unsigned long rng_state = 1;
static unsigned long rng(void)
{
   rng_state = rng_state * 6364136223846793005UL + 1442695040888963407UL;
   return rng_state >> 33;
}

static void exercise(const uint8 *data, long len, unsigned long seed)
{
   int err = 0, used = 0;
   stb_vorbis *v;
   int iter;
   long feed_pos;
   uint8 buf[8192];

   rng_state = seed ? seed : 1;
   g_alloc_count_limit = ~(size_t)0;

   v = stb_vorbis_open_pushdata(data, (int)len, &used, &err, NULL);
   if (!v) return;
   feed_pos = used;

   for (iter = 0; iter < 300; ++iter) {
      unsigned long action = rng() % 100;
      if (action < 60) {
         /* feed a random-sized chunk */
         int chunk = 1 + (int)(rng() % 4096);
         int samples = 0, ch = 0, u;
         float **out;
         if (feed_pos >= len) break;
         if (chunk > len - feed_pos) chunk = (int)(len - feed_pos);
         memcpy(buf, data + feed_pos, (size_t)chunk);
         u = stb_vorbis_decode_frame_pushdata(v, buf, chunk, &ch, &out, &samples);
         if (u > 0) feed_pos += u;
         /* randomly inject a malloc failure point for future index allocs */
         if ((rng() % 32) == 0) g_alloc_count_limit = rng() % 3;
         else g_alloc_count_limit = ~(size_t)0;
      } else if (action < 85) {
         /* random seek; on success, re-feed from the returned offset */
         stb_vorbis_int64 target = (stb_vorbis_int64)(rng() % (unsigned long)(10*1024*1024));
         stb_vorbis_int64 off = stb_vorbis_seek_pushdata(v, target);
         if (off >= 0) {
            if (off > len) { printf("seek returned out-of-range offset %lld (len %ld)\n", (long long)off, len); abort(); }
            feed_pos = off;
         }
      } else if (action < 95) {
         /* random flush + refeed from a random stream position */
         stb_vorbis_flush_pushdata(v);
         feed_pos = (long)(rng() % (unsigned long)(len > 0 ? len : 1));
      } else {
         /* flush_at with a random offset, then honor the contract and
            actually feed from that offset */
         long off = (long)(rng() % (unsigned long)(len > 0 ? len : 1));
         stb_vorbis_flush_pushdata_at(v, off);
         feed_pos = off;
      }
   }
   stb_vorbis_close(v);
}

int main(int argc, char **argv)
{
   int i;
   for (i = 1; i < argc; ++i) {
      FILE *f = fopen(argv[i], "rb");
      long len;
      uint8 *data;
      int trunc;
      if (!f) { perror(argv[i]); continue; }
      fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
      data = malloc(len ? len : 1);
      if (len > 0 && fread(data, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(data); continue; }
      fclose(f);
      /* full file plus several truncations and mutations */
      for (trunc = 0; trunc < 6; ++trunc) {
         long use_len = len;
         if (trunc > 0) use_len = len * trunc / 6;
         exercise(data, use_len, 12345 + i*77 + trunc);
         /* mutate some bytes */
         if (len > 16) {
            int k;
            for (k = 0; k < 8; ++k)
               data[rng() % len] ^= (uint8)(1 << (rng() % 8));
            exercise(data, len, 999 + i*31 + trunc);
         }
      }
      free(data);
   }
   printf("robustness driver done\n");
   return 0;
}
