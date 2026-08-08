#pragma once
#include "packet.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socklen_t = int;
  using ssize_t   = SSIZE_T;
  #define HERO_INVALID INVALID_SOCKET
  #define HERO_ERROR   SOCKET_ERROR
  using hero_sock_t = SOCKET;
#else
  #include <arpa/inet.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #define HERO_INVALID (-1)
  #define HERO_ERROR   (-1)
  using hero_sock_t = int;
#endif

namespace hero {

static constexpr size_t   MTU               = 1400;
static constexpr size_t   HEADER_SIZE       = sizeof(PacketHeader);
static constexpr size_t   MAX_PAYLOAD       = MTU - HEADER_SIZE;
static constexpr size_t   FRAG_HEADER_SIZE  = 6; // frag_id:2 + index:2 + total:2
static constexpr size_t   FRAG_PAYLOAD      = MAX_PAYLOAD - FRAG_HEADER_SIZE;
static constexpr uint32_t RETRY_MS          = 100;
static constexpr uint32_t MAX_RETRIES       = 3;
static constexpr size_t   MAX_CHANNELS      = 256;
static constexpr int      MAX_RECV_PER_POLL = 64;
static constexpr size_t   PENDING_SIZE      = 256; // power of 2
static constexpr size_t   RECV_QUEUE_SIZE   = 512; // power of 2

static_assert((PENDING_SIZE    & (PENDING_SIZE    - 1)) == 0);
static_assert((RECV_QUEUE_SIZE & (RECV_QUEUE_SIZE - 1)) == 0);

uint64_t now_ms();

// ---------------------------------------------------------------------------

struct ChannelConfig {
    bool reliable = false;
    bool ordered  = false;
};

// ---------------------------------------------------------------------------

struct Addr {
    sockaddr_in sa{};
    bool operator==(const Addr& o) const {
        return sa.sin_addr.s_addr == o.sa.sin_addr.s_addr &&
               sa.sin_port        == o.sa.sin_port;
    }
};

struct AddrHash {
    size_t operator()(const Addr& a) const {
        size_t h = (size_t)a.sa.sin_addr.s_addr;
        h ^= (size_t)a.sa.sin_port << 16;
        return h;
    }
};

// ---------------------------------------------------------------------------
// Fixed-size pending slot — no heap alloc per pending packet

struct PendingPacket {
    uint8_t  raw[MTU];
    size_t   raw_len  = 0;
    uint16_t seq      = 0;
    uint64_t sent_at  = 0;
    uint8_t  retries  = 0;
    bool     occupied = false;
};

// ---------------------------------------------------------------------------

struct FragState {
    uint16_t                      frag_id   = 0;
    uint16_t                      total     = 0;
    uint8_t                       channel   = 0;
    uint64_t                      last_seen = 0;
    std::vector<std::vector<uint8_t>> parts;

    bool complete() const {
        if (parts.empty()) return false;
        for (auto& p : parts) if (p.empty()) return false;
        return (uint16_t)parts.size() == total;
    }
};

// ---------------------------------------------------------------------------
// Lockless SPSC ring — network thread writes, user thread reads

struct RecvEntry {
    Packet packet;
    Addr   from;
};

class RecvQueue {
public:
    bool push(Packet&& pkt, const Addr& from) {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t next = (head + 1) & (RECV_QUEUE_SIZE - 1);
        if (next == m_tail.load(std::memory_order_acquire))
            return false;
        m_buf[head] = { std::move(pkt), from };
        m_head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(Packet& out_pkt, Addr& out_from) {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire))
            return false;
        out_pkt  = std::move(m_buf[tail].packet);
        out_from = m_buf[tail].from;
        m_tail.store((tail + 1) & (RECV_QUEUE_SIZE - 1),
                     std::memory_order_release);
        return true;
    }

private:
    std::array<RecvEntry, RECV_QUEUE_SIZE> m_buf{};
    std::atomic<size_t>                    m_head{0};
    std::atomic<size_t>                    m_tail{0};
};

// ---------------------------------------------------------------------------
// Connection events

