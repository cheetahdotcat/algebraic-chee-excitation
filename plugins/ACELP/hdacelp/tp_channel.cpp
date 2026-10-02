#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#include "tp_channel.hpp"
#include <string.h>

namespace hdacelp {

// K = 5, rate-1/3 mother code (octal 23, 35, 37), punctured to the class rate.
// The first two generators are the optimal K = 5 rate-1/2 code (d_free 7) and
// share no polynomial factor, so every punctured rate stays non-catastrophic.
// (25/33 would not: both contain x^2+x+1, and long frames fall apart.)
static const int K = 5;
static const int TAIL = K - 1;
static const unsigned GEN[3] = { 023, 035, 037 };
static const int CRC_BITS = 8;

FecRates fec_rates(int level) {
    switch (level) {
    case FEC_LIGHT:  return { 3, 4, 0, 0, true };
    case FEC_ETSI:   return { 2, 3, 0, 0, true };   // ~ ETSI: class 1 coded, class 0 raw
    case FEC_STRONG: return { 1, 2, 3, 4, true };
    case FEC_MAX:    return { 1, 3, 1, 2, true };
    default:         return { 0, 0, 0, 0, false };  // OFF: everything raw
    }
}

// Puncturing patterns over the three mother outputs, one row per input bit.
static const uint8_t P13[1][3] = { { 1, 1, 1 } };
static const uint8_t P12[1][3] = { { 1, 1, 0 } };
// Standard puncturing of the (23, 35) rate-1/2 subcode (X = 10 / 110,
// Y = 11 / 101).
static const uint8_t P23[2][3] = { { 1, 1, 0 }, { 0, 1, 0 } };
static const uint8_t P34[3][3] = { { 1, 1, 0 }, { 0, 1, 0 }, { 1, 0, 0 } };

static const uint8_t (*pattern(int n, int d, int &period))[3] {
    if (n == 1 && d == 3) { period = 1; return P13; }
    if (n == 1 && d == 2) { period = 1; return P12; }
    if (n == 2 && d == 3) { period = 2; return P23; }
    period = 3;
    return P34;
}

static int coded_len(int inBits, int n, int d) {
    if (d == 0) return inBits;
    int period;
    const uint8_t (*p)[3] = pattern(n, d, period);
    int len = 0;
    for (int i = 0; i < inBits; i++) len += p[i % period][0] + p[i % period][1] + p[i % period][2];
    return len;
}

static inline int parity(unsigned v) { return __builtin_parity(v); }

// Convolutional encode in[0..k) (+ zero tail), punctured, as soft symbols.
static int conv_encode(const uint8_t *in, int k, int n, int d, int16_t *out) {
    int period;
    const uint8_t (*p)[3] = pattern(n, d, period);
    unsigned reg = 0;
    int o = 0;
    for (int i = 0; i < k + TAIL; i++) {
        const unsigned b = i < k ? in[i] : 0;
        reg = ((reg << 1) | b) & 0x1F;
        for (int g = 0; g < 3; g++)
            if (p[i % period][g]) out[o++] = parity(reg & GEN[g]) ? -127 : 127;
    }
    return o;
}

// Soft-decision Viterbi for conv_encode; returns the k information bits.
static void viterbi(const int16_t *sym, int k, int n, int d, uint8_t *outBits, uint8_t *dec) {
    int period;
    const uint8_t (*p)[3] = pattern(n, d, period);
    const int T = k + TAIL;
    int32_t pm[16], nm[16];
    for (int s = 0; s < 16; s++) pm[s] = s == 0 ? 0 : -(1 << 28);
    int o = 0;
    for (int t = 0; t < T; t++) {
        int16_t r[3] = { 0, 0, 0 };
        for (int g = 0; g < 3; g++)
            if (p[t % period][g]) r[g] = sym[o++];
        for (int ns = 0; ns < 16; ns++) {
            const unsigned b = ns & 1;
            int32_t best = -(1 << 30);
            uint8_t bestTop = 0;
            for (unsigned top = 0; top < 2; top++) {
                const unsigned s = (ns >> 1) | (top << 3);
                const unsigned reg = ((s << 1) | b) & 0x1F;
                int32_t m = pm[s];
                for (int g = 0; g < 3; g++) m += parity(reg & GEN[g]) ? -r[g] : r[g];
                if (m > best) { best = m; bestTop = (uint8_t)top; }
            }
            nm[ns] = best;
            dec[t * 16 + ns] = bestTop;
        }
        memcpy(pm, nm, sizeof(pm));
    }
    unsigned state = 0; // tail forces the final state to 0
    for (int t = T - 1; t >= 0; t--) {
        if (t < k) outBits[t] = state & 1;
        state = (state >> 1) | ((unsigned)dec[t * 16 + state] << 3);
    }
}

static uint8_t crc8(const uint8_t *bits, int n) {
    uint8_t c = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t fb = (uint8_t)(((c >> 7) & 1) ^ bits[i]);
        c = (uint8_t)(c << 1);
        if (fb) c ^= 0x07;
    }
    return c;
}

