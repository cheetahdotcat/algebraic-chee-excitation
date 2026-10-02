// TETRA-style protected channel for HD ACELP frames.
//
// Like the ETSI TETRA channel coder, source bits are split into classes:
//   class 1 — envelope (LSF), pitch and gains: CRC-8 + convolutional code
//   class 0 — algebraic pulse positions and signs: weaker code or none
// The coded stream is interleaved and sent as soft symbols (+127 = 0,
// -127 = 1, 0 = erased), the same container the TETRA path uses, so the
// bitstream corruption stages apply unchanged (a bit error negates a symbol).
// The receiver deinterleaves, runs a soft-decision Viterbi decoder (K = 5,
// rate-1/3 mother code, punctured) and uses the CRC as bad-frame indicator.
#ifndef TP_CHANNEL_HPP_INCLUDED
#define TP_CHANNEL_HPP_INCLUDED

#include <stdint.h>
#include "hd_acelp.hpp"

namespace hdacelp {

enum FecLevel { FEC_OFF = 0, FEC_LIGHT, FEC_ETSI, FEC_STRONG, FEC_MAX, FEC_NUM_LEVELS };

// Code rate as numerator / denominator per class (0/0 = uncoded).
struct FecRates { int n1, d1, n0, d0; bool crc; };
FecRates fec_rates(int level);

static const int TP_MAX_SYMBOLS = 2048;

// Per-symbol meaning, for visualization.
enum TpSymbolClass { TP_SYM_CLASS1 = 0, TP_SYM_CLASS0_CODED = 1, TP_SYM_RAW = 2 };

class Channel {
public:
    void configure(const Config& c, int fecLevel);
    int  symbols() const { return nsym_; }
    int  fecLevel() const { return fec_; }

    // words: packed source frame (pack_words); sym: soft symbols (symbols()).
    void encode(const int16_t* words, int16_t* sym);
    // Returns false when the CRC fails (bad frame). words: recovered frame.
    bool decode(const int16_t* sym, int16_t* words);

    TpSymbolClass symbolClass(int i) const { return (TpSymbolClass)cls_[i]; }

private:
    int fec_ = FEC_OFF;
    int nbits_ = 0;                 // source bits
    int n1_ = 0, n0_ = 0;           // class sizes
    int16_t idx1_[1024], idx0_[1536]; // source bit positions of each class
    int c1_ = 0, c0_ = 0, raw0_ = 0;  // coded lengths: class 1, class 0 coded, class 0 raw
    int nsym_ = 0;
    int16_t perm_[TP_MAX_SYMBOLS];  // interleaver: channel position -> stream position
    uint8_t cls_[TP_MAX_SYMBOLS];   // class of each channel position
    FecRates r_ = { 0, 0, 0, 0, false };
};

} // namespace hdacelp

#endif
