// The plugin Makefile builds with -O0 for debugging; the codec is far too heavy
// for that at 32/48 kHz, so always optimize this translation unit.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#include "hd_acelp.hpp"
#include <math.h>
#include <string.h>

namespace hdacelp {

static const float  W_MU       = 0.68f;   // weighting filter tilt pole
static const double LSF_RANGE  = 1.5;     // residual range in units of mean LSF spacing
static const float  GP_MAX     = 1.2f;
static const float  GC_DB_MIN  = -100.0f;
static const float  GC_DB_MAX  = 6.0f;
static const float  EXC_CLIP   = 4.0f;    // keeps corrupted gains from blowing up

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int   clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int bits_for(int n) { // bits to index n values
    int b = 0;
    while ((1 << b) < n) b++;
    return b;
}

// ---------------------------------------------------------------------------
// Config / bitstream layout

namespace {
struct ModeSpec {
    int tracks;        // 0 = one 16-position track per 16 samples
    int ppt;
    int lsfLo, lsfHi;
    int gpBits, gcBits;
    int pitDeltaBits;
};
const ModeSpec MODES[HD_NUM_MODES] = {
    { 1, 1, 3, 2, 3, 5, 4 },  // 1 pulse anywhere in the subframe
    { 2, 1, 4, 3, 3, 5, 4 },  // 2 pulses, interleaved halves
    { 4, 1, 4, 3, 4, 6, 5 },  // 4 pulses
    { 0, 1, 5, 4, 4, 7, 5 },  // 1 pulse per 16-sample track
    { 0, 2, 5, 4, 4, 7, 5 },
    { 0, 3, 5, 4, 4, 7, 5 },
    { 0, 4, 5, 4, 4, 7, 5 },
};
}

Config Config::make(int fs, int mode, int math) {
    Config c;
    if (fs != 16000 && fs != 32000 && fs != 48000) fs = 32000;
    c.fs = fs;
    c.order = fs == 16000 ? 16 : (fs == 32000 ? 24 : 32);
    c.frameLen = fs / 50;
    c.subLen = fs / 200;
    c.mode = clampi(mode, 0, HD_NUM_MODES - 1);
    c.math = math == MATH_FIXED16 ? MATH_FIXED16 : MATH_FLOAT;
    const ModeSpec &m = MODES[c.mode];
    c.tracks = m.tracks ? m.tracks : c.subLen / 16;
    c.pulsesPerTrack = m.ppt;
    c.positions = c.subLen / c.tracks;
    c.posBits = bits_for(c.positions);
    c.lsfBitsLo = m.lsfLo;
    c.lsfBitsHi = m.lsfHi;
    c.gpBits = m.gpBits;
    c.gcBits = m.gcBits;
    c.pitMin = fs / 500;
    c.pitMax = fs / 50;
    c.pitAbsBits = bits_for(c.pitMax - c.pitMin + 1);
    c.pitDeltaBits = m.pitDeltaBits;
    c.lookahead = c.subLen;
    return c;
}

Config Config::makeTetraPlus(int fs, int level, int math) {
    Config c;
    c.profile = PROFILE_TETRA_PLUS;
    c.fs = fs == 16000 ? 16000 : 8000;
    const bool wb = c.fs == 16000;
    c.order = wb ? 16 : 10;
    c.frameLen = c.fs * 30 / 1000;          // 30 ms, as ETSI TETRA
    c.subLen = c.frameLen / HD_NSUB;        // 7.5 ms
    c.mode = clampi(level, 0, TP_NUM_LEVELS - 1);
    c.math = math == MATH_FIXED16 ? MATH_FIXED16 : MATH_FLOAT;
    c.tracks = c.subLen / 15;               // interleaved 15-position tracks
    c.pulsesPerTrack = c.mode + 1;          // ETSI: 1 pulse per track
    c.positions = 15;
    c.posBits = 4;
    static const int lsfLo[2][3] = { { 4, 5, 6 }, { 5, 5, 6 } };
    static const int lsfHi[2][3] = { { 3, 4, 5 }, { 4, 4, 5 } };
    c.lsfBitsLo = lsfLo[wb][c.mode];
    c.lsfBitsHi = lsfHi[wb][c.mode];
    c.gpBits = 4;
    c.gcBits = c.mode == 0 ? 6 : 7;
    c.pitMin = c.fs / 400;                  // ETSI range: 20 .. 147 at 8 kHz
    c.pitMax = c.fs * 147 / 8000;
    c.pitAbsBits = bits_for(c.pitMax - c.pitMin + 1);
    c.pitDeltaBits = 5;
    c.lookahead = c.subLen;
    return c;
}

int Config::numFields() const {
    return order + HD_NSUB * (1 + gainFields() + 2 * tracks * pulsesPerTrack);
}

void Config::layout(Field *out) const {
    int k = 0;
    for (int i = 0; i < order; i++) out[k++] = { (unsigned char)lsfBits(i), CAT_LSF };
    for (int s = 0; s < HD_NSUB; s++) {
        out[k++] = { (unsigned char)((s & 1) ? pitDeltaBits : pitAbsBits), CAT_PITCH };
        if (gainVq) {
            out[k++] = { 7, CAT_GAIN };
        } else {
            out[k++] = { (unsigned char)gpBits, CAT_GAIN };
            out[k++] = { (unsigned char)gcBits, CAT_GAIN };
        }
        for (int p = 0; p < tracks * pulsesPerTrack; p++) {
            out[k++] = { (unsigned char)posBits, CAT_CB };
            out[k++] = { 1, CAT_SIGN };
        }
    }
}

int Config::numBits() const {
    Field f[HD_MAX_FIELDS];
    layout(f);
    int n = 0;
    for (int i = 0; i < numFields(); i++) n += f[i].bits;
    return n;
}

int pack_words(const Config &c, const int16_t *idx, int16_t *words) {
    Field f[HD_MAX_FIELDS];
    c.layout(f);
    const int nf = c.numFields();
    const int nw = (c.numBits() + 15) / 16;
    memset(words, 0, sizeof(int16_t) * nw);
    int bit = 0;
    for (int i = 0; i < nf; i++) {
        const unsigned v = (unsigned)(uint16_t)idx[i];
        for (int b = f[i].bits - 1; b >= 0; b--, bit++)
            if ((v >> b) & 1u) words[bit >> 4] = (int16_t)((uint16_t)words[bit >> 4] | (0x8000u >> (bit & 15)));
    }
    return nw;
}

void unpack_words(const Config &c, const int16_t *words, int16_t *idx) {
    Field f[HD_MAX_FIELDS];
    c.layout(f);
    const int nf = c.numFields();
    int bit = 0;
    for (int i = 0; i < nf; i++) {
        unsigned v = 0;
        for (int b = 0; b < f[i].bits; b++, bit++)
            v = (v << 1) | (((uint16_t)words[bit >> 4] >> (15 - (bit & 15))) & 1u);
        idx[i] = (int16_t)v;
    }
}

// ---------------------------------------------------------------------------
// LSF quantizer (predictive scalar, shared by encoder and decoder)

static double lsf_min_gap(const Config &c) { return 2.0 * M_PI * 50.0 / c.fs; }

static void lsf_dequant(const Config &c, const int16_t *idx, const double *prevQ, double *out) {
    const int M = c.order;
    const double g = M_PI / (M + 1);
    for (int i = 0; i < M; i++) {
        const double mean = g * (i + 1);
        const double pred = mean + c.lsfPred * (prevQ[i] - mean);
        const int b = c.lsfBits(i);
        const double R = LSF_RANGE * g;
        const double step = 2.0 * R / (1 << b);
        const int q = clampi(idx[i], 0, (1 << b) - 1);
        out[i] = pred - R + (q + 0.5) * step;
    }
    lsf_stabilize(out, M, lsf_min_gap(c));
}

static void lsf_quant(const Config &c, const double *lsf, const double *prevQ, int16_t *idx, double *outQ) {
    const int M = c.order;
    const double g = M_PI / (M + 1);
    for (int i = 0; i < M; i++) {
        const double mean = g * (i + 1);
        const double pred = mean + c.lsfPred * (prevQ[i] - mean);
        const int b = c.lsfBits(i);
        const double R = LSF_RANGE * g;
        const double step = 2.0 * R / (1 << b);
        idx[i] = (int16_t)clampi((int)floor((lsf[i] - pred + R) / step), 0, (1 << b) - 1);
    }
    lsf_dequant(c, idx, prevQ, outQ);
}

static void lsf_uniform(int M, double *lsf) {
    for (int i = 0; i < M; i++) lsf[i] = M_PI * (i + 1) / (M + 1);
}

static void lsf_interp(const double *a, const double *b, int M, double w, double *out) {
    for (int i = 0; i < M; i++) out[i] = (1.0 - w) * a[i] + w * b[i];
}

// ---------------------------------------------------------------------------
// Gain quantizers

static float gp_dequant(int q, int bits) {
    const int top = (1 << bits) - 1;
    return clampi(q, 0, top) * (GP_MAX / top);
}
static int gp_quant(float gp, int bits) {
    const int top = (1 << bits) - 1;
    return clampi((int)lrintf(gp / (GP_MAX / top)), 0, top);
}
static float gc_step(int bits) { return (GC_DB_MAX - GC_DB_MIN) / ((1 << bits) - 1); }
static float gc_db(const Config &c, int q) {
    return GC_DB_MIN + c.gcOffsetDb + clampi(q, 0, (1 << c.gcBits) - 1) * gc_step(c.gcBits);
}
// Innovation gain is sent as the RMS level (dB) of the scaled code vector.
static float gc_dequant(const Config &c, int q, float codeRms) {
    return powf(10.0f, gc_db(c, q) / 20.0f) / (codeRms > 1e-9f ? codeRms : 1e-9f);
}
static int gc_quant(const Config &c, float gc, float codeRms) {
    const float lvl = gc * codeRms;
    const float db = 20.0f * log10f(lvl > 1e-12f ? lvl : 1e-12f);
    return clampi((int)lrintf((db - GC_DB_MIN - c.gcOffsetDb) / gc_step(c.gcBits)), 0, (1 << c.gcBits) - 1);
}

// ---------------------------------------------------------------------------
// Indexed joint gain codebook (Config::gainVq), in the spirit of ETSI TETRA's
// energy VQ: 7 bits = 16 codebook-gain corrections (major) x 8 pitch gains
// (minor). The correction is relative to an MA prediction of the innovation
// level from the last four subframes, so a damaged index lingers for a while.

static const float VQ_GP[8] = { 0.0f, 0.15f, 0.3f, 0.45f, 0.6f, 0.8f, 1.0f, 1.2f };
static const float VQ_MEAN_DB = -40.0f;
static inline float vq_corr_db(int cl) { return HD_VQ_LOW_DB + HD_VQ_STEP_DB * cl; }
static inline int   vq_index(int cl, int gl) { return cl * 8 + gl; }

static float vq_predict(const float *hist) {
    float p = VQ_MEAN_DB;
    for (int i = 0; i < 4; i++) p += HD_VQ_TAPS[i] * (hist[i] - VQ_MEAN_DB);
    return p;
}
static void vq_push(float *hist, float lvl) {
    hist[3] = hist[2]; hist[2] = hist[1]; hist[1] = hist[0]; hist[0] = lvl;
}

// ---------------------------------------------------------------------------
// Excitation helpers

// v[n] = exc[n - T], repeating the vector itself for lags shorter than L.
static void adaptive_vector(const float *excCur, int T, int L, float *v) {
    for (int n = 0; n < L; n++) v[n] = (n < T) ? excCur[n - T] : v[n - T];
}

static void convolve(const float *x, const float *h, int L, float *y) {
    for (int n = 0; n < L; n++) {
        float s = 0.0f;
        for (int k = 0; k <= n; k++) s += x[k] * h[n - k];
        y[n] = s;
    }
}

static float dot(const float *a, const float *b, int L) {
    float s = 0.0f;
    for (int i = 0; i < L; i++) s += a[i] * b[i];
    return s;
}

// Pitch sharpening 1/(1 - beta z^-T) truncated to the subframe.
static void sharpen(float *x, int L, int T, float beta) {
    if (T >= L || beta <= 0.0f) return;
    for (int n = T; n < L; n++) x[n] += beta * x[n - T];
}

static void build_code(const Config &c, const int16_t *f, float *code) {
    const int L = c.subLen, T = c.tracks, P = c.pulsesPerTrack;
    memset(code, 0, sizeof(float) * L);
    for (int t = 0; t < T; t++)
        for (int j = 0; j < P; j++) {
            const int k = 2 * (t * P + j);
            const int n = clampi(f[k], 0, c.positions - 1) * T + t;
            code[n] += f[k + 1] ? -1.0f : 1.0f;
        }
}

static float rms(const float *x, int L) { return sqrtf(dot(x, x, L) / L); }

// All-pole synthesis 1/A(z). mem[0] is the most recent past output.
static void synth_filter_float(const double *a, int M, const float *x, float *y, int L, float *mem) {
    float hist[HD_MAX_ORDER + HD_MAX_SUB];
    for (int k = 0; k < M; k++) hist[M - 1 - k] = mem[k];
    float *yy = hist + M;
    for (int n = 0; n < L; n++) {
        double s = x[n];
        for (int k = 1; k <= M; k++) s -= a[k] * yy[n - k];
        // Hard guard (pre-emphasis domain): extreme LP modification or corrupted
        // parameters must never let the recursion run away. Encoder and
        // decoder share this, so they stay in lockstep.
        yy[n] = (float)(s > 32.0 ? 32.0 : (s < -32.0 ? -32.0 : s));
        y[n] = yy[n];
    }
    for (int k = 0; k < M; k++) mem[k] = yy[L - 1 - k];
}

// ---------------------------------------------------------------------------
// Fixed-point (MATH_FIXED16) kernels
//
// Emulates a 16-bit DSP implementation in the style of the ETSI reference
// codecs: signals and filter states are int16 in Q13 (so +-4.0 full scale in
// the pre-emphasis domain, saturating), LPC coefficients are int16 with a
// per-filter block exponent (Q12 at most, like ETSI's A(z) in Q12),
// accumulators are 32-bit and saturate on every multiply-accumulate. Values
// are kept in float buffers but always sit exactly on the Q13 grid, so the
// math mode can be switched between frames without resetting anything.

static const float FX_ONE = 8192.0f; // Q13

static inline int16_t sat16(int64_t v) { return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); }
static inline int32_t sat32(int64_t v) {
    return (int32_t)(v > 2147483647LL ? 2147483647LL : (v < -2147483648LL ? -2147483648LL : v));
}
static inline int16_t fx_from(float v) {
    const float s = v * FX_ONE;
    if (!(s > -32768.0f)) return -32768; // also catches NaN
    if (s >= 32767.0f) return 32767;
    return (int16_t)lrintf(s);
}
static inline float fx_to(int32_t q) { return (float)q * (1.0f / FX_ONE); }