static inline int get_bit(const int16_t *words, int i) { return ((uint16_t)words[i >> 4] >> (15 - (i & 15))) & 1; }
static inline void set_bit(int16_t *words, int i, int v) {
    if (v) words[i >> 4] = (int16_t)((uint16_t)words[i >> 4] | (0x8000u >> (i & 15)));
}

void Channel::configure(const Config &c, int fecLevel) {
    fec_ = fecLevel < 0 ? 0 : (fecLevel >= FEC_NUM_LEVELS ? FEC_NUM_LEVELS - 1 : fecLevel);
    r_ = fec_rates(fec_);
    Field f[HD_MAX_FIELDS];
    c.layout(f);
    n1_ = n0_ = 0;
    int bit = 0;
    for (int i = 0; i < c.numFields(); i++) {
        const bool cls1 = r_.d1 != 0 && (f[i].cat == CAT_LSF || f[i].cat == CAT_PITCH || f[i].cat == CAT_GAIN);
        for (int b = 0; b < f[i].bits; b++, bit++) {
            if (cls1 && n1_ < 1024) idx1_[n1_++] = (int16_t)bit;
            else if (n0_ < 1536)    idx0_[n0_++] = (int16_t)bit;
        }
    }
    nbits_ = bit;
    c1_ = r_.d1 ? coded_len(n1_ + CRC_BITS + TAIL, r_.n1, r_.d1) : 0;
    c0_ = r_.d0 ? coded_len(n0_ + TAIL, r_.n0, r_.d0) : 0;
    raw0_ = r_.d0 ? 0 : n0_;
    nsym_ = c1_ + c0_ + raw0_;
    if (nsym_ > TP_MAX_SYMBOLS) nsym_ = TP_MAX_SYMBOLS;

    // Pruned 16-column block interleaver: write rows, read columns.
    const int cols = 16, rows = (nsym_ + cols - 1) / cols;
    int pos = 0;
    for (int col = 0; col < cols; col++)
        for (int row = 0; row < rows; row++) {
            const int s = row * cols + col;
            if (s < nsym_) {
                perm_[pos] = (int16_t)s;
                cls_[pos] = (uint8_t)(s < c1_ ? TP_SYM_CLASS1 : (s < c1_ + c0_ ? TP_SYM_CLASS0_CODED : TP_SYM_RAW));
                pos++;
            }
        }
}

void Channel::encode(const int16_t *words, int16_t *sym) {
    static thread_local int16_t stream[TP_MAX_SYMBOLS + 64];
    static thread_local uint8_t in[2048];
    int o = 0;
    if (r_.d1) {
        for (int i = 0; i < n1_; i++) in[i] = (uint8_t)get_bit(words, idx1_[i]);
        const uint8_t c = crc8(in, n1_);
        for (int i = 0; i < CRC_BITS; i++) in[n1_ + i] = (c >> (7 - i)) & 1;
        o += conv_encode(in, n1_ + CRC_BITS, r_.n1, r_.d1, stream + o);
    }
    for (int i = 0; i < n0_; i++) in[i] = (uint8_t)get_bit(words, idx0_[i]);
    if (r_.d0) o += conv_encode(in, n0_, r_.n0, r_.d0, stream + o);
    else for (int i = 0; i < n0_; i++) stream[o++] = in[i] ? -127 : 127;
    for (int p = 0; p < nsym_; p++) sym[p] = stream[perm_[p]];
}

bool Channel::decode(const int16_t *sym, int16_t *words) {
    static thread_local int16_t stream[TP_MAX_SYMBOLS + 64];
    static thread_local uint8_t bits[2048];
    static thread_local uint8_t dec[(2048 + TAIL) * 16];
    for (int p = 0; p < nsym_; p++) stream[perm_[p]] = sym[p];
    memset(words, 0, sizeof(int16_t) * ((nbits_ + 15) / 16));
    bool ok = true;
    int o = 0;
    if (r_.d1) {
        viterbi(stream, n1_ + CRC_BITS, r_.n1, r_.d1, bits, dec);
        uint8_t rx = 0;
        for (int i = 0; i < CRC_BITS; i++) rx = (uint8_t)((rx << 1) | bits[n1_ + i]);
        ok = crc8(bits, n1_) == rx;
        for (int i = 0; i < n1_; i++) set_bit(words, idx1_[i], bits[i]);
        o += c1_;
    }
    if (r_.d0) {
        viterbi(stream + o, n0_, r_.n0, r_.d0, bits, dec);
        for (int i = 0; i < n0_; i++) set_bit(words, idx0_[i], bits[i]);
    } else {
        for (int i = 0; i < n0_; i++) set_bit(words, idx0_[i], stream[o + i] < 0);
    }
    return ok;
}

} // namespace hdacelp
