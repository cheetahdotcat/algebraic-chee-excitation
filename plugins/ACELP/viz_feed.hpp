/*
 * viz_feed.hpp — per-frame codec data handed from the codec thread to the UI.
 *
 * Every decoded frame produces one CodecVizRecord (envelope, pitch and gains,
 * algebraic pulses, the bitstream with per-bit meaning and corruption flags,
 * and the values shown on the encode/decode chain). Records go into a small
 * ring owned by the plugin instance; the UI (DPF direct access) drains all
 * records it has not seen yet, so the scrolling displays never skip frames
 * even though the UI only repaints ~30 times a second.
 *
 * Single writer (codec thread), single reader (UI thread). Each slot carries
 * a version number (odd while being written) so torn reads are detected and
 * dropped instead of drawn.
 */
#ifndef VIZ_FEED_HPP_INCLUDED
#define VIZ_FEED_HPP_INCLUDED

#include <atomic>
#include <cstdint>
#include <cstring>

#define VIZ_ENV_BINS   96        // envelope rows on a fixed log axis
#define VIZ_ENV_FMIN   50.0
#define VIZ_ENV_FMAX   24000.0
#define VIZ_ENV_NONE   (-200.0f) // bin above the codec's Nyquist
#define VIZ_MAX_BITS   2048
#define VIZ_NSUB       4
#define VIZ_MAX_PULSES 64
#define VIZ_RING       128

// Meaning of a bitstream bit (low 3 bits of CodecVizRecord::bits[] >> 1).
enum VizBitCat {
    kVizBitLsf = 0,    // envelope (LSF / LSP indices)
    kVizBitPitch,      // pitch lag
    kVizBitPulse,      // algebraic pulse position
    kVizBitSign,       // pulse sign
    kVizBitShift,      // (TETRA codebook shift)
    kVizBitGain,       // pitch / codebook gain
    kVizBitFec,        // channel symbol: class 1 (CRC + convolutional code)
    kVizBitFec0,       // channel symbol: class 0 (lighter code)
    kVizBitCount
};
// bits[] encoding: bit0 = value, bits1-3 = VizBitCat, bit4 = corrupted
#define VIZ_BIT(value, cat, corrupted) (uint8_t)(((value) ? 1 : 0) | ((cat) << 1) | ((corrupted) ? 16 : 0))

struct CodecVizRecord {
    uint8_t kind;          // 0 = TETRA, 1 = HD ACELP
    uint8_t math;          // HD: 0 float, 1 fixed16
    uint8_t mode;          // HD bitrate mode
    uint8_t order;         // LPC order
    uint8_t estimated;     // TETRA: envelope/pitch/pulses estimated from output
    uint8_t lpEnc;         // LP modification active in the encoder
    uint8_t lpDec;         // LP modification active in the decoder
    uint8_t decMode;       // decoder-side bitrate mode (differs when split)
    uint8_t decMath;       // decoder-side math
    uint8_t excActive;     // decoder excitation modification active
    uint8_t paramCorr;     // a parameter-domain corruption is active
    uint8_t bitCorr;       // a bitstream-domain corruption is active
    uint16_t heldKeys;     // MIDI corruption keys held (bit k = key k)
    int     fs;            // codec rate
    int     frameMs;       // frame duration
    float   kbps;
    int     nbits;
    int     nflipped;      // bits that differ from the uncorrupted frame
    float   inDb, outDb;   // frame levels (dBFS)
    float   guardDb;       // output level guard gain (0 = inactive)
    uint8_t fec;           // T+: channel protection level (FecLevel), 255 = n/a
    uint8_t bfi;           // T+: bad frame (CRC failed) -> concealed
    uint16_t nbad;         // T+: consecutive bad frames
    int     srcBits;       // source bits per frame (nbits = channel symbols for T+ / TETRA)
    uint8_t target;        // corruption target (CorruptTarget)
    uint8_t gainVq;        // encoder uses the indexed gain codebook
    // LAN link
    uint8_t netMode;       // 0 off, 1 send, 2 receive, 3 loop
    uint8_t netOk;         // sockets up
    uint8_t netPlaying;    // receiver: jitter buffer started
    uint8_t netConceal;    // receiver: this frame was lost/late and concealed
    uint8_t netDepth;      // receiver: frames buffered
    uint8_t netChannel;
    float   netLossPct;    // receiver: lost + late frames, recent window (%)
    uint32_t netStream;    // receiver: locked stream / sender: own stream id
    uint32_t netTx, netRx; // packet counters
    int     nTargeted;     // bits flipped inside the targeted codec fields
    float   env[VIZ_ENV_BINS];             // synthesis envelope + level, dB
    float   f0[VIZ_NSUB];                  // Hz, 0 = unvoiced/unknown
    float   gp[VIZ_NSUB];
    float   gcDb[VIZ_NSUB];
    uint8_t npulses[VIZ_NSUB];
    int16_t pulse[VIZ_NSUB][VIZ_MAX_PULSES]; // signed, |v| = 1 + position * 1000 / subLen
    uint8_t bits[VIZ_MAX_BITS];
};

struct VizFeedSlot {
    std::atomic<uint32_t> ver;
    CodecVizRecord rec;
};

struct VizFeed {
    std::atomic<uint32_t> count{0};  // records written so far
    VizFeedSlot slot[VIZ_RING];

    VizFeed() {
        for (int i = 0; i < VIZ_RING; ++i) slot[i].ver.store(0, std::memory_order_relaxed);
    }

    // codec thread
    void push(const CodecVizRecord& r) {
        const uint32_t n = count.load(std::memory_order_relaxed);
        VizFeedSlot& s = slot[n % VIZ_RING];
        s.ver.store(2u * n + 1u, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(&s.rec, &r, sizeof(r));
        s.ver.store(2u * n + 2u, std::memory_order_release);
        count.store(n + 1u, std::memory_order_release);
    }

    // UI thread: copy record number n; false if overwritten or torn.
    bool get(uint32_t n, CodecVizRecord& out) const {
        const VizFeedSlot& s = slot[n % VIZ_RING];
        if (s.ver.load(std::memory_order_acquire) != 2u * n + 2u) return false;
        std::memcpy(&out, &s.rec, sizeof(out));
        std::atomic_thread_fence(std::memory_order_acquire);
        return s.ver.load(std::memory_order_relaxed) == 2u * n + 2u;
    }
};

#endif // VIZ_FEED_HPP_INCLUDED
