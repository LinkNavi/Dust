// HEROBench — standalone workspace
// To enable ENet comparison: install enet and add -DHERO_BENCH_ENET to CXXFLAGS
// and link with -lenet

#include <HERO.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <thread>
#include <vector>

#ifdef HERO_BENCH_ENET
  #include <enet/enet.h>
#endif

using Clock = std::chrono::steady_clock;
using us    = std::chrono::microseconds;
using ms    = std::chrono::milliseconds;

static int64_t elapsed_us(Clock::time_point t) {
    return std::chrono::duration_cast<us>(Clock::now() - t).count();
}
static void sleep_ms(int n) { std::this_thread::sleep_for(ms(n)); }

static constexpr uint8_t  CH_UNRELIABLE = 0;
static constexpr uint8_t  CH_RELIABLE   = 1;
static constexpr uint8_t  CH_CHUNK      = 2;
static constexpr uint16_t PORT          = 19000;

// ---------------------------------------------------------------------------
// result helpers

struct BenchResult {
    const char* label;
    double      pkt_per_sec;
    double      mb_per_sec;
    int64_t     avg_us;
    int64_t     min_us;
    int64_t     max_us;
};

static void print_throughput(const char* label, int64_t taken_us,
                             size_t count, size_t bytes) {
    double sec  = taken_us / 1e6;
    double pps  = count / sec;
    double mbps = (bytes / 1e6) / sec;
    std::printf("  %-32s %8.0f pkt/s  %6.2f MB/s  %.3f s\n",
                label, pps, mbps, sec);
}

static void print_latency(const char* label, int64_t avg,
                          int64_t mn, int64_t mx) {
    std::printf("  %-32s avg %4lld us  min %4lld us  max %4lld us\n",
                label, (long long)avg, (long long)mn, (long long)mx);
}

static void print_frag(const char* label, int64_t total_us, int64_t reassembly_us) {
    std::printf("  %-32s total %6lld us  reassembly %6lld us\n",
                label, (long long)total_us, (long long)reassembly_us);
}

// ---------------------------------------------------------------------------
// HERO benchmarks

static void hero_throughput(int n) {
    std::atomic<int>  received{0};
    std::atomic<bool> done{false};

    std::thread srv([&] {
        hero::Socket s;
        s.setChannel(CH_UNRELIABLE, { false, false });
        s.bind(PORT);
        while (!done) {
            s.poll();
            hero::Packet p; hero::Addr f;
            while (s.dequeue(p, f)) received++;
        }
    });

    sleep_ms(50);
    hero::Socket client;
    client.setChannel(CH_UNRELIABLE, { false, false });
    client.connect("127.0.0.1", PORT);
    sleep_ms(20);

    uint8_t payload[32]{};
    auto t0 = Clock::now();
    for (int i = 0; i < n; i++) {
        hero::Packet p(CH_UNRELIABLE);
        p.write_raw(payload, 32);
        client.send(p);
    }
    auto deadline = Clock::now() + ms(2000);
    while (received < n && Clock::now() < deadline) sleep_ms(1);
    int64_t taken = elapsed_us(t0);

    done = true; srv.join();
    print_throughput("HERO unreliable 32B", taken, n, (size_t)n * 32);
    std::printf("    received %d / %d\n", received.load(), n);
}

static void hero_rtt(int n) {
    std::atomic<bool> done{false};
    std::thread srv([&] {
        hero::Socket s;
        s.bind(PORT);
        while (!done) {
            s.poll();
            hero::Packet p; hero::Addr f;
            while (s.dequeue(p, f)) {
                hero::Packet r(CH_UNRELIABLE);
                r.write_raw(p.payload().data(), (uint32_t)p.payload().size());
                s.sendTo(r, f);
            }
        }
    });

    sleep_ms(50);
    hero::Socket client;
    client.connect("127.0.0.1", PORT);
    sleep_ms(20);

    int64_t total = 0, mn = INT64_MAX, mx = 0;
    uint8_t payload[8]{};
    int     got = 0;

    for (int i = 0; i < n; i++) {
        hero::Packet p(CH_UNRELIABLE);
        p.write_raw(payload, 8);
        auto t0 = Clock::now();
        client.send(p);
        auto dl = Clock::now() + ms(500);
        while (Clock::now() < dl) {
            client.poll();
            hero::Packet r; hero::Addr f;
            if (client.dequeue(r, f)) {
                int64_t rtt = elapsed_us(t0);
                total += rtt; if (rtt < mn) mn = rtt; if (rtt > mx) mx = rtt;
                got++; break;
            }
        }
        sleep_ms(1);
    }
    done = true; srv.join();
    print_latency("HERO RTT", got ? total/got : 0, mn, mx);
}

