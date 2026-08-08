#include "hero/socket.hpp"
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <time.h>
#endif

namespace hero {

// ---------------------------------------------------------------------------
// helpers

uint64_t now_ms() {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

// ---------------------------------------------------------------------------
// Socket

Socket::Socket() {
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
    m_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_fd == HERO_INVALID)
        throw std::runtime_error("socket() failed");
    set_nonblocking();
}

Socket::~Socket() {
#ifdef _WIN32
    closesocket(m_fd);
    WSACleanup();
#else
    ::close(m_fd);
#endif
}

void Socket::set_nonblocking() {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(m_fd, FIONBIO, &mode);
#else
    int flags = fcntl(m_fd, F_GETFL, 0);
    fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

PeerState& Socket::peer_state(const Addr& a) {
    return m_peers[a];
}

void Socket::setChannel(uint8_t ch, ChannelConfig cfg) {
    m_channels[ch] = cfg;
}

// ---------------------------------------------------------------------------
// bind / connect / disconnect

void Socket::bind(uint16_t port) {
    sockaddr_in sa{};
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port        = htons(port);
    if (::bind(m_fd, (sockaddr*)&sa, sizeof(sa)) == HERO_ERROR)
        throw std::runtime_error("bind() failed");
    m_bound = true;
}

void Socket::connect(const std::string& host, uint16_t port) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &sa.sin_addr) != 1)
        throw std::runtime_error("Invalid address: " + host);
    m_peer.sa  = sa;
    m_has_peer = true;
    m_peer_ps  = &peer_state(m_peer); // one lookup, cached for every send() after

    Packet conn(0);
    conn.reserve(0);
    raw_send(Flag::CONN, conn, m_peer, *m_peer_ps);
}

void Socket::disconnect() {
    if (!m_has_peer) return;
    Packet stop(0);
    raw_send(Flag::STOP, stop, m_peer, *m_peer_ps);
    m_peers.erase(m_peer);
    m_has_peer = false;
    m_peer_ps  = nullptr;
}

// ---------------------------------------------------------------------------
// send

void Socket::send(const Packet& pkt) {
    if (!m_has_peer) throw std::runtime_error("No peer");
    send_to(pkt, m_peer);
}

void Socket::send_to(const Packet& pkt, const Addr& addr) {
    send_to_ps(pkt, addr, peer_state(addr));
}

void Socket::sendTo(const Packet& pkt, const Addr& addr) {
    send_to(pkt, addr); // server path: still looks up per-addr, unavoidable with multiple peers
}

void Socket::send_to_ps(const Packet& pkt, const Addr& addr, PeerState& ps) {
    const auto& cfg     = m_channels[pkt.channel()];
    const auto& payload = pkt.payload();

    if (payload.size() <= MAX_PAYLOAD) {
        size_t len = pkt.write_wire(m_send_buf, Flag::GIVE, ps.seq_out,
                                     ps.seq_in, ps.ack_bits);
        ::sendto(m_fd, (const char*)m_send_buf, (int)len, 0,
                 (const sockaddr*)&addr.sa, sizeof(addr.sa));
        if (cfg.reliable)
            enqueue_pending(m_send_buf, len, ps.seq_out, ps);
        ps.seq_out++;
    } else {
        send_fragmented(pkt, addr, ps, cfg);
    }
}

void Socket::raw_send(Flag flag, const Packet& pkt,
                      const Addr& addr, PeerState& ps) {
    size_t len = pkt.write_wire(m_send_buf, flag, ps.seq_out, ps.seq_in, ps.ack_bits);
    ::sendto(m_fd, (const char*)m_send_buf, (int)len, 0,
             (const sockaddr*)&addr.sa, sizeof(addr.sa));
    ps.seq_out++;
}

void Socket::send_fragmented(const Packet& pkt, const Addr& addr,
                              PeerState& ps, const ChannelConfig& cfg) {
    const auto& payload  = pkt.payload();
    uint16_t    frag_id  = m_frag_id_counter++;
    size_t      total    = (payload.size() + FRAG_PAYLOAD - 1) / FRAG_PAYLOAD;

    if (total > 65535) throw std::runtime_error("Payload too large to fragment");

    for (size_t i = 0; i < total; i++) {
        size_t off   = i * FRAG_PAYLOAD;
        size_t chunk = std::min(FRAG_PAYLOAD, payload.size() - off);

        Packet frag(pkt.channel());
        frag.reserve(FRAG_HEADER_SIZE + 4 + chunk);
        frag.write<uint16_t>(frag_id);
        frag.write<uint16_t>((uint16_t)i);
        frag.write<uint16_t>((uint16_t)total);
        frag.writeBlob(payload.data() + off, (uint32_t)chunk);

        size_t len = frag.write_wire(m_send_buf, Flag::FRAG, ps.seq_out,
                                              ps.seq_in, ps.ack_bits);
                ::sendto(m_fd, (const char*)m_send_buf, (int)len, 0,
                         (const sockaddr*)&addr.sa, sizeof(addr.sa));
                if (cfg.reliable)
                    enqueue_pending(m_send_buf, len, ps.seq_out, ps);
        ps.seq_out++;
    }
}

