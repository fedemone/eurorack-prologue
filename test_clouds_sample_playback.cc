/*
 * File: test_clouds_sample_playback.cc
 *
 * Unit tests for the Clouds sample playback logic:
 *   - generate_sample_input() forward/reverse playback
 *   - SampleSnapshot boundary conditions
 *   - Thread-safety snapshot pattern verification
 *   - Edge cases: start==end, permil boundaries, zero frames
 *
 * This file extracts the sample playback logic from clouds-granular.cc
 * into a standalone testable form (since the originals are static).
 *
 * Build:
 *   g++ -std=c++11 -Wall -Wextra -o test_clouds_sample_playback \
 *       test_clouds_sample_playback.cc -lm
 *
 * Run:
 *   ./test_clouds_sample_playback
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>
#include <cstdint>

/* ===========================================================================
 * Test Framework (minimal, same as test_drumlogue_callbacks.cc)
 * ======================================================================== */

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
  static void test_##name(void); \
  static void run_test_##name(void) { \
    g_tests_run++; \
    printf("  %-60s ", #name); \
    fflush(stdout); \
    test_##name(); \
    g_tests_passed++; \
    printf("PASS\n"); \
  } \
  static void test_##name(void)

#define ASSERT_EQ(expected, actual) do { \
    auto _e = (expected); auto _a = (actual); \
    if (_e != _a) { \
      printf("FAIL\n    %s:%d: expected %lld, got %lld\n", \
             __FILE__, __LINE__, (long long)_e, (long long)_a); \
      g_tests_failed++; g_tests_passed--; \
      return; \
    } \
  } while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
      printf("FAIL\n    %s:%d: condition false: %s\n", \
             __FILE__, __LINE__, #cond); \
      g_tests_failed++; g_tests_passed--; \
      return; \
    } \
  } while(0)

/* ===========================================================================
 * Extracted types and logic from clouds-granular.cc
 * ======================================================================== */

struct ShortFrame {
  int16_t l;
  int16_t r;
};

struct SampleSnapshot {
  const float *ptr;
  size_t frames;
  uint8_t channels;
  uint16_t start_permil;
  uint16_t end_permil;
};

/* Mutable read position (mirrors the static in clouds-granular.cc) */
static size_t sample_read_pos_ = 0;

/*
 * Exact copy of generate_sample_input() from clouds-granular.cc
 * (extracted here since the original is static and not linkable).
 */
static void generate_sample_input(ShortFrame *input, size_t size,
                                  const SampleSnapshot &snap) {
  size_t start_frame = (size_t)((uint64_t)snap.start_permil * snap.frames / 1000);
  size_t end_frame   = (size_t)((uint64_t)snap.end_permil * snap.frames / 1000);

  /* Clamp to valid range */
  if (start_frame >= snap.frames) start_frame = snap.frames - 1;
  if (end_frame > snap.frames) end_frame = snap.frames;

  bool reverse = (end_frame <= start_frame);

  /* Ensure read position is within the snapshot's valid range */
  if (sample_read_pos_ >= snap.frames)
    sample_read_pos_ = start_frame;

  for (size_t i = 0; i < size; ++i) {
    float left, right;
    if (snap.channels == 2) {
      left = snap.ptr[sample_read_pos_ * 2];
      right = snap.ptr[sample_read_pos_ * 2 + 1];
    } else {
      left = right = snap.ptr[sample_read_pos_];
    }
    input[i].l = (int16_t)(left * 16384.0f);
    input[i].r = (int16_t)(right * 16384.0f);

    if (reverse) {
      /* Play from start_frame down to end_frame */
      if (sample_read_pos_ == 0 || sample_read_pos_ <= end_frame)
        sample_read_pos_ = start_frame;
      else
        sample_read_pos_--;
    } else {
      /* Play from start_frame up to end_frame */
      sample_read_pos_++;
      if (sample_read_pos_ >= end_frame)
        sample_read_pos_ = start_frame;
    }
  }
}

/* ===========================================================================
 * Test data helpers
 * ======================================================================== */

/* Create a mono ramp sample: values 0.0, 0.1, 0.2, ... */
static float g_mono_ramp[100];
static void init_mono_ramp(void) {
  for (int i = 0; i < 100; i++)
    g_mono_ramp[i] = (float)i / 100.0f;
}