static void hero_reliable(int n) {
    std::atomic<int>  received{0};
    std::atomic<bool> done{false};

    std::thread srv([&] {
        hero::Socket s;
        s.setChannel(CH_RELIABLE, { true, false });
        s.bind(PORT);
        while (!done) {
            s.poll();
            hero::Packet p; hero::Addr f;
            while (s.dequeue(p, f)) {
                received++;
                // ACK: send() carries the seq/ack fields automatically via
                // send_to_ps's header — a tiny empty reply is enough to let
                // process_acks() on the client clear the pending slot.
                hero::Packet ack(CH_RELIABLE);
                s.sendTo(ack, f);
            }
        }
    });

    sleep_ms(50);
    hero::Socket client;
    client.setChannel(CH_RELIABLE, { true, false });
    client.connect("127.0.0.1", PORT);
    sleep_ms(20);

    uint8_t payload[32]{};
    auto t0 = Clock::now();
    for (int i = 0; i < n; i++) {
        hero::Packet p(CH_RELIABLE);
        p.write_raw(payload, 32);
        client.send(p);
        client.poll(); // let acks flow in as we send, not just after
    }
    auto deadline = Clock::now() + ms(3000);
    while (received < n && Clock::now() < deadline) { client.poll(); sleep_ms(1); }
    int64_t taken = elapsed_us(t0);

    done = true; srv.join();
    print_throughput("HERO reliable 32B", taken, n, (size_t)n * 32);
    std::printf("    received %d / %d\n", received.load(), n);
}

static void hero_frag(size_t bytes) {
    std::atomic<bool> got{false};
    std::atomic<int64_t> reassembly_us{0};
    std::atomic<bool> done{false};

    std::thread srv([&] {
        hero::Socket s;
        s.setChannel(CH_CHUNK, { true, true });
        s.bind(PORT);
        while (!done) {
            auto t0 = Clock::now();
            s.poll();
            hero::Packet p; hero::Addr f;
            while (s.dequeue(p, f)) {
                reassembly_us = elapsed_us(t0);
                got = true;
            }
        }
    });

    sleep_ms(50);
    hero::Socket client;
    client.setChannel(CH_CHUNK, { true, true });
    client.connect("127.0.0.1", PORT);
    sleep_ms(20);

    std::vector<uint8_t> big(bytes, 0xAB);
    auto t0 = Clock::now();
    hero::Packet p(CH_CHUNK);
    p.write_raw(big.data(), (uint32_t)big.size());
    client.send(p);

    auto dl = Clock::now() + ms(5000);
    while (!got && Clock::now() < dl) { client.poll(); sleep_ms(1); }

    int64_t total = elapsed_us(t0);
    done = true; srv.join();
    print_frag("HERO frag 512KB", total, reassembly_us);
}

// ---------------------------------------------------------------------------
// ENet benchmarks (compiled out if HERO_BENCH_ENET not defined)

#ifdef HERO_BENCH_ENET

