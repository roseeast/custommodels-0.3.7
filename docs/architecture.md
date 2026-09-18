# CustomModel architecture

## Phase 1 foundation

Phase 1 provides a Windows x86 ASI shell that can identify a small set of SA-MP builds without installing hooks. It does not load models, exchange protocol messages, or resolve undocumented client internals.

The preserved foundation has three parts:

- `custommodel_shared` contains PE parsing and the verified build-identification table. It has no Windows dependency and is covered by native tests.
- `CustomModel.asi` contains the Windows entry point, logging, loaded-module inspection, address-range validation, compatibility selection, and runtime state.
- `custommodel_tests` checks malformed PE handling and the known build table.

## Phase 2A scope

Phase 2A adds four small components without changing game state:

- `custommodel_shared` owns the platform-independent version 1 wire format, byte-order conversion, bounds checking, and typed message decoding.
- `ClientTransport` is the boundary between protocol bytes and a revision-specific SA-MP carrier. Phase 2A supplied only the fail-closed implementation; Phase 2B adds the reviewed R1/R3-1 adapter without changing this interface.
- `custommodel_server_core` owns per-player handshake and capability state plus the server transport boundary without depending on a SA-MP or open.mp SDK.
- the optional `CustomModelServer` component is a thin open.mp adapter. It depends on explicitly supplied, pinned SDK and network checkouts; neither dependency is fetched or linked into the ASI.
- host tests cover protocol failures and server state transitions without GTA SA, Wine, or a server executable.

There are no model messages or RenderWare calls in this phase.

## Phase 2B transport

Phase 2B adds a narrowly scoped RakNet carrier for R1 and R3-1. The shared `CMOD` packet remains unchanged. A one-byte legacy RakNet user-packet discriminator precedes it, so the layers are:

```text
0x5E | CMOD header | CMOD payload
```

The discriminator is the legacy RakNet `ID_USER_PACKET_ENUM` value, not a SA-MP RPC or synchronization packet ID. A packet is claimed only when `0x5E` is followed by the complete `CMOD` magic. Other packet IDs, short prefixes, and `0x5E` packets without that magic are returned to SA-MP unchanged.

The client state machine is transport-independent:

```text
Disconnected -> Connected -> HelloSent -> WelcomeReceived -> Compatible
                                     \-> Rejected
```

`TakeHello` succeeds once while connected. The RakNet adapter does not announce the connection on the first `IsConnected()` edge. It lets the first received packet return to SA-MP, then announces the connection on the next `Receive` call so SA-MP can send ClientJoin before CustomModel sends Hello. This follows the reviewed `chandling` transport sequence and prevents open.mp from discarding Hello before it has associated the RakNet peer with an `IPlayer`. A disconnect clears the accepted message, rejection, scheduling state, and one-Hello guard. Invalid protocol bytes and invalid Welcome fields do not change state.

The Windows adapter resolves `CNetGame*`, calls the public `CNetGame::GetRakClient` path, validates that the resulting vtable and required targets belong to executable or readable `samp.dll` memory, and replaces only `Receive` vtable slot 8. It saves the original entry before replacement. The hook repeatedly calls the original `Receive`; an unrelated packet is returned immediately without mutation or deallocation, while a recognized CustomModel packet is decoded locally, released exactly once through `DeallocatePacket` slot 9, and skipped. Consumption is capped at 64 packets per call so hostile traffic cannot keep SA-MP's receive loop indefinitely.

`Send` uses raw-buffer vtable slot 7 with high priority, reliable-ordered delivery, channel zero, and the exact serialized byte length. The reviewed generic RakNet source describes its raw length as bits, but the exact R1 and R3-1 SA-MP binaries demonstrably treat the slot-7 argument as bytes: passing `27 * 8` produced a 216-byte packet on both clients. The adapter therefore follows the exact-binary runtime evidence rather than applying the generic implementation's length unit to SA-MP's embedded implementation. The reviewed MSVC x86 vtable layout places the BitStream overload at slot 6 and the raw overload at slot 7 despite their source declaration order. The MinGW build uses an explicit x86 fastcall bridge: `this` is placed in ECX, an unused EDX argument fills the second register, and the native thiscall arguments remain on the stack. No MinGW C++ vtable is passed into SA-MP.

R2, the public R4 record, and the observed R4-family record continue to select the unavailable transport.

