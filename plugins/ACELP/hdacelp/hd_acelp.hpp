// HD ACELP - a wideband / super-wideband / fullband algebraic CELP codec with
// user-controllable linear prediction.
//
// Unlike the ETSI TETRA reference coder (fixed point, global state, 8 kHz
// only) this codec is floating point and instance based, so several instances
// can coexist and the sample rate is a runtime choice.
//
//   rate    order  frame  subframe  tracks x 16 positions
//   16 kHz   16    320     80        5
//   32 kHz   24    640    160       10
//   48 kHz   32    960    240       15
//
// Frames are 20 ms (4 x 5 ms subframes) with 5 ms of lookahead. A frame is a
// flat array of quantizer indices ("fields"); layout() describes each field's
// width and meaning so the bitstream can be packed, visualized and corrupted
// generically.
//
// Linear prediction modification (LpMod) is applied in the LSF domain, either
//   * in the decoder: the decoded excitation is resynthesized through a
//     modified envelope (clean formant / timbre effect), or
//   * in the encoder: the modified envelope is what gets transmitted, and the
//     analysis-by-synthesis loop tries to compensate for it with the
//     excitation (the codec "fights" the modification, giving artifacts).
#ifndef HD_ACELP_HPP_INCLUDED
#define HD_ACELP_HPP_INCLUDED

#include <math.h>
#include <stdint.h>
#include "hd_lpc.hpp"

namespace hdacelp {

// Field categories. The numbering matches the parameter-domain corruption
// categories in corrupt.cpp (LSP, pitch, codebook, sign, shift, gain).
enum FieldCat { CAT_LSF = 0, CAT_PITCH = 1, CAT_CB = 2, CAT_SIGN = 3, CAT_SHIFT = 4, CAT_GAIN = 5 };

struct Field {
    unsigned char bits;
    unsigned char cat;
};

static const int HD_NSUB       = 4;
static const int HD_MAX_FRAME  = 960;
static const int HD_MAX_SUB    = 240;
static const int HD_MAX_TRACKS = 15;
static const int HD_MAX_PPT    = 4;  // pulses per track
static const int HD_MAX_PITCH  = 960;
static const int HD_MAX_FIELDS = HD_MAX_ORDER + HD_NSUB * (3 + 2 * HD_MAX_TRACKS * HD_MAX_PPT);
static const int HD_MAX_BITS   = 4096;
static const int HD_MAX_WORDS  = HD_MAX_BITS / 16;

// Bitrate modes, lowest first. Modes 0-2 thin out the algebraic codebook
// (1, 2 or 4 pulses per subframe, anywhere in the subframe or in wide tracks)
// and coarsen the envelope and gain quantizers; from mode 3 on every 16-sample
// track carries 1..4 pulses.
static const int HD_NUM_MODES = 7;

// Indexed joint gain codebook (Config::gainVq): 7-bit index = codebook-gain
// correction level (major, HD_VQ_LEVELS steps of HD_VQ_STEP_DB around an MA
// prediction) x pitch-gain level (minor, 8 levels). Exposed so the corruption
// engine can move the index in a predictor-aware way.
static const int   HD_VQ_LEVELS    = 16;
static const float HD_VQ_STEP_DB   = 3.0f;
static const float HD_VQ_LOW_DB    = -24.0f;   // correction of level 0
static const float HD_VQ_TAPS[4]   = { 0.5f, 0.25f, 0.1f, 0.05f };

// Arithmetic used for the signal path (filters, excitation, output).
enum Math {
    MATH_FLOAT   = 0,  // 32-bit float (double for the LPC maths)
    MATH_FIXED16 = 1   // ETSI-style 16-bit: Q13 samples/states, block-scaled
                       // 16-bit LPC coefficients, saturating 32-bit accumulators
};

// Codec profiles. HD: 20 ms frames at 16/32/48 kHz. TETRA_PLUS: the ETSI
// TETRA structure (30 ms frames of 4 x 7.5 ms subframes, interleaved 15-position
// algebraic tracks, ETSI pitch range) at 8 or 16 kHz with finer quantizers.
enum Profile { PROFILE_HD = 0, PROFILE_TETRA_PLUS = 1 };
static const int TP_NUM_LEVELS = 3;

struct Config {
    int profile = PROFILE_HD;
    int fs;
    int order;
    int frameLen;
    int subLen;
    int mode;
    int math;
    int tracks;
    int pulsesPerTrack;
    int positions;       // per track (subLen / tracks)
    int posBits;
    int lsfBitsLo, lsfBitsHi;  // lower / upper half of the LSFs
    int gpBits, gcBits;
    int pitMin, pitMax;
    int pitAbsBits, pitDeltaBits;
    int lookahead;

