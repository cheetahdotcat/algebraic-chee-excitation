
#include "math.h"
#include "corrupt.hpp"
#include "hdacelp/hd_acelp.hpp"
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
static const CorruptField ANA_FIELDS[23] = {
    {8,kFieldLsp},{9,kFieldLsp},{9,kFieldLsp},
    {8,kFieldPitch},{14,kFieldCodebook},{1,kFieldSign},{1,kFieldShift},{6,kFieldGainVQ},
    {5,kFieldPitch},{14,kFieldCodebook},{1,kFieldSign},{1,kFieldShift},{6,kFieldGainVQ},
    {5,kFieldPitch},{14,kFieldCodebook},{1,kFieldSign},{1,kFieldShift},{6,kFieldGainVQ},
    {5,kFieldPitch},{14,kFieldCodebook},{1,kFieldSign},{1,kFieldShift},{6,kFieldGainVQ},
};

// TETRA joint gain codebook (codec/ener_qua.tab): log energies in Q8 of the
// pitch and codebook contributions. Used to move a VQ index by energy.
static const short TETRA_GAIN_VQ[64][2] = {
    {  12,  48}, {  69, 287}, {  98, 589}, { 103, 865}, { 406, 561}, {  95,1161}, { 418, 913}, { 465,1288},
    { 705,1056}, { 822, 699}, {1036,1016}, { 842,1353}, { 555,1646}, { 681,2119}, { 891,1758}, {1071,1529},
    {1236,1281}, {1455, 962}, {1661,1279}, {1416,1514}, {1243,1768}, {1110,2088}, {1349,2406}, {1480,2035},
    {1558,1762}, {1770,1554}, {2027,1319}, {1952,1030}, {2281, 914}, {2337,1209}, {2451,1441}, {2234,1441},
    {2033,1592}, {2279,1644}, {2109,1808}, {2319,1859}, {2469,1656}, {2533,1857}, {2414,2084}, {2114,2087},
    {1834,1887}, {1769,2242}, {1977,2560}, {2278,2344}, {2343,2749}, {2661,2416}, {2857,2037}, {3239,2405},
    {2848,2836}, {3426,2863}, {3475,3416}, {2831,3409}, {2410,3322}, {2033,3303}, {1609,3514}, {1625,2836},
    { 880,2775}, {1122,3500}, { 605,3517}, {  88,3532}, { 111,2956}, {  89,2436}, {  88,1938}, {  74,1526},
};

unsigned corrupt_target_mask(int target) {
    switch (target) {
    case kTargetSpeech:   return 0x1FFu;
    case kTargetSynth:    return 1u << kFieldLsp;
    case kTargetPitch:    return 1u << kFieldPitch;
    case kTargetGain:     return (1u << kFieldGain) | (1u << kFieldGainPitch) | (1u << kFieldGainVQ) | (1u << kFieldGainPVQ);
    case kTargetCodebook: return (1u << kFieldCodebook) | (1u << kFieldSign) | (1u << kFieldShift);
    default:              return 0;
    }
}

