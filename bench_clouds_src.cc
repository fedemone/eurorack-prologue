/*
 * File: bench_clouds_src.cc
 *
 * What does CloudsFX's 48 kHz <-> 32 kHz conversion cost on its own?
 *
 * bench-units says the FX unit runs 4.6-6.7 points of mean higher than the
 * synth across the four modes.  Both build the same engine from the same
 * sources, so that difference is not the engine -- but "not the engine" is a
 * subtraction between two runs, and a subtraction is not a measurement.  This
 * measures the suspect directly.
 *
 * It drives exactly the call pattern of clouds_fx_process() and
 * engine_block() with the engine removed: both SrcDowns, both SrcUps, the
 * InputNeeded() pull loop that decides when a block falls due, and the FIFO
 * memmoves around them.  Everything CloudsFX does per render except
 * GranularProcessor.  The synth runs one converter where this runs four,
 * which is where the asymmetry comes from.
 *
 * Reported in bench-units' units -- percent of the deadline for one 64-frame
 * render at 48 kHz -- so the rows can be put beside each other.  ARM under
 * QEMU, so relative only.
 *
 * Build/run: make bench-clouds-src
 */
#include "clouds_src.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <vector>
#include <cmath>

static const size_t kMaxBlockSize = 32;
static clouds_src::SrcDown src_down_l_, src_down_r_;
static clouds_src::SrcUp   src_up_l_,   src_up_r_;
static const int kInFifoSize = 256, kEngFifoSize = 128, kChunk = 64, kInPrime = 48;
static float in_l_[kInFifoSize], in_r_[kInFifoSize];
static float eng_l_[kEngFifoSize], eng_r_[kEngFifoSize];
static int in_avail_ = 0, eng_avail_ = 0;

static double now_s() { timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
  return t.tv_sec + 1e-9*t.tv_nsec; }

static void engine_block_no_engine() {
  const int need48 = src_down_l_.InputNeeded((int)kMaxBlockSize);
  if (in_avail_ < need48) {
    memset(&in_l_[in_avail_],0,(need48-in_avail_)*sizeof(float));
    memset(&in_r_[in_avail_],0,(need48-in_avail_)*sizeof(float));
    in_avail_ = need48;
  }
  memcpy(src_down_l_.Input(), in_l_, need48*sizeof(float));
  memcpy(src_down_r_.Input(), in_r_, need48*sizeof(float));
  in_avail_ -= need48;
  memmove(in_l_, in_l_+need48, in_avail_*sizeof(float));
  memmove(in_r_, in_r_+need48, in_avail_*sizeof(float));
  float dl[kMaxBlockSize], dr[kMaxBlockSize];
  src_down_l_.Process(dl,(int)kMaxBlockSize,1);
  src_down_r_.Process(dr,(int)kMaxBlockSize,1);
  /* the engine would run here; instead just move the samples across so the
   * FIFO bookkeeping below is identical */
  for (size_t i=0;i<kMaxBlockSize;++i){ eng_l_[eng_avail_+i]=dl[i]*29490.f;
                                        eng_r_[eng_avail_+i]=dr[i]*29490.f; }
  eng_avail_ += (int)kMaxBlockSize;
}

int main() {
  src_down_l_.Init(); src_down_r_.Init(); src_up_l_.Init(); src_up_r_.Init();
  memset(in_l_,0,sizeof in_l_); memset(in_r_,0,sizeof in_r_);
  memset(eng_l_,0,sizeof eng_l_); memset(eng_r_,0,sizeof eng_r_);
  in_avail_ = kInPrime; eng_avail_ = 0;

  const uint32_t frames = 64, renders = 4000;
  const double deadline = frames / 48000.0;
  static float in[128*2], out[128*2];
  double ph = 0.0;
  std::vector<double> t; t.reserve(renders);

  for (uint32_t r=0; r<renders; ++r) {
    for (uint32_t i=0;i<frames;++i){ float x=(float)(0.6*sin(ph)+0.3*sin(ph*3.7));
      ph+=0.03; in[i*2]=x; in[i*2+1]=-x*0.8f; }
    const double t0 = now_s();
    uint32_t done = 0;
    while (done < frames) {
      int n = (int)(frames-done); if (n > kChunk) n = kChunk;
      if (in_avail_ > kInFifoSize-kChunk) {
        int drop = in_avail_-(kInFifoSize-kChunk);
        memmove(in_l_,in_l_+drop,(in_avail_-drop)*sizeof(float));
        memmove(in_r_,in_r_+drop,(in_avail_-drop)*sizeof(float));
        in_avail_ -= drop;
      }
      const float *src = in + done*2;
      for (int i=0;i<n;++i){ in_l_[in_avail_+i]=src[i*2]; in_r_[in_avail_+i]=src[i*2+1]; }
      in_avail_ += n;
      const int need32 = src_up_l_.InputNeeded(n);
      while (eng_avail_ < need32) engine_block_no_engine();
      memcpy(src_up_l_.Input(), eng_l_, need32*sizeof(float));
      memcpy(src_up_r_.Input(), eng_r_, need32*sizeof(float));
      eng_avail_ -= need32;
      memmove(eng_l_, eng_l_+need32, eng_avail_*sizeof(float));
      memmove(eng_r_, eng_r_+need32, eng_avail_*sizeof(float));
      float ol[kChunk], orr[kChunk];
      src_up_l_.Process(ol,n,1); src_up_r_.Process(orr,n,1);
      float *dst = out + done*2;
      for (int i=0;i<n;++i){ dst[i*2]=ol[i]*(0.75f/32768.f); dst[i*2+1]=orr[i]*(0.75f/32768.f); }
      done += n;
    }
    t.push_back(now_s()-t0);
  }
  std::sort(t.begin(), t.end());
  double sum=0; for (double v: t) sum+=v;
  printf("SRC + FIFO only, %u frames (%.2f ms deadline), %u renders\n",
         frames, deadline*1e3, renders);
  printf("%-14s %7.1f%% %7.1f%% %7.1f%%\n", "src-only",
         100.0*(sum/renders)/deadline,
         100.0*t[(size_t)(renders*0.99)]/deadline,
         100.0*t[(size_t)(renders*0.999)]/deadline);
  return (int)out[0]*0;
}
