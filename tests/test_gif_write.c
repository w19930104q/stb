// test_gif_write.c - round-trip tests for the GIF writer in stb_image_write.h
//
// GIFs written with stb_image_write are decoded two independent ways:
//   - a minimal GIF decoder implemented in this file (checks file structure,
//     palettes, transparency, delays, loop count, LZW streams and pixels)
//   - stb_image's GIF loader (stbi_load / stbi_load_gif_from_memory)
// and the decoded pixels are compared against the source images.
//
// Also sanity-checks that the other image writers are unaffected.
//
// Exit code 0 = all tests passed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static int num_checks = 0;
static int num_failures = 0;

#define CHECK(cond) do { \
      ++num_checks; \
      if (!(cond)) { \
         ++num_failures; \
         printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      } \
   } while (0)

//////////////////////////////////////////////////////////////////////////////
// memory buffer write target for the _to_func APIs

typedef struct
{
   unsigned char *data;
   size_t len, cap;
} membuf;

static void membuf_write(void *context, void *data, int size)
{
   membuf *m = (membuf *) context;
   if (m->len + (size_t) size > m->cap) {
      size_t ncap = m->cap ? m->cap * 2 : 4096;
      while (ncap < m->len + (size_t) size)
         ncap *= 2;
      m->data = (unsigned char *) realloc(m->data, ncap);
      if (m->data == NULL) {
         fprintf(stderr, "out of memory\n");
         exit(1);
      }
      m->cap = ncap;
   }
   memcpy(m->data + m->len, data, (size_t) size);
   m->len += (size_t) size;
}

//////////////////////////////////////////////////////////////////////////////
// minimal independent GIF decoder (structure + LZW), used to validate the
// writer's output without relying on stb_image

#define GD_MAX_FRAMES 64

typedef struct
{
   int delay_cs;
   int transparent;      // -1 or palette index
   int local_palette;    // 1 if the frame has its own local color table
   int palette_size;     // number of entries in the color table used
   unsigned char palette[256][3];
   unsigned char *indices; // w*h bytes
} gd_frame;

typedef struct
{
   const unsigned char *data;
   size_t len, pos;
   int w, h;
   int loop_count;       // -1 if no looping extension
   unsigned char gct[256][3];
   int gct_size;
   gd_frame frames[GD_MAX_FRAMES];
   int num_frames;
   const char *error;
} gifdec;

static int gd_byte(gifdec *g)
{
   if (g->pos >= g->len) {
      g->error = "unexpected end of file";
      return 0;
   }
   return g->data[g->pos++];
}

static int gd_word(gifdec *g)
{
   int lo = gd_byte(g);
   int hi = gd_byte(g);
   return lo | (hi << 8);
}

static void gd_skip_subblocks(gifdec *g)
{
   for (;;) {
      int n = gd_byte(g);
      if (g->error || n == 0)
         return;
      if (g->pos + (size_t) n > g->len) {
         g->error = "end of file in sub-blocks";
         return;
      }
      g->pos += (size_t) n;
   }
}

// LZW-decompress the image data stream into out[0..out_count-1]
static int gd_lzw(gifdec *g, unsigned char *out, int out_count)
{
   unsigned char *data = NULL;
   size_t data_len = 0, data_cap = 0, bitpos = 0;
   unsigned short prefix[4096];
   unsigned char suffix[4096];
   unsigned char stack[4096 + 1];
   int min_code_size, clear, eoi, code_size, next, old, prev_first = 0;
   int out_pos = 0, ok = 0;

   min_code_size = gd_byte(g);
   if (g->error)
      return 0;
   if (min_code_size < 2 || min_code_size > 8) {
      g->error = "bad LZW minimum code size";
      return 0;
   }

   // gather the sub-blocks into one contiguous byte stream
   for (;;) {
      int n = gd_byte(g);
      if (g->error)
         goto done;
      if (n == 0)
         break;
      if (g->pos + (size_t) n > g->len) {
         g->error = "end of file in image data";
         goto done;
      }
      if (data_len + (size_t) n > data_cap) {
         size_t ncap = data_cap ? data_cap * 2 : 4096;
         while (ncap < data_len + (size_t) n)
            ncap *= 2;
         data = (unsigned char *) realloc(data, ncap);
         if (data == NULL) {
            g->error = "out of memory";
            goto done;
         }
         data_cap = ncap;
      }
      memcpy(data + data_len, g->data + g->pos, (size_t) n);
      data_len += (size_t) n;
      g->pos += (size_t) n;
   }

   clear = 1 << min_code_size;
   eoi = clear + 1;
   code_size = min_code_size + 1;
   next = eoi + 1;
   old = -1;

   for (;;) {
      int code, i, sp, in_code;
      unsigned char first;

      if (bitpos + (size_t) code_size > data_len * 8) {
         g->error = "LZW data overrun";
         goto done;
      }
      code = 0;
      for (i = 0; i < code_size; ++i) {
         size_t p = bitpos + (size_t) i;
         code |= ((data[p >> 3] >> (p & 7)) & 1) << i;
      }
      bitpos += (size_t) code_size;

      if (code == clear) {
         code_size = min_code_size + 1;
         next = eoi + 1;
         old = -1;
         continue;
      }
      if (code == eoi) {
         if (out_pos != out_count)
            g->error = "wrong number of pixels in LZW stream";
         else
            ok = 1;
         goto done;
      }
      if (old == -1) {
         if (code >= clear) {
            g->error = "bad first LZW code";
            goto done;
         }
         if (out_pos >= out_count) {
            g->error = "too many pixels in LZW stream";
            goto done;
         }
         out[out_pos++] = (unsigned char) code;
         old = code;
         prev_first = code;
         continue;
      }

      sp = 0;
      in_code = code;
      if (code == next) {
         stack[sp++] = (unsigned char) prev_first; // KwKwK case
         code = old;
      } else if (code > next) {
         g->error = "LZW code out of range";
         goto done;
      }
      while (code >= clear) {
         if (sp >= 4096) {
            g->error = "LZW string too long";
            goto done;
         }
         stack[sp++] = suffix[code];
         code = prefix[code];
      }
      first = (unsigned char) code;
      stack[sp++] = first;

      if (next < 4096) {
         prefix[next] = (unsigned short) old;
         suffix[next] = first;
         ++next;
         if (next == (1 << code_size) && code_size < 12)
            ++code_size;
      }
      prev_first = first;
      old = in_code;

      while (sp > 0) {
         if (out_pos >= out_count) {
            g->error = "too many pixels in LZW stream";
            goto done;
         }
         out[out_pos++] = stack[--sp];
      }
   }

done:
   free(data);
   return ok;
}

