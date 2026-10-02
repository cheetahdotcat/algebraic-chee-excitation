/*
 * lpc_viz.hpp — lock-free hand-off of the latest decoded frame's linear
 * prediction data (synthesis filter envelope, LSFs, excitation) from the codec
 * thread to the UI, for the current-frame panels. The LpcViz holder is owned by
 * the plugin instance; the UI reaches it through DPF direct access.
 *
 * A sequence lock (odd generation = write in progress) lets the UI skip torn
 * frames instead of drawing half an envelope.
 */
#ifndef LPC_VIZ_HPP_INCLUDED
#define LPC_VIZ_HPP_INCLUDED

#include <atomic>
#include <cmath>
#include <cstring>

#define LPC_VIZ_BINS 184        // envelope points (one per ~2 px of plot width)
#define LPC_VIZ_MAX_ORDER 32
#define LPC_VIZ_MAX_FRAME 960
#define LPC_VIZ_NSUB 4
#define LPC_VIZ_FREQ_KNEE 500.0 // log-like axis: x = log(1+f/knee)/log(1+nyq/knee)

struct LpcVizFrame {
    int   fs;                  // codec sample rate (Nyquist = fs/2)
    int   order;
    int   frameLen;
    bool  estimated;           // TETRA: envelope/residual re-analysed from the output
    bool  modActive;           // HD: decoder-side LP modification active
    float envRef[LPC_VIZ_BINS];   // dB, decoded (transmitted) envelope
    float envMod[LPC_VIZ_BINS];   // dB, envelope actually used for synthesis
    float lsfRef[LPC_VIZ_MAX_ORDER], lsfMod[LPC_VIZ_MAX_ORDER]; // Hz
    float excAdaptive[LPC_VIZ_MAX_FRAME]; // pitch contribution (or residual)
    float excFixed[LPC_VIZ_MAX_FRAME];    // algebraic codebook contribution
    int   pitch[LPC_VIZ_NSUB];            // samples, 0 = unknown
    float gp[LPC_VIZ_NSUB];
    float gcDb[LPC_VIZ_NSUB];
};

// Owned by the plugin instance (the UI reaches it through DPF direct access).
struct LpcViz {
    std::atomic<unsigned> seq{0};
    LpcVizFrame frame;
};

// Codec thread (single writer).
inline void lpc_viz_write(LpcViz& v, const LpcVizFrame& f)
{
    v.seq.fetch_add(1u, std::memory_order_acq_rel); // odd: writing
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(&v.frame, &f, sizeof(f));
    v.seq.fetch_add(1u, std::memory_order_release); // even: done
}

// UI thread. Returns false (leaving `out` untouched) when no consistent frame
// could be read; on success returns true and sets `gen` to the frame's
// generation so callers can skip unchanged frames.
inline bool lpc_viz_read(const LpcViz& v, LpcVizFrame& out, unsigned& gen)
{
    for (int tries = 0; tries < 4; ++tries) {
        const unsigned s0 = v.seq.load(std::memory_order_acquire);
        if (s0 & 1u) continue;
        std::memcpy(&out, &v.frame, sizeof(out));
        std::atomic_thread_fence(std::memory_order_acquire);
        if (v.seq.load(std::memory_order_relaxed) == s0) {
            gen = s0;
            return s0 != 0;
        }
    }
    return false;
}

// Plot x position (0..1) of frequency f for Nyquist nyq, and its inverse.
inline double lpc_viz_x(double f, double nyq)
{
    return std::log(1.0 + f / LPC_VIZ_FREQ_KNEE) / std::log(1.0 + nyq / LPC_VIZ_FREQ_KNEE);
}
inline double lpc_viz_freq(double x, double nyq)
{
    return LPC_VIZ_FREQ_KNEE * (std::pow(1.0 + nyq / LPC_VIZ_FREQ_KNEE, x) - 1.0);
}

#endif // LPC_VIZ_HPP_INCLUDED