static inline int field_max(const CorruptField *f, int i) { return (1 << f[i].bits) - 1; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Nudge one field by a random offset scaled by magnitude*range, clamped valid.
static void warp_field(Word16 *vals, const CorruptField *f, int i, float magnitude) {
    const int hi = field_max(f, i);
    const int span = (int)(magnitude * (float)hi + 0.999f);
    if (span <= 0) return;
    int v = (int)vals[i] + (int)(rng_bi() * (float)span);
    vals[i] = (Word16)clampi(v, 0, hi);
}

// Gain wobble: a held random offset (new step with probability `intensity`
// per frame, depth `magnitude`), ramped across the frame's subframes so the
// result pumps and shifts the voicing instead of popping.
static void gain_wobble(Word16 *vals, const CorruptField *fields, int n, const CorruptCfg &cfg, CorruptState *st) {
    const float prevOfs = st->gainOfs;
    float target = prevOfs;
    if (rng_f01() < cfg.intensity) {
        target = rng_bi() * cfg.magnitude;
        if (target > 0.0f) target *= 0.5f;   // boosts are capped harder than cuts
    }
    int count[4] = { 0, 0, 0, 0 }, total[4] = { 0, 0, 0, 0 };
    auto typeOf = [](int c) {
        return c == kFieldGain ? 0 : (c == kFieldGainPitch ? 1 : (c == kFieldGainVQ ? 2 : (c == kFieldGainPVQ ? 3 : -1)));
    };
    for (int i = 0; i < n; i++) {
        const int t = typeOf(fields[i].cat);
        if (t >= 0) total[t]++;
    }
    for (int i = 0; i < n; i++) {
        const int t = typeOf(fields[i].cat);
        if (t < 0) continue;
        const float o = prevOfs + (target - prevOfs) * (float)(++count[t]) / (float)total[t];
        const int hi = field_max(fields, i);
        if (t == 0) {         // codebook gain: index ~ dB, +-1/4 of the table (~ +-26 dB)
            vals[i] = (Word16)clampi(vals[i] + (int)lrintf(o * 0.25f * hi), 0, hi);
        } else if (t == 1) {  // pitch gain: linear, +-40 % of the range (voicing)
            vals[i] = (Word16)clampi(vals[i] + (int)lrintf(o * 0.4f * hi), 0, hi);
        } else if (t == 3) {  // HD predictive gain codebook
            // The decoder's MA predictor already carries part of earlier
            // offsets; add only the difference so the level offset tracks the
            // wobble instead of integrating into a runaway.
            const float want = o * 8.0f; // steps (x HD_VQ_STEP_DB dB)
            float carried = 0.0f;
            for (int k = 0; k < 4; k++) carried += hdacelp::HD_VQ_TAPS[k] * st->pvqOfs[k];
            const int cl = vals[i] >> 3, gl = vals[i] & 7;
            const int ncl = clampi(cl + (int)lrintf(want - carried), 0, hdacelp::HD_VQ_LEVELS - 1);
            const float applied = carried + (float)(ncl - cl);
            st->pvqOfs[3] = st->pvqOfs[2]; st->pvqOfs[2] = st->pvqOfs[1]; st->pvqOfs[1] = st->pvqOfs[0]; st->pvqOfs[0] = applied;
            const int ngl = clampi(gl + (int)lrintf(o * 0.4f * 7), 0, 7); // voicing
            vals[i] = (Word16)(ncl * 8 + ngl);
        } else {              // TETRA joint VQ: nearest entry to the shifted energies
            const int cur = clampi(vals[i], 0, 63);
            const float tp = TETRA_GAIN_VQ[cur][0] + o * 700.0f;
            const float tc = TETRA_GAIN_VQ[cur][1] + o * 1100.0f;
            int best = cur;
            float bd = 1e30f;
            for (int k = 0; k < 64; k++) {
                const float dp = TETRA_GAIN_VQ[k][0] - tp, dc = TETRA_GAIN_VQ[k][1] - tc;
                const float d = dp * dp + dc * dc;
                if (d < bd) { bd = d; best = k; }
            }
            vals[i] = (Word16)best;
        }
    }
    st->gainOfs = target;
}

void corrupt_apply_fields(Word16 *vals, const CorruptField *fields, int n, const CorruptCfg &cfg,
                          CorruptState *st) {
    Word16 *prev = st->freezePrev;
    int *have_prev = &st->haveFreeze;
    const int cap = (int)(sizeof(st->freezePrev) / sizeof(st->freezePrev[0]));
    if (n > cap) n = cap;
    if (cfg.mode < kCorruptLsp || cfg.mode > kCorruptFreeze) return;
    const float in = cfg.intensity;
    const float mg = cfg.magnitude;

    // Freeze: with probability `intensity`, replace the whole frame with the
    // previously seen parameters -> robotic held-note stutter.
    if (cfg.mode == kCorruptFreeze) {
        if (cfg.hold) {
            // key: capture on the hit, then hold that frame every frame,
            // blended by magnitude (1 = complete freeze)
            if (cfg.trigger || !*have_prev) {
                memcpy(prev, vals, sizeof(Word16) * n);
                *have_prev = 1;
            }
            for (int i = 0; i < n; i++) {
                const int mixed = (int)((1.0f - mg) * (float)vals[i] + mg * (float)prev[i] + 0.5f);
                vals[i] = (Word16)clampi(mixed, 0, field_max(fields, i));
            }
            return;
        }
        // knob: with probability `intensity`, replace the frame with the
        // previously seen parameters -> robotic held-note stutter
        if (*have_prev && rng_f01() < in) {
            // magnitude blends toward the frozen frame (0=subtle,1=full hold)
            for (int i = 0; i < n; i++) {
                int mixed = (int)((1.0f - mg) * (float)vals[i] + mg * (float)prev[i] + 0.5f);
                vals[i] = (Word16)clampi(mixed, 0, field_max(fields, i));
            }
        } else {
            memcpy(prev, vals, sizeof(Word16) * n);
            *have_prev = 1;
        }
        return;
    }

    if (cfg.mode == kCorruptGain) {
        gain_wobble(vals, fields, n, cfg, st);
        return;
    }

    int targetCat;
    switch (cfg.mode) {
        case kCorruptLsp:      targetCat = kFieldLsp;      break;
        case kCorruptPitch:    targetCat = kFieldPitch;    break;
        case kCorruptCodebook: targetCat = kFieldCodebook; break;
        default: return;
    }

    for (int i = 0; i < n; i++) {
        if (fields[i].cat != targetCat) continue;
        if (rng_f01() >= in) continue;
        if (cfg.mode == kCorruptCodebook) {
            // XOR a number of random bits proportional to magnitude -> texture
            const int nbits = 1 + (int)(mg * (fields[i].bits - 1));
            int v = (int)vals[i];
            for (int b = 0; b < nbits; b++) v ^= (1 << rng_range(fields[i].bits));
            vals[i] = (Word16)clampi(v, 0, field_max(fields, i));
        } else {
            warp_field(vals, fields, i, mg); // LSP / pitch
        }
    }
}

void corrupt_apply_params(Word16 *ana, int ana_len, const CorruptCfg &cfg) {
    if (ana_len < 23) return;
    static CorruptState st; // TETRA: one stream
    corrupt_apply_fields(ana, ANA_FIELDS, 23, cfg, &st);
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
    // Shifting a 16-bit word by 16+ bits is meaningless (and >= 32 is UB).
    if (max_shift_bits < 0) max_shift_bits = 0; else if (max_shift_bits > 15) max_shift_bits = 15;
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

float corrupt_ber(float depth) {
    if (depth <= 0.0f) return 0.0f;
    if (depth > 1.0f) depth = 1.0f;
    return 1e-4f * powf(500.0f, depth);
}
int corrupt_slip_bits(float d)      { return 1 + (int)lrintf(d * d * 15.0f); }
int corrupt_overflow_words(float d) { return 1 + (int)lrintf(d * d * 15.0f); }
int corrupt_shuffle_swaps(float d)  { return 1 + (int)lrintf(d * d * 31.0f); }

// Poisson-distributed count with mean lambda.
static int rng_poisson(float lambda) {
    if (lambda <= 0.0f) return 0;
    if (lambda > 30.0f) { // normal approximation
        const float u1 = rng_f01() + 1e-7f, u2 = rng_f01();
        const float g = sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
        const int k = (int)lrintf(lambda + sqrtf(lambda) * g);
        return k < 0 ? 0 : k;
    }
    const float L = expf(-lambda);
    int k = 0;
    float p = 1.0f;
    do { k++; p *= rng_f01(); } while (p > L);
    return k - 1;
}

// One channel-bit error at unit index i.
static inline void flip_unit(Word16 *f, int i, bool soft) {
    if (soft) f[i] = (Word16)(f[i] == -32768 ? 32767 : -f[i]);
    else      f[i >> 4] = (Word16)((uint16_t)f[i >> 4] ^ (0x8000u >> (i & 15)));
}

static void calibrated_bitstream(Word16 *frame, int len, const CorruptCfg &cfg, bool soft) {
    const float d = cfg.magnitude < 0.0f ? 0.0f : (cfg.magnitude > 1.0f ? 1.0f : cfg.magnitude);
    if (!cfg.trigger && !(rng_f01() < cfg.intensity)) return; // this frame is not hit
    const int units = soft ? len : len * 16;
    const float ber = corrupt_ber(d);
    switch (cfg.mode) {
        case kCorruptBitFlips: {
            const int k = rng_poisson(ber * units);
            for (int i = 0; i < k; i++) flip_unit(frame, rng_range(units), soft);
            break;
        }
        case kCorruptBurst: {
            // Gilbert-Elliott: mean burst length 2..16 units, 50 % errors
            // inside a burst, entry rate chosen so the average BER matches.
            if (ber <= 0.0f) break;
            const float px = 1.0f / (2.0f + 14.0f * d);
            const float pe = 2.0f * ber * px / (1.0f - 2.0f * ber);
            int bad = 0;
            for (int i = 0; i < units; i++) {
                if (!bad) { if (rng_f01() < pe) bad = 1; }
                else      { if (rng_f01() < px) bad = 0; }
                if (bad && (rng_u32() & 1)) flip_unit(frame, i, soft);
            }
            break;
        }
        case kCorruptBitSlip: {
            const int n = corrupt_slip_bits(d);
            if (soft) {
                // symbols arrive n positions late; the gap is erased (0 = no information)
                const int m = n < len ? n : len;
                memmove(frame + m, frame, sizeof(Word16) * (len - m));
                for (int i = 0; i < m; i++) frame[i] = 0;
            } else {
                bit_slip_Word16(frame, len, n);
            }
            break;
        }
        case kCorruptOverflow: {
            const int n = corrupt_overflow_words(d);
            for (int i = 0; i < n; i++) frame[rng_range(len)] = (rng_u32() & 1) ? (Word16)0x7FFF : (Word16)0x8000;
            break;
        }
        case kCorruptReinterleave: {
            const int n = corrupt_shuffle_swaps(d);
            for (int i = 0; i < n; i++) {
                const int a = rng_range(len), b = rng_range(len);
                const Word16 t = frame[a]; frame[a] = frame[b]; frame[b] = t;
            }
            break;
        }
        default: break;
    }
}

void corrupt_apply_bitstream(Word16 *frame, int len, const CorruptCfg &cfg, bool softSymbols) {
    if (cfg.mode < kCorruptBitFlips || cfg.mode > kCorruptReinterleave) return;
    if (cfg.calibrated) { calibrated_bitstream(frame, len, cfg, softSymbols); return; }
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

#pragma region "Targeted bitstream corruption"
void corrupt_apply_bitstream_fields(Word16 *vals, const CorruptField *fields, int n, const CorruptCfg &cfg,
                                    unsigned mask) {
    if (cfg.mode < kCorruptBitFlips || cfg.mode > kCorruptReinterleave || mask == 0) return;
    // gather the selected fields' bits (MSB first) into a packed stream
    Word16 words[256];
    memset(words, 0, sizeof(words));
    int nbits = 0;
    for (int i = 0; i < n; i++) {
        if (!((mask >> fields[i].cat) & 1u)) continue;
        for (int b = fields[i].bits - 1; b >= 0 && nbits < 4096; b--, nbits++)
            if (((uint16_t)vals[i] >> b) & 1u) words[nbits >> 4] = (Word16)((uint16_t)words[nbits >> 4] | (0x8000u >> (nbits & 15)));
    }
    if (nbits == 0) return;
    corrupt_apply_bitstream(words, (nbits + 15) / 16, cfg, false);
    // scatter back
    int bit = 0;
    for (int i = 0; i < n; i++) {
        if (!((mask >> fields[i].cat) & 1u)) continue;
        unsigned v = 0;
        for (int b = 0; b < fields[i].bits && bit < 4096; b++, bit++)
            v = (v << 1) | (((uint16_t)words[bit >> 4] >> (15 - (bit & 15))) & 1u);
        vals[i] = (Word16)v;
    }
}

void corrupt_apply_bitstream_ana(Word16 *ana, int ana_len, const CorruptCfg &cfg, unsigned mask) {
    if (ana_len < 23) return;
    corrupt_apply_bitstream_fields(ana, ANA_FIELDS, 23, cfg, mask);
}
#pragma endregion