// Quantize A(z) to int16 with a block exponent. Returns the Q of the result.
// High-order filters are sensitive to coefficient rounding, so if the
// quantized filter is unstable, bandwidth-expand and try again (both encoder
// and decoder do exactly the same, so they stay in lockstep).
static int quantize_lpc_fx(const double *a, int M, int16_t *aq) {
    double w[HD_MAX_ORDER + 1], chk[HD_MAX_ORDER + 1];
    memcpy(w, a, sizeof(double) * (M + 1));
    int q = 12;
    for (int tries = 0; tries < 16; tries++) {
        double mx = 1.0;
        for (int k = 1; k <= M; k++) mx = fabs(w[k]) > mx ? fabs(w[k]) : mx;
        q = 12;
        while (q > 0 && mx * (double)(1 << q) > 32767.0) q--;
        for (int k = 0; k <= M; k++) {
            aq[k] = sat16(llrint(w[k] * (double)(1 << q)));
            chk[k] = aq[k] / (double)(1 << q);
        }
        chk[0] = 1.0;
        if (is_stable(chk, M)) break;
        weight_lpc(w, M, 0.99, w);
    }
    return q;
}

static void synth_filter_fx(const double *a, int M, const float *x, float *y, int L, float *mem) {
    int16_t aq[HD_MAX_ORDER + 1];
    const int qa = quantize_lpc_fx(a, M, aq);
    const int64_t rnd = qa > 0 ? (int64_t)1 << (qa - 1) : 0;
    int16_t hist[HD_MAX_ORDER + HD_MAX_SUB];
    for (int k = 0; k < M; k++) hist[M - 1 - k] = fx_from(mem[k]);
    int16_t *yy = hist + M;
    for (int n = 0; n < L; n++) {
        int32_t acc = sat32((int64_t)fx_from(x[n]) << qa);
        for (int k = 1; k <= M; k++) acc = sat32((int64_t)acc - (int32_t)aq[k] * yy[n - k]);
        yy[n] = sat16(((int64_t)acc + rnd) >> qa);
        y[n] = fx_to(yy[n]);
    }
    for (int k = 0; k < M; k++) mem[k] = fx_to(yy[L - 1 - k]);
}

