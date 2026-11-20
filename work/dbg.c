// test harness for sample-accurate pushdata seeking in stb_vorbis.
// not part of the repo test suite; lives in artifacts/build.
//
// usage: test_push_seek <file.ogg>
//
// verifies:
//  1. sequential pushdata decode is bit-identical to the reference
//  2. stb_vorbis_get_pushdata_byte_offset tracks the fed position
//  3. pushdata seeks (forward, backward, repeated, to-start, near-end)
//     produce samples bit-identical to the reference at the target
//  4. the seek index stays within STB_VORBIS_PUSHDATA_SEEK_INDEX_MAX
//  5. flush_pushdata / flush_pushdata_at behavior

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../stb_vorbis.c"

static uint8 *filedata;
static long filelen;

#define MAXCH 8
static float *ref[MAXCH];
static long ref_samples;
static int ref_channels;

static int failures = 0;
static int checks = 0;

#define TCHECK(cond, ...) do { \
   ++checks; \
   if (!(cond)) { \
      printf("FAIL line %d: ", __LINE__); \
      printf(__VA_ARGS__); \
      printf("\n"); \
      ++failures; \
   } \
} while (0)

/* ---------------- reference (ground truth) ---------------- */

static void append_ref(int ch, const float *data, int n, long at)
{
   memcpy(ref[ch] + at, data, (size_t)n * sizeof(float));
}

#ifndef STB_VORBIS_NO_PULLDATA_API
/* reference: full decode through the pull API */
static void build_reference(void)
{
   int err;
   stb_vorbis *v = stb_vorbis_open_memory(filedata, (int)filelen, &err, NULL);
   long cap, n = 0;
   int c;
   if (!v) { printf("cannot open pull decoder: %d\n", err); exit(1); }
   ref_channels = stb_vorbis_get_info(v).channels;
   if (ref_channels > MAXCH) { printf("too many channels\n"); exit(1); }
   cap = stb_vorbis_stream_length_in_samples(v) + 65536;
   for (c = 0; c < ref_channels; ++c)
      ref[c] = (float *) malloc((size_t)cap * sizeof(float));
   for (;;) {
      float **out;
      int ch, s = stb_vorbis_get_frame_float(v, &ch, &out);
      if (s <= 0) break;
      if (n + s > cap) { printf("ref overflow\n"); exit(1); }
      for (c = 0; c < ch; ++c) append_ref(c, out[c], s, n);
      n += s;
   }
   ref_samples = n;
   stb_vorbis_close(v);
}
#else
/* reference: full sequential decode through the push API itself */
static void build_reference(void)
{
   int err, used;
   stb_vorbis *v = stb_vorbis_open_pushdata(filedata, (int)filelen, &used, &err, NULL);
   long cap = 0, n = 0;
   long pos;
   int c;
   if (!v) { printf("cannot open push decoder: %d\n", err); exit(1); }
   ref_channels = stb_vorbis_get_info(v).channels;
   if (ref_channels > MAXCH) { printf("too many channels\n"); exit(1); }
   pos = used;
   for (;;) {
      float **out;
      int ch = 0, samples = 0, u;
      if (pos >= filelen) break;
      u = stb_vorbis_decode_frame_pushdata(v, filedata + pos, (int)(filelen - pos), &ch, &out, &samples);
      if (u == 0 && samples == 0) break; /* no progress with all data given: EOF */
      pos += u;
      if (samples > 0) {
         if (n + samples > cap) {
            cap = (n + samples) * 2 + 65536;
            for (c = 0; c < ref_channels; ++c)
               ref[c] = (float *) realloc(ref[c], (size_t)cap * sizeof(float));
         }
         for (c = 0; c < ch; ++c) append_ref(c, out[c], samples, n);
         n += samples;
      }
   }
   ref_samples = n;
   stb_vorbis_close(v);
}
#endif

/* ---------------- push feeder ---------------- */

#define FEED_CAP   262144
#ifndef FEED_CHUNK
#define FEED_CHUNK 4096
#endif

typedef struct {
   stb_vorbis *v;
   long pos;     /* absolute stream offset of buf[0] */
   int  buflen;
   uint8 buf[FEED_CAP];
} Feeder;

static Feeder fd;

static void feeder_open(void)
{
   int err, used;
   fd.v = stb_vorbis_open_pushdata(filedata, (int)filelen, &used, &err, NULL);
   if (!fd.v) { printf("cannot open push decoder: %d\n", err); exit(1); }
   fd.pos = used;
   fd.buflen = 0;
}

static void feeder_close(void)
{
   stb_vorbis_close(fd.v);
   fd.v = NULL;
}

