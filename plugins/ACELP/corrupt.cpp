
#include "math.h"
#include "corrupt.hpp"
#include <stdint.h>
#include <string.h>

#pragma region "PRNG"
// Fast, deterministic xorshift32. All corruption runs on a single (background)
// thread, so a plain static state is safe and avoids rand()'s locking/quality
// issues. Seedable for reproducible glitch.
static uint32_t g_rng = 0x9E3779B9u;

void corrupt_seed(unsigned s) { g_rng = s ? s : 0x9E3779B9u; }

static inline uint32_t rng_u32() {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_rng = x;
    return x;
}
static inline int   rng_pct()  { return (int)(rng_u32() % 100u); }        // 0..99
static inline float rng_f01()  { return (rng_u32() >> 8) * (1.0f / 16777216.0f); }
static inline int   rng_range(int n) { return n > 0 ? (int)(rng_u32() % (uint32_t)n) : 0; } // 0..n-1
// signed symmetric [-1,1]
static inline float rng_bi()   { return rng_f01() * 2.0f - 1.0f; }
#pragma endregion

#pragma region "Parameter-domain (ana[]) corruption"
// Field layout of the 23-entry ana[] frame, matching the codec's bitno[] table:
//   {8,9,9}=LSP, then 4 subframes of {pitch, codebook(14), sign(1), shift(1), gain(6)}.
enum { F_LSP, F_PITCH, F_CB, F_SIGN, F_SHIFT, F_GAIN };
static const struct { unsigned char bits; unsigned char cat; } ANA_FIELDS[23] = {
    {8,F_LSP},{9,F_LSP},{9,F_LSP},
    {8,F_PITCH},{14,F_CB},{1,F_SIGN},{1,F_SHIFT},{6,F_GAIN},
    {5,F_PITCH},{14,F_CB},{1,F_SIGN},{1,F_SHIFT},{6,F_GAIN},
    {5,F_PITCH},{14,F_CB},{1,F_SIGN},{1,F_SHIFT},{6,F_GAIN},
    {5,F_PITCH},{14,F_CB},{1,F_SIGN},{1,F_SHIFT},{6,F_GAIN},
};