// Signal-path synthesis: dispatches on the math mode. (Encoder-internal
// analysis filters such as the impulse response always run in float.)
static void synth_filter(const Config &c, const double *a, const float *x, float *y, int L, float *mem) {
    if (c.math == MATH_FIXED16) synth_filter_fx(a, c.order, x, y, L, mem);
    else                        synth_filter_float(a, c.order, x, y, L, mem);
}

// ex = gp * v + gc * code, shared by encoder and decoder.
static void make_excitation(const Config &c, const float *v, const float *code, float gp, float gc,
                            float *ex, int L) {
    if (c.math != MATH_FIXED16) {
        for (int n = 0; n < L; n++) ex[n] = clampf(gp * v[n] + gc * code[n], -EXC_CLIP, EXC_CLIP);
        return;
    }
    const int32_t gpq = sat16(lrintf(gp * 16384.0f));            // Q14
    const int32_t gcq = sat32(llrintf(gc * FX_ONE));              // Q13, 32-bit
    for (int n = 0; n < L; n++) {
        const int32_t cq = sat16(lrintf(code[n] * 4096.0f));      // Q12
        const int64_t t1 = (int64_t)gpq * fx_from(v[n]);          // Q27
        const int64_t t2 = (int64_t)gcq * cq;                     // Q25
        const int64_t acc = sat32((t1 >> 1) + sat32(t2 << 1));    // Q26
        ex[n] = fx_to(sat16((acc + (1 << 12)) >> 13));            // Q13, +-4.0
    }
}

// Gain ramp g0 -> g1 over the subframe, then de-emphasis 1/(1 - 0.68 z^-1).
static void output_stage(const Config &c, const float *syn, float g0, float g1, int L, float *deemph,
                         float *out) {
    if (c.math != MATH_FIXED16) {
        for (int n = 0; n < L; n++) {
            const float g = g0 + (g1 - g0) * (float)(n + 1) / L;
            *deemph = syn[n] * g + c.preEmph * *deemph;
            out[n] = *deemph;
        }
        return;
    }
    const int32_t mu = (int32_t)lrintf(c.preEmph * 32768.0f);    // Q15
    int32_t d = fx_from(*deemph);
    for (int n = 0; n < L; n++) {
        const float g = clampf(g0 + (g1 - g0) * (float)(n + 1) / L, 0.0f, 100.0f);
        const int32_t gq = (int32_t)lrintf(g * 256.0f);            // Q8
        const int32_t sc = sat16(((int64_t)fx_from(syn[n]) * gq + 128) >> 8);
        d = sat16(sc + (((int64_t)mu * d + 16384) >> 15));
        out[n] = fx_to(d);
    }
    *deemph = fx_to(d);
}