Runtime tests on exact R1 and R3-1 clients confirm the complete `Hello -> Welcome -> Compatible` round trip. open.mp invokes both incoming handlers at read offset 8 and receives the 26-byte CMOD Hello after consuming the carrier. Those tests also exposed and confirmed the correction of an independent raw-length bug: the client previously passed 216 for a 27-byte transport frame, so each server packet contained 189 trailing bytes and was correctly rejected. The client now passes 27 directly.

## Phase 3 asset distribution

Phase 3 extends the established transport without changing its carrier, RakNet ABI, or build policy:

```text
AssetRegistry -> ManifestBegin/ManifestAsset/ManifestEnd
              -> Client ManifestReceiver
              -> AssetCache lookup
              -> one download worker
              -> exact-size and SHA-256 verification
              -> atomic cache rename
              -> AssetReady or AssetError
```

The shared layer owns bounded asset metadata, validation, SHA-256 helpers, and explicit wire serialization. The SDK-independent server core owns deterministic registration, manifest publication, and per-player readiness. The open.mp component loads an optional `custommodel-assets.cfg`, sends a manifest only after Welcome, and translates bounded readiness messages back into server state. No open.mp type crosses into shared or server-core asset code.

The client has three focused modules. `ManifestReceiver` enforces one revision, sequential indexes, unique IDs and logical names, matching begin/end totals, and strict limits. `AssetCache` derives every path from the 32-byte content digest and a fixed DFF/TXD extension. `ClientAssetManager` owns one worker thread and synchronized work/result queues. The worker performs cache verification, HTTP transfer, hashing, and filesystem operations. The RakNet/network thread alone drains results and sends protocol reports; the worker never calls SA-MP or GTA.

WinHTTP supplies HTTP/HTTPS, proxy, redirect, timeout, and platform certificate handling without a deployable DLL. PicoSHA2 supplies the pinned header-only SHA-256 implementation. Downloads go to `CustomModel/cache/tmp/<hash>.<type>.tmp`; only an exact-size, matching digest is renamed into `CustomModel/cache/sha256/<prefix>/<hash>.<type>`. Failed transfers remove the temporary file. Existing entries are re-hashed on every lookup, and corrupt entries are removed before replacement.

The client session generation is incremented on disconnect and reconnect. Queued work and results are cleared; a transfer already executing may finish its filesystem operation, but its stale generation cannot alter new-session state or send a report. A reconnect receives a new manifest and can immediately report a verified cache hit.

Phase 3 does not interpret DFF/TXD bytes, register models, call GTA/RenderWare, or associate assets with game entities. Those remain Phase 4 work.

## Initialization

`DllMain` performs only loader-lock-safe scheduling work. On process attach it disables thread notifications and starts a short-lived initialization thread. The worker:

1. opens `CustomModel/logs/custommodel.log` relative to the ASI location;
2. locates the already loaded `samp.dll` with `GetModuleHandleW`;
3. bounds and parses the in-memory PE headers;
4. rejects a non-PE32 module;
5. matches `AddressOfEntryPoint` against the explicit build table;
6. selects the matching compatibility record;
7. initializes the shared protocol;
8. selects the R1/R3-1 RakNet adapter or the fail-closed transport;
9. waits up to 30 seconds for the selected build's `CNetGame` and RakClient instances;
10. validates and installs the single Receive vtable hook when available;
11. starts the bounded asset worker for supported transports and enters `Ready`.

Any failure enters `Disabled` and leaves the game process running. An unknown entry point never falls back to a nearby revision.

## Shutdown

The adapter stores the exact vtable slot and original Receive address. Shutdown restores the entry only when it still points at CustomModel's hook, then clears callbacks and connection state. If the slot is inaccessible or a later hook has replaced it, shutdown leaves the adapter active instead of clearing function pointers that a chained hook could still call. Normal ASI deployment is process-lifetime; runtime hot-unload after another module has chained through CustomModel is not supported because that other module would retain a return address into an unloaded ASI.

## Compatibility data

`SampVersion` represents the intended R1 through R4 family, excluding R5. The detection table contains runtime-verified entries, a runtime-observed R4-family entry whose exact minor revision remains uncertain, and the preserved public-reference R4 entry. Enum membership alone does not imply that a build is recognized.

The runtime-observed `0x0CBCD0` build has its own `R4Family` identity. It does not share the `R4` identity used by the existing `0x0CBCB0` public-reference record, so later internal mappings cannot be applied across those binaries accidentally.

Each recognized revision has a `SampCompatibility` record. The R1 and R3-1 records now contain only the independently corroborated `CNetGame*` and `GetRakClient` RVAs needed by the transport. R2 and both R4 records remain zero. RPC and other unrelated mappings remain unresolved for every build. `HasMapping` and `HasAllMappings` keep incomplete paths fail-closed.