static inline int field_max(int i) { return (1 << ANA_FIELDS[i].bits) - 1; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Nudge one field by a random offset scaled by magnitude*range, clamped valid.
static void warp_field(Word16 *ana, int i, float magnitude) {
    const int hi = field_max(i);
    const int span = (int)(magnitude * (float)hi + 0.999f);
    if (span <= 0) return;
    int v = (int)ana[i] + (int)(rng_bi() * (float)span);
    ana[i] = (Word16)clampi(v, 0, hi);
}

void corrupt_apply_params(Word16 *ana, int ana_len, const CorruptCfg &cfg) {
    if (cfg.mode < kCorruptLsp || cfg.mode > kCorruptFreeze) return;
    if (ana_len < 23) return;
    const float in = cfg.intensity;
    const float mg = cfg.magnitude;

    // Freeze: with probability `intensity`, replace the whole frame with the
    // previously seen parameters -> robotic held-note stutter.
    static Word16 prev[23];
    static int    have_prev = 0;
    if (cfg.mode == kCorruptFreeze) {
        if (have_prev && rng_f01() < in) {
            // magnitude blends toward the frozen frame (0=subtle,1=full hold)
            for (int i = 0; i < 23; i++) {
                int mixed = (int)((1.0f - mg) * (float)ana[i] + mg * (float)prev[i] + 0.5f);
                ana[i] = (Word16)clampi(mixed, 0, field_max(i));
            }
        } else {
            memcpy(prev, ana, sizeof(Word16) * 23);
            have_prev = 1;
        }
        return;
    }

    int targetCat;
    switch (cfg.mode) {
        case kCorruptLsp:      targetCat = F_LSP;   break;
        case kCorruptPitch:    targetCat = F_PITCH; break;
        case kCorruptCodebook: targetCat = F_CB;    break;
        case kCorruptGain:     targetCat = F_GAIN;  break;
        default: return;
    }

    for (int i = 0; i < 23; i++) {
        if (ANA_FIELDS[i].cat != targetCat) continue;
        if (rng_f01() >= in) continue;
        if (cfg.mode == kCorruptGain) {
            // spike to max, drop to zero, or warp — magnitude biases toward extremes
            const int hi = field_max(i);
            const float r = rng_f01();
            if (r < 0.5f * mg)          ana[i] = (Word16)hi;   // spike
            else if (r < mg)            ana[i] = 0;            // drop
            else                        warp_field(ana, i, mg);
        } else if (cfg.mode == kCorruptCodebook) {
            // XOR a number of random bits proportional to magnitude -> texture
            const int nbits = 1 + (int)(mg * (ANA_FIELDS[i].bits - 1));
            int v = (int)ana[i];
            for (int b = 0; b < nbits; b++) v ^= (1 << rng_range(ANA_FIELDS[i].bits));
            ana[i] = (Word16)clampi(v, 0, field_max(i));
        } else {
            warp_field(ana, i, mg); // LSP / pitch
        }
    }
}
#pragma endregion

#pragma region "Bitstream-domain corruption (fixed + hardened)"
// Shift the whole frame left by 1 bit, carrying across words (a 1-bit "slip").
void bit_desync_shift_left_Word16(Word16 *array, int len) {
    uint16_t carry = 0;
    for (int i = 0; i < len; i++) {
        uint16_t val = (uint16_t)array[i];
        uint16_t new_carry = (val & 0x8000) >> 15;
        val = (uint16_t)((val << 1) | carry);
        array[i] = (Word16)val;
        carry = new_carry;
    }
}

// Slip the entire frame by `nbits` (>=1) bit positions -> catastrophic desync.
static void bit_slip_Word16(Word16 *array, int len, int nbits) {
    for (int s = 0; s < nbits; s++) bit_desync_shift_left_Word16(array, len);
}

// Gilbert-Elliott burst error model over the frame's bits. `p_enter` (from
// intensity) is the chance of entering a bad burst; `p_stay` (from magnitude)
// keeps it going. In the bad state, bits are flipped.
static void burst_error_Word16(Word16 *array, int len, float p_enter, float p_stay) {
    int bad = 0;
    for (int i = 0; i < len; i++) {
        uint16_t val = (uint16_t)array[i];
        for (int b = 0; b < 16; b++) {
            if (!bad) { if (rng_f01() < p_enter) bad = 1; }
            else      { if (rng_f01() > p_stay)  bad = 0; }
            if (bad)  val ^= (uint16_t)(1u << b);
        }
        array[i] = (Word16)val;
    }
}

// FIXED: was `i <= len` (out-of-bounds write) and wrote an index ramp. Now a
// bounded saturating-overflow: random words are slammed to +/- full scale.
void corrupt_by_overflow(short *acelp_array, int len) {
    for (int i = 0; i < len; i++) {
        if (rng_pct() < 20) acelp_array[i] = (rng_u32() & 1) ? (short)0x7FFF : (short)0x8000;
    }
}

// FIXED: was overwriting values with their indices (destroying the signal).
// Now an actual permutation: randomly swap word positions, preserving values.
void corrupt_by_wrong_interleave(short *coded_array, int len) {
    if (len < 2) return;
    const int swaps = len / 2;
    for (int n = 0; n < swaps; n++) {
        int a = rng_range(len), b = rng_range(len);
        short t = coded_array[a]; coded_array[a] = coded_array[b]; coded_array[b] = t;
    }
}

void random_bit_desync_Word16(Word16 *array, int len, int max_shift_bits, int flip_probability_percent) {
    for (int i = 0; i < len; i++) {
        uint16_t val = (uint16_t)array[i];
        if (rng_pct() < 4) continue;
        int shift_dir = rng_range(3) - 1;                 // -1,0,1
        int shift_amt = rng_range(max_shift_bits + 1);
        if (shift_dir == 1)      val = (uint16_t)(val << shift_amt);
        else if (shift_dir == -1) val = (uint16_t)(val >> shift_amt);
        if (rng_pct() < flip_probability_percent) val ^= (uint16_t)(1u << rng_range(16));
        array[i] = (Word16)val;
    }
}

void corrupt_bit_flips_Word16(Word16 *frame, int len, int bit_flip_percent) {
    for (int i = 0; i < len; i++) {
        uint16_t val = (uint16_t)frame[i];
        for (int b = 0; b < 16; b++)
            if (rng_pct() < bit_flip_percent) val ^= (uint16_t)(1u << b);
        frame[i] = (Word16)val;
    }
}

// Kept for API compatibility; byte-oriented burst on a raw buffer.
void burst_error(uint8_t *frame, int len, int burst_len) {
    if (burst_len <= 0 || len * 8 <= burst_len) return;
    int start = rng_range(len * 8 - burst_len);
    for (int i = 0; i < burst_len; i++) {
        int bit_pos = start + i;
        frame[bit_pos / 8] ^= (uint8_t)(1 << (bit_pos % 8));
    }
}

void corrupt_apply_bitstream(Word16 *frame, int len, const CorruptCfg &cfg) {
    if (cfg.mode < kCorruptBitFlips || cfg.mode > kCorruptReinterleave) return;
    const float in = cfg.intensity;
    const float mg = cfg.magnitude;
    switch (cfg.mode) {
        case kCorruptBitFlips:
            corrupt_bit_flips_Word16(frame, len, clampi((int)(in * 50.0f), 0, 100));
            break;
        case kCorruptBitSlip:
            if (rng_f01() < in) bit_slip_Word16(frame, len, 1 + (int)(mg * 15.0f));
            break;
        case kCorruptBurst:
            burst_error_Word16(frame, len, in * 0.10f, 0.5f + mg * 0.49f);
            break;
        case kCorruptOverflow:
            if (rng_f01() < in) corrupt_by_overflow(frame, len);
            break;
        case kCorruptReinterleave:
            if (rng_f01() < in) corrupt_by_wrong_interleave(frame, len);
            break;
        default: break;
    }
}
#pragma endregion