// ---------------------------------------------------------------------------
// LP modification

bool LpMod::lsfActive() const {
    return fabsf(warp) > 1e-3f || fabsf(depth) > 1e-3f || smooth > 1e-3f || freeze > 1e-3f || crush > 1e-3f ||
           mirror > 1e-3f;
}

LatticeShape LpModifier::lattice(const LpMod &m) {
    LatticeShape s;
    s.order = m.order;
    s.taper = m.taper;
    s.band = m.band;
    s.resonance = m.resonance;
    if (m.jitter > 1e-3f) {
        for (int i = 1; i <= M_; i++) {
            rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
            const double u = (rng_ >> 8) * (2.0 / 16777216.0) - 1.0;
            // log-area-ratio units; scaled so the overall wobble is similar
            // at every LPC order (16 / 24 / 32 coefficients)
            jit_[i] = u * 0.8 * m.jitter * sqrt(16.0 / M_);
        }
        s.jitter = jit_;
    }
    return s;
}

void LpModifier::reset(int M) {
    M_ = M;
    haveSmoothed_ = false;
    frozen_ = false;
    lsf_uniform(M, held_);
    lsf_uniform(M, smoothed_);
}

void LpModifier::apply(double *lsf, const LpMod &m, double minGap) {
    const int M = M_;
    // Modified envelopes get a wider minimum LSF spacing: warp and depth pile
    // LSFs together, and near-coincident LSFs mean ill-conditioned, ringing
    // high-order filters.
    const double modGap = 0.2 * M_PI / (M + 1);
    if (minGap < modGap) minGap = modGap;
    // Freeze: capture the envelope at the moment freeze engages, then blend.
    if (m.freeze > 1e-3f) {
        if (!frozen_) {
            memcpy(held_, lsf, sizeof(double) * M);
            frozen_ = true;
        }
        const double f = m.freeze;
        for (int i = 0; i < M; i++) lsf[i] = (1.0 - f) * lsf[i] + f * held_[i];
    } else {
        frozen_ = false;
    }
    // Smoothing: one-pole per frame (20 ms). smooth=1 -> ~1 s time constant.
    if (!haveSmoothed_) {
        memcpy(smoothed_, lsf, sizeof(double) * M);
        haveSmoothed_ = true;
    }
    const double lam = 0.98 * m.smooth;
    for (int i = 0; i < M; i++) {
        smoothed_[i] = lam * smoothed_[i] + (1.0 - lam) * lsf[i];
        lsf[i] = smoothed_[i];
    }
    // Crush: snap to a coarse frequency grid.
    if (m.crush > 1e-3f) {
        const double step = m.crush * 1.5 * M_PI / (M + 1);
        for (int i = 0; i < M; i++) lsf[i] = step * floor(lsf[i] / step + 0.5);
    }
    lsf_stabilize(lsf, M, minGap);
    // Mirror: blend towards the envelope reflected around fs/4 (A(-z)); a
    // convex mix of two ascending LSF sets stays ascending, hence stable.
    if (m.mirror > 1e-3f) {
        double mir[HD_MAX_ORDER];
        for (int i = 0; i < M; i++) mir[i] = M_PI - lsf[M - 1 - i];
        for (int i = 0; i < M; i++) lsf[i] = (1.0 - m.mirror) * lsf[i] + m.mirror * mir[i];
        lsf_stabilize(lsf, M, minGap);
    }
    // Warp + depth operate on the power spectrum of the envelope, then go
    // back through autocorrelation + Levinson. Moving LSFs directly crams them
    // together (100+ dB envelopes a high-order float filter cannot realize);
    // Levinson with a noise floor always yields a stable, bounded filter.
    // The refit also runs without warp/depth: it doubles as a sanitizer that
    // bounds whatever freeze/smooth/crush produced.
    {
        const double al = 0.45 * clampf(m.warp, -1.0f, 1.0f);
        const double p = pow(2.0, 1.3 * clampf(m.depth, -1.0f, 1.0f));
        double a[HD_MAX_ORDER + 1], r[HD_MAX_ORDER + 1], out[HD_MAX_ORDER];
        lsf2a_stable(lsf, M, a);
        const int K = 512;
        static thread_local double logP[K];
        double mean = 0.0;
        for (int i = 0; i < K; i++) {
            const double w = M_PI * (i + 0.5) / K;
            // output frequency w shows the input envelope at the inverse warp
            const double psi = w + 2.0 * atan2(-al * sin(w), 1.0 + al * cos(w));
            double re = 0.0, im = 0.0;
            for (int k = 0; k <= M; k++) {
                re += a[k] * cos(k * psi);
                im -= a[k] * sin(k * psi);
            }
            logP[i] = -log(re * re + im * im + 1e-30);
            mean += logP[i];
        }
        mean /= K;
        for (int k = 0; k <= M; k++) r[k] = 0.0;
        for (int i = 0; i < K; i++) {
            const double w = M_PI * (i + 0.5) / K;
            const double P = exp(p * (logP[i] - mean));
            const double c1 = cos(w);
            double c0 = 1.0, c = c1;
            r[0] += P;
            for (int k = 1; k <= M; k++) {
                r[k] += P * c;
                const double cn = 2.0 * c1 * c - c0;
                c0 = c;
                c = cn;
            }
        }
        r[0] *= 1.0003; // ~-35 dB floor bounds the envelope's dynamic range
        if (levinson(r, M, a) && a2lsf(a, M, out)) memcpy(lsf, out, sizeof(double) * M);
    }
    lsf_stabilize(lsf, M, minGap);
}

// ---------------------------------------------------------------------------
// Encoder

// Open-loop pitch on a decimated (8 kHz equivalent) weighted-speech signal.
static int open_loop_pitch(const float *cur, int len, int D, int tmin, int tmax) {
    float ds[(HD_MAX_PITCH + HD_MAX_FRAME) / 2 + 8];
    const float *base = cur - tmax;
    const int n = (tmax + len) / D;
    for (int k = 0; k < n; k++) {
        float s = 0.0f;
        for (int j = 0; j < D; j++) s += base[k * D + j];
        ds[k] = s / D;
    }
    const int c0 = tmax / D, cl = len / D;
    const int lo = (tmin + D - 1) / D, hi = tmax / D;
    int best = lo;
    float bestC = -1e30f;
    for (int tau = lo; tau <= hi; tau++) {
        float r = 0.0f, e = 1e-9f;
        for (int k = 0; k < cl; k++) {
            r += ds[c0 + k] * ds[c0 + k - tau];
            e += ds[c0 + k - tau] * ds[c0 + k - tau];
        }
        // mild bias towards short lags to avoid pitch multiples
        float crit = (r > 0.0f ? r : 0.0f) / sqrtf(e);
        crit *= 1.0f - 0.2f * (float)(tau - lo) / (float)(hi - lo);
        if (crit > bestC) {
            bestC = crit;
            best = tau;
        }
    }
    return best * D;
}