static int gd_parse(gifdec *g, const unsigned char *data, size_t len)
{
   int packed, i;
   int have_gce = 0, gce_delay = 0, gce_transparent = -1;

   memset(g, 0, sizeof(*g));
   g->data = data;
   g->len = len;
   g->loop_count = -1;

   if (len < 13 || memcmp(data, "GIF89a", 6) != 0) {
      g->error = "bad GIF header";
      return 0;
   }
   g->pos = 6;
   g->w = gd_word(g);
   g->h = gd_word(g);
   packed = gd_byte(g);
   gd_byte(g); // background color index
   gd_byte(g); // pixel aspect ratio
   if (packed & 0x80) {
      g->gct_size = 2 << (packed & 7);
      for (i = 0; i < g->gct_size; ++i) {
         g->gct[i][0] = (unsigned char) gd_byte(g);
         g->gct[i][1] = (unsigned char) gd_byte(g);
         g->gct[i][2] = (unsigned char) gd_byte(g);
      }
   }
   if (g->error)
      return 0;

   for (;;) {
      int tag = gd_byte(g);
      if (g->error)
         return 0;
      if (tag == 0x3B) // trailer
         break;
      if (tag == 0x21) { // extension
         int label = gd_byte(g);
         if (label == 0xF9) { // graphic control extension
            int size = gd_byte(g);
            int gce_packed, tindex, term;
            if (size != 4) {
               g->error = "bad graphic control extension size";
               return 0;
            }
            gce_packed = gd_byte(g);
            gce_delay = gd_word(g);
            tindex = gd_byte(g);
            term = gd_byte(g);
            if (term != 0) {
               g->error = "bad graphic control extension terminator";
               return 0;
            }
            gce_transparent = (gce_packed & 1) ? tindex : -1;
            have_gce = 1;
         } else if (label == 0xFF) { // application extension
            int size = gd_byte(g);
            unsigned char app[11];
            if (size != 11) {
               g->error = "bad application extension size";
               return 0;
            }
            for (i = 0; i < 11; ++i)
               app[i] = (unsigned char) gd_byte(g);
            if (memcmp(app, "NETSCAPE2.0", 11) == 0) {
               int sz = gd_byte(g);
               if (sz != 3 || gd_byte(g) != 1) {
                  g->error = "bad NETSCAPE extension";
                  return 0;
               }
               g->loop_count = gd_word(g);
               if (gd_byte(g) != 0) {
                  g->error = "bad NETSCAPE extension terminator";
                  return 0;
               }
            } else {
               gd_skip_subblocks(g);
            }
         } else {
            gd_skip_subblocks(g);
         }
         if (g->error)
            return 0;
      } else if (tag == 0x2C) { // image descriptor
         gd_frame *f;
         int left, top, fw, fh, lpacked;
         if (g->num_frames >= GD_MAX_FRAMES) {
            g->error = "too many frames";
            return 0;
         }
         f = &g->frames[g->num_frames];
         left = gd_word(g);
         top = gd_word(g);
         fw = gd_word(g);
         fh = gd_word(g);
         if (left != 0 || top != 0 || fw != g->w || fh != g->h) {
            g->error = "unexpected frame rectangle";
            return 0;
         }
         lpacked = gd_byte(g);
         if (lpacked & 0x40) {
            g->error = "unexpected interlaced frame";
            return 0;
         }
         f->local_palette = (lpacked & 0x80) != 0;
         if (f->local_palette) {
            f->palette_size = 2 << (lpacked & 7);
            for (i = 0; i < f->palette_size; ++i) {
               f->palette[i][0] = (unsigned char) gd_byte(g);
               f->palette[i][1] = (unsigned char) gd_byte(g);
               f->palette[i][2] = (unsigned char) gd_byte(g);
            }
         } else {
            f->palette_size = g->gct_size;
            memcpy(f->palette, g->gct, sizeof(g->gct));
         }
         f->delay_cs = have_gce ? gce_delay : 0;
         f->transparent = have_gce ? gce_transparent : -1;
         f->indices = (unsigned char *) malloc((size_t) fw * fh);
         if (f->indices == NULL) {
            g->error = "out of memory";
            return 0;
         }
         if (!gd_lzw(g, f->indices, fw * fh))
            return 0;
         ++g->num_frames;
         have_gce = 0;
         gce_delay = 0;
         gce_transparent = -1;
      } else {
         g->error = "unknown block";
         return 0;
      }
   }

   if (g->pos != g->len) {
      g->error = "trailing data after trailer";
      return 0;
   }
   return 1;
}

