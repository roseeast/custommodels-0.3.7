# CustomModel protocol version 1

Phase 2A defines the CustomModel payload format. Phase 2B defines the RakNet carrier for SA-MP 0.3.7 R1 and R3-1. Phase 3 adds bounded manifest streaming and asset status messages without changing the carrier or header.

## RakNet carrier

| Offset | Width | Field | Value |
|---:|---:|---|---:|
| 0 | 1 | Legacy RakNet user packet ID | `0x5E` |
| 1 | 4 | CustomModel magic | `CMOD` |
| 5 | variable | Remaining protocol packet | Header and payload below |

`0x5E` is the `ID_USER_PACKET_ENUM` value in the reviewed legacy RakNet branch. It is a user-packet boundary, not an existing SA-MP RPC or synchronization ID. CustomModel requires both the carrier byte and all four magic bytes before claiming a packet. This second discriminator allows other `0x5E` traffic to pass through unchanged.

The largest framed RakNet payload is 4097 bytes: one carrier byte plus the existing 4096-byte protocol limit. Client sends use high priority, reliable-ordered delivery, and ordering channel zero. Large asset data remains outside this channel.

The client queues Hello only after SA-MP has received and processed its first connected packet. `IsConnected()` becomes true before the server necessarily has an `IPlayer`, so sending on that first edge can place Hello ahead of ClientJoin and make LegacyNetwork discard it before component dispatch.

The exact R1 and R3-1 raw slot-7 `Send` implementations take the framed length in bytes. A protocol v1 Hello is 26 CMOD bytes and 27 transport bytes after adding `0x5E`, so the raw call receives length `27`. This is distinct from open.mp's server-side `Span` API, whose length is expressed in bits.

## Wire rules

- Every integer is unsigned and encoded in network byte order (big-endian).
- Native C++ structures are never copied to or from the wire.
- A complete packet may not exceed 4096 bytes, including its 12-byte header.
- Version 1 permits at most 4084 payload bytes.
- A decoder rejects trailing bytes, truncated data, unknown message IDs, unknown enum values, and capability bits not declared by protocol version 1.
- Incoming data is parsed from a bounded byte view. Payload lengths are checked before any message-specific read.

## Header

| Offset | Width | Field | Version 1 value |
|---:|---:|---|---|
| 0 | 4 | Magic | `0x434D4F44` (`CMOD`) |
| 4 | 2 | Protocol version | `1` |
| 6 | 2 | Message type | See message IDs below |
| 8 | 4 | Payload size | Bytes following the header |

The payload size must equal the number of bytes remaining in the transport frame. It cannot describe a prefix of a larger frame.

## Versioning policy

The header version selects the complete wire grammar. A version 1 decoder rejects any other header version before interpreting the payload. `Hello` repeats the client's requested protocol version so the server handshake policy can produce a typed `UnsupportedProtocol` rejection after the outer transport has selected a compatible envelope.

Adding a capability uses a previously unused bit and requires a protocol revision if version 1 peers would reject it. Changing a field's width, order, or meaning also requires a new protocol version. Message IDs are never reused.

## Message IDs

| ID | Name | Payload bytes |
|---:|---|---:|
| 1 | `Hello` | 14 |
| 2 | `Welcome` | 14 |
| 3 | `Reject` | 2 |
| 4 | `Ping` | 8 |
| 5 | `Pong` | 8 |
| 6 | `ManifestBegin` | 20 |
| 7 | `ManifestAsset` | 62 + name bytes + URL bytes |
| 8 | `ManifestEnd` | 20 |
| 9 | `AssetReady` | 12 |
| 10 | `AssetError` | 14 |

The manifest uses one bounded message per asset rather than one unbounded packet. No model registration message exists yet.

## Hello

| Payload offset | Width | Field |
|---:|---:|---|
| 0 | 2 | Client runtime major |
| 2 | 2 | Client runtime minor |
| 4 | 2 | Client runtime patch |
| 6 | 2 | Requested protocol version |
| 8 | 2 | Detected `SampVersion` wire value |
| 10 | 4 | Declared capability flags |

`SampVersion::Unknown` is a valid wire value so a server can return `UnsupportedSampBuild`. Other values must be defined by the shared enum. Recognition remains stricter than enum validity: the Phase 2B server accepts only registered R1 and R3-1 identities, matching the client transports currently enabled.

| Wire value | Build identifier | Currently recognized entry point |
|---:|---|---:|
| 0 | `Unknown` | None |
| 1 | `R1` | `0x31DF13` |
| 2 | `R2` | `0x3195DD` |
| 3 | `R3` | None |
| 4 | `R3_1` | `0x0CC4D0` |
| 5 | `R4` | `0x0CBCB0` public-reference record |
| 6 | `R4_2` | None |
| 7 | `R4Family` | `0x0CBCD0` runtime-observed record |

## Welcome

| Payload offset | Width | Field |
|---:|---:|---|
| 0 | 2 | Accepted protocol version |
| 2 | 2 | Server runtime major |
| 4 | 2 | Server runtime minor |
| 6 | 2 | Server runtime patch |
| 8 | 2 | Server protocol version |
| 10 | 4 | Negotiated capability flags |