    // Algorithm constants. Encoder and decoder each use their own Config, so
    // these can deliberately disagree (the decoder then drifts).
    float preEmph    = 0.68f;  // pre-emphasis / de-emphasis coefficient
    float lsfPred    = 0.6f;   // inter-frame LSF prediction factor
    float gcOffsetDb = 0.0f;   // shift of the codebook-gain quantizer table
    float sharpenMax = 0.8f;   // upper bound of the pitch sharpening factor
    // Gain quantizer: scalar (separate pitch / codebook gain fields) or an
    // indexed joint codebook like ETSI TETRA (one field per subframe).
    bool  gainVq     = false;
    int   gainFields() const { return gainVq ? 1 : 2; }

    // fs in {16000, 32000, 48000}; mode in 0..HD_NUM_MODES-1.
    static Config make(int fs, int mode, int math = MATH_FLOAT);
    // fs in {8000, 16000}; level 0..TP_NUM_LEVELS-1 (low / mid / high).
    static Config makeTetraPlus(int fs, int level, int math = MATH_FLOAT);
    int frameMs() const { return frameLen * 1000 / fs; }

    int  lsfBits(int i) const { return i < order / 2 ? lsfBitsLo : lsfBitsHi; }

    int  numFields() const;
    void layout(Field *out) const;  // numFields() entries
    int  numBits() const;
    double bitrate() const { return numBits() * (double)fs / frameLen; }
};

// Pack the fields MSB-first into 16-bit words (the same container the TETRA
// path uses, so the bitstream corruption stages apply unchanged).
int pack_words(const Config &c, const int16_t *idx, int16_t *words);   // returns word count
void unpack_words(const Config &c, const int16_t *words, int16_t *idx);

// ---------------------------------------------------------------------------
// Linear prediction modification

struct LpMod {
    float warp   = 0.0f;  // -1..1  all-pass frequency warp (formant shift)
    float depth  = 0.0f;  // -1..1  formant contrast: -1 flat/whispery, +1 exaggerated
    float order  = HD_MAX_ORDER; // effective LPC order, 0 (raw excitation) .. M
    float smooth = 0.0f;  //  0..1  temporal smoothing of the envelope
    float freeze = 0.0f;  //  0..1  blend towards the envelope held when freeze engaged
    float crush  = 0.0f;  //  0..1  snap LSFs to a coarse grid
    float mirror = 0.0f;  //  0..1  blend towards the envelope mirrored around fs/4
    // lattice (reflection-coefficient) page, see shape_lattice()
    float taper     = 0.0f;  // 0..1  soft order cut
    float resonance = 1.0f;  // 0.25..1.75
    float band      = 0.0f;  // lowest coefficients removed (0..8)
    float jitter    = 0.0f;  // 0..1  per-frame random wobble of the coefficients

    bool lsfActive() const;
    bool latticeActive(int M) const {
        return order < M || taper > 1e-3f || band > 1e-3f || fabsf(resonance - 1.0f) > 1e-3f || jitter > 1e-3f;
    }
    bool active(int M) const { return lsfActive() || latticeActive(M); }
};

// Stateful LSF-domain modifier (freeze + smoothing need memory).
class LpModifier {
public:
    void reset(int M);
    // Applies freeze, smoothing, warp, depth and crush to lsf[0..M-1].
    void apply(double *lsf, const LpMod &m, double minGap);
    // Once per frame: draws new jitter offsets; returns the lattice shape.
    LatticeShape lattice(const LpMod &m);

private:
    int    M_ = 0;
    double held_[HD_MAX_ORDER];
    double smoothed_[HD_MAX_ORDER];
    bool   haveSmoothed_ = false;
    bool   frozen_ = false;
    double jit_[HD_MAX_ORDER + 1];
    uint32_t rng_ = 0x2545F491u;
};

// ---------------------------------------------------------------------------

class Encoder {
public:
    void init(const Config &c);
    // Change mode/math without resetting the codec state (same fs; falls back
    // to init() otherwise). Takes effect on the next frame; the decoder must
    // be switched on the same frame.
    void reconfigure(const Config &c);
    // In-loop LP modification ("encoder" stage). Default is inactive.
    void setLpMod(const LpMod &m) { mod_ = m; }
    // x: frameLen samples at c.fs, nominally in [-1, 1]. idx: numFields entries.
    // synth (optional): local synthesis, identical to an unmodified decoder
    // output delayed by the lookahead.
    void encode(const float *x, int16_t *idx, float *synth = nullptr);

private:
    Config c_;
    LpMod mod_;
    LpModifier modifier_;