static void enet_throughput(int n) {
    std::atomic<int>  received{0};
    std::atomic<bool> done{false};

    std::thread srv([&] {
        ENetAddress addr{}; addr.host = ENET_HOST_ANY; addr.port = PORT;
        ENetHost* host = enet_host_create(&addr, 32, 1, 0, 0);
        while (!done) {
            ENetEvent ev;
            while (enet_host_service(host, &ev, 0) > 0) {
                if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                    received++;
                    enet_packet_destroy(ev.packet);
                }
            }
        }
        enet_host_destroy(host);
    });

    sleep_ms(50);

    ENetAddress saddr{}; enet_address_set_host(&saddr, "127.0.0.1"); saddr.port = PORT;
    ENetHost* host = enet_host_create(nullptr, 1, 1, 0, 0);
    ENetPeer* peer = enet_host_connect(host, &saddr, 1, 0);
    ENetEvent ev;
    enet_host_service(host, &ev, 1000); // wait for connect
    sleep_ms(20);

    uint8_t payload[32]{};
    auto t0 = Clock::now();
    for (int i = 0; i < n; i++) {
        ENetPacket* p = enet_packet_create(payload, 32, 0); // unreliable
        enet_peer_send(peer, 0, p);
        enet_host_flush(host);
    }
    auto deadline = Clock::now() + ms(2000);
    while (received < n && Clock::now() < deadline) {
        enet_host_service(host, &ev, 1);
    }
    int64_t taken = elapsed_us(t0);

    done = true;
    enet_peer_disconnect(peer, 0);
    enet_host_destroy(host);
    srv.join();

    print_throughput("ENet unreliable 32B", taken, n, (size_t)n * 32);
    std::printf("    received %d / %d\n", received.load(), n);
}

static void enet_rtt(int n) {
    std::atomic<bool> done{false};
    std::thread srv([&] {
        ENetAddress addr{}; addr.host = ENET_HOST_ANY; addr.port = PORT;
        ENetHost* host = enet_host_create(&addr, 32, 1, 0, 0);
        while (!done) {
            ENetEvent ev;
            while (enet_host_service(host, &ev, 0) > 0) {
                if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                    ENetPacket* r = enet_packet_create(
                        ev.packet->data, ev.packet->dataLength, 0);
                    enet_peer_send(ev.peer, 0, r);
                    enet_host_flush(host);
                    enet_packet_destroy(ev.packet);
                }
            }
        }
        enet_host_destroy(host);
    });

    sleep_ms(50);
    ENetAddress saddr{}; enet_address_set_host(&saddr, "127.0.0.1"); saddr.port = PORT;
    ENetHost* host = enet_host_create(nullptr, 1, 1, 0, 0);
    ENetPeer* peer = enet_host_connect(host, &saddr, 1, 0);
    ENetEvent ev; enet_host_service(host, &ev, 1000);
    sleep_ms(20);

    int64_t total = 0, mn = INT64_MAX, mx = 0;
    uint8_t payload[8]{};
    int got = 0;

    for (int i = 0; i < n; i++) {
        ENetPacket* p = enet_packet_create(payload, 8, 0);
        auto t0 = Clock::now();
        enet_peer_send(peer, 0, p);
        enet_host_flush(host);
        auto dl = Clock::now() + ms(500);
        while (Clock::now() < dl) {
            if (enet_host_service(host, &ev, 1) > 0 &&
                ev.type == ENET_EVENT_TYPE_RECEIVE) {
                int64_t rtt = elapsed_us(t0);
                total += rtt; if (rtt < mn) mn = rtt; if (rtt > mx) mx = rtt;
                enet_packet_destroy(ev.packet);
                got++; break;
            }
        }
        sleep_ms(1);
    }
    done = true;
    enet_peer_disconnect(peer, 0);
    enet_host_destroy(host);
    srv.join();
    print_latency("ENet RTT", got ? total/got : 0, mn, mx);
}

#endif // HERO_BENCH_ENET

// ---------------------------------------------------------------------------

int main() {
    std::printf("=== HERO Bench ===\n\n");

#ifdef HERO_BENCH_ENET
    if (enet_initialize() != 0) {
        std::fprintf(stderr, "Failed to initialize ENet\n");
        return 1;
    }
#endif

    std::printf("[1] Throughput (10000 packets, 32B)\n");
    hero_throughput(10000);
#ifdef HERO_BENCH_ENET
    enet_throughput(10000);
#endif
    std::printf("\n");

    std::printf("[2] Round-trip latency (100 pings)\n");
    hero_rtt(100);
#ifdef HERO_BENCH_ENET
    enet_rtt(100);
#endif
    std::printf("\n");

    std::printf("[3] Reliable overhead (1000 packets, 32B)\n");
    hero_reliable(1000);
    std::printf("\n");

    std::printf("[4] Frag reassembly (512 KB)\n");
    hero_frag(512 * 1024);
    std::printf("\n");

    std::printf("=== done ===\n");

#ifdef HERO_BENCH_ENET
    enet_deinitialize();
#endif
    return 0;
}
