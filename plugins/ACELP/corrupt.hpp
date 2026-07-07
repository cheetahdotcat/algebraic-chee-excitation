#ifndef CORRUPT_HPP_INCLUDED
#define CORRUPT_HPP_INCLUDED

#include "math.h"
#include "codec/source.h"

// ---------------------------------------------------------------------------
// Corruption configuration + dispatch
//
// Corruption is applied at two stages of the encode pipeline:
//   * PARAMETER domain  - the 23 quantized codec parameters (ana[]) BEFORE
//                         channel coding. Musical/controllable: the decoder
//                         still resynthesizes coherent-but-mangled speech.
//   * BITSTREAM domain  - the channel-coded/interleaved 432-word frame. Extreme
//                         /destructive: fights the channel decoder's error
//                         correction, producing catastrophic breakup.
//
// intensity/magnitude are normalized 0..1 (rate / depth respectively).
// ---------------------------------------------------------------------------
enum CorruptMode {
    kCorruptOff = 0,
    // Parameter-domain (musical)
    kCorruptLsp        = 1,   // formant/spectral scramble
    kCorruptPitch      = 2,   // pitch-lag chaos
    kCorruptCodebook   = 3,   // excitation codebook scramble (texture)
    kCorruptGain       = 4,   // subframe gain spikes/drops
    kCorruptFreeze     = 5,   // hold previous frame's parameters (robotic)
    // Bitstream-domain (extreme)
    kCorruptBitFlips   = 6,
    kCorruptBitSlip    = 7,   // desync: shift the whole frame by N bits
    kCorruptBurst      = 8,   // Gilbert-Elliott burst errors
    kCorruptOverflow   = 9,   // saturate random words to extremes
    kCorruptReinterleave = 10 // permute word positions
};

struct CorruptCfg {
    int   mode;       // CorruptMode
    float intensity;  // 0..1  (probability / rate)
    float magnitude;  // 0..1  (depth / amount)
};

// Deterministic, RT-safe PRNG control (optional; auto-seeded otherwise).
void corrupt_seed(unsigned s);

// Stage entry points. corrupt_apply_params handles modes 1..5 (ignores others);
// corrupt_apply_bitstream handles modes 6..10 (ignores others).
void corrupt_apply_params(Word16 *ana, int ana_len, const CorruptCfg &cfg);
void corrupt_apply_bitstream(Word16 *frame, int len, const CorruptCfg &cfg);

// ---------------------------------------------------------------------------
// Individual algorithms (also used directly by the MIDI-triggered path).
// ---------------------------------------------------------------------------
void bit_desync_shift_left_Word16(Word16 *array, int len);
void burst_error(uint8_t *frame, int len, int burst_len);
void corrupt_by_overflow(short *acelp_array, int len);

void corrupt_by_wrong_interleave(short *coded_array, int len);
void random_bit_desync_Word16(Word16 *array, int len, int max_shift_bits, int flip_probability_percent);
void corrupt_bit_flips_Word16(Word16 *frame, int len, int bit_flip_percent);

#endif // CORRUPT_HPP_INCLUDED
