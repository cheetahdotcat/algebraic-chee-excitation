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
    // Calibrated bitstream models (knobs and MIDI keys). `intensity` is then
    // the probability that a frame is hit, `magnitude` the depth:
    //   bit flips / burst: log bit error rate, corrupt_ber(magnitude)
    //   slip:              corrupt_slip_bits(magnitude) bits
    //   overflow:          corrupt_overflow_words(magnitude) words
    //   reinterleave:      corrupt_shuffle_swaps(magnitude) swaps
    // `trigger` forces a hit on this frame (start of a MIDI key hit).
    // Uncalibrated configs keep the original (much harsher) behaviour.
    bool  calibrated = false;
    bool  trigger    = false;
    // Freeze: capture the frame on `trigger` and keep holding it (no refresh
    // between frozen frames) — the MIDI key behaviour.
    bool  hold       = false;
};

// Calibration curves shared by the DSP and the UI readouts.
inline float corrupt_rate_curve(float knob01) { return knob01 * knob01; } // knob -> rate, fine low end
float corrupt_ber(float depth);              // 1e-4 (0) .. 5e-2 (1), logarithmic
int   corrupt_slip_bits(float depth);        // 1 .. 16
int   corrupt_overflow_words(float depth);   // 1 .. 16
int   corrupt_shuffle_swaps(float depth);    // 1 .. 32

// Generic description of a quantized parameter frame, so the musical
// parameter-domain stages work on any codec layout (TETRA ana[] or HD ACELP).
enum CorruptFieldCat {
    kFieldLsp = 0, kFieldPitch, kFieldCodebook, kFieldSign, kFieldShift,
    kFieldGain,       // codebook gain, index monotonic in dB
    kFieldGainPitch,  // pitch gain, index linear in gain
    kFieldGainVQ,     // TETRA joint (pitch, codebook) energy VQ index
    kFieldGainPVQ     // HD indexed gain codebook: predictive, level-major index
};

// Memory of the parameter-domain stages (freeze, gain wobble). One per codec
// stream; owned by the caller.
struct CorruptState {
    Word16 freezePrev[600];
    int    haveFreeze = 0;
    float  gainOfs = 0.0f;   // current gain-wobble offset (-1..1 of the depth)
    float  pvqOfs[4] = { 0, 0, 0, 0 }; // offsets (steps) already in the gain predictor
};

// What the bitstream modes (and MIDI bitstream keys) hit.
enum CorruptTarget {
    kTargetAll = 0,   // the whole transmitted bitstream / channel symbols
    kTargetSpeech,    // all codec parameters (no channel coding overhead)
    kTargetSynth,     // LPC envelope (LSF / LSP)
    kTargetPitch,     // pitch lag
    kTargetGain,      // gains
    kTargetCodebook,  // algebraic codebook: pulse positions, signs, shift
    kNumTargets
};
unsigned corrupt_target_mask(int target);   // bit (1 << CorruptFieldCat); 0 = kTargetAll
struct CorruptField {
    unsigned char bits;
    unsigned char cat;   // CorruptFieldCat
};

// Deterministic, RT-safe PRNG control (optional; auto-seeded otherwise).
void corrupt_seed(unsigned s);

// Stage entry points. corrupt_apply_params handles modes 1..5 (ignores others);
// corrupt_apply_bitstream handles modes 6..10 (ignores others).
void corrupt_apply_params(Word16 *ana, int ana_len, const CorruptCfg &cfg);
// Same stages over an arbitrary field table. freezePrev (n entries) and
// haveFreeze hold the Freeze mode's memory and belong to the caller.
void corrupt_apply_fields(Word16 *vals, const CorruptField *fields, int n, const CorruptCfg &cfg,
                          CorruptState *state);
// Bitstream modes restricted to the bits of the fields selected by `mask`
// (see corrupt_target_mask): the selected bits are gathered into a virtual
// bitstream, corrupted, and scattered back into the field values.
void corrupt_apply_bitstream_fields(Word16 *vals, const CorruptField *fields, int n, const CorruptCfg &cfg,
                                    unsigned mask);
// Same for the TETRA ana[] frame.
void corrupt_apply_bitstream_ana(Word16 *ana, int ana_len, const CorruptCfg &cfg, unsigned mask);
// softSymbols: frame holds channel soft symbols (TETRA), one per channel bit;
// a bit error then negates a symbol. Otherwise bits are packed 16 per word.
void corrupt_apply_bitstream(Word16 *frame, int len, const CorruptCfg &cfg, bool softSymbols = false);

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
