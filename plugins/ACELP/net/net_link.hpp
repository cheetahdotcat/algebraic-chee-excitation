/*
 * net_link.hpp — LAN link for HD ACELP / TETRA+ frames.
 *
 * Senders transmit one UDP datagram per codec frame to a multicast group
 * ("channel" 1..16 -> 239.255.76.67, port NET_BASE_PORT + channel) with
 * TTL 1, so packets never leave the local network. Receivers on the same
 * channel join the group and lock onto the first stream they hear.
 *
 * Threading: the codec thread calls send() / receive(); a private network
 * thread owns the sockets. Frames cross over through single-producer /
 * single-consumer rings, so neither side ever blocks on the other.
 * Receive-side impairment (random loss, jitter) is applied on the network
 * thread before frames reach the codec thread.
 */
#ifndef NET_LINK_HPP_INCLUDED
#define NET_LINK_HPP_INCLUDED

#include <atomic>
#include <cstdint>
#include <thread>

#include "../hdacelp/hd_acelp.hpp"

namespace acenet {

static const int  NET_BASE_PORT = 47100;
static const char NET_GROUP[]   = "239.255.76.67";
static const int  NET_CHANNELS  = 16;

// One codec frame as it travels over the network.
struct NetFrame {
    uint32_t stream;            // sender id (random per sender)
    uint32_t seq;               // frame counter
    uint8_t  type;              // codec type (1..5, see paramCodecType)
    uint8_t  mode;              // HD mode / TETRA+ level
    uint8_t  math;              // hdacelp::Math
    uint8_t  gainVq;
    float    preEmph, lsfPred, gcOffsetDb, sharpenMax; // encoder constants
    uint16_t nbits;
    int16_t  words[hdacelp::HD_MAX_WORDS];
    double   arrival;           // receiver: release time (seconds, steady clock)
};

// Wire format: see net_link.cpp (serialize / parse). Returns bytes / false.
int  serialize(const NetFrame& f, uint8_t* buf, int cap);
bool parse(const uint8_t* buf, int len, NetFrame& out);

// Lock-free single-producer / single-consumer ring.
template <int N>
class FrameRing {
public:
    bool push(const NetFrame& f) {
        const uint32_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= (uint32_t)N) return false;
        slots_[h % N] = f;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(NetFrame& f) {
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;
        f = slots_[t % N];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
    void clear() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

private:
    NetFrame slots_[N];
    std::atomic<uint32_t> head_{0}, tail_{0};
};

class NetLink {
public:
    NetLink();
    ~NetLink();

    // (Re)start with the given role on a channel (1..16). Safe to call
    // repeatedly with the same arguments. Called from the codec thread.
    void configure(bool sending, bool receiving, int channel);
    void stop();

    // Receive-side impairment: random loss (0..1) and uniform jitter (ms).
    void setImpairment(float loss, float jitterMs) {
        loss_.store(loss, std::memory_order_relaxed);
        jitterMs_.store(jitterMs, std::memory_order_relaxed);
    }
    // Multicast TTL (1 = LAN; 0 = this host only, used for testing).
    void setTtl(int ttl) { ttl_ = ttl; }

    bool send(const NetFrame& f) { return sending_ && tx_.push(f); }
    bool receive(NetFrame& f) { return rx_.pop(f); }

    uint32_t streamId() const { return myStream_; }
    bool     ok() const { return ok_.load(std::memory_order_relaxed); }

    // statistics (network thread writes, anyone reads)
    std::atomic<uint32_t> txPackets{0}, rxPackets{0}, rxImpairDrops{0}, rxRejected{0};
    std::atomic<uint32_t> lockedStream{0};

private:
    void threadMain();
    bool openSockets();
    void closeSockets();

    std::thread thread_;
    std::atomic<bool> run_{false};
    std::atomic<bool> ok_{false};
    bool sending_ = false, receiving_ = false;
    int  channel_ = 0;
    int  ttl_ = 1;
    int  txSock_ = -1, rxSock_ = -1;
    uint32_t myStream_;
    double lastRx_ = 0.0;
    std::atomic<float> loss_{0.0f}, jitterMs_{0.0f};
    FrameRing<64> tx_, rx_;
    // frames delayed by the jitter simulation, released in time order
    NetFrame held_[64];
    int nHeld_ = 0;
    uint32_t rng_;
};

double now_seconds();

} // namespace acenet

#endif