void Encoder::init(const Config &c) {
    c_ = c;
    const int M = c.order, N = c.frameLen;
    hpX1_ = hpY1_ = preX1_ = deemph_ = 0.0f;
    hist_ = N / 2;
    memset(sp_, 0, sizeof(sp_));
    memset(wsp_, 0, sizeof(wsp_));
    memset(exc_, 0, sizeof(exc_));
    memset(synMem_, 0, sizeof(synMem_));
    memset(errHist_, 0, sizeof(errHist_));
    wspMem_ = wErrMem_ = 0.0f;
    lsf_uniform(M, lsfPrev_);
    lsf_uniform(M, lsfQPrev_);
    prevT_ = c.pitMin;
    mod_ = LpMod();          // a fresh encoder starts unmodified
    for (int i = 0; i < 4; i++) gcHist_[i] = VQ_MEAN_DB;
    prevGpQ_ = 0.0f;
    gamma1_ = c.fs < 12800 ? 0.94 : pow(0.92, 12800.0 / c.fs);
    modifier_.reset(M);

    // Asymmetric LPC window over [hist/... | frame | lookahead]: rising
    // half-Hamming peaking at the end of the current frame, then a quarter
    // cosine over the lookahead.
    const int past = N / 2;
    const int rise = past + N;
    winLen_ = past + N + c.lookahead;
    for (int i = 0; i < rise; i++)
        win_[i] = (float)(0.54 - 0.46 * cos(M_PI * i / (rise - 1)));
    for (int i = 0; i < c.lookahead; i++)
        win_[rise + i] = (float)cos(0.5 * M_PI * (i + 1) / c.lookahead);
}

void Encoder::reconfigure(const Config &c) {
    if (c.fs != c_.fs || c.frameLen != c_.frameLen || c.profile != c_.profile) init(c);
    else c_ = c;
}

