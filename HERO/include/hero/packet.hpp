#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <arpa/inet.h>
#endif

namespace hero {

enum class Flag : uint8_t {
    CONN = 0,
    GIVE = 1,
    PING = 2,
    PONG = 3,
    FRAG = 4,
    STOP = 5,
};

#ifdef _MSC_VER
  #pragma pack(push, 1)
#endif
struct PacketHeader {
    uint8_t  flag;
    uint8_t  channel;
    uint16_t seq;
    uint16_t ack_num;
    uint32_t ack_bits;
    uint16_t payload_len;
}
#ifndef _MSC_VER
  __attribute__((packed))
#endif
;
#ifdef _MSC_VER
  #pragma pack(pop)
#endif

static_assert(sizeof(PacketHeader) == 12);

class Packet {
public:
    Packet() = default;

    explicit Packet(uint8_t channel)
        : m_channel(channel) { m_buf.reserve(1400); }

    static Packet from_bytes(const uint8_t* data, size_t len) {
        if (len < sizeof(PacketHeader))
            throw std::runtime_error("Packet too small");
        Packet p;
        std::memcpy(&p.m_header, data, sizeof(PacketHeader));
        p.m_header.seq         = ntohs(p.m_header.seq);
        p.m_header.ack_num     = ntohs(p.m_header.ack_num);
        p.m_header.ack_bits    = ntohl(p.m_header.ack_bits);
        p.m_header.payload_len = ntohs(p.m_header.payload_len);
        size_t plen = p.m_header.payload_len;
        if (len < sizeof(PacketHeader) + plen)
            throw std::runtime_error("Truncated payload");
        p.m_buf.assign(data + sizeof(PacketHeader),
                       data + sizeof(PacketHeader) + plen);
        p.m_channel = p.m_header.channel;
        return p;
    }

    size_t write_wire(uint8_t* out, Flag flag, uint16_t seq,
                       uint16_t ack_num, uint32_t ack_bits) const {
        PacketHeader h;
        h.flag        = static_cast<uint8_t>(flag);
        h.channel     = m_channel;
        h.seq         = htons(seq);
        h.ack_num     = htons(ack_num);
        h.ack_bits    = htonl(ack_bits);
        h.payload_len = htons((uint16_t)m_buf.size());

        std::memcpy(out, &h, sizeof(h));
        std::memcpy(out + sizeof(h), m_buf.data(), m_buf.size());
        return sizeof(h) + m_buf.size();
    }

    // --- write ---

    template<typename T>
    Packet& write(T val) {
        static_assert(std::is_trivially_copyable_v<T>);
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&val);
        m_buf.insert(m_buf.end(), ptr, ptr + sizeof(T));
        return *this;
    }

    // raw bytes, no length prefix — used for frag payloads
    Packet& write_raw(const uint8_t* data, size_t len) {
        m_buf.insert(m_buf.end(), data, data + len);
        return *this;
    }

    Packet& writeBlob(const uint8_t* data, uint32_t len) {
        write<uint32_t>(len);
        m_buf.insert(m_buf.end(), data, data + len);
        return *this;
    }

    Packet& writeBlob(std::span<const uint8_t> data) {
        return writeBlob(data.data(), static_cast<uint32_t>(data.size()));
    }

    // --- read ---

    template<typename T>
    T read() {
        static_assert(std::is_trivially_copyable_v<T>);
        if (m_read_pos + sizeof(T) > m_buf.size())
            throw std::runtime_error("Packet read overrun");
        T val;
        std::memcpy(&val, m_buf.data() + m_read_pos, sizeof(T));
        m_read_pos += sizeof(T);
        return val;
    }

    std::span<const uint8_t> readBlob() {
        uint32_t len = read<uint32_t>();
        if (m_read_pos + len > m_buf.size())
            throw std::runtime_error("Blob read overrun");
        std::span<const uint8_t> s(m_buf.data() + m_read_pos, len);
        m_read_pos += len;
        return s;
    }

    // raw read — no length prefix, reads remaining bytes
    std::span<const uint8_t> read_remaining() {
        std::span<const uint8_t> s(m_buf.data() + m_read_pos,
                                   m_buf.size() - m_read_pos);
        m_read_pos = m_buf.size();
        return s;
    }

    void reserve(size_t n) { m_buf.reserve(n); }
    bool done()  const { return m_read_pos >= m_buf.size(); }

    // --- accessors ---

    Flag     flag()     const { return static_cast<Flag>(m_header.flag); }
    uint8_t  channel()  const { return m_channel; }
    uint16_t seq()      const { return m_header.seq; }
    uint16_t ack_num()  const { return m_header.ack_num; }
    uint32_t ack_bits() const { return m_header.ack_bits; }

    const std::vector<uint8_t>& payload() const { return m_buf; }

    // for zero-copy frag reassembly — socket writes directly into buffer
    uint8_t* payload_ptr() {
        return m_buf.data();
    }
    void set_payload_size(size_t n) { m_buf.resize(n); }

private:
    PacketHeader         m_header{};
    uint8_t              m_channel  = 0;
    std::vector<uint8_t> m_buf;
    size_t               m_read_pos = 0;
};

} // namespace hero