    float  hpX1_, hpY1_, preX1_, deemph_;
    float  sp_[HD_MAX_FRAME * 2 + HD_MAX_SUB];     // [hist | frame | lookahead]
    int    hist_;
    float  win_[HD_MAX_FRAME * 2 + HD_MAX_SUB];
    int    winLen_;
    float  wsp_[HD_MAX_PITCH + HD_MAX_FRAME];      // weighted speech [pitMax | frame]
    float  wspMem_;
    float  exc_[HD_MAX_PITCH + HD_MAX_FRAME + 1];  // [pitMax | frame]
    float  synMem_[HD_MAX_ORDER];                  // past local synthesis, [0] newest
    float  errHist_[HD_MAX_ORDER];                 // past weighted-domain error input, [0] newest
    float  wErrMem_;
    double lsfPrev_[HD_MAX_ORDER], lsfQPrev_[HD_MAX_ORDER];
    int    prevT_;
    float  gcHist_[4];   // gain VQ: past quantized innovation levels (dB)
    float  prevGpQ_;     // gain VQ: last pitch gain (drives pitch sharpening)
    double gamma1_;
    float  phi_[HD_MAX_SUB * HD_MAX_SUB];
};

// Decoder-side excitation modification. Unlike LP modification this alters
// the decoded parameters themselves, so the decoder's adaptive-codebook memory
// drifts away from the encoder's (intended: it is an effect, not a channel).
struct ExcMod {
    float pitchRatio = 1.0f;  // F0 multiplier (pitch lag divided by it)
    float gpScale    = 1.0f;  // pitch-gain multiplier: 0 = noise only ("whisper"), >1 buzzier
    float gcScale    = 1.0f;  // algebraic-codebook gain multiplier
    bool active() const { return pitchRatio != 1.0f || gpScale != 1.0f || gcScale != 1.0f; }
};

// What the decoder did with one frame, for visualization.
struct FrameTrace {
    int    order;
    int    frameLen;
    bool   modActive;
    double lsfRef[HD_MAX_ORDER], lsfMod[HD_MAX_ORDER];         // end-of-frame LSFs
    double aRef[HD_MAX_ORDER + 1], aMod[HD_MAX_ORDER + 1];     // last-subframe filters
    float  excAdaptive[HD_MAX_FRAME];  // gp * v  (pitch / long-term prediction)
    float  excFixed[HD_MAX_FRAME];     // gc * c  (algebraic pulses, sharpened)
    int    pitch[HD_NSUB];
    float  gp[HD_NSUB];
    float  gcDb[HD_NSUB];              // innovation level, dB
    float  compGain;                   // loudness compensation (last subframe)
    int    nPulses[HD_NSUB];           // algebraic pulses per subframe
    int16_t pulses[HD_NSUB][HD_MAX_TRACKS * HD_MAX_PPT]; // signed (position + 1)
};

class Decoder {
public:
    void init(const Config &c);
    void reconfigure(const Config &c);
    // mod may be null (no decoder-side modification). trace may be null.
    void decode(const int16_t *idx, float *out, const LpMod *mod, FrameTrace *trace = nullptr,
                const ExcMod *exc = nullptr);

private:
    Config c_;
    LpModifier modifier_;
    float  exc_[HD_MAX_PITCH + HD_MAX_FRAME + 1];
    float  synMem_[HD_MAX_ORDER], synRefMem_[HD_MAX_ORDER];
    float  eRef_, eMod_;
    float  dRef_, dMod_;   // de-emphasis states used to measure loudness
    float  deemph_;
    double lsfQPrev_[HD_MAX_ORDER], lsfMPrev_[HD_MAX_ORDER];
    int    prevT_;
    float  gcHist_[4];
    float  prevGpQ_;
    float  prevGain_;
};

} // namespace hdacelp

#endif