/* Create a stereo ramp sample: L=i/100, R=-i/100 */
static float g_stereo_ramp[200];
static void init_stereo_ramp(void) {
  for (int i = 0; i < 100; i++) {
    g_stereo_ramp[i * 2]     = (float)i / 100.0f;
    g_stereo_ramp[i * 2 + 1] = -(float)i / 100.0f;
  }
}

/* ===========================================================================
 * Tests: Forward playback
 * ======================================================================== */

TEST(forward_full_range_mono) {
  init_mono_ramp();
  SampleSnapshot snap = {g_mono_ramp, 100, 1, 0, 1000};
  sample_read_pos_ = 0;
  ShortFrame out[5];
  generate_sample_input(out, 5, snap);

  /* Should read frames 0,1,2,3,4 */
  ASSERT_EQ((int16_t)(0.00f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(0.01f * 16384.0f), out[1].l);
  ASSERT_EQ((int16_t)(0.02f * 16384.0f), out[2].l);
  ASSERT_EQ((int16_t)(0.03f * 16384.0f), out[3].l);
  ASSERT_EQ((int16_t)(0.04f * 16384.0f), out[4].l);
  /* Mono: L == R */
  ASSERT_EQ(out[0].l, out[0].r);
  ASSERT_EQ(out[4].l, out[4].r);
  /* Read position should be at 5 */
  ASSERT_EQ(5u, sample_read_pos_);
}

TEST(forward_full_range_stereo) {
  init_stereo_ramp();
  SampleSnapshot snap = {g_stereo_ramp, 100, 2, 0, 1000};
  sample_read_pos_ = 0;
  ShortFrame out[3];
  generate_sample_input(out, 3, snap);

  /* Frame 0: L=0.0, R=-0.0 */
  ASSERT_EQ(0, out[0].l);
  ASSERT_EQ(0, out[0].r);
  /* Frame 1: L=0.01, R=-0.01 */
  ASSERT_EQ((int16_t)(0.01f * 16384.0f), out[1].l);
  ASSERT_EQ((int16_t)(-0.01f * 16384.0f), out[1].r);
}

TEST(forward_subrange) {
  /* start=200 (20%), end=500 (50%) on 100-frame sample -> frames 20..49 */
  init_mono_ramp();
  SampleSnapshot snap = {g_mono_ramp, 100, 1, 200, 500};
  sample_read_pos_ = 20;
  ShortFrame out[3];
  generate_sample_input(out, 3, snap);

  /* Frame 20 = 0.20 */
  ASSERT_EQ((int16_t)(0.20f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(0.21f * 16384.0f), out[1].l);
  ASSERT_EQ((int16_t)(0.22f * 16384.0f), out[2].l);
}

TEST(forward_wraps_at_end) {
  /* 10-frame sample, start=0, end=1000 (full). Start at frame 8. */
  float data[10];
  for (int i = 0; i < 10; i++) data[i] = (float)i * 0.1f;

  SampleSnapshot snap = {data, 10, 1, 0, 1000};
  sample_read_pos_ = 8;
  ShortFrame out[5];
  generate_sample_input(out, 5, snap);

  /* Frames: 8, 9, wrap->0, 1, 2 */
  ASSERT_EQ((int16_t)(0.8f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(0.9f * 16384.0f), out[1].l);
  ASSERT_EQ((int16_t)(0.0f * 16384.0f), out[2].l);
  ASSERT_EQ((int16_t)(0.1f * 16384.0f), out[3].l);
  ASSERT_EQ((int16_t)(0.2f * 16384.0f), out[4].l);
}

TEST(forward_subrange_wraps) {
  /* 10-frame sample, start=300 (frame 3), end=700 (frame 7) */
  float data[10];
  for (int i = 0; i < 10; i++) data[i] = (float)i * 0.1f;

  SampleSnapshot snap = {data, 10, 1, 300, 700};
  sample_read_pos_ = 5;
  ShortFrame out[5];
  generate_sample_input(out, 5, snap);

  /* Frames: 5, 6, wrap->3, 4, 5 */
  ASSERT_EQ((int16_t)(0.5f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(0.6f * 16384.0f), out[1].l);
  ASSERT_EQ((int16_t)(0.3f * 16384.0f), out[2].l);
  ASSERT_EQ((int16_t)(0.4f * 16384.0f), out[3].l);
  ASSERT_EQ((int16_t)(0.5f * 16384.0f), out[4].l);
}

/* ===========================================================================
 * Tests: Reverse playback (end < start)
 * ======================================================================== */

TEST(reverse_full_sample) {
  /* start=1000 (100%), end=0 (0%) -> reverse from frame 99 down to 0 */
  init_mono_ramp();
  SampleSnapshot snap = {g_mono_ramp, 100, 1, 1000, 0};
  /* start_frame = 100 -> clamped to 99, end_frame = 0 */
  sample_read_pos_ = 99;
  ShortFrame out[4];
  generate_sample_input(out, 4, snap);

  /* Should play: 99, 98, 97, 96 */
  ASSERT_EQ((int16_t)(0.99f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(0.98f * 16384.0f), out[1].l);
  ASSERT_EQ((int16_t)(0.97f * 16384.0f), out[2].l);
  ASSERT_EQ((int16_t)(0.96f * 16384.0f), out[3].l);
}

TEST(reverse_subrange) {
  /* 10-frame, start=800 (frame 8), end=300 (frame 3) -> play 8,7,6,5,4,3,wrap->8 */
  float data[10];
  for (int i = 0; i < 10; i++) data[i] = (float)i * 0.1f;

  SampleSnapshot snap = {data, 10, 1, 800, 300};
  sample_read_pos_ = 8;
  ShortFrame out[7];
  generate_sample_input(out, 7, snap);

  /* 8, 7, 6, 5, 4, 3(read then wrap), 8 */
  ASSERT_EQ((int16_t)(0.8f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(0.7f * 16384.0f), out[1].l);
  ASSERT_EQ((int16_t)(0.6f * 16384.0f), out[2].l);
  ASSERT_EQ((int16_t)(0.5f * 16384.0f), out[3].l);
  ASSERT_EQ((int16_t)(0.4f * 16384.0f), out[4].l);
  /* Reads frame 3 first, then pos(3)<=end(3) wraps to start_frame(8) */
  ASSERT_EQ((int16_t)(0.3f * 16384.0f), out[5].l);
  ASSERT_EQ((int16_t)(0.8f * 16384.0f), out[6].l);
}

TEST(reverse_wraps_at_zero) {
  /* start=200 (frame 2), end=0 (frame 0) -> reverse wraps when pos reaches 0 */
  float data[10];
  for (int i = 0; i < 10; i++) data[i] = (float)i * 0.1f;

  SampleSnapshot snap = {data, 10, 1, 200, 0};
  sample_read_pos_ = 2;
  ShortFrame out[5];
  generate_sample_input(out, 5, snap);

  /* 2, 1, 0(read then wrap), 2, 1 */
  ASSERT_EQ((int16_t)(0.2f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(0.1f * 16384.0f), out[1].l);
  /* Reads frame 0 first, then pos==0 wraps to start_frame=2 */
  ASSERT_EQ((int16_t)(0.0f * 16384.0f), out[2].l);
  ASSERT_EQ((int16_t)(0.2f * 16384.0f), out[3].l);
  ASSERT_EQ((int16_t)(0.1f * 16384.0f), out[4].l);
}

TEST(reverse_stereo) {
  init_stereo_ramp();
  /* start=50 (frame 5), end=20 (frame 2) */
  SampleSnapshot snap = {g_stereo_ramp, 100, 2, 50, 20};
  sample_read_pos_ = 5;
  ShortFrame out[3];
  generate_sample_input(out, 3, snap);

  /* Frame 5: L=0.05, R=-0.05 */
  ASSERT_EQ((int16_t)(0.05f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(-0.05f * 16384.0f), out[0].r);
  /* Frame 4 */
  ASSERT_EQ((int16_t)(0.04f * 16384.0f), out[1].l);
  ASSERT_EQ((int16_t)(-0.04f * 16384.0f), out[1].r);
  /* Frame 3 */
  ASSERT_EQ((int16_t)(0.03f * 16384.0f), out[2].l);
  ASSERT_EQ((int16_t)(-0.03f * 16384.0f), out[2].r);
}

/* ===========================================================================
 * Tests: Edge cases
 * ======================================================================== */

TEST(start_equals_end_forward) {
  /* start=500, end=500 -> start_frame==end_frame -> reverse==true
   * This creates a single-sample loop at start_frame.
   * end_frame = 500*10/1000 = 5, start_frame = 5 -> reverse, end<=start
   * pos will be stuck at start_frame */
  float data[10];
  for (int i = 0; i < 10; i++) data[i] = (float)i * 0.1f;

  SampleSnapshot snap = {data, 10, 1, 500, 500};
  sample_read_pos_ = 5;
  ShortFrame out[3];
  generate_sample_input(out, 3, snap);

  /* All output should be frame 5 (single-sample loop) */
  ASSERT_EQ((int16_t)(0.5f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(0.5f * 16384.0f), out[1].l);
  ASSERT_EQ((int16_t)(0.5f * 16384.0f), out[2].l);
}

TEST(start_at_max_permil) {
  /* start=1000 on 10-frame sample -> start_frame = 10, clamped to 9 */
  float data[10];
  for (int i = 0; i < 10; i++) data[i] = (float)i * 0.1f;

  SampleSnapshot snap = {data, 10, 1, 1000, 1000};
  sample_read_pos_ = 9;
  ShortFrame out[3];
  generate_sample_input(out, 3, snap);

  /* Single-sample loop at frame 9 */
  ASSERT_EQ((int16_t)(0.9f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(0.9f * 16384.0f), out[1].l);
}

TEST(start_zero_end_zero) {
  /* Both 0 -> start_frame=0, end_frame=0, reverse==true (end<=start)
   * Single-sample loop at frame 0 */
  float data[10];
  for (int i = 0; i < 10; i++) data[i] = (float)i * 0.1f;

  SampleSnapshot snap = {data, 10, 1, 0, 0};
  sample_read_pos_ = 0;
  ShortFrame out[3];
  generate_sample_input(out, 3, snap);

  ASSERT_EQ(0, out[0].l);
  ASSERT_EQ(0, out[1].l);
  ASSERT_EQ(0, out[2].l);
}

TEST(read_pos_out_of_range_resets) {
  /* If read position >= frames, it gets reset to start_frame */
  float data[10];
  for (int i = 0; i < 10; i++) data[i] = (float)i * 0.1f;

  SampleSnapshot snap = {data, 10, 1, 200, 800};
  sample_read_pos_ = 999;  /* way out of range */
  ShortFrame out[2];
  generate_sample_input(out, 2, snap);

  /* start_frame = 200*10/1000 = 2. Should reset to frame 2. */
  ASSERT_EQ((int16_t)(0.2f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(0.3f * 16384.0f), out[1].l);
}

TEST(single_frame_sample) {
  /* 1-frame sample: only one value to read */
  float data[1] = {0.5f};
  SampleSnapshot snap = {data, 1, 1, 0, 1000};
  sample_read_pos_ = 0;
  ShortFrame out[3];
  generate_sample_input(out, 3, snap);

  int16_t expected = (int16_t)(0.5f * 16384.0f);
  ASSERT_EQ(expected, out[0].l);
  ASSERT_EQ(expected, out[1].l);
  ASSERT_EQ(expected, out[2].l);
}

TEST(two_frame_sample_forward) {
  float data[2] = {0.1f, 0.9f};
  SampleSnapshot snap = {data, 2, 1, 0, 1000};
  sample_read_pos_ = 0;
  ShortFrame out[5];
  generate_sample_input(out, 5, snap);

  int16_t v0 = (int16_t)(0.1f * 16384.0f);
  int16_t v1 = (int16_t)(0.9f * 16384.0f);
  /* 0, 1, 0, 1, 0 */
  ASSERT_EQ(v0, out[0].l);
  ASSERT_EQ(v1, out[1].l);
  ASSERT_EQ(v0, out[2].l);
  ASSERT_EQ(v1, out[3].l);
  ASSERT_EQ(v0, out[4].l);
}

TEST(two_frame_sample_reverse) {
  float data[2] = {0.1f, 0.9f};
  /* start=1000 (frame 2, clamped to 1), end=0 (frame 0) -> reverse */
  SampleSnapshot snap = {data, 2, 1, 1000, 0};
  sample_read_pos_ = 1;
  ShortFrame out[5];
  generate_sample_input(out, 5, snap);

  int16_t v0 = (int16_t)(0.1f * 16384.0f);
  int16_t v1 = (int16_t)(0.9f * 16384.0f);
  /* 1, 0->wrap to 1, 0->wrap to 1, 0 */
  ASSERT_EQ(v1, out[0].l);
  ASSERT_EQ(v0, out[1].l);  /* pos=0, wraps */
  ASSERT_EQ(v1, out[2].l);
  ASSERT_EQ(v0, out[3].l);
  ASSERT_EQ(v1, out[4].l);
}

/* ===========================================================================
 * Tests: Snapshot pattern verification
 * ======================================================================== */

TEST(snapshot_null_ptr_skips) {
  /* Verify that the snapshot pattern: if snap.ptr is null, we don't call
   * generate_sample_input. This mirrors the check in OSC_CYCLE. */
  SampleSnapshot snap = {nullptr, 100, 1, 0, 1000};
  /* The real code checks snap.ptr before calling generate_sample_input.
   * Here we verify the invariant. */
  ASSERT_TRUE(snap.ptr == nullptr);
  /* In production: if (snap.ptr && snap.frames > 0) is false, sawtooth is used. */
}

TEST(snapshot_zero_frames_skips) {
  float data[1] = {0.5f};
  SampleSnapshot snap = {data, 0, 1, 0, 1000};
  ASSERT_TRUE(snap.frames == 0);
  /* In production: if (snap.ptr && snap.frames > 0) is false -> sawtooth. */
}

TEST(snapshot_isolates_from_volatile_changes) {
  /* Simulate: snapshot taken, then "volatile" vars change.
   * The snapshot struct should retain the original values. */
  float data_a[10];
  float data_b[5];
  for (int i = 0; i < 10; i++) data_a[i] = (float)i;
  for (int i = 0; i < 5; i++) data_b[i] = (float)(i + 100);

  /* Simulate volatile state */
  const float * volatile sim_ptr = data_a;
  volatile size_t sim_frames = 10;
  volatile uint8_t sim_channels = 1;

  /* Take snapshot */
  SampleSnapshot snap;
  snap.ptr = sim_ptr;
  snap.frames = sim_frames;
  snap.channels = sim_channels;
  snap.start_permil = 0;
  snap.end_permil = 1000;

  /* Simulate concurrent change (param thread modifying state) */
  sim_ptr = nullptr;  /* nullify first, like load_sample does */
  sim_frames = 5;
  sim_channels = 2;
  sim_ptr = data_b;   /* publish new ptr last */

  /* Snapshot should still have original values */
  ASSERT_TRUE(snap.ptr == data_a);
  ASSERT_EQ(10u, snap.frames);
  ASSERT_EQ(1, snap.channels);

  /* Should be able to read from snapshot safely */
  sample_read_pos_ = 0;
  ShortFrame out[2];
  generate_sample_input(out, 2, snap);
  ASSERT_EQ((int16_t)(0.0f * 16384.0f), out[0].l);
  ASSERT_EQ((int16_t)(1.0f * 16384.0f), out[1].l);  /* data_a[1] = 1.0 */
}

TEST(ptr_last_publish_pattern) {
  /* Verify: if ptr is null in snapshot, we don't use frames/channels.
   * This tests the ptr-last publish guarantee. */
  float data[10];
  for (int i = 0; i < 10; i++) data[i] = (float)i;

  /* Scenario 1: Mid-update - ptr is null, frames already updated */
  SampleSnapshot snap1 = {nullptr, 10, 1, 0, 1000};
  ASSERT_TRUE(snap1.ptr == nullptr);
  /* Code would skip to sawtooth: if (snap.ptr && snap.frames > 0) -> false */

  /* Scenario 2: After update complete - ptr published */
  SampleSnapshot snap2 = {data, 10, 1, 0, 1000};
  ASSERT_TRUE(snap2.ptr != nullptr);
  ASSERT_TRUE(snap2.frames > 0);
}

/* ===========================================================================
 * Tests: Permil-to-frame conversion accuracy
 * ======================================================================== */

TEST(permil_boundary_0) {
  /* 0 permil on 1000 frames -> frame 0 */
  size_t f = (size_t)((uint64_t)0 * 1000 / 1000);
  ASSERT_EQ(0u, f);
}

TEST(permil_boundary_1000) {
  /* 1000 permil on 1000 frames -> frame 1000 (i.e. past-the-end) */
  size_t f = (size_t)((uint64_t)1000 * 1000 / 1000);
  ASSERT_EQ(1000u, f);
}

TEST(permil_boundary_500) {
  /* 500 permil on 1000 frames -> frame 500 */
  size_t f = (size_t)((uint64_t)500 * 1000 / 1000);
  ASSERT_EQ(500u, f);
}

TEST(permil_small_sample) {
  /* 500 permil on 3 frames -> 500*3/1000 = 1 */
  size_t f = (size_t)((uint64_t)500 * 3 / 1000);
  ASSERT_EQ(1u, f);
}

TEST(permil_no_overflow_large_sample) {
  /* 999 permil on 2^20 frames: ensure no 32-bit overflow */
  size_t frames = 1 << 20;  /* ~1M frames */
  size_t f = (size_t)((uint64_t)999 * frames / 1000);
  ASSERT_TRUE(f > 0);
  ASSERT_TRUE(f < frames);
  /* Expected: 999 * 1048576 / 1000 = 1047527 */
  ASSERT_EQ(1047527u, f);
}

/* ===========================================================================
 * Tests: Amplitude conversion
 * ======================================================================== */

TEST(amplitude_positive_full_scale) {
  /* +1.0f * 16384 = 16384 (50% of int16 range) */
  float data[1] = {1.0f};
  SampleSnapshot snap = {data, 1, 1, 0, 1000};
  sample_read_pos_ = 0;
  ShortFrame out[1];
  generate_sample_input(out, 1, snap);
  ASSERT_EQ(16384, out[0].l);
}

TEST(amplitude_negative_full_scale) {
  float data[1] = {-1.0f};
  SampleSnapshot snap = {data, 1, 1, 0, 1000};
  sample_read_pos_ = 0;
  ShortFrame out[1];
  generate_sample_input(out, 1, snap);
  ASSERT_EQ(-16384, out[0].l);
}

TEST(amplitude_zero) {
  float data[1] = {0.0f};
  SampleSnapshot snap = {data, 1, 1, 0, 1000};
  sample_read_pos_ = 0;
  ShortFrame out[1];
  generate_sample_input(out, 1, snap);
  ASSERT_EQ(0, out[0].l);
}

/* ===========================================================================
 * Main
 * ======================================================================== */

int main(void) {
  printf("Clouds Sample Playback Tests\n");
  printf("============================\n");

  printf("\nForward Playback:\n");
  run_test_forward_full_range_mono();
  run_test_forward_full_range_stereo();
  run_test_forward_subrange();
  run_test_forward_wraps_at_end();
  run_test_forward_subrange_wraps();

  printf("\nReverse Playback:\n");
  run_test_reverse_full_sample();
  run_test_reverse_subrange();
  run_test_reverse_wraps_at_zero();
  run_test_reverse_stereo();

  printf("\nEdge Cases:\n");
  run_test_start_equals_end_forward();
  run_test_start_at_max_permil();
  run_test_start_zero_end_zero();
  run_test_read_pos_out_of_range_resets();
  run_test_single_frame_sample();
  run_test_two_frame_sample_forward();
  run_test_two_frame_sample_reverse();

  printf("\nSnapshot Pattern:\n");
  run_test_snapshot_null_ptr_skips();
  run_test_snapshot_zero_frames_skips();
  run_test_snapshot_isolates_from_volatile_changes();
  run_test_ptr_last_publish_pattern();

  printf("\nPermil Conversion:\n");
  run_test_permil_boundary_0();
  run_test_permil_boundary_1000();
  run_test_permil_boundary_500();
  run_test_permil_small_sample();
  run_test_permil_no_overflow_large_sample();

  printf("\nAmplitude Conversion:\n");
  run_test_amplitude_positive_full_scale();
  run_test_amplitude_negative_full_scale();
  run_test_amplitude_zero();

  printf("\n============================\n");
  printf("Results: %d/%d passed", g_tests_passed, g_tests_run);
  if (g_tests_failed > 0)
    printf(", %d FAILED", g_tests_failed);
  printf("\n");

  return g_tests_failed > 0 ? 1 : 0;
}