enum class EventType : uint8_t {
    Connected,
    Disconnected,
};

struct ConnEvent {
    EventType type;
    Addr      peer;
};

class EventQueue {
public:
    bool push(ConnEvent e) {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t next = (head + 1) & (EQ_SIZE - 1);
        if (next == m_tail.load(std::memory_order_acquire))
            return false;
        m_buf[head] = e;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(ConnEvent& out) {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire))
            return false;
        out = m_buf[tail];
        m_tail.store((tail + 1) & (EQ_SIZE - 1),
                     std::memory_order_release);
        return true;
    }

private:
    static constexpr size_t EQ_SIZE = 64;
    static_assert((EQ_SIZE & (EQ_SIZE - 1)) == 0);
    std::array<ConnEvent, EQ_SIZE> m_buf{};
    std::atomic<size_t>            m_head{0};
    std::atomic<size_t>            m_tail{0};
};

// ---------------------------------------------------------------------------

struct PeerState {
    uint16_t seq_out  = 0;
    uint16_t seq_in   = 0;
    uint32_t ack_bits = 0;
    size_t   occupied_count = 0;
    size_t   overrun_count  = 0;   // NEW: pending-ring slots clobbered before being acked

    std::array<PendingPacket, PENDING_SIZE> pending{};
    std::unordered_map<uint16_t, FragState> frags;
    std::array<uint16_t,           MAX_CHANNELS> next_ordered{};
    std::array<std::vector<Packet>, MAX_CHANNELS> hold_queue{};
};

// ---------------------------------------------------------------------------

class Socket {
public:
    Socket();
    ~Socket();

    Socket(const Socket&)            = delete;
    Socket& operator=(const Socket&) = delete;

    void setChannel(uint8_t ch, ChannelConfig cfg);

    void bind(uint16_t port);
    void connect(const std::string& host, uint16_t port);
    void disconnect();

    void send(const Packet& pkt);              // client — default peer
    void sendTo(const Packet& pkt, const Addr& addr); // server

    // call from network thread
    void poll();

    // call from any thread — drain recv queue
    bool dequeue(Packet& out_pkt, Addr& out_from);
size_t overrunCount(const Addr& addr);
    // drain connection events (connect/disconnect notifications)
    bool dequeue_event(ConnEvent& out);

private:
    hero_sock_t m_fd       = HERO_INVALID;
    bool        m_bound    = false;
    bool        m_has_peer = false;
    Addr        m_peer{};
    uint16_t    m_frag_id_counter = 0;

    std::array<ChannelConfig, MAX_CHANNELS>       m_channels{};
    std::unordered_map<Addr, PeerState, AddrHash> m_peers;
    RecvQueue                                     m_recv_queue;

    uint8_t    m_recv_buf[65536]{};
    uint8_t    m_send_buf[MTU]{};      // NEW: reused scratch buffer for sends
    PeerState* m_peer_ps = nullptr;    // NEW: cached client-side peer state
    EventQueue m_event_queue;

    PeerState& peer_state(const Addr& a);
    void       set_nonblocking();

    void send_to(const Packet& pkt, const Addr& addr);
        void send_to_ps(const Packet& pkt, const Addr& addr, PeerState& ps);
        void raw_send(Flag flag, const Packet& pkt, const Addr& addr, PeerState& ps);
    void send_fragmented(const Packet& pkt, const Addr& addr,
                         PeerState& ps, const ChannelConfig& cfg);
    void enqueue_pending(const uint8_t* wire, size_t len,
                         uint16_t seq, PeerState& ps);

    void process_acks(PeerState& ps, uint16_t ack_num, uint32_t ack_bits);
    void update_ack_state(PeerState& ps, uint16_t seq);
    void retransmit_pending();

    std::optional<Packet> handle_frag(Packet& pkt, PeerState& ps);
    void                  expire_frags();
    void                  deliver_ordered(Packet& pkt, const Addr& from, PeerState& ps);

    void handle_conn(const Addr& from, PeerState& ps);
    void handle_stop(const Addr& from);
    void handle_ping(const Addr& from, PeerState& ps);
};

} // namespace hero