## Address safety

`ModuleInfo::ResolveRva` rejects RVA zero, overflow, and ranges outside `SizeOfImage`. `ModuleInfo::IsAccessible` additionally checks every covered virtual-memory region for committed, accessible pages belonging to the expected module allocation. A future caller must pass both compatibility checks before using a resolved address.

No signature scanner exists because there is no project-verified pattern to scan for and no current feature needs one.

## Protocol and transport boundary

The protocol accepts and produces bounded byte arrays. It contains no Windows, SA-MP, RakNet, player-ID, or memory-address knowledge. The separate shared transport-frame helper adds or recognizes the RakNet discriminator. `ClientTransport` exposes initialization, shutdown, send, incoming dispatch, availability, and connection-state callbacks. The R1/R3-1 adapter owns all RakNet ABI knowledge.

Recognizing an entry point still does not imply transport support. R2, the public-reference R4 record, and the observed R4-family client log protocol initialization followed by `SA-MP transport unavailable for this build` and remain fail-closed.

## Server handshake state

`PlayerCapabilityTracker` transitions a server-owned player ID through:

```text
Unknown -> HandshakePending -> Compatible
                            -> Rejected
```

It stores the client's runtime version, requested protocol, declared SA-MP build, declared capabilities, and negotiated capabilities. The Phase 2B server policy accepts only R1 and R3-1; recognized R2 and R4 identities receive `UnsupportedSampBuild` until their live transports are independently verified. Protocol, runtime, build, missing mandatory capability, and malformed-handshake failures produce typed `Reject` messages. Rejection has no gameplay or kick behavior.

`HandshakeEndpoint` joins the transport boundary to this tracker. It recognizes framed Hello packets, delegates validation, encodes Welcome or Reject, and sends the complete framed response. A byte-identical duplicate Hello resends the same Welcome without changing a compatible session. A reconnect starts a new pending session; disconnect and server reset erase the old state. Host tests use a small in-memory transport for the full R1/R3-1 protocol round trip.

## open.mp server adapter

`CustomModelServer` is an opt-in open.mp component behind `ServerTransport`. It registers the official per-packet callback for `0x5E` and the player connect/disconnect callbacks. open.mp reads the packet ID and resets `NetworkBitStream` to bit offset 8 before every handler. The adapter therefore passes only the bounded `CMOD` bytes to `HandshakeEndpoint::DispatchProtocolPayload`; it does not expect or reconstruct `0x5E` on the receive side. Outbound responses still use the complete `0x5E + CMOD` transport frame.

The adapter inspects only the first four unread bytes without moving the bitstream cursor. If they are not complete `CMOD` magic, it returns `true`, allowing ordinary `0x5E` traffic to continue through open.mp unchanged. Non-byte-aligned, oversized, truncated, or malformed claimed frames receive a typed rejection and return `false` to stop later handlers. The open.mp LegacyNetwork loop retains and deallocates its RakNet `Packet`; the component never owns or frees it.

A highest-priority global incoming observer logs only packet ID `0x5E`, never reads or changes the stream, and always returns `true`. The per-packet handler logs the player ID, read offset, remaining size, and at most 16 preview bytes. If the observer logs but the per-packet handler does not, an intervening global handler stopped propagation. If neither logs, LegacyNetwork did not dispatch the packet to an established player. Startup logs also show global and `0x5E` handler counts for collision diagnosis.

Responses use `IPlayer::sendPacket` with the exact framed bytes and open.mp's reliable channel. The SDK span length is expressed in bits, as required by that API. The component declares all capability bits and requires `DFF`, `TXD`, and `SHA256_CACHE` before publishing a Phase 3 manifest. `CHAR_MODEL` and `OBJECT_MODEL` remain declarations only; no model loading exists.

The adapter checks `ICore::getNetworkBitStreamVersion()` against the compiled `NetworkBitStream::Version` before registering callbacks. A mismatch leaves it inactive. The adapter is deliberately optional: the default host tests and MinGW ASI build have no open.mp dependency. Building it requires the pinned open.mp SDK and open.mp-network paths described in `building.md`.

The ELF32 component has been load-tested on the official open.mp `1.5.8.3079` Linux x86 server. The component also compiles as ELF32 i386 against the SDK and network revisions embedded by open.mp `1.5.8.3134`. The Phase 2B R1/R3-1 handshake is runtime verified; Phase 3 manifest/download behavior still requires the manual live test in `building.md`.
