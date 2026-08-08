# HERO
## (The) Hero Execrates Raw UDP

# Goals
- Easy asf to use
- Fast asf
- Arbitrary binary payloads
- Data fragmentation + reassembly
- Reliability tiers (not everything needs to be guaranteed)
- Low latency (piggyback ACKs, no dedicated ACK packets)
- Cross-platform (Linux and Windows)

# Non-Goals
- Not HTTP, not a web protocol
- No request/response model (everything is push-based)

# Use Cases
- Multiplayer movement
- Chat systems
- File/map sharing

# Packet Header (10 bytes, fixed)
```
[flag:1][channel:1][seq:2][ack_num:2][ack_bits:4][payload_len:2]
```
- **flag** — packet type
- **channel** — priority tier (0=inputs, 1=state, 2=chunks, 3=chat)
- **seq** — uint16, wraps at 65535
- **ack_num** — last received seq from remote
- **ack_bits** — uint32 bitfield of previous 32 packets received
- **payload_len** — uint16, max 65507 bytes (UDP limit)

# Packet Types
| Flag | Value | Name | Purpose |
|------|-------|------|---------|
| CONN | 0 | Connection | Establish connection |
| GIVE | 1 | Give | Push data to recipient |
| PING | 2 | Ping | Latency measurement |
| PONG | 3 | Pong | Ping reply |
| FRAG | 4 | Fragment | Chunk of a larger payload |
| STOP | 5 | Disconnect | Gracefully close connection |

# Reliability Tiers
| Channel | Type | Use |
|---------|------|-----|
| 0 | Unreliable | Inputs, position updates |
| 1 | Reliable unordered | State changes, events |
| 2 | Reliable ordered | File/map chunks |
| 3 | Unreliable | Chat (fire and forget) |

# Fragmentation
- Payloads exceeding MTU (~1400 bytes) are split into FRAG packets
- Each FRAG carries: `frag_id:2, frag_index:1, frag_total:1` prepended to payload
- Receiver reassembles by frag_id; drops incomplete sets on timeout

# Connection Flow
```
Client                    Server
  |                          |
  |--- CONN (seq=0) -------->|
  |<-- GIVE (ack=0) ---------|  (server confirms, sends session info)
  |                          |
  |--- GIVE (seq=1, data) -->|  (piggybacked ack in header)
  |--- PING (seq=2) -------->|
  |<-- PONG (ack=2) ---------|
  |                          |
  |--- STOP (seq=N) -------->|
```

# Constraints
- Max payload: 65,507 bytes (UDP hard limit)
- Seq numbers: uint16 (0–65535, wraps)
- ACK bitfield covers last 32 packets
- Max retries for reliable packets: 3 (then disconnect)
- Connection timeout: 5000ms
