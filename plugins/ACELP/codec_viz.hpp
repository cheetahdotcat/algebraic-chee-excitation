/*
 * codec_viz.hpp — tiny lock-free hand-off of the latest encoded ACELP channel
 * frame from the DSP side to the plugin UI, for the on-screen data-stream
 * visualization.
 *
 * The accessor is a header-only inline function, so all translation units
 * linked into the SAME plugin binary (VST2/VST3/CLAP/standalone in DPF) share
 * one instance of the static-local buffer. For LV2, where DPF builds the UI and
 * DSP as separate shared objects, each gets its own instance and the visualizer
 * simply shows nothing rather than misbehaving.
 *
 * The DSP writes from the background/codec thread; the UI reads from the UI
 * thread. A torn read is harmless for a visualizer, but we still publish via an
 * atomic generation counter so the UI can tell when a new frame has arrived.
 */
#ifndef CODEC_VIZ_HPP_INCLUDED
#define CODEC_VIZ_HPP_INCLUDED

#include <atomic>
#include <cstring>

// One TETRA time-slot channel frame (TS7k2_size). Kept as a literal so this
// header stays free of codec/DSP includes.
#define CODEC_VIZ_WORDS 432

struct CodecFrameViz {
    std::atomic<unsigned> generation;
    short frame[CODEC_VIZ_WORDS];
};

inline CodecFrameViz& codec_viz()
{
    static CodecFrameViz v; // static storage => zero-initialized
    return v;
}

// Called from the DSP side once per encoded frame.
inline void codec_viz_write(const short* f)
{
    CodecFrameViz& v = codec_viz();
    std::memcpy(v.frame, f, sizeof(short) * CODEC_VIZ_WORDS);
    v.generation.fetch_add(1u, std::memory_order_release);
}

// Called from the UI; copies the latest frame into `out` and returns the
// generation counter (so callers can skip redrawing when nothing changed).
inline unsigned codec_viz_read(short* out)
{
    CodecFrameViz& v = codec_viz();
    const unsigned g = v.generation.load(std::memory_order_acquire);
    std::memcpy(out, v.frame, sizeof(short) * CODEC_VIZ_WORDS);
    return g;
}

#endif // CODEC_VIZ_HPP_INCLUDED