Negotiated capabilities are the intersection of the client's declaration and the server policy's configured capabilities. The SDK-independent server-core default remains zero. The Phase 3 open.mp adapter declares all five flags and requires `DFF`, `TXD`, and `SHA256_CACHE` before manifest publication. This permits asset distribution only; it is not evidence that model loading exists.

## Reject

| Value | Reason |
|---:|---|
| 1 | `UnsupportedProtocol` |
| 2 | `UnsupportedClientRuntime` |
| 3 | `UnsupportedSampBuild` |
| 4 | `MalformedHandshake` |
| 5 | `UnsupportedCapabilities` |

`Reject` changes capability state; it does not kick the player.

The server accepts a Hello only while the player is pending. A byte-identical duplicate after compatibility is idempotent and resends Welcome. A different Hello or malformed claimed frame rejects the session. Disconnect, reconnect, and server reset clear prior state before another handshake.

## Ping and Pong

Both messages contain one opaque 64-bit nonce. A `Pong` copies the corresponding `Ping` nonce. Timing policy is outside the shared serializer.

## Capability flags

| Bit | Mask | Name | Current meaning |
|---:|---:|---|---|
| 0 | `0x00000001` | `CHAR_MODEL` | Reserved declaration; not loaded in Phase 3 |
| 1 | `0x00000002` | `OBJECT_MODEL` | Reserved declaration; not loaded in Phase 3 |
| 2 | `0x00000004` | `DFF` | Client accepts opaque DFF asset metadata and bytes |
| 3 | `0x00000008` | `TXD` | Client accepts opaque TXD asset metadata and bytes |
| 4 | `0x00000010` | `SHA256_CACHE` | Client verifies and content-addresses cached assets |

No capability grants gameplay authority. DFF/TXD support in Phase 3 ends at verified caching; it does not imply parsing, loading, or model synchronization.

## Manifest stream

The server sends the stream only after Welcome has made the client compatible:

```text
ManifestBegin -> ManifestAsset(index 0..N-1) -> ManifestEnd
```

`ManifestBegin` and `ManifestEnd` have the same fields:

| Payload offset | Width | Field |
|---:|---:|---|
| 0 | 8 | Manifest revision |
| 8 | 4 | Asset count |
| 12 | 8 | Total announced file bytes |

The revision is nonzero. Count is at most 256. Total bytes are at most 512 MiB, and a zero count requires a zero total. The end values must exactly match the begin values and the entries received between them.

Each `ManifestAsset` contains:

| Order | Width | Field |
|---:|---:|---|
| 1 | 8 | Manifest revision |
| 2 | 4 | Sequential asset index |
| 3 | 4 | Asset ID |
| 4 | 2 | Asset type (`1` DFF, `2` TXD) |
| 5 | 2 + N | Logical-name byte length and bytes |
| 6 | 2 + N | Download-URL byte length and bytes |
| 7 | 8 | Exact file size |
| 8 | 32 | Raw SHA-256 digest |

Asset IDs are nonzero and unique within a manifest. Logical names are unique, 1-96 bytes, and use the conservative ASCII set `A-Z a-z 0-9 _ - . /`; empty, `.` and `..` path segments are rejected. URLs are 1-2048 printable ASCII bytes, must use `http://` or `https://`, must have a nonempty authority, and may not contain credentials, backslashes, or fragments. Each file is 1 byte through 64 MiB. SHA-256 is always 32 raw bytes on the wire.

The receiver requires one active stream, matching revisions, exact sequential indexes, exact aggregate size, and no trailing bytes in any message. A failed stream cannot be resumed; reconnect/session reset is required. Total encoded manifest metadata is bounded to 1 MiB, while each individual CMOD packet remains subject to the 4096-byte packet limit.

## Asset status

`AssetReady` contains a manifest revision (`u64`) and asset ID (`u32`). `AssetError` adds a typed `u16` reason:

| Value | Reason |
|---:|---|
| 1 | `InvalidManifest` |
| 2 | `UnsupportedScheme` |
| 3 | `NetworkFailure` |
| 4 | `Timeout` |
| 5 | `SizeMismatch` |
| 6 | `HashMismatch` |
| 7 | `DiskFailure` |
| 8 | `InternalError` |

Clients send Ready only after a verified cache hit or successful exact-size download, SHA-256 match, and atomic cache commit. Status messages with the wrong revision or unknown asset ID are rejected by server state. They contain no local path, URL, or arbitrary error text.

## open.mp receive boundary

The concrete component registers a per-packet handler for ID `0x5E`. open.mp reads the ID, then resets the bitstream read offset to exactly 8 bits before every global and per-ID handler. The handler therefore receives `CMOD...`, not `0x5E + CMOD...`. It passes that payload directly to the shared decoder and adds the carrier only when sending a response.

The adapter returns `true` for a short prefix or non-`CMOD` magic and `false` only after the complete magic claims the packet. A read-only global observer for ID `0x5E` always returns `true` and exists only to diagnose propagation through other components. open.mp owns and deallocates its received RakNet packet after dispatch; CustomModel neither frees nor retains server network memory.