static void gd_free(gifdec *g)
{
   int i;
   for (i = 0; i < g->num_frames; ++i)
      free(g->frames[i].indices);
}

// composite a decoded frame to RGBA the way a viewer would: palette colors
// are opaque, the transparent index is fully transparent black
static void gd_pixel(const gifdec *g, int frame, int x, int y, unsigned char rgba[4])
{
   const gd_frame *f = &g->frames[frame];
   int idx = f->indices[y * g->w + x];
   if (idx == f->transparent) {
      rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
   } else {
      rgba[0] = f->palette[idx][0];
      rgba[1] = f->palette[idx][1];
      rgba[2] = f->palette[idx][2];
      rgba[3] = 255;
   }
}

//////////////////////////////////////////////////////////////////////////////
// test image helpers

static unsigned char *make_pixels(int w, int h, int comp)
{
   return (unsigned char *) malloc((size_t) w * h * comp);
}

// compare a decoded GIF frame against an RGBA source image
static int check_frame_rgba(const gifdec *g, int frame, const unsigned char *src, int src_comp)
{
   int x, y, errors = 0;
   for (y = 0; y < g->h; ++y) {
      for (x = 0; x < g->w; ++x) {
         unsigned char got[4];
         unsigned char want[4];
         const unsigned char *px = src + ((size_t) y * g->w + x) * src_comp;
         switch (src_comp) {
            case 1:  want[0] = want[1] = want[2] = px[0]; want[3] = 255; break;
            case 2:  want[0] = want[1] = want[2] = px[0]; want[3] = px[1]; break;
            case 3:  want[0] = px[0]; want[1] = px[1]; want[2] = px[2]; want[3] = 255; break;
            default: want[0] = px[0]; want[1] = px[1]; want[2] = px[2]; want[3] = px[3]; break;
         }
         // writer maps alpha < 128 to fully transparent
         if (want[3] < 128) {
            want[0] = want[1] = want[2] = want[3] = 0;
         } else {
            want[3] = 255;
         }
         gd_pixel(g, frame, x, y, got);
         if (memcmp(got, want, 4) != 0) {
            if (errors < 5)
               printf("  pixel mismatch at (%d,%d): got %d,%d,%d,%d want %d,%d,%d,%d\n",
                      x, y, got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
            ++errors;
         }
      }
   }
   return errors == 0;
}

static int keep_outputs = 0;

static void maybe_remove(const char *filename)
{
   if (!keep_outputs)
      remove(filename);
}

//////////////////////////////////////////////////////////////////////////////

// single frame, few distinct colors + transparency: must round-trip exactly
static void test_single_frame(void)
{
   const int w = 64, h = 48;
   unsigned char *img = make_pixels(w, h, 4);
   membuf m = { 0, 0, 0 };
   gifdec g;
   int x, y, ok, rw, rh, rc;
   unsigned char *loaded;

   static const unsigned char colors[5][3] = {
      { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 }, { 12, 34, 56 }
   };

   for (y = 0; y < h; ++y) {
      for (x = 0; x < w; ++x) {
         unsigned char *px = img + ((size_t) y * w + x) * 4;
         int cx = x - w/2, cy = y - h/2;
         if (cx*cx + cy*cy < 100) {
            // transparent hole in the middle
            px[0] = px[1] = px[2] = px[3] = 0;
         } else {
            const unsigned char *c = colors[(y / 8) % 5];
            px[0] = c[0]; px[1] = c[1]; px[2] = c[2]; px[3] = 255;
         }
      }
   }

   // write to memory
   ok = stbi_write_gif_to_func(membuf_write, &m, w, h, 4, img);
   CHECK(ok);
   CHECK(m.len > 13);

   // decode with the independent decoder
   CHECK(gd_parse(&g, m.data, m.len));
   if (g.error) printf("  parse error: %s\n", g.error);
   if (g.num_frames >= 1) {
      CHECK(g.w == w && g.h == h);
      CHECK(g.num_frames == 1);
      CHECK(g.loop_count == -1);           // no loop extension for single-frame write
      CHECK(g.frames[0].local_palette == 0); // first frame uses the global table
      CHECK(g.frames[0].transparent >= 0);   // transparency was recorded
      CHECK(g.frames[0].palette_size == 8);  // 6 colors -> 8-entry table
      CHECK(check_frame_rgba(&g, 0, img, 4));
      gd_free(&g);
   }

   // decode with stb_image from memory
   {
      stbi_uc *p = stbi_load_from_memory(m.data, (int) m.len, &rw, &rh, &rc, 4);
      CHECK(p != NULL);
      if (p) {
         int i, match = 1;
         CHECK(rw == w && rh == h);
         for (i = 0; i < w*h; ++i) {
            unsigned char want[4];
            memcpy(want, img + (size_t) i * 4, 4);
            if (want[3] < 128) { want[0] = want[1] = want[2] = want[3] = 0; }
            else want[3] = 255;
            if (memcmp(p + (size_t) i * 4, want, 4) != 0) { match = 0; break; }
         }
         CHECK(match);
         stbi_image_free(p);
      }
   }

   // write to a file and read it back with stb_image
   ok = stbi_write_gif("test_gif_single.gif", w, h, 4, img);
   CHECK(ok);
   loaded = stbi_load("test_gif_single.gif", &rw, &rh, &rc, 4);
   CHECK(loaded != NULL);
   if (loaded) {
      int i, match = 1;
      CHECK(rw == w && rh == h);
      for (i = 0; i < w*h; ++i) {
         unsigned char want[4];
         memcpy(want, img + (size_t) i * 4, 4);
         if (want[3] < 128) { want[0] = want[1] = want[2] = want[3] = 0; }
         else want[3] = 255;
         if (memcmp(loaded + (size_t) i * 4, want, 4) != 0) { match = 0; break; }
      }
      CHECK(match);
      stbi_image_free(loaded);
   }
   maybe_remove("test_gif_single.gif");

   free(m.data);
   free(img);
}

// multi-frame animation: per-frame local palettes, delays, transparency, loop
static void test_multi_frame(void)
{
   const int w = 40, h = 30, nframes = 3;
   unsigned char *frames[3] = { 0, 0, 0 };
   membuf m = { 0, 0, 0 };
   stbiw_gif gif;
   gifdec g;
   int f, x, y, ok;

   for (f = 0; f < nframes; ++f) {
      frames[f] = make_pixels(w, h, 4);
      for (y = 0; y < h; ++y) {
         for (x = 0; x < w; ++x) {
            unsigned char *px = frames[f] + ((size_t) y * w + x) * 4;
            // each frame uses a disjoint set of colors so local palettes
            // are distinguishable: frame 0 reddish, 1 greenish, 2 bluish
            // (frame 1 uses fewer colors to leave room for transparency)
            int v = ((x * 5 + y * 3) & 0xff) >> (f == 1 ? 1 : 0);
            px[0] = (unsigned char) (f == 0 ? v : 0);
            px[1] = (unsigned char) (f == 1 ? v : 0);
            px[2] = (unsigned char) (f == 2 ? v : 0);
            px[3] = 255;
            // frame 1 gets a transparent stripe
            if (f == 1 && x >= 10 && x < 20)
               px[0] = px[1] = px[2] = px[3] = 0;
         }
      }
   }

   ok = stbi_write_gif_begin_to_func(&gif, membuf_write, &m, w, h, 0); // loop forever
   CHECK(ok);
   ok = stbi_write_gif_frame(&gif, frames[0], 4, 5);
   CHECK(ok);
   ok = stbi_write_gif_frame(&gif, frames[1], 4, 15);
   CHECK(ok);
   ok = stbi_write_gif_frame(&gif, frames[2], 4, 25);
   CHECK(ok);
   ok = stbi_write_gif_end(&gif);
   CHECK(ok);

   CHECK(gd_parse(&g, m.data, m.len));
   if (g.error) printf("  parse error: %s\n", g.error);
   if (g.num_frames == 3) {
      CHECK(g.loop_count == 0);                 // NETSCAPE loop extension, forever
      CHECK(g.frames[0].local_palette == 0);    // first frame: global table
      CHECK(g.frames[1].local_palette == 1);    // later frames: local tables
      CHECK(g.frames[2].local_palette == 1);
      CHECK(g.frames[0].delay_cs == 5);
      CHECK(g.frames[1].delay_cs == 15);
      CHECK(g.frames[2].delay_cs == 25);
      CHECK(g.frames[0].transparent == -1);
      CHECK(g.frames[1].transparent >= 0);      // transparent stripe recorded
      CHECK(g.frames[2].transparent == -1);
      CHECK(check_frame_rgba(&g, 0, frames[0], 4));
      CHECK(check_frame_rgba(&g, 1, frames[1], 4));
      CHECK(check_frame_rgba(&g, 2, frames[2], 4));
   }
   gd_free(&g);

   // cross-check with stb_image's multi-frame GIF loader
   {
      int *delays = NULL, rw, rh, z, rc;
      stbi_uc *p = stbi_load_gif_from_memory(m.data, (int) m.len, &delays, &rw, &rh, &z, &rc, 4);
      CHECK(p != NULL);
      if (p) {
         int i, match;
         CHECK(rw == w && rh == h && z == 3);
         CHECK(delays != NULL);
         if (delays) {
            CHECK(delays[0] == 50);   // stb_image reports delays in ms
            CHECK(delays[1] == 150);
            CHECK(delays[2] == 250);
         }
         // frames are composited: frame 1's transparent stripe shows frame 0
         match = 1;
         for (i = 0; i < w*h; ++i) {
            unsigned char want[4];
            memcpy(want, frames[0] + (size_t) i * 4, 4);
            if (memcmp(p + (size_t) i * 4, want, 4) != 0) { match = 0; break; }
         }
         CHECK(match);
         match = 1;
         for (i = 0; i < w*h; ++i) {
            const unsigned char *src = frames[1] + (size_t) i * 4;
            unsigned char want[4];
            if (src[3] < 128)
               memcpy(want, frames[0] + (size_t) i * 4, 4); // composited over frame 0
            else {
               memcpy(want, src, 4);
               want[3] = 255;
            }
            if (memcmp(p + (size_t) (w*h + i) * 4, want, 4) != 0) { match = 0; break; }
         }
         CHECK(match);
         match = 1;
         for (i = 0; i < w*h; ++i) {
            unsigned char want[4];
            memcpy(want, frames[2] + (size_t) i * 4, 4);
            want[3] = 255;
            if (memcmp(p + (size_t) (2*w*h + i) * 4, want, 4) != 0) { match = 0; break; }
         }
         CHECK(match);
         stbi_image_free(p);
         if (delays) stbi_image_free(delays);
      }
   }

   // also write the animation to a file so it can be inspected externally
   {
      ok = stbi_write_gif_begin(&gif, "test_gif_anim.gif", w, h, 0);
      CHECK(ok);
      ok = stbi_write_gif_frame(&gif, frames[0], 4, 5) && ok;
      ok = stbi_write_gif_frame(&gif, frames[1], 4, 15) && ok;
      ok = stbi_write_gif_frame(&gif, frames[2], 4, 25) && ok;
      ok = stbi_write_gif_end(&gif) && ok;
      CHECK(ok);
      maybe_remove("test_gif_anim.gif");
   }

   for (f = 0; f < nframes; ++f)
      free(frames[f]);
   free(m.data);
}

// explicit palette + indices: exact control over palette, transparency
static void test_indexed(void)
{
   const int w = 33, h = 21;
   const int ncolors = 17;
   unsigned char palette[17*3];
   unsigned char palette2[3*3] = { 255, 0, 255, 0, 0, 0, 200, 100, 50 };
   unsigned char *idx = (unsigned char *) malloc((size_t) w * h);
   membuf m = { 0, 0, 0 };
   stbiw_gif gif;
   gifdec g;
   int i, ok;

   for (i = 0; i < ncolors; ++i) {
      palette[i*3+0] = (unsigned char) (i * 15);
      palette[i*3+1] = (unsigned char) (255 - i * 10);
      palette[i*3+2] = (unsigned char) (i * 7);
   }
   for (i = 0; i < w*h; ++i)
      idx[i] = (unsigned char) (i % ncolors);

   ok = stbi_write_gif_begin_to_func(&gif, membuf_write, &m, w, h, -1);
   CHECK(ok);
   ok = stbi_write_gif_frame_indexed(&gif, idx, palette, ncolors, 7, 3);
   CHECK(ok);
   // second frame with a different, smaller palette -> local color table
   for (i = 0; i < w*h; ++i)
      idx[i] = (unsigned char) ((i / w) % 3);
   ok = stbi_write_gif_frame_indexed(&gif, idx, palette2, 3, 9, -1);
   CHECK(ok);
   ok = stbi_write_gif_end(&gif);
   CHECK(ok);

   CHECK(gd_parse(&g, m.data, m.len));
   if (g.error) printf("  parse error: %s\n", g.error);
   if (g.num_frames == 2) {
      CHECK(g.frames[0].palette_size == 32);   // 17 colors -> 32-entry table
      CHECK(g.frames[0].transparent == 3);
      CHECK(g.frames[0].delay_cs == 7);
      CHECK(g.frames[0].local_palette == 0);
      for (i = 0; i < ncolors; ++i) {
         CHECK(g.frames[0].palette[i][0] == palette[i*3+0]);
         CHECK(g.frames[0].palette[i][1] == palette[i*3+1]);
         CHECK(g.frames[0].palette[i][2] == palette[i*3+2]);
      }
      // indices must come back exactly
      {
         int match = 1;
         for (i = 0; i < w*h; ++i)
            if (g.frames[0].indices[i] != (i % ncolors)) { match = 0; break; }
         CHECK(match);
      }
      CHECK(g.frames[1].local_palette == 1);
      CHECK(g.frames[1].palette_size == 4);    // 3 colors -> 4-entry table
      CHECK(g.frames[1].transparent == -1);
      CHECK(g.frames[1].delay_cs == 9);
      for (i = 0; i < 3; ++i) {
         CHECK(g.frames[1].palette[i][0] == palette2[i*3+0]);
         CHECK(g.frames[1].palette[i][1] == palette2[i*3+1]);
         CHECK(g.frames[1].palette[i][2] == palette2[i*3+2]);
      }
      {
         int match = 1;
         for (i = 0; i < w*h; ++i)
            if (g.frames[1].indices[i] != ((i / w) % 3)) { match = 0; break; }
         CHECK(match);
      }
   }
   gd_free(&g);

   // invalid inputs must be rejected
   idx[0] = 200; // >= ncolors
   ok = stbi_write_gif_begin_to_func(&gif, membuf_write, &m, w, h, -1);
   CHECK(ok);
   ok = stbi_write_gif_frame_indexed(&gif, idx, palette, ncolors, 0, -1);
   CHECK(!ok);
   stbi_write_gif_end(&gif);

   free(m.data);
   free(idx);
}

// write a single indexed frame to memory and check the indices round-trip
static void check_indexed_roundtrip(const unsigned char *idx, const unsigned char *palette, int ncolors, int w, int h)
{
   membuf m = { 0, 0, 0 };
   stbiw_gif gif;
   gifdec g;
   int i, ok, match;

   ok = stbi_write_gif_begin_to_func(&gif, membuf_write, &m, w, h, -1);
   CHECK(ok);
   ok = stbi_write_gif_frame_indexed(&gif, idx, palette, ncolors, 0, -1);
   CHECK(ok);
   ok = stbi_write_gif_end(&gif);
   CHECK(ok);

   CHECK(gd_parse(&g, m.data, m.len));
   if (g.error) printf("  parse error: %s\n", g.error);
   if (g.num_frames == 1) {
      match = 1;
      for (i = 0; i < w*h; ++i)
         if (g.frames[0].indices[i] != idx[i]) { match = 0; break; }
      CHECK(match);
   }
   gd_free(&g);
   free(m.data);
}

// LZW stress: large images that grow the dictionary to 4096 entries and
// force clear codes, plus long runs and code-size-transition patterns
static void test_lzw_stress(void)
{
   unsigned char palette[256*3];
   unsigned char *idx;
   int i;

   for (i = 0; i < 256; ++i) {
      palette[i*3+0] = (unsigned char) i;
      palette[i*3+1] = (unsigned char) (i * 7);
      palette[i*3+2] = (unsigned char) (255 - i);
   }

   // pseudo-random noise: dictionary fills up, clear codes are emitted
   {
      const int w = 300, h = 200;
      unsigned int state = 12345;
      idx = (unsigned char *) malloc((size_t) w * h);
      for (i = 0; i < w*h; ++i) {
         state = state * 1664525u + 1013904223u;
         idx[i] = (unsigned char) (state >> 24);
      }
      check_indexed_roundtrip(idx, palette, 256, w, h);
      free(idx);
   }

   // solid color: maximum run-length compression
   {
      const int w = 500, h = 300;
      idx = (unsigned char *) malloc((size_t) w * h);
      memset(idx, 17, (size_t) w * h);
      check_indexed_roundtrip(idx, palette, 256, w, h);
      free(idx);
   }

   // cycling 0..255: forces the dictionary through every code size
   {
      const int w = 256, h = 256;
      idx = (unsigned char *) malloc((size_t) w * h);
      for (i = 0; i < w*h; ++i)
         idx[i] = (unsigned char) (i & 0xff);
      check_indexed_roundtrip(idx, palette, 256, w, h);
      free(idx);
   }

   // tiny palettes exercise the minimum code size floor of 2
   {
      const int w = 64, h = 64;
      idx = (unsigned char *) malloc((size_t) w * h);
      for (i = 0; i < w*h; ++i)
         idx[i] = (unsigned char) ((i / w + i % w) & 1);
      check_indexed_roundtrip(idx, palette, 2, w, h);
      for (i = 0; i < w*h; ++i)
         idx[i] = (unsigned char) ((i * 7) % 3);
      check_indexed_roundtrip(idx, palette, 3, w, h);
      free(idx);
   }

   // 1x1 and single-row images
   {
      idx = (unsigned char *) malloc(97);
      idx[0] = 42;
      check_indexed_roundtrip(idx, palette, 256, 1, 1);
      for (i = 0; i < 97; ++i)
         idx[i] = (unsigned char) i;
      check_indexed_roundtrip(idx, palette, 256, 97, 1);
      check_indexed_roundtrip(idx, palette, 256, 1, 97);
      free(idx);
   }
// randomized sweep: many sizes and color counts through the pixel API,
   // decoded with the independent decoder and compared exactly
   {
      unsigned int state = 999;
      int trial;
      for (trial = 0; trial < 60; ++trial) {
         int w, h, ncolors, j, ok2;
         unsigned char *img, *pal;
         membuf m2 = { 0, 0, 0 };
         gifdec g2;
         state = state * 1664525u + 1013904223u; w = 1 + (state >> 24) % 64;
         state = state * 1664525u + 1013904223u; h = 1 + (state >> 24) % 64;
         state = state * 1664525u + 1013904223u; ncolors = 1 + (state >> 24) % 256;
         pal = (unsigned char *) malloc((size_t) ncolors * 3);
         img = (unsigned char *) malloc((size_t) w * h * 4);
         for (j = 0; j < ncolors; ++j) {
            state = state * 1664525u + 1013904223u; pal[j*3+0] = (unsigned char) (state >> 24);
            state = state * 1664525u + 1013904223u; pal[j*3+1] = (unsigned char) (state >> 24);
            state = state * 1664525u + 1013904223u; pal[j*3+2] = (unsigned char) (state >> 24);
         }
         for (j = 0; j < w*h; ++j) {
            const unsigned char *c;
            state = state * 1664525u + 1013904223u;
            c = pal + ((state >> 24) % ncolors) * 3;
            img[j*4+0] = c[0]; img[j*4+1] = c[1]; img[j*4+2] = c[2]; img[j*4+3] = 255;
         }
         ok2 = stbi_write_gif_to_func(membuf_write, &m2, w, h, 4, img);
         CHECK(ok2);
         CHECK(gd_parse(&g2, m2.data, m2.len));
         if (g2.error) printf("  parse error (trial %d, %dx%dx%d): %s\n", trial, w, h, ncolors, g2.error);
         if (g2.num_frames == 1)
            CHECK(check_frame_rgba(&g2, 0, img, 4));
         gd_free(&g2);
         free(m2.data);
         free(pal);
         free(img);
      }
   }
}

// more than 256 distinct colors: writer must quantize (lossy) but still
// produce a valid file with colors close to the source
static void test_quantize(void)
{
   const int w = 64, h = 64;
   unsigned char *img = make_pixels(w, h, 3);
   membuf m = { 0, 0, 0 };
   gifdec g;
   int x, y, ok, close_enough = 1;

   for (y = 0; y < h; ++y)
      for (x = 0; x < w; ++x) {
         unsigned char *px = img + ((size_t) y * w + x) * 3;
         px[0] = (unsigned char) (x * 4);       // 256 distinct values per channel
         px[1] = (unsigned char) (y * 4);
         px[2] = (unsigned char) ((x + y) * 2);
      }

   ok = stbi_write_gif_to_func(membuf_write, &m, w, h, 3, img);
   CHECK(ok);
   CHECK(gd_parse(&g, m.data, m.len));
   if (g.error) printf("  parse error: %s\n", g.error);
   if (g.num_frames == 1) {
      unsigned char got[4];
      CHECK(g.frames[0].transparent == -1);
      CHECK(g.frames[0].palette_size == 256); // 216-color cube -> 256-entry table
      for (y = 0; y < h && close_enough; ++y)
         for (x = 0; x < w; ++x) {
            const unsigned char *src = img + ((size_t) y * w + x) * 3;
            int c;
            gd_pixel(&g, 0, x, y, got);
            for (c = 0; c < 3; ++c)
               if (abs(got[c] - src[c]) > 43) { close_enough = 0; break; } // 6-level cube: max error 255-5*51 rounded
            if (!close_enough) break;
         }
      CHECK(close_enough);
   }
   gd_free(&g);
   free(m.data);
   free(img);
}

// grayscale and YA inputs, and stbi_flip_vertically_on_write
static void test_comp_and_flip(void)
{
   const int w = 32, h = 24;
   unsigned char *gray = make_pixels(w, h, 1);
   unsigned char *ya = make_pixels(w, h, 2);
   membuf m = { 0, 0, 0 };
   gifdec g;
   int i, ok;

   for (i = 0; i < w*h; ++i)
      gray[i] = (unsigned char) ((i * 3) & 0xff); // 256 grays, exact
   ok = stbi_write_gif_to_func(membuf_write, &m, w, h, 1, gray);
   CHECK(ok);
   CHECK(gd_parse(&g, m.data, m.len));
   if (g.num_frames == 1)
      CHECK(check_frame_rgba(&g, 0, gray, 1));
   gd_free(&g);
   free(m.data);
   m.data = 0; m.len = m.cap = 0;

   for (i = 0; i < w*h; ++i) {
      ya[i*2+0] = (unsigned char) ((i * 3) % 200); // <= 200 grays + transparent
      ya[i*2+1] = (i % 7 == 0) ? 0 : 255; // some transparent pixels
   }
   ok = stbi_write_gif_to_func(membuf_write, &m, w, h, 2, ya);
   CHECK(ok);
   CHECK(gd_parse(&g, m.data, m.len));
   if (g.num_frames == 1) {
      CHECK(g.frames[0].transparent >= 0);
      CHECK(check_frame_rgba(&g, 0, ya, 2));
   }
   gd_free(&g);
   free(m.data);
   m.data = 0; m.len = m.cap = 0;

   // flip: written image must be the source upside down
   stbi_flip_vertically_on_write(1);
   ok = stbi_write_gif_to_func(membuf_write, &m, w, h, 1, gray);
   CHECK(ok);
   stbi_flip_vertically_on_write(0);
   CHECK(gd_parse(&g, m.data, m.len));
   if (g.num_frames == 1) {
      int x, y, match = 1;
      for (y = 0; y < h && match; ++y)
         for (x = 0; x < w; ++x) {
            unsigned char got[4];
            gd_pixel(&g, 0, x, y, got);
            if (got[0] != gray[(h - 1 - y) * w + x] || got[3] != 255) { match = 0; break; }
         }
      CHECK(match);
   }
   gd_free(&g);
   free(m.data);

   free(gray);
   free(ya);
}

// the other writers must be unaffected by the GIF addition
static void test_other_formats(void)
{
   const int w = 37, h = 23;
   unsigned char *img = make_pixels(w, h, 4);
   membuf m = { 0, 0, 0 };
   int i, ok, rw, rh, rc;

   for (i = 0; i < w*h; ++i) {
      img[i*4+0] = (unsigned char) (i * 3);
      img[i*4+1] = (unsigned char) (i * 5);
      img[i*4+2] = (unsigned char) (i * 7);
      img[i*4+3] = 255;
   }

   // PNG round-trips exactly
   ok = stbi_write_png_to_func(membuf_write, &m, w, h, 4, img, 0);
   CHECK(ok);
   {
      stbi_uc *p = stbi_load_from_memory(m.data, (int) m.len, &rw, &rh, &rc, 4);
      CHECK(p != NULL);
      if (p) {
         CHECK(rw == w && rh == h);
         CHECK(memcmp(p, img, (size_t) w * h * 4) == 0);
         stbi_image_free(p);
      }
   }
   free(m.data);
   m.data = 0; m.len = m.cap = 0;

   // BMP: check the written file's structure (this repo's stb_image BMP
   // reader has a pre-existing signedness bug and can't be used here)
   ok = stbi_write_bmp_to_func(membuf_write, &m, w, h, 3, img);
   CHECK(ok);
   CHECK(m.len == 54 + ((size_t) w * 3 + 3) / 4 * 4 * h);
   CHECK(m.len > 54 && m.data[0] == 'B' && m.data[1] == 'M');
   if (m.len >= 54) {
      unsigned int fsize, fw, fh, bpp;
      memcpy(&fsize, m.data + 2, 4);
      memcpy(&fw, m.data + 18, 4);
      memcpy(&fh, m.data + 22, 4);
      memcpy(&bpp, m.data + 28, 2);
      CHECK(fsize == m.len);
      CHECK(fw == (unsigned int) w && fh == (unsigned int) h);
      CHECK(bpp == 24);
   }
   free(m.data);
   m.data = 0; m.len = m.cap = 0;

   // TGA round-trips exactly
   ok = stbi_write_tga_to_func(membuf_write, &m, w, h, 4, img);
   CHECK(ok);
   {
      stbi_uc *p = stbi_load_from_memory(m.data, (int) m.len, &rw, &rh, &rc, 4);
      CHECK(p != NULL);
      if (p) {
         CHECK(rw == w && rh == h);
         CHECK(memcmp(p, img, (size_t) w * h * 4) == 0);
         stbi_image_free(p);
      }
   }
   free(m.data);

   free(img);
}

// edge cases: all-transparent frame, zero-frame file, invalid arguments
static void test_edge_cases(void)
{
   membuf m = { 0, 0, 0 };
   stbiw_gif gif;
   gifdec g;
   int ok;

   // frame where every pixel is transparent: single-color palette
   {
      unsigned char img[8*8*4];
      memset(img, 0, sizeof(img));
      ok = stbi_write_gif_to_func(membuf_write, &m, 8, 8, 4, img);
      CHECK(ok);
      CHECK(gd_parse(&g, m.data, m.len));
      if (g.error) printf("  parse error: %s\n", g.error);
      if (g.num_frames == 1) {
         int i, match = 1;
         CHECK(g.frames[0].transparent >= 0);
         CHECK(g.frames[0].palette_size == 2);
         for (i = 0; i < 64; ++i)
            if (g.frames[0].indices[i] != g.frames[0].transparent) { match = 0; break; }
         CHECK(match);
      }
      gd_free(&g);
      free(m.data);
      m.data = 0; m.len = m.cap = 0;
   }

   // begin + end with no frames: still a structurally valid GIF
   ok = stbi_write_gif_begin_to_func(&gif, membuf_write, &m, 10, 10, -1);
   CHECK(ok);
   ok = stbi_write_gif_end(&gif);
   CHECK(ok);
   CHECK(m.len == 6 + 7 + 6 + 1); // header + LSD + 2-color GCT + trailer
   CHECK(m.len > 0 && memcmp(m.data, "GIF89a", 6) == 0);
   CHECK(m.len > 0 && m.data[m.len-1] == 0x3B);
   free(m.data);
   m.data = 0; m.len = m.cap = 0;

   // invalid arguments are rejected
   {
      unsigned char img[4*4*4];
      unsigned char pal[3] = { 0, 0, 0 };
      unsigned char idx[16] = { 0 };
      memset(img, 0, sizeof(img));
      CHECK(!stbi_write_gif_begin_to_func(&gif, membuf_write, &m, 0, 4, -1)); // w=0
      CHECK(!stbi_write_gif_begin_to_func(&gif, membuf_write, &m, 4, -3, -1)); // h<0
      CHECK(!stbi_write_gif_begin_to_func(&gif, membuf_write, &m, 70000, 4, -1)); // too wide
      CHECK(!stbi_write_gif_to_func(membuf_write, &m, 4, 4, 5, img)); // bad comp
      CHECK(!stbi_write_gif_to_func(membuf_write, &m, 4, 4, 0, img)); // bad comp
      ok = stbi_write_gif_begin_to_func(&gif, membuf_write, &m, 4, 4, -1);
      CHECK(ok);
      CHECK(!stbi_write_gif_frame(&gif, NULL, 4, 0));            // NULL pixels
      CHECK(!stbi_write_gif_frame(&gif, img, 9, 0));             // bad comp
      CHECK(!stbi_write_gif_frame_indexed(&gif, idx, pal, 0, 0, -1));   // no colors
      CHECK(!stbi_write_gif_frame_indexed(&gif, idx, pal, 257, 0, -1)); // too many colors
      CHECK(!stbi_write_gif_frame_indexed(&gif, idx, pal, 1, 0, 1));    // transparent out of range
      CHECK(!stbi_write_gif_frame_indexed(&gif, idx, pal, 1, 0, -2));   // transparent out of range
      CHECK(stbi_write_gif_frame_indexed(&gif, idx, pal, 1, 0, 0));     // ...but this one is fine
      ok = stbi_write_gif_end(&gif);
      CHECK(ok);
      free(m.data);
      m.data = 0; m.len = m.cap = 0;
   }
}

int main(void)
{
   keep_outputs = getenv("GIF_TEST_KEEP") != NULL;

   test_single_frame();
   test_multi_frame();
   test_indexed();
   test_lzw_stress();
   test_quantize();
   test_comp_and_flip();
   test_edge_cases();
   test_other_formats();

   printf("%d checks, %d failures\n", num_checks, num_failures);
   return num_failures ? 1 : 0;
}