/* one decode call, topping up the buffer first; returns samples (may be 0) */
static int feeder_step(float ***out, int *channels, int *used_out)
{
   int samples = 0, ch = 0, used;
   if (fd.buflen < FEED_CAP && fd.pos + fd.buflen < filelen) {
      long want = FEED_CAP - fd.buflen;
      long avail = filelen - (fd.pos + fd.buflen);
      long n = want < avail ? want : avail;
      if (n > FEED_CHUNK) n = FEED_CHUNK;
      memcpy(fd.buf + fd.buflen, filedata + fd.pos + fd.buflen, (size_t)n);
      fd.buflen += (int)n;
   }
   if (fd.buflen == 0) { *used_out = 0; return -1; }
   used = stb_vorbis_decode_frame_pushdata(fd.v, fd.buf, fd.buflen, &ch, out, &samples);
   /* offset tracking must follow consumption */
   {
      stb_vorbis_int64 off = stb_vorbis_get_pushdata_byte_offset(fd.v);
      if (off != -1 && off != fd.pos + used)
         fprintf(stderr, "MISMATCH off=%lld pos=%ld used=%d crc=%d blockoff=%lld block=%p buf=%p seekactive=%d\n",
            (long long)off, fd.pos, used, fd.v->page_crc_tests,
            (long long)fd.v->push_block_offset, (void*)fd.v->push_block, (void*)fd.buf, fd.v->push_seek_active);
      if (off != -1)
         TCHECK(off == fd.pos + used, "byte offset tracking: got %lld, expected %ld",
               (long long)off, fd.pos + used);
   }
   if (used > 0) {
      memmove(fd.buf, fd.buf + used, (size_t)(fd.buflen - used));
      fd.buflen -= used;
      fd.pos += used;
   }
   if (channels) *channels = ch;
   if (used_out) *used_out = used;
   return samples;
}

/* next frame with samples; -1 at end of stream */
static int feeder_next(float ***out, int *channels)
{
   for (;;) {
      int used;
      int s = feeder_step(out, channels, &used);
      if (s > 0) return s;
      if (s < 0) return -1;
      if (used == 0 && fd.pos + fd.buflen >= filelen) return -1; /* no more data */
   }
}

/* decode and verify frames against ref[start..], until at least min_samples
   have been verified; returns total samples verified, -1 on early EOF */
static long verify_frames(long start, long min_samples, const char *what)
{
   long pos = start;
   while (pos - start < min_samples) {
      float **out;
      int ch = 0, c;
      int s = feeder_next(&out, &ch);
      long i;
      if (s < 0) {
         TCHECK(0, "%s: unexpected end of stream at sample %ld (wanted %ld more)",
               what, pos, min_samples - (pos - start));
         return -1;
      }
      if (pos + s > ref_samples) {
         TCHECK(0, "%s: decoded past end of reference (%ld + %d > %ld)", what, pos, s, ref_samples);
         return -1;
      }
      for (c = 0; c < ch; ++c) {
         for (i = 0; i < s; ++i) {
            if (out[c][i] != ref[c][pos + i]) {
               TCHECK(0, "%s: ch%d sample %ld: got %.9g, expected %.9g",
                     what, c, pos + i, (double)out[c][i], (double)ref[c][pos+i]);
               return -1;
            }
         }
      }
      pos += s;
   }
   return pos - start;
}

/* ---------------- seek driver ---------------- */

static void do_seek(long target, long verify_count, const char *what)
{
   stb_vorbis_int64 off = stb_vorbis_seek_pushdata(fd.v, target);
   TCHECK(off >= 0, "%s: seek to %ld failed (error %d)", what, target, stb_vorbis_get_error(fd.v));
   if (off < 0) return;
   if (off != fd.pos) {
      /* must reposition the stream */
      fd.pos = off;
      fd.buflen = 0;
   }
   verify_frames(target, verify_count, what);
   /* after verifying N samples, the decoder must report sample target+N */
   {
      stb_vorbis_int64 loc = stb_vorbis_get_sample_offset64(fd.v);
      TCHECK(loc >= target + verify_count, "%s: sample offset %lld after seek to %ld + %ld",
            what, (long long)loc, target, verify_count);
   }
}

static unsigned long rng_state = 12345;
static unsigned long rng(void)
{
   rng_state = rng_state * 6364136223846793005UL + 1442695040888963407UL;
   return rng_state >> 33;
}