void Encoder::encode(const float *x, int16_t *idx, float *synthOut) {
    const Config &c = c_;
    const int M = c.order, N = c.frameLen, L = c.subLen, LA = c.lookahead;
    const int spLen = hist_ + N + LA;
    const double minGap = lsf_min_gap(c);

    // --- Pre-processing: DC block + pre-emphasis, append to the history.
    memmove(sp_, sp_ + N, sizeof(float) * (spLen - N));
    float *dst = sp_ + spLen - N;
    for (int i = 0; i < N; i++) {
        const float hp = x[i] - hpX1_ + 0.995f * hpY1_;
        hpX1_ = x[i];
        hpY1_ = hp;
        dst[i] = hp - c.preEmph * preX1_;
        preX1_ = hp;
        if (c.math == MATH_FIXED16) dst[i] = fx_to(fx_from(dst[i])); // 16-bit input
    }
    float *speech = sp_ + hist_; // current frame (lookahead follows it)

    // --- LP analysis
    double r[HD_MAX_ORDER + 1], a[HD_MAX_ORDER + 1], lsfNew[HD_MAX_ORDER];
    autocorr(speech - N / 2, win_, winLen_, M, r);
    lag_window(r, M, c.fs);
    if (!levinson(r, M, a) || !a2lsf(a, M, lsfNew)) {
        memcpy(lsfNew, lsfPrev_, sizeof(double) * M);
    }
    lsf_stabilize(lsfNew, M, minGap);

    // --- Encoder-stage LP modification goes into the transmitted envelope.
    double lsfTx[HD_MAX_ORDER];
    memcpy(lsfTx, lsfNew, sizeof(double) * M);
    if (mod_.lsfActive()) modifier_.apply(lsfTx, mod_, minGap);
    if (mod_.latticeActive(M)) {
        // Lattice shaping lives in the LPC domain; convert back so the
        // shaped envelope is what gets quantized and transmitted.
        double at[HD_MAX_ORDER + 1], ar[HD_MAX_ORDER + 1], lt[HD_MAX_ORDER];
        lsf2a_stable(lsfTx, M, at);
        const LatticeShape ls = modifier_.lattice(mod_);
        if (shape_lattice(at, M, ls, ar) && a2lsf(ar, M, lt)) memcpy(lsfTx, lt, sizeof(lt));
        lsf_stabilize(lsfTx, M, minGap);
    }

    double lsfQ[HD_MAX_ORDER];
    lsf_quant(c, lsfTx, lsfQPrev_, idx, lsfQ);
    // Perceptual weighting follows the transmitted envelope: with an
    // in-loop modification the codec then chases the real waveform through
    // the wrong filter instead of settling for a whitened residual.
    const double *lsfW = mod_.active(M) ? lsfTx : lsfNew;

    // --- Per-subframe filters
    double A[HD_NSUB][HD_MAX_ORDER + 1], Aq[HD_NSUB][HD_MAX_ORDER + 1], Aw[HD_NSUB][HD_MAX_ORDER + 1];
    for (int s = 0; s < HD_NSUB; s++) {
        double li[HD_MAX_ORDER];
        const double w = (s + 1.0) / HD_NSUB;
        lsf_interp(lsfPrev_, lsfW, M, w, li);
        lsf2a_stable(li, M, A[s]);
        weight_lpc(A[s], M, gamma1_, Aw[s]);
        lsf_interp(lsfQPrev_, lsfQ, M, w, li);
        lsf2a_stable(li, M, Aq[s]);
    }

    // --- Weighted speech for the open-loop pitch search
    memmove(wsp_, wsp_ + N, sizeof(float) * c.pitMax);
    float *wsp = wsp_ + c.pitMax;
    for (int s = 0; s < HD_NSUB; s++) {
        const float *sp = speech + s * L;
        for (int n = 0; n < L; n++) {
            double v = 0.0;
            for (int k = 0; k <= M; k++) v += Aw[s][k] * sp[n - k];
            wspMem_ = (float)v + W_MU * wspMem_;
            wsp[s * L + n] = wspMem_;
        }
    }
    const int D = c.fs / 8000;
    int olT[2];
    for (int h = 0; h < 2; h++)
        olT[h] = open_loop_pitch(wsp + h * (N / 2), N / 2, D, c.pitMin, c.pitMax);

    // --- Subframe loop (analysis by synthesis)
    memmove(exc_, exc_ + N, sizeof(float) * c.pitMax);
    float *exc = exc_ + c.pitMax;
    int k = M;
    for (int s = 0; s < HD_NSUB; s++) {
        const float *sp = speech + s * L;
        float *ex = exc + s * L;

        // Target: weighted (speech - zero-input synthesis)
        float zir[HD_MAX_SUB], zero[HD_MAX_SUB] = { 0 };
        float mem[HD_MAX_ORDER];
        memcpy(mem, synMem_, sizeof(mem));
        synth_filter_float(Aq[s], M, zero, zir, L, mem);
        float e0[HD_MAX_ORDER + HD_MAX_SUB];
        for (int j = 0; j < M; j++) e0[M - 1 - j] = errHist_[j];
        for (int n = 0; n < L; n++) e0[M + n] = sp[n] - zir[n];
        float xt[HD_MAX_SUB];
        float wm = wErrMem_;
        for (int n = 0; n < L; n++) {
            double v = 0.0;
            for (int j = 0; j <= M; j++) v += Aw[s][j] * e0[M + n - j];
            wm = (float)v + W_MU * wm;
            xt[n] = wm;
        }

        // Impulse response of Aw(z) / (Aq(z) (1 - mu z^-1))
        float h[HD_MAX_SUB];
        {
            float num[HD_MAX_SUB];
            for (int n = 0; n < L; n++) num[n] = (n <= M) ? (float)Aw[s][n] : 0.0f;
            float m2[HD_MAX_ORDER] = { 0 };
            synth_filter_float(Aq[s], M, num, h, L, m2);
            for (int n = 1; n < L; n++) h[n] += W_MU * h[n - 1];
        }

        // --- Adaptive codebook (closed loop around the open-loop estimate)
        int tlo, thi;
        if ((s & 1) == 0) {
            const int T0 = olT[s >> 1], dl = D + 2;
            tlo = T0 - dl;
            thi = T0 + dl;
        } else {
            const int half = 1 << (c.pitDeltaBits - 1);
            tlo = prevT_ - half;
            thi = prevT_ + half - 1;
        }
        tlo = clampi(tlo, c.pitMin, c.pitMax);
        thi = clampi(thi, c.pitMin, c.pitMax);
        int bestT = tlo;
        {
            float v[HD_MAX_SUB], y[HD_MAX_SUB];
            float bestCrit = -1.0f;
            bool recur = false;
            for (int T = tlo; T <= thi; T++) {
                if (recur && T > L) {
                    // y_T[n] = y_{T-1}[n-1] + exc[-T] h[n]  (valid while T-1 >= L)
                    const float e = ex[-T];
                    for (int n = L - 1; n > 0; n--) y[n] = y[n - 1] + e * h[n];
                    y[0] = e * h[0];
                } else {
                    adaptive_vector(ex, T, L, v);
                    convolve(v, h, L, y);
                }
                recur = (T >= L);
                const float xy = dot(xt, y, L), yy = dot(y, y, L) + 1e-12f;
                const float crit = xy > 0.0f ? xy * xy / yy : 0.0f;
                if (crit > bestCrit) {
                    bestCrit = crit;
                    bestT = T;
                }
            }
        }
        const int T = bestT;
        idx[k++] = (int16_t)((s & 1) ? (T - prevT_ + (1 << (c.pitDeltaBits - 1))) : (T - c.pitMin));
        prevT_ = T;

        float v[HD_MAX_SUB], y1[HD_MAX_SUB];
        adaptive_vector(ex, T, L, v);
        convolve(v, h, L, y1);
        float gp = dot(xt, y1, L) / (dot(y1, y1, L) + 1e-12f);
        gp = clampf(gp, 0.0f, GP_MAX);
        // scalar: quantize the pitch gain now; VQ: search target uses the
        // unquantized gain, the joint search below picks the final pair
        float gpQ = gp;
        int gcPos;
        if (c.gainVq) {
            gcPos = k++;
        } else {
            const int gpI = gp_quant(gp, c.gpBits);
            gpQ = gp_dequant(gpI, c.gpBits);
            idx[k++] = (int16_t)gpI;
            gcPos = k++;
        }

        float x2[HD_MAX_SUB];
        for (int n = 0; n < L; n++) x2[n] = xt[n] - gpQ * y1[n];

        // --- Algebraic codebook (VQ: sharpening from the previous subframe's
        // pitch gain, which the decoder knows before reading this one)
        const float bsrc = c.gainVq ? prevGpQ_ : gpQ;
        const float beta = bsrc < c.sharpenMax ? bsrc : c.sharpenMax;
        float hs[HD_MAX_SUB];
        memcpy(hs, h, sizeof(float) * L);
        sharpen(hs, L, T, beta);

        float d[HD_MAX_SUB];
        for (int n = 0; n < L; n++) {
            float acc = 0.0f;
            for (int m = n; m < L; m++) acc += x2[m] * hs[m - n];
            d[n] = acc;
        }
        // phi(i,j) = sum_{m >= max(i,j)} hs[m-i] hs[m-j], built along diagonals
        float *phi = phi_;
        for (int dg = 0; dg < L; dg++) {
            float acc = 0.0f;
            for (int i = L - 1 - dg; i >= 0; i--) {
                const int j = i + dg;
                acc += hs[L - 1 - j] * hs[L - 1 - i];
                phi[i * L + j] = phi[j * L + i] = acc;
            }
        }
        const int TR = c.tracks, P = c.pulsesPerTrack, K = TR * P;
        float sgn[HD_MAX_SUB], dd[HD_MAX_SUB], accv[HD_MAX_SUB];
        for (int n = 0; n < L; n++) {
            sgn[n] = d[n] >= 0.0f ? 1.0f : -1.0f;
            dd[n] = fabsf(d[n]);
            accv[n] = 0.0f;
        }
        int pos[HD_MAX_TRACKS * HD_MAX_PPT];
        int cnt[HD_MAX_TRACKS] = { 0 };
        float C = 0.0f, E = 0.0f;
        auto addPulse = [&](int n, float w) {
            const float *row = phi + n * L;
            const float sn = sgn[n] * w;
            for (int m = 0; m < L; m++) accv[m] += sgn[m] * sn * row[m];
        };
        for (int p = 0; p < K; p++) {
            int best = -1;
            float bn = 0.0f, bd = 1.0f;
            for (int n = 0; n < L; n++) {
                if (cnt[n % TR] >= P) continue;
                const float num = (C + dd[n]) * (C + dd[n]);
                const float den = E + phi[n * L + n] + 2.0f * accv[n] + 1e-12f;
                if (best < 0 || num * bd > bn * den) {
                    best = n;
                    bn = num;
                    bd = den;
                }
            }
            pos[p] = best;
            cnt[best % TR]++;
            C += dd[best];
            E += phi[best * L + best] + 2.0f * accv[best];
            addPulse(best, 1.0f);
        }
        // Refinement: re-place each pulse optimally within its own track.
        for (int pass = 0; pass < 2; pass++) {
            for (int p = 0; p < K; p++) {
                const int n0 = pos[p];
                addPulse(n0, -1.0f);
                C -= dd[n0];
                E -= 2.0f * accv[n0] + phi[n0 * L + n0];
                const int t = n0 % TR;
                int best = n0;
                float bn = -1.0f, bd = 1.0f;
                for (int n = t; n < L; n += TR) {
                    const float num = (C + dd[n]) * (C + dd[n]);
                    const float den = E + phi[n * L + n] + 2.0f * accv[n] + 1e-12f;
                    if (bn < 0.0f || num * bd > bn * den) {
                        best = n;
                        bn = num;
                        bd = den;
                    }
                }
                pos[p] = best;
                C += dd[best];
                E += phi[best * L + best] + 2.0f * accv[best];
                addPulse(best, 1.0f);
            }
        }
        // Emit pulses track by track.
        {
            int fill[HD_MAX_TRACKS] = { 0 };
            for (int p = 0; p < K; p++) {
                const int n = pos[p], t = n % TR;
                const int f = k + 2 * (t * P + fill[t]++);
                idx[f] = (int16_t)(n / TR);
                idx[f + 1] = (int16_t)(sgn[n] < 0.0f ? 1 : 0);
            }
        }
        float code[HD_MAX_SUB];
        build_code(c, idx + k, code);
        k += 2 * K;
        sharpen(code, L, T, beta);

        float z[HD_MAX_SUB];
        convolve(code, h, L, z);
        const float zz = dot(z, z, L) + 1e-12f;
        float gc = dot(x2, z, L) / zz;
        if (gc < 0.0f) gc = 0.0f;
        if (mod_.active(M)) {
            // With a modified (mismatched) envelope the MMSE gain collapses
            // towards silence; lean towards energy matching instead so the
            // codec audibly fights the envelope.
            gc = 0.25f * gc + 0.75f * sqrtf(dot(x2, x2, L) / zz);
        }
        const float cr = rms(code, L);
        float gcQ;
        if (c.gainVq) {
            // joint search: minimize |x - gp*y1 - gc*z|^2 over all 128 entries
            const float xy = dot(xt, y1, L), yy = dot(y1, y1, L), xz = dot(xt, z, L), yz = dot(y1, z, L);
            const float pred = vq_predict(gcHist_);
            float bestE = 1e30f;
            int best = 0;
            for (int cl = 0; cl < 16; cl++) {
                const float g = powf(10.0f, (pred + vq_corr_db(cl) + c.gcOffsetDb) / 20.0f) / (cr > 1e-9f ? cr : 1e-9f);
                for (int gl = 0; gl < 8; gl++) {
                    const float p = VQ_GP[gl];
                    const float e = -2.0f * p * xy + p * p * yy - 2.0f * g * xz + g * g * (zz - 1e-12f) + 2.0f * p * g * yz;
                    if (e < bestE) { bestE = e; best = vq_index(cl, gl); }
                }
            }
            idx[gcPos] = (int16_t)best;
            const float lvl = pred + vq_corr_db(best >> 3);
            gpQ = VQ_GP[best & 7];
            gcQ = powf(10.0f, (lvl + c.gcOffsetDb) / 20.0f) / (cr > 1e-9f ? cr : 1e-9f);
            vq_push(gcHist_, lvl);
            prevGpQ_ = gpQ;
        } else {
            const int gcI = gc_quant(c, gc, cr);
            idx[gcPos] = (int16_t)gcI;
            gcQ = gc_dequant(c, gcI, cr);
        }

        // --- Excitation + memory update (mirrors the decoder exactly)
        make_excitation(c, v, code, gpQ, gcQ, ex, L);
        float syn[HD_MAX_SUB];
        synth_filter(c, Aq[s], ex, syn, L, synMem_);
        for (int n = 0; n < L; n++) e0[M + n] = sp[n] - syn[n];
        for (int n = 0; n < L; n++) {
            double vv = 0.0;
            for (int j = 0; j <= M; j++) vv += Aw[s][j] * e0[M + n - j];
            wErrMem_ = (float)vv + W_MU * wErrMem_;
        }
        for (int j = 0; j < M; j++) errHist_[j] = e0[M + L - 1 - j];

        if (synthOut) output_stage(c, syn, 1.0f, 1.0f, L, &deemph_, synthOut + s * L);
    }

    memcpy(lsfPrev_, lsfW, sizeof(double) * M);
    memcpy(lsfQPrev_, lsfQ, sizeof(double) * M);
}