void Socket::enqueue_pending(const uint8_t* wire, size_t len,
                              uint16_t seq, PeerState& ps) {
    size_t slot = seq & (PENDING_SIZE - 1);
    auto&  pp   = ps.pending[slot];
    if (pp.occupied) {
        // Slot still holds an unacked packet (pp.seq) that hasn't been
        // retransmitted-to-exhaustion or acked yet — we're about to
        // overwrite it before it was ever confirmed delivered.
        ps.overrun_count++;
    } else {
        ps.occupied_count++;
    }
    pp.occupied = true;
    pp.seq      = seq;
    pp.sent_at  = now_ms();
    pp.retries  = 0;
    pp.raw_len  = len;
    std::memcpy(pp.raw, wire, len);
}

size_t Socket::overrunCount(const Addr& addr) {
    auto it = m_peers.find(addr);
    return it == m_peers.end() ? 0 : it->second.overrun_count;
}

// ---------------------------------------------------------------------------
// poll / dequeue

#ifdef __linux__
#include <sys/socket.h>

void Socket::poll() {
    retransmit_pending();
    expire_frags();

    constexpr int BATCH = 32;
    static thread_local uint8_t     bufs[BATCH][65536];
    static thread_local iovec       iovs[BATCH];
    static thread_local mmsghdr     msgs[BATCH];
    static thread_local sockaddr_in froms[BATCH];

    for (int i = 0; i < BATCH; i++) {
        iovs[i].iov_base       = bufs[i];
        iovs[i].iov_len        = sizeof(bufs[i]);
        msgs[i].msg_hdr.msg_iov    = &iovs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name    = &froms[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(froms[i]);
    }

    // Keep pulling batches until the socket has nothing left (EAGAIN),
    // rather than stopping after one fixed-size batch — bursts get fully
    // drained in one poll() instead of needing dozens of calls.
    while (true) {
        int n = ::recvmmsg(m_fd, msgs, BATCH, MSG_DONTWAIT, nullptr);
        if (n <= 0) break;

        for (int i = 0; i < n; i++) {
            Addr from; from.sa = froms[i];
            PeerState& ps = peer_state(from);

            Packet pkt;
            try { pkt = Packet::from_bytes(bufs[i], (size_t)msgs[i].msg_len); }
            catch (...) { continue; }

            process_acks(ps, pkt.ack_num(), pkt.ack_bits());
            update_ack_state(ps, pkt.seq());

            switch (pkt.flag()) {
                case Flag::CONN: handle_conn(from, ps); break;
                case Flag::STOP: handle_stop(from);     break;
                case Flag::PING: handle_ping(from, ps); break;
                case Flag::PONG: break;
                case Flag::FRAG: {
                    auto maybe = handle_frag(pkt, ps);
                    if (maybe) m_recv_queue.push(std::move(*maybe), from);
                } break;
                case Flag::GIVE: {
                    auto& cfg = m_channels[pkt.channel()];
                    if (cfg.reliable && cfg.ordered)
                        deliver_ordered(pkt, from, ps);
                    else
                        m_recv_queue.push(std::move(pkt), from);
                } break;
                default: break;
            }
        }
        if (n < BATCH) break; // fewer than requested means the socket's drained
    }
}
#else
// non-Linux: keep your existing recvfrom loop, but drain-to-EAGAIN instead
// of a fixed MAX_RECV_PER_POLL cap
void Socket::poll() {
    retransmit_pending();
    expire_frags();

    sockaddr_in from_sa{};
    socklen_t   from_len = sizeof(from_sa);

    while (true) {
        ssize_t n = ::recvfrom(m_fd, (char*)m_recv_buf, sizeof(m_recv_buf),
                               0, (sockaddr*)&from_sa, &from_len);
        if (n <= 0) break; // EAGAIN/EWOULDBLOCK on non-blocking socket

        Addr from; from.sa = from_sa;
        PeerState& ps = peer_state(from);

        Packet pkt;
        try { pkt = Packet::from_bytes(m_recv_buf, (size_t)n); }
        catch (...) { continue; }

        process_acks(ps, pkt.ack_num(), pkt.ack_bits());
        update_ack_state(ps, pkt.seq());

        switch (pkt.flag()) {
            case Flag::CONN: handle_conn(from, ps); break;
            case Flag::STOP: handle_stop(from);     break;
            case Flag::PING: handle_ping(from, ps); break;
            case Flag::PONG: break;
            case Flag::FRAG: {
                auto maybe = handle_frag(pkt, ps);
                if (maybe) m_recv_queue.push(std::move(*maybe), from);
            } break;
            case Flag::GIVE: {
                auto& cfg = m_channels[pkt.channel()];
                if (cfg.reliable && cfg.ordered)
                    deliver_ordered(pkt, from, ps);
                else
                    m_recv_queue.push(std::move(pkt), from);
            } break;
            default: break;
        }
    }
}
#endif

bool Socket::dequeue(Packet& out_pkt, Addr& out_from) {
    return m_recv_queue.pop(out_pkt, out_from);
}

bool Socket::dequeue_event(ConnEvent& out) {
    return m_event_queue.pop(out);
}

// ---------------------------------------------------------------------------
// ACK

void Socket::process_acks(PeerState& ps, uint16_t ack_num, uint32_t ack_bits) {
    auto clear = [&](uint16_t seq) {
        size_t slot = seq & (PENDING_SIZE - 1);
        auto&  pp   = ps.pending[slot];
        if (pp.occupied && pp.seq == seq) {
            pp.occupied = false;
            ps.occupied_count--;
        }
    };
    clear(ack_num);
    while (ack_bits) {
        int i = __builtin_ctz(ack_bits);      // index of lowest set bit
        clear(ack_num - 1 - (uint16_t)i);
        ack_bits &= ack_bits - 1;             // clear that bit
    }
}

void Socket::update_ack_state(PeerState& ps, uint16_t seq) {
    int16_t diff = (int16_t)(seq - ps.seq_in);
    if (diff > 0) {
        ps.ack_bits = (ps.ack_bits << (uint32_t)diff) | (1u << (diff - 1));
        ps.seq_in   = seq;
    } else if (diff < 0 && diff >= -32) {
        ps.ack_bits |= (1u << (-diff - 1));
    }
}

void Socket::retransmit_pending() {
    uint64_t t = now_ms();
    for (auto& [addr, ps] : m_peers) {
        if (ps.occupied_count == 0) continue; // nothing pending for this peer, skip the ring scan
        size_t remaining = ps.occupied_count;
        for (auto& pp : ps.pending) {
            if (!pp.occupied) continue;
            if (t - pp.sent_at >= RETRY_MS) {
                if (pp.retries >= MAX_RETRIES) {
                    pp.occupied = false;
                    ps.occupied_count--;
                    m_event_queue.push({ EventType::Disconnected, addr });
                } else {
                    ::sendto(m_fd, (const char*)pp.raw, (int)pp.raw_len, 0,
                             (const sockaddr*)&addr.sa, sizeof(addr.sa));
                    pp.sent_at = t;
                    pp.retries++;
                }
            }
            if (--remaining == 0) break; // found every occupied slot, stop scanning early
        }
    }
}

// ---------------------------------------------------------------------------
// fragmentation

std::optional<Packet> Socket::handle_frag(Packet& pkt, PeerState& ps) {
    uint16_t frag_id    = pkt.read<uint16_t>();
    uint16_t frag_index = pkt.read<uint16_t>();
    uint16_t frag_total = pkt.read<uint16_t>();
    auto     blob       = pkt.readBlob();

    auto& fs = ps.frags[frag_id];
    if (fs.parts.empty()) {
        fs.frag_id   = frag_id;
        fs.total     = frag_total;
        fs.channel   = pkt.channel();
        fs.parts.resize(frag_total);    }
    fs.last_seen = now_ms();
    fs.parts[frag_index].assign(blob.begin(), blob.end());

    if (!fs.complete()) return std::nullopt;

    Packet out(fs.channel);
    for (auto& part : fs.parts)
        out.writeBlob(part.data(), (uint32_t)part.size());

    ps.frags.erase(frag_id);
    return out;
}

void Socket::expire_frags() {
    uint64_t t = now_ms();
    for (auto& [addr, ps] : m_peers) {
        for (auto it = ps.frags.begin(); it != ps.frags.end(); ) {
            if (t - it->second.last_seen > 5000)
                it = ps.frags.erase(it);
            else
                ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// ordered delivery

void Socket::deliver_ordered(Packet& pkt, const Addr& from, PeerState& ps) {
    uint8_t  ch       = pkt.channel();
    uint16_t seq      = pkt.seq();
    uint16_t expected = ps.next_ordered[ch];

    if (seq == expected) {
        m_recv_queue.push(std::move(pkt), from);
        ps.next_ordered[ch]++;

        auto& q = ps.hold_queue[ch];
        bool flushed = true;
        while (flushed) {
            flushed = false;
            for (auto it = q.begin(); it != q.end(); ) {
                if (it->seq() == ps.next_ordered[ch]) {
                    m_recv_queue.push(std::move(*it), from);
                    ps.next_ordered[ch]++;
                    it = q.erase(it);
                    flushed = true;
                } else { ++it; }
            }
        }
    } else if ((int16_t)(seq - expected) > 0) {
        ps.hold_queue[ch].push_back(std::move(pkt));
    }
    // duplicate: discard
}

// ---------------------------------------------------------------------------
// control packets

void Socket::handle_conn(const Addr& from, PeerState& ps) {
    Packet ack(0);
    raw_send(Flag::GIVE, ack, from, ps);
    m_event_queue.push({ EventType::Connected, from });
}

void Socket::handle_stop(const Addr& from) {
    m_event_queue.push({ EventType::Disconnected, from });
    m_peers.erase(from);
}

void Socket::handle_ping(const Addr& from, PeerState& ps) {
    Packet pong(0);
    raw_send(Flag::PONG, pong, from, ps);
}

} // namespace hero
