#include "net_link.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#define ACENET_POSIX 1
#endif

namespace acenet {

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Wire format (big endian):
//   0  'A' 'C' 'E' '1'
//   4  u8 version (1), u8 type, u8 mode, u8 flags (bit0 fixed math, bit1 gain VQ)
//   8  u32 stream
//  12  u32 seq
//  16  4 x f32 encoder constants (pre-emphasis, LSF prediction, gain offset, sharpening)
//  32  u16 nbits, u16 reserved
//  36  payload: the frame bits, MSB first, ceil(nbits / 8) bytes

static const int HEADER = 36;

static void put32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void putf(uint8_t* p, float f) { uint32_t v; std::memcpy(&v, &f, 4); put32(p, v); }
static float getf(const uint8_t* p) { const uint32_t v = get32(p); float f; std::memcpy(&f, &v, 4); return f; }

static hdacelp::Config frameConfig(const NetFrame& f) {
    const bool tp = f.type >= 4;
    const int fs = f.type == 1 ? 16000 : f.type == 2 ? 32000 : f.type == 3 ? 48000 : f.type == 4 ? 8000 : 16000;
    hdacelp::Config c = tp ? hdacelp::Config::makeTetraPlus(fs, f.mode, f.math) : hdacelp::Config::make(fs, f.mode, f.math);
    c.gainVq = f.gainVq != 0;
    return c;
}

int serialize(const NetFrame& f, uint8_t* buf, int cap) {
    const int nbytes = (f.nbits + 7) / 8;
    if (HEADER + nbytes > cap) return 0;
    buf[0] = 'A'; buf[1] = 'C'; buf[2] = 'E'; buf[3] = '1';
    buf[4] = 1;
    buf[5] = f.type;
    buf[6] = f.mode;
    buf[7] = (uint8_t)((f.math ? 1 : 0) | (f.gainVq ? 2 : 0));
    put32(buf + 8, f.stream);
    put32(buf + 12, f.seq);
    putf(buf + 16, f.preEmph);
    putf(buf + 20, f.lsfPred);
    putf(buf + 24, f.gcOffsetDb);
    putf(buf + 28, f.sharpenMax);
    buf[32] = (uint8_t)(f.nbits >> 8);
    buf[33] = (uint8_t)f.nbits;
    buf[34] = buf[35] = 0;
    for (int i = 0; i < nbytes; i++) {
        const uint16_t w = (uint16_t)f.words[i >> 1];
        buf[HEADER + i] = (i & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    return HEADER + nbytes;
}

static bool finiteIn(float v, float lo, float hi) { return v == v && v >= lo && v <= hi; }

bool parse(const uint8_t* buf, int len, NetFrame& f) {
    if (len < HEADER || std::memcmp(buf, "ACE1", 4) != 0 || buf[4] != 1) return false;
    f.type = buf[5];
    f.mode = buf[6];
    f.math = buf[7] & 1;
    f.gainVq = (buf[7] >> 1) & 1;
    if (f.type < 1 || f.type > 5) return false;
    if (f.mode >= (f.type >= 4 ? hdacelp::TP_NUM_LEVELS : hdacelp::HD_NUM_MODES)) return false;
    f.stream = get32(buf + 8);
    f.seq = get32(buf + 12);
    f.preEmph = getf(buf + 16);
    f.lsfPred = getf(buf + 20);
    f.gcOffsetDb = getf(buf + 24);
    f.sharpenMax = getf(buf + 28);
    if (!finiteIn(f.preEmph, 0.0f, 0.95f) || !finiteIn(f.lsfPred, 0.0f, 0.95f) ||
        !finiteIn(f.gcOffsetDb, -24.0f, 24.0f) || !finiteIn(f.sharpenMax, 0.0f, 1.0f)) return false;
    f.nbits = (uint16_t)(buf[32] << 8 | buf[33]);
    // the bit count must match what the declared codec produces
    if (f.nbits != frameConfig(f).numBits()) return false;
    const int nbytes = (f.nbits + 7) / 8;
    if (len != HEADER + nbytes) return false;
    std::memset(f.words, 0, sizeof(f.words));
    for (int i = 0; i < nbytes; i++) {
        const uint16_t b = buf[HEADER + i];
        f.words[i >> 1] = (int16_t)((uint16_t)f.words[i >> 1] | ((i & 1) ? b : (uint16_t)(b << 8)));
    }
    return true;
}

// ---------------------------------------------------------------------------

NetLink::NetLink() {
    rng_ = (uint32_t)(now_seconds() * 1e6) ^ (uint32_t)(uintptr_t)this;
    myStream_ = rng_ ? rng_ : 1u;
    // ACE_NET_TTL=0 keeps all traffic on this host (testing); it can only
    // narrow the scope, never widen it beyond the LAN.
    if (const char* e = getenv("ACE_NET_TTL")) {
        const int t = atoi(e);
        if (t >= 0 && t < ttl_) ttl_ = t;
    }
}

NetLink::~NetLink() { stop(); }

void NetLink::configure(bool sending, bool receiving, int channel) {
    if (channel < 1) channel = 1;
    if (channel > NET_CHANNELS) channel = NET_CHANNELS;
    if (run_.load() && sending == sending_ && receiving == receiving_ && channel == channel_) return;
    stop();
    if (!sending && !receiving) return;
    sending_ = sending;
    receiving_ = receiving;
    channel_ = channel;
    tx_.clear();
    rx_.clear();
    nHeld_ = 0;
    lockedStream.store(0);
    run_.store(true);
    thread_ = std::thread(&NetLink::threadMain, this);
}

void NetLink::stop() {
    run_.store(false);
    if (thread_.joinable()) thread_.join();
    ok_.store(false);
}

#if ACENET_POSIX

bool NetLink::openSockets() {
    const int port = NET_BASE_PORT + channel_;
    in_addr group;
    inet_pton(AF_INET, NET_GROUP, &group);
    if (sending_) {
        txSock_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (txSock_ < 0) return false;
        const unsigned char ttl = (unsigned char)ttl_, loop = 1;
        setsockopt(txSock_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        setsockopt(txSock_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    }
    if (receiving_) {
        rxSock_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (rxSock_ < 0) return false;
        const int one = 1;
        setsockopt(rxSock_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
        setsockopt(rxSock_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        addr.sin_addr = group; // bind to the group: only this channel's traffic
        if (bind(rxSock_, (sockaddr*)&addr, sizeof(addr)) < 0) return false;
        ip_mreq mreq = {};
        mreq.imr_multiaddr = group;
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(rxSock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) return false;
    }
    return true;
}

void NetLink::closeSockets() {
    if (txSock_ >= 0) close(txSock_);
    if (rxSock_ >= 0) close(rxSock_);
    txSock_ = rxSock_ = -1;
}

void NetLink::threadMain() {
    ok_.store(openSockets());
    const int port = NET_BASE_PORT + channel_;
    sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, NET_GROUP, &dst.sin_addr);
    uint8_t buf[2048];
    NetFrame f;

    while (run_.load(std::memory_order_acquire)) {
        // --- transmit everything the codec thread queued
        while (txSock_ >= 0 && tx_.pop(f)) {
            const int n = serialize(f, buf, sizeof(buf));
            if (n > 0 && sendto(txSock_, buf, n, 0, (sockaddr*)&dst, sizeof(dst)) == n)
                txPackets.fetch_add(1, std::memory_order_relaxed);
        }

        // --- receive (short poll keeps transmit latency low)
        if (rxSock_ >= 0) {
            pollfd pfd = { rxSock_, POLLIN, 0 };
            if (poll(&pfd, 1, 2) > 0 && (pfd.revents & POLLIN)) {
                for (;;) {
                    const ssize_t n = recv(rxSock_, buf, sizeof(buf), MSG_DONTWAIT);
                    if (n <= 0) break;
                    if (!parse(buf, (int)n, f)) { rxRejected.fetch_add(1); continue; }
                    const double t = now_seconds();
                    // lock onto one stream; take over if it went silent for 1 s
                    uint32_t locked = lockedStream.load();
                    if (locked == 0 || t - lastRx_ > 1.0) {
                        locked = f.stream;
                        lockedStream.store(locked);
                    }
                    if (f.stream != locked) continue;
                    lastRx_ = t;
                    rxPackets.fetch_add(1, std::memory_order_relaxed);
                    // impairment: random loss, uniform jitter (reorders)
                    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                    if ((rng_ >> 8) * (1.0f / 16777216.0f) < loss_.load()) { rxImpairDrops.fetch_add(1); continue; }
                    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                    f.arrival = t + (rng_ >> 8) * (1.0 / 16777216.0) * jitterMs_.load() / 1000.0;
                    if (nHeld_ < 64) held_[nHeld_++] = f;
                    else rx_.push(f);
                }
            }
            // release held frames whose time has come
            const double t = now_seconds();
            for (int i = 0; i < nHeld_;) {
                if (held_[i].arrival <= t) {
                    rx_.push(held_[i]);
                    held_[i] = held_[--nHeld_];
                } else {
                    ++i;
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    closeSockets();
}

#else // no networking on this platform yet

bool NetLink::openSockets() { return false; }
void NetLink::closeSockets() {}
void NetLink::threadMain() {
    ok_.store(false);
    while (run_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

#endif

} // namespace acenet