// ---------------------------------------------------------------------------
// Decoder

void Decoder::init(const Config &c) {
    c_ = c;
    memset(exc_, 0, sizeof(exc_));
    memset(synMem_, 0, sizeof(synMem_));
    memset(synRefMem_, 0, sizeof(synRefMem_));
    eRef_ = eMod_ = 0.0f;
    dRef_ = dMod_ = 0.0f;
    deemph_ = 0.0f;
    lsf_uniform(c.order, lsfQPrev_);
    lsf_uniform(c.order, lsfMPrev_);
    prevT_ = c.pitMin;
    for (int i = 0; i < 4; i++) gcHist_[i] = VQ_MEAN_DB;
    prevGpQ_ = 0.0f;
    prevGain_ = 1.0f;
    modifier_.reset(c.order);
}

void Decoder::reconfigure(const Config &c) {
    if (c.fs != c_.fs || c.frameLen != c_.frameLen || c.profile != c_.profile) init(c);
    else c_ = c;
}

void Decoder::decode(const int16_t *idx, float *out, const LpMod *mod, FrameTrace *trace, const ExcMod *em) {
    const Config &c = c_;
    const int M = c.order, N = c.frameLen, L = c.subLen;
    const double minGap = lsf_min_gap(c);
    const bool modOn = mod && mod->active(M);

    double lsfQ[HD_MAX_ORDER], lsfM[HD_MAX_ORDER];
    lsf_dequant(c, idx, lsfQPrev_, lsfQ);
    memcpy(lsfM, lsfQ, sizeof(double) * M);
    if (mod && mod->lsfActive()) modifier_.apply(lsfM, *mod, minGap);
    LatticeShape ls;
    const bool latticeOn = modOn && mod->latticeActive(M);
    if (latticeOn) ls = modifier_.lattice(*mod);  // jitter: new offsets per frame

    memmove(exc_, exc_ + N, sizeof(float) * c.pitMax);
    float *exc = exc_ + c.pitMax;
    int k = M;
    for (int s = 0; s < HD_NSUB; s++) {
        const double w = (s + 1.0) / HD_NSUB;
        double li[HD_MAX_ORDER], Aq[HD_MAX_ORDER + 1], Am[HD_MAX_ORDER + 1];
        lsf_interp(lsfQPrev_, lsfQ, M, w, li);
        lsf2a_stable(li, M, Aq);
        if (modOn) {
            lsf_interp(lsfMPrev_, lsfM, M, w, li);
            lsf2a_stable(li, M, Am);
            if (latticeOn) {
                double t[HD_MAX_ORDER + 1];
                if (shape_lattice(Am, M, ls, t)) memcpy(Am, t, sizeof(t[0]) * (M + 1));
            }
        } else {
            memcpy(Am, Aq, sizeof(double) * (M + 1));
        }

        // Pitch lag
        int T;
        if (s & 1) T = prevT_ + idx[k] - (1 << (c.pitDeltaBits - 1));
        else       T = c.pitMin + idx[k];
        T = clampi(T, c.pitMin, c.pitMax);
        prevT_ = T; // delta decoding follows the transmitted lag
        float gpQ, vqLvl = 0.0f;
        int gcI = 0;
        float sharpenSrc;
        if (c.gainVq) {
            const int vq = clampi(idx[k + 1], 0, 127);
            gpQ = VQ_GP[vq & 7];
            vqLvl = vq_predict(gcHist_) + vq_corr_db(vq >> 3);
            vq_push(gcHist_, vqLvl);
            sharpenSrc = prevGpQ_;
            prevGpQ_ = gpQ;
            k += 2;
        } else {
            gpQ = gp_dequant(idx[k + 1], c.gpBits);
            gcI = idx[k + 2];
            sharpenSrc = gpQ;
            k += 3;
        }
        if (em && em->active()) {
            // Pitch shift by rescaling the lag (up to an octave above pitMin),
            // voicing/noise by rescaling the two gains.
            T = clampi((int)lrintf(T / (em->pitchRatio > 0.05f ? em->pitchRatio : 0.05f)), c.pitMin / 2, c.pitMax);
            gpQ = clampf(gpQ * em->gpScale, 0.0f, 1.5f);
        }

        float *ex = exc + s * L;
        float v[HD_MAX_SUB], code[HD_MAX_SUB];
        adaptive_vector(ex, T, L, v);
        build_code(c, idx + k, code);
        k += 2 * c.tracks * c.pulsesPerTrack;
        sharpen(code, L, T, sharpenSrc < c.sharpenMax ? sharpenSrc : c.sharpenMax);
        const float crd = rms(code, L);
        float gcQ = c.gainVq ? powf(10.0f, (vqLvl + c.gcOffsetDb) / 20.0f) / (crd > 1e-9f ? crd : 1e-9f)
                             : gc_dequant(c, gcI, crd);
        if (em && em->active()) gcQ *= em->gcScale;
        make_excitation(c, v, code, gpQ, gcQ, ex, L);
        if (trace) {
            for (int n = 0; n < L; n++) {
                trace->excAdaptive[s * L + n] = gpQ * v[n];
                trace->excFixed[s * L + n] = gcQ * code[n];
            }
            trace->pitch[s] = T;
            trace->gp[s] = gpQ;
            trace->gcDb[s] = (c.gainVq ? vqLvl + c.gcOffsetDb : gc_db(c, gcI)) + (em && em->active() ? 20.0f * log10f(em->gcScale + 1e-6f) : 0.0f);
            int np = 0;
            const int K = c.tracks * c.pulsesPerTrack;
            const int16_t *pf = idx + k - 2 * K;
            for (int t = 0; t < c.tracks; t++)
                for (int j = 0; j < c.pulsesPerTrack; j++) {
                    const int f = 2 * (t * c.pulsesPerTrack + j);
                    const int n = clampi(pf[f], 0, c.positions - 1) * c.tracks + t;
                    trace->pulses[s][np++] = (int16_t)(pf[f + 1] ? -(n + 1) : (n + 1));
                }
            trace->nPulses[s] = np;
            if (s == HD_NSUB - 1) {
                memcpy(trace->aRef, Aq, sizeof(double) * (M + 1));
                memcpy(trace->aMod, Am, sizeof(double) * (M + 1));
            }
        }

        // Reference (unmodified) synthesis always runs so the loudness
        // compensation has a warm state the moment modification kicks in.
        float ref[HD_MAX_SUB], syn[HD_MAX_SUB];
        synth_filter(c, Aq, ex, ref, L, synRefMem_);
        synth_filter(c, Am, ex, syn, L, synMem_);
        float gTarget = 1.0f;
        if (modOn) {
            // Loudness compensation: match smoothed subframe energies of the
            // modified and reference synthesis (applied post-filter so the
            // filter memory stays unscaled).
            // measured after de-emphasis, i.e. on what is actually heard
            // (envelopes pushed to high frequencies would otherwise come out
            // quieter); the gain itself is applied before de-emphasis
            float er = 0.0f, em = 0.0f;
            for (int n = 0; n < L; n++) {
                dRef_ = ref[n] + c.preEmph * dRef_;
                dMod_ = syn[n] + c.preEmph * dMod_;
                er += dRef_ * dRef_;
                em += dMod_ * dMod_;
            }
            eRef_ = 0.6f * eRef_ + 0.4f * er;
            eMod_ = 0.6f * eMod_ + 0.4f * em;
            gTarget = clampf(sqrtf((eRef_ + 1e-9f) / (eMod_ + 1e-9f)), 0.01f, 100.0f);
        } else {
            eRef_ = eMod_ = 0.0f;
        }
        output_stage(c, syn, prevGain_, gTarget, L, &deemph_, out + s * L);
        prevGain_ = gTarget;
    }
    if (trace) {
        trace->order = M;
        trace->frameLen = N;
        trace->modActive = modOn;
        trace->compGain = prevGain_;
        memcpy(trace->lsfRef, lsfQ, sizeof(double) * M);
        memcpy(trace->lsfMod, modOn ? lsfM : lsfQ, sizeof(double) * M);
    }
    memcpy(lsfQPrev_, lsfQ, sizeof(double) * M);
    memcpy(lsfMPrev_, lsfM, sizeof(double) * M);
}

} // namespace hdacelp