int main(int argc, char **argv)
{
   FILE *f;
   long i;
   int fwd = 0, bwd = 0;

   if (argc < 2) { printf("usage: %s file.ogg\n", argv[0]); return 1; }
   f = fopen(argv[1], "rb");
   if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
   fseek(f, 0, SEEK_END);
   filelen = ftell(f);
   fseek(f, 0, SEEK_SET);
   filedata = (uint8 *) malloc((size_t)filelen);
   if (fread(filedata, 1, (size_t)filelen, f) != (size_t)filelen) { printf("read error\n"); return 1; }
   fclose(f);

   build_reference();
   printf("reference: %ld samples, %d channels, %ld bytes\n", ref_samples, ref_channels, filelen);

   /* ---- 1. sequential push decode must match the reference exactly ---- */
   feeder_open();
   {
      long got = verify_frames(0, ref_samples, "sequential");
      TCHECK(got == ref_samples, "sequential decode length: got %ld, expected %ld", got, ref_samples);
      /* stream must be exhausted apart from possibly a partial trailing page */
      TCHECK(fd.v->seek_index_count <= STB_VORBIS_PUSHDATA_SEEK_INDEX_MAX,
            "index bound: %d > %d", fd.v->seek_index_count, STB_VORBIS_PUSHDATA_SEEK_INDEX_MAX);
      printf("sequential: %ld samples verified, index entries %d (max %d)\n",
             got, fd.v->seek_index_count, STB_VORBIS_PUSHDATA_SEEK_INDEX_MAX);
   }

   /* ---- 2. scripted seeks ---- */
   {
      long N = ref_samples;
      do_seek(N/2, 8192, "seek backward to N/2");
      do_seek(N/4, 8192, "seek backward to N/4");
      do_seek(3*N/4, 8192, "seek forward to 3N/4");
      do_seek(1000, 8192, "seek near start");
      do_seek(0, 8192, "seek to 0");
      do_seek(N - 9000, 8192, "seek near end");
      do_seek(N - 9000, 4096, "repeat seek near end");
      do_seek(N/3, 4096, "seek to N/3");
      do_seek(N/3, 4096, "repeat seek to N/3");
      do_seek(N/3, 4096, "third seek to N/3");
   }

   /* ---- 3. random walk, both directions ---- */
   rng_state = 999331;
   for (i = 0; i < 60; ++i) {
      long target = (long)(rng() % (unsigned long)(ref_samples - 10000));
      stb_vorbis_int64 cur = stb_vorbis_get_sample_offset64(fd.v);
      char what[64];
      if (cur >= 0 && target >= cur) ++fwd; else ++bwd;
      sprintf(what, "random seek #%ld to %ld", (long)i, target);
      do_seek(target, 2048 + (long)(rng() % 6000), what);
   }
   printf("random walk: %d forward, %d backward seeks\n", fwd, bwd);
   TCHECK(fwd > 0 && bwd > 0, "random walk should exercise both directions");

#ifndef STB_VORBIS_NO_PULLDATA_API
   /* ---- 3b. pull-mode seek regression: must still match the reference ---- */
   {
      int err;
      stb_vorbis *pv = stb_vorbis_open_memory(filedata, (int)filelen, &err, NULL);
      float *pbuf[MAXCH];
      int c;
      for (c = 0; c < ref_channels; ++c) pbuf[c] = (float *) malloc(4096 * sizeof(float));
      TCHECK(pv != NULL, "pull reopen failed");
      if (pv) {
         rng_state = 424242;
         for (i = 0; i < 20; ++i) {
            long target = (long)(rng() % (unsigned long)(ref_samples - 9000));
            long got = 0;
            int ok = stb_vorbis_seek(pv, (unsigned int)target);
            TCHECK(ok, "pull seek to %ld", target);
            while (got < 4096) {
               int s = stb_vorbis_get_samples_float(pv, ref_channels, pbuf, 4096);
               long i2;
               if (s <= 0) break;
               for (c = 0; c < ref_channels; ++c)
                  for (i2 = 0; i2 < s; ++i2)
                     if (pbuf[c][i2] != ref[c][target + got + i2]) {
                        TCHECK(0, "pull seek to %ld: mismatch at %ld", target, target+got+i2);
                        goto pull_done;
                     }
               got += s;
            }
         }
      pull_done:
         stb_vorbis_close(pv);
      }
      for (c = 0; c < ref_channels; ++c) free(pbuf[c]);
   }
#endif

   /* ---- 4. seek while a previous seek is still pending ---- */
   {
      stb_vorbis_int64 off;
      long cur = stb_vorbis_get_sample_offset64(fd.v);
      long t1 = cur + 100000;
      int k;
      if (t1 > ref_samples - 20000) t1 = ref_samples - 20000;
      off = stb_vorbis_seek_pushdata(fd.v, t1);
      TCHECK(off >= 0, "pending-interrupt: first seek failed");
      /* feed a few frames, not enough to reach t1 */
      for (k = 0; k < 5; ++k) {
         float **out; int ch, used;
         feeder_step(&out, &ch, &used);
      }
      TCHECK(fd.v->push_seek_active, "pending-interrupt: seek should still be pending");
      /* now seek somewhere else entirely */
      do_seek(ref_samples/5, 8192, "pending-interrupt: second seek");
   }

   /* ---- 4b. seek again without feeding in between ---- */
   {
      stb_vorbis_int64 off1, off2;
      off1 = stb_vorbis_seek_pushdata(fd.v, ref_samples/7);
      TCHECK(off1 >= 0, "double-seek: first failed");
      /* immediately seek elsewhere without feeding any data */
      off2 = stb_vorbis_seek_pushdata(fd.v, 6*ref_samples/7);
      TCHECK(off2 >= 0, "double-seek: second failed");
      if (off2 != fd.pos) { fd.pos = off2; fd.buflen = 0; }
      verify_frames(6*ref_samples/7, 4096, "double-seek verify");
      /* and back, again without feeding */
      off1 = stb_vorbis_seek_pushdata(fd.v, 12345);
      TCHECK(off1 >= 0, "double-seek: third failed");
      if (off1 != fd.pos) { fd.pos = off1; fd.buflen = 0; }
      verify_frames(12345, 4096, "double-seek verify 2");
   }

   /* ---- 4c. seek to the exact end of the stream ---- */
   {
      stb_vorbis_int64 off = stb_vorbis_seek_pushdata(fd.v, ref_samples);
      TCHECK(off >= 0, "seek-to-end failed");
      if (off >= 0) {
         float **out; int ch;
         if (off != fd.pos) { fd.pos = off; fd.buflen = 0; }
         /* decoding must run into EOF without emitting more samples */
         TCHECK(feeder_next(&out, &ch) < 0, "seek-to-end should produce no more samples");
      }
      /* and we must still be able to seek back afterwards */
      do_seek(ref_samples/2, 4096, "seek after seek-to-end");
   }

   /* ---- 5. flush_pushdata: position becomes unknown, resync still works ---- */
   {
      /* find a real page offset from the seek index */
      int idx = fd.v->seek_index_count / 2;
      long page_off;
      stb_vorbis_int64 loc;
      TCHECK(idx > 0, "need a non-empty index for the flush test");
      page_off = (long) fd.v->seek_index[idx].byte_offset;
      stb_vorbis_flush_pushdata(fd.v);
      TCHECK(stb_vorbis_get_pushdata_byte_offset(fd.v) == -1, "flush must invalidate byte offset");
      fd.pos = page_off;
      fd.buflen = 0;
      /* decode until position is known again, then verify continuity */
      for (;;) {
         float **out; int ch, used;
         int s = feeder_step(&out, &ch, &used);
         (void)s;
         loc = stb_vorbis_get_sample_offset64(fd.v);
         if (loc >= 0) break;
         if (fd.pos + fd.buflen >= filelen) { TCHECK(0, "flush resync never recovered"); break; }
      }
      /* skip one frame (the resync discards one), then verify */
      {
         float **out; int ch;
         int s = feeder_next(&out, &ch);
         long start;
         TCHECK(s >= 0, "flush: no frame after resync");
         start = (long) stb_vorbis_get_sample_offset64(fd.v) - s;
         /* 'start' is where the frame we just consumed began; the NEXT frame
            continues at start+s; verify from there */
         {
            long pos = start + s;
            while (pos < start + s + 4096) {
               int c, s2 = feeder_next(&out, &ch);
               long i2;
               if (s2 < 0) break;
               for (c = 0; c < ch; ++c)
                  for (i2 = 0; i2 < s2; ++i2)
                     if (out[c][i2] != ref[c][pos + i2]) {
                        TCHECK(0, "flush: mismatch at %ld", pos + i2);
                        goto flush_done;
                     }
               pos += s2;
            }
         }
      }
   flush_done:
      /* seek must still work using the pre-flush index */
      do_seek(ref_samples/2, 4096, "seek after flush");
   }

   /* ---- 6. flush_pushdata_at keeps tracking alive ---- */
   {
      long target_page = (long) fd.v->seek_index[fd.v->seek_index_count/2].byte_offset;
      stb_vorbis_flush_pushdata_at(fd.v, target_page);
      TCHECK(stb_vorbis_get_pushdata_byte_offset(fd.v) == target_page,
            "flush_pushdata_at must set the byte offset");
      fd.pos = target_page;
      fd.buflen = 0;
      /* decode a while so the index grows again */
      {
         float **out; int ch;
         long n;
         for (n = 0; n < 30; ++n) { if (feeder_next(&out, &ch) < 0) break; }
      }
      do_seek(ref_samples/3, 4096, "seek after flush_pushdata_at");
   }

   TCHECK(fd.v->seek_index_count <= STB_VORBIS_PUSHDATA_SEEK_INDEX_MAX,
         "final index bound: %d > %d", fd.v->seek_index_count, STB_VORBIS_PUSHDATA_SEEK_INDEX_MAX);

   feeder_close();
   free(filedata);

   printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
   return failures ? 1 : 0;
}
