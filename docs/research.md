# CustomModel research ledger

This ledger separates facts used by production code from work that still needs a source or binary check.

## Status definitions

- **VERIFIED**: supported by a trustworthy source and independently confirmed closely enough for the stated fact.
- **RUNTIME-VERIFIED**: exercised against the exact identified binary for the stated operation. This status does not automatically extend to adjacent offsets, layouts, or receive ownership paths.
- **RUNTIME-OBSERVED**: the entry-point value was measured from a real client, but its exact minor revision identity is not confirmed.
- **PUBLIC-REFERENCE**: the value came from previously documented public research and remains preserved without a matching runtime observation in this project.
- **INFERRED**: derived from related evidence but not directly established. Inferred mappings are documentation-only.
- **UNKNOWN**: no sufficiently specific mapping was found.
- **UNVERIFIED**: available evidence is insufficient to use the information in production code.

Runtime observation verifies only the PE `AddressOfEntryPoint` value for the tested binary. It does not verify internal offsets, layouts, calling conventions, signatures, or hook compatibility.

## Runtime observations

| Revision/build label | Entry point RVA | Source of knowledge | Knowledge status | Runtime verification | Exact revision confirmed |
|---|---:|---|---|---|---|
| SA-MP 0.3.7 R1 | `0x31DF13` | Real-client runtime test reported by the project maintainer | VERIFIED | Confirmed | Yes |
| SA-MP 0.3.7 R2 | `0x3195DD` | Real-client runtime test reported by the project maintainer | VERIFIED | Confirmed | Yes |
| SA-MP 0.3.7 R3-1 | `0x0CC4D0` | Real-client runtime test reported by the project maintainer | VERIFIED | Confirmed | Yes |
| SA-MP 0.3.7 R4-family | `0x0CBCD0` | Real-client runtime test reported by the project maintainer | RUNTIME-OBSERVED | Confirmed | No; exact R4/R4-2 identity remains uncertain |

## Preserved public-reference entry

| Revision/build label | Entry point RVA | Source of knowledge | Knowledge status | Runtime verification | Exact revision confirmed |
|---|---:|---|---|---|---|
| SA-MP 0.3.7 R4 | `0x0CBCB0` | Original project specification, citing public reverse-engineering research | PUBLIC-REFERENCE | Not observed in current runtime tests | Not independently confirmed |

`0x0CBCB0` and `0x0CBCD0` are distinct entry points and distinct build records. No current evidence establishes that they are the same binary or interchangeable minor revisions.

## SA-MP networking mapping audit

All numeric values below are module-relative RVAs in `samp.dll`. `PUBLIC-REFERENCE` means only that the named source publishes the mapping. It does not upgrade that mapping to `RUNTIME-VERIFIED`. Phase 2B promotes only the R1 and R3-1 `CNetGame*` and `GetRakClient` records into the production compatibility registry; R2 and both R4 records remain unresolved.

The SAMP-API citations are pinned to commit `6d4db99ab41f19d1a6a7c6cd48f5878bd1e14b62`, chandling to `6ade726e174fe3fc1793d96791f1b32adc1d4b33`, sampvoice to `90e08665e17749e07df2b95662edca89756ba6c4`, and OpenMPPlus to `7cee168fe2279e643d99c5c9689337c4a2c80d8d`.

### Production transport mappings

| Exact detected build | Entry point RVA | `CNetGame*` RVA | `GetRakClient` RVA | Source status | Runtime status |
|---|---:|---:|---:|---|---|
| R1 | `0x31DF13` | `0x21A0F8` | `0x1A40` | PUBLIC-REFERENCE, corroborated by pinned SAMP-API and chandling records | RUNTIME-VERIFIED for RakClient resolution, deferred connection timing, exact 27-byte raw Send delivery, inbound Welcome handling, complete handshake, and disconnect detection |
| R3-1 | `0x0CC4D0` | `0x26E8DC` | `0x1A40` | PUBLIC-REFERENCE, corroborated by pinned SAMP-API and chandling records | RUNTIME-VERIFIED for RakClient resolution, deferred connection timing, exact 27-byte raw Send delivery, inbound Welcome handling, complete handshake, and disconnect detection |

Only these two exact build identities select the live adapter, and the server handshake policy accepts only these identities. The registry does not copy the values to R2, plain R3, R4, or R4-family.

### SA-MP 0.3.7 R1

| Build | Module | RVA/location | Symbol or path | Purpose | Calling convention / argument layout | Source | Status |
|---|---|---:|---|---|---|---|---|
| R1 | `samp.dll` | `0x21A0F8` | global `CNetGame*` slot | Locate the active `CNetGame` instance | Data pointer; no calling convention | [SAMP-API R1 `CNetGame.cpp`](https://github.com/BlastHackNet/SAMP-API/blob/6d4db99ab41f19d1a6a7c6cd48f5878bd1e14b62/src/sampapi/0.3.7-R1/CNetGame.cpp), [chandling database](https://github.com/dotSILENT/chandling/blob/6ade726e174fe3fc1793d96791f1b32adc1d4b33/chandling_offsets.ini) | PUBLIC-REFERENCE |
| R1 | `samp.dll` | `0x1A40` | `CNetGame::GetRakClient` | Return the `RakClientInterface*` owned by `CNetGame` | `RakClientInterface* __thiscall(CNetGame*)` | [SAMP-API R1 `CNetGame.cpp`](https://github.com/BlastHackNet/SAMP-API/blob/6d4db99ab41f19d1a6a7c6cd48f5878bd1e14b62/src/sampapi/0.3.7-R1/CNetGame.cpp) | PUBLIC-REFERENCE |
| R1 | `samp.dll` | `0xAD70` | `CNetGame::UpdateNetwork` | Publicly named network-update path; internal receive behavior still needs confirmation | `void __thiscall(CNetGame*)` | [SAMP-API R1 `CNetGame.cpp`](https://github.com/BlastHackNet/SAMP-API/blob/6d4db99ab41f19d1a6a7c6cd48f5878bd1e14b62/src/sampapi/0.3.7-R1/CNetGame.cpp) | PUBLIC-REFERENCE |
| R1 | `samp.dll` | `0x2401D3` | RakClient initialization site | sampvoice intercepts RakClient initialization and substitutes a forwarding interface | Naked x86 hook; exact overwritten instruction span and binary fingerprint are not established here | [sampvoice `addresses.hpp`](https://github.com/CyberMor/sampvoice/blob/90e08665e17749e07df2b95662edca89756ba6c4/client/libraries/util/addresses.hpp), [sampvoice `raknet.hpp`](https://github.com/CyberMor/sampvoice/blob/90e08665e17749e07df2b95662edca89756ba6c4/client/libraries/util/raknet.hpp) | PUBLIC-REFERENCE |
| R1 | `samp.dll` | Virtual methods; fixed RVAs UNKNOWN | `RakClientInterface::Send`, `Receive`, `RPC` | Raw packet send, packet receive, and RPC send paths | Published as x86 C++ virtual members. Overloads include `Send(BitStream*, priority, reliability, channel)`, `Packet* Receive()`, and `RPC(int*, BitStream*, priority, reliability, channel, shiftTimestamp)` | [sampvoice RakClient interface](https://github.com/CyberMor/sampvoice/blob/90e08665e17749e07df2b95662edca89756ba6c4/client/libraries/raknet/rakclient.h), [RakClientInterface reference](https://github.com/whyega/SAMP-RakNet/blob/master/RakNet/RakClientInterface.h) | PUBLIC-REFERENCE |
| R1 | `samp.dll` | UNKNOWN | generic RPC receive/dispatch | Dispatch a received RPC to its registered handler | `RPCParameters*` callback shape is public; dispatcher entry, exact ABI, and preservation strategy are unknown | Same RakClient interface references | UNKNOWN |

### SA-MP 0.3.7 R2

| Build | Module | RVA/location | Symbol or path | Purpose | Calling convention / argument layout | Source | Status |
|---|---|---:|---|---|---|---|---|
| R2 | `samp.dll` | `0x21A100` | global `CNetGame*` slot (`OFFSET_SampInfo`) | Locate the active `CNetGame` instance | Data pointer; no calling convention | [chandling database](https://github.com/dotSILENT/chandling/blob/6ade726e174fe3fc1793d96791f1b32adc1d4b33/chandling_offsets.ini) | PUBLIC-REFERENCE |
| R2 | `samp.dll` | UNKNOWN | RakClient pointer/member or getter | Obtain `RakClientInterface*` | UNKNOWN | No R2-specific declaration found in the reviewed sources | UNKNOWN |
| R2 | `samp.dll` | UNKNOWN | RPC send path | Send a CustomModel carrier | UNKNOWN | No R2-specific mapping found | UNKNOWN |
| R2 | `samp.dll` | UNKNOWN | packet receive path | Inspect and preserve incoming packets | UNKNOWN | No R2-specific mapping found | UNKNOWN |
| R2 | `samp.dll` | UNKNOWN | RPC receive/dispatch | Inspect and preserve incoming RPCs | UNKNOWN | No R2-specific mapping found | UNKNOWN |

### SA-MP 0.3.7 R3 and R3-1

| Build | Module | RVA/location | Symbol or path | Purpose | Calling convention / argument layout | Source | Status |
|---|---|---:|---|---|---|---|---|
| R3 | `samp.dll` | `0x26E8DC` | global `CNetGame*` slot (`OFFSET_SampInfo`) | Locate the active `CNetGame` instance | Data pointer; no calling convention | [chandling database](https://github.com/dotSILENT/chandling/blob/6ade726e174fe3fc1793d96791f1b32adc1d4b33/chandling_offsets.ini) | PUBLIC-REFERENCE |
| R3-1 | `samp.dll` | `0x26E8DC` | global `CNetGame*` slot | Locate the active `CNetGame` instance | Data pointer; no calling convention | [SAMP-API R3-1 `CNetGame.cpp`](https://github.com/BlastHackNet/SAMP-API/blob/6d4db99ab41f19d1a6a7c6cd48f5878bd1e14b62/src/sampapi/0.3.7-R3-1/CNetGame.cpp), [chandling database](https://github.com/dotSILENT/chandling/blob/6ade726e174fe3fc1793d96791f1b32adc1d4b33/chandling_offsets.ini) | PUBLIC-REFERENCE |
| R3-1 | `samp.dll` | `0x1A40` | `CNetGame::GetRakClient` | Return the `RakClientInterface*` owned by `CNetGame` | `RakClientInterface* __thiscall(CNetGame*)` | [SAMP-API R3-1 `CNetGame.cpp`](https://github.com/BlastHackNet/SAMP-API/blob/6d4db99ab41f19d1a6a7c6cd48f5878bd1e14b62/src/sampapi/0.3.7-R3-1/CNetGame.cpp) | PUBLIC-REFERENCE |
| R3-1 | `samp.dll` | `0xAF20` | `CNetGame::UpdateNetwork` | Publicly named network-update path; internal receive behavior still needs confirmation | `void __thiscall(CNetGame*)` | [SAMP-API R3-1 `CNetGame.cpp`](https://github.com/BlastHackNet/SAMP-API/blob/6d4db99ab41f19d1a6a7c6cd48f5878bd1e14b62/src/sampapi/0.3.7-R3-1/CNetGame.cpp) | PUBLIC-REFERENCE |
| R3-1 | `samp.dll` | `0xB270` | `CNetGame::Process` | Top-level public `CNetGame` process path | `void __thiscall(CNetGame*)`; its suitability as a receive hook is not established | Same SAMP-API source | PUBLIC-REFERENCE |
| Source-labelled R3 | `samp.dll` | `0xB658` | RakClient initialization site | sampvoice intercepts RakClient initialization and substitutes a forwarding interface | Naked x86 hook; source uses the broad `SAMP_R3` label, so exact R3 versus R3-1 applicability is unconfirmed | [sampvoice `addresses.hpp`](https://github.com/CyberMor/sampvoice/blob/90e08665e17749e07df2b95662edca89756ba6c4/client/libraries/util/addresses.hpp), [sampvoice `raknet.hpp`](https://github.com/CyberMor/sampvoice/blob/90e08665e17749e07df2b95662edca89756ba6c4/client/libraries/util/raknet.hpp) | PUBLIC-REFERENCE |
| R3/R3-1 | `samp.dll` | Virtual methods; fixed RVAs UNKNOWN | `RakClientInterface::Send`, `Receive`, `RPC` | Raw packet send, packet receive, and RPC send paths | Public interface shape matches the R1 description; exact vtable identity for each binary is not runtime verified | [sampvoice RakClient interface](https://github.com/CyberMor/sampvoice/blob/90e08665e17749e07df2b95662edca89756ba6c4/client/libraries/raknet/rakclient.h) | PUBLIC-REFERENCE |
| R3/R3-1 | `samp.dll` | UNKNOWN | generic RPC receive/dispatch | Dispatch a received RPC to its registered handler | Callback shape is public; dispatcher entry, exact ABI, and preservation strategy are unknown | Reviewed sources do not publish a complete build-specific mapping | UNKNOWN |

### SA-MP 0.3.7 R4 / R4-2 public records

| Build | Module | RVA/location | Symbol or path | Purpose | Calling convention / argument layout | Source | Status |
|---|---|---:|---|---|---|---|---|
| Source-labelled R4 | `samp.dll` | `0x26EA0C` | global `CNetGame*` slot (`OFFSET_SampInfo`) | Locate the active `CNetGame` instance | Data pointer; no calling convention | [chandling database](https://github.com/dotSILENT/chandling/blob/6ade726e174fe3fc1793d96791f1b32adc1d4b33/chandling_offsets.ini) | PUBLIC-REFERENCE / UNCORRELATED to either project entry point by chandling itself |
| Source-labelled R4-2 | `samp.dll` | `0x26EA0C` | global `CNetGame*` slot (`OFFSET_SampInfo`) | Locate the active `CNetGame` instance | Data pointer; no calling convention | Same chandling database; byte signatures distinguish its R4 and R4-2 records | PUBLIC-REFERENCE / UNCORRELATED to either project entry point by chandling itself |
| Exact profile `0x0CBCB0` | `samp.dll` | `0x26EA0C` | global `CNetGame*` slot | Locate the active `CNetGame` instance | Data pointer; no calling convention | [OpenMPPlus profile](https://github.com/Rohatcengizhanbucak/OpenMPPlus/blob/7cee168fe2279e643d99c5c9689337c4a2c80d8d/src/core/samp_profile.cpp), corroborated by chandling's source-labelled record | PUBLIC-REFERENCE; exact entry point is correlated in code, but no binary hash or CustomModel runtime test exists |
| Exact profile `0x0CBCB0` | `CNetGame` object | member `+0x2C` | `RakClientInterface*` member | Obtain the client-owned RakClient object | Borrowed pointer field | [OpenMPPlus profile and resolver](https://github.com/Rohatcengizhanbucak/OpenMPPlus/blob/7cee168fe2279e643d99c5c9689337c4a2c80d8d/src/client/win32_rak_transport.cpp) | PUBLIC-REFERENCE; OpenMPPlus labels this a code-level profile rather than a released compatibility target |
| Exact profile `0x0CBCB0` | RakClient vtable | standard interface order | raw Send, Receive, DeallocatePacket, IsConnected | Candidate common RakClient ABI | MSVC-style interface in [OpenMPPlus](https://github.com/Rohatcengizhanbucak/OpenMPPlus/blob/7cee168fe2279e643d99c5c9689337c4a2c80d8d/src/client/win32_rak_transport.hpp) and cross-version interface in [chandling](https://github.com/dotSILENT/chandling/blob/6ade726e174fe3fc1793d96791f1b32adc1d4b33/chandling/raknet/RakClientInterface.h) | PUBLIC-REFERENCE / UNCORRELATED for the raw Receive and deallocation path on this exact binary |
| Exact profile `0x0CBCB0` | RakClient vtable | slots 25/26 in that project | RPC raw/BitStream send | OpenMPPlus live transport architecture | [OpenMPPlus transport](https://github.com/Rohatcengizhanbucak/OpenMPPlus/blob/7cee168fe2279e643d99c5c9689337c4a2c80d8d/src/client/win32_rak_transport.cpp) | PUBLIC-REFERENCE; does not exercise CustomModel's raw Send slot 7 or Receive/Deallocate slots |
| Project `0x0CBCD0` R4-family record | `samp.dll` | UNKNOWN | all RakClient paths | Exact minor revision and transport chain | UNKNOWN | RUNTIME-OBSERVED entry point only; no reviewed source correlates internal mappings to it |

OpenMPPlus explicitly limits its published release compatibility matrix to SA-MP 0.3.DL-R1 even though its code contains an R4 profile. chandling uses a version signature at `samp.dll + 0xBABE` rather than PE entry points and hooks a common Receive signature across supported marketing revisions. Neither source supplies a hash tying its complete raw Receive/deallocation ABI to the project's `0x0CBCB0` binary, and neither mentions `0x0CBCD0`. Consequently both project R4 records remain disabled.

### RakNet interface and ownership audit

These facts are shared by the R1 and R3-1 adapter. They are not copied to an unknown interface: the adapter first resolves the exact build's `RakClientInterface*`, then verifies that its vtable and every selected function belong to the loaded `samp.dll` image.

| Build | Symbol/layout | Location or value | Calling convention / layout | Purpose and preservation rule | Source | Verification status | Runtime status |
|---|---|---:|---|---|---|---|---|
| R1, R3-1 | `RakClientInterface::Send(const char*, ...)` | vtable slot 7 | MSVC x86 `__thiscall`; `this` in ECX, five arguments on stack, callee cleanup | Send the exact framed byte count with `HIGH_PRIORITY`, `RELIABLE_ORDERED`, channel 0 | [Pinned RakClient interface](https://github.com/openmultiplayer/RakNet/blob/64d8ddb14d3341436217a9a8815b9511016bb2fa/Include/raknet/RakClientInterface.h), [pinned RakClient implementation](https://github.com/openmultiplayer/RakNet/blob/64d8ddb14d3341436217a9a8815b9511016bb2fa/Source/RakClient.cpp), [pinned sampvoice interface](https://github.com/CyberMor/sampvoice/blob/90e08665e17749e07df2b95662edca89756ba6c4/client/libraries/raknet/rakclient.h) | VERIFIED interface order; generic RakNet's documented bit-length contract does not match the exact SA-MP embedded implementation | RUNTIME-VERIFIED on exact R1 and R3-1: argument `216` produced exactly 216 packet bytes, proving this slot takes bytes in those binaries |
| R1, R3-1 | `RakClientInterface::Receive()` | vtable slot 8 | MSVC x86 `__thiscall`; `this` in ECX, returns borrowed `Packet*` requiring release by the caller when consumed | The sole hook target; save and call the original. Return unrelated packets unchanged | Same pinned interfaces, [chandling Receive architecture](https://github.com/dotSILENT/chandling/blob/6ade726e174fe3fc1793d96791f1b32adc1d4b33/chandling/Hooks.cpp) | VERIFIED interface order and purpose | RUNTIME-VERIFIED for ordinary traffic and inbound CMOD Welcome consumption on exact R1/R3-1 |
| R1, R3-1 | `RakClientInterface::DeallocatePacket(Packet*)` | vtable slot 9 | MSVC x86 `__thiscall`; `this` in ECX, `Packet*` on stack, callee cleanup | Release each consumed packet exactly once, then call original `Receive` again | [Pinned RakClient interface](https://github.com/openmultiplayer/RakNet/blob/64d8ddb14d3341436217a9a8815b9511016bb2fa/Include/raknet/RakClientInterface.h), [RakPeer ownership implementation](https://github.com/openmultiplayer/RakNet/blob/64d8ddb14d3341436217a9a8815b9511016bb2fa/Source/RakPeer.cpp), [sampvoice consume loop](https://github.com/CyberMor/sampvoice/blob/90e08665e17749e07df2b95662edca89756ba6c4/client/libraries/util/raknet.hpp) | VERIFIED API ownership rule and interface order | Not runtime-tested |
| R1, R3-1 | `RakClientInterface::IsConnected()` | vtable slot 18 | MSVC x86 `__thiscall`; `this` in ECX, returns `bool` | Detect connection edges without a per-frame Hello | Same pinned RakClient interfaces | VERIFIED interface order and purpose | RUNTIME-VERIFIED for connected/disconnected edge logs on exact R1 and R3-1 |
| R1, R3-1 | `Packet` | 21 bytes; `length` offset 8; `data` offset 16 | x86 fields under `#pragma pack(push, 1)`: `PlayerIndex`, six-byte `PlayerID`, two 32-bit lengths, pointer, bool | Read only the length and borrowed data pointer after bounded memory validation | [Pinned sampvoice packet declaration](https://github.com/CyberMor/sampvoice/blob/90e08665e17749e07df2b95662edca89756ba6c4/client/libraries/raknet/rakclient.h) | PUBLIC-REFERENCE with compile-time offset and size assertions | Not runtime-tested |
| R1, R3-1 | legacy packet IDs | disconnect `32`, lost `33`, user boundary `0x5E` | First byte of RakNet packet data | Reset connection state; claim traffic only for `0x5E` followed by complete `CMOD` magic | [Pinned packet enumeration](https://github.com/openmultiplayer/RakNet/blob/64d8ddb14d3341436217a9a8815b9511016bb2fa/Include/raknet/PacketEnumerations.h) | VERIFIED by compiling the pinned legacy enum with `RAKNET_LEGACY=1` | Not runtime-tested |

The source declaration lists the raw `Send` overload before the BitStream overload, but the MSVC x86 vtable groups those overloads in the opposite order. A targeted Clang MS-ABI layout dump produced: BitStream `Send` 6, raw `Send` 7, `Receive` 8, `DeallocatePacket` 9, and `IsConnected` 18. The first Phase 2B draft used slot 6 for raw `Send`; the stabilization audit corrected this before runtime testing.

The MinGW adapter does not instantiate or call through a MinGW-layout C++ interface. It reads the reviewed slot directly and uses a `__fastcall` bridge whose dummy EDX parameter makes the emitted x86 register/stack shape match the MSVC `__thiscall` member. This is mechanically checked by the cross-compiler build but remains a runtime ABI assumption until exercised on both exact clients.

### Receive hook safety result

CustomModel patches only the resolved object's vtable slot 8. It does not use chandling's code signature or patch a guessed function RVA. Installation uses an atomic compare/exchange against the saved original slot. Shutdown restores that exact pointer only if the slot still points to CustomModel; if restoration is unsafe, state is retained rather than clearing a callback target that another hook could still chain through.

For each hook call, non-CustomModel packets are returned without changing `Packet::data`, `Packet::length`, or ownership. A packet is consumed only after both `0x5E` and complete `CMOD` magic match. Recognized packets are bounds-checked, decoded when fully readable and within 4097 bytes, deallocated once through the original interface, and never returned. The loop then asks the original `Receive` for the next queued packet. A 64-packet bound prevents an untrusted queue from retaining control indefinitely.

The exact R1 and R3-1 clients exercised RakClient resolution, hook installation, normal connection traffic, raw Send delivery into open.mp, inbound Reject consumption, and disconnect detection without a crash. Both produced the same server observation: 1720 unread bits after the carrier, or 215 CMOD-view bytes, even though the header declared a 14-byte payload. The server correctly rejected those 189 trailing bytes.

The timing audit found that CustomModel announced `Connected` and called Send before invoking the original `Receive` that returned SA-MP's first connected packet. open.mp dispatches ordinary packets only inside `if (player)`, and `playerFromRakIndex` is populated by the later ClientJoin RPC path. A packet that arrives before that association is deallocated without entering global or per-ID component dispatch. The pinned `chandling` implementation uses the corresponding safe sequence: receive the first packet, arm its custom init, then send on the next `Receive` call. CustomModel now follows that sequence, and open.mp handler invocation is RUNTIME-VERIFIED on exact R1 and R3-1.

The subsequent 216-byte observation isolated a separate length-unit defect. The serializer returned 26 bytes and the transport vector returned 27 bytes, but the adapter passed `frame.size() * 8`, or 216, to slot 7. Both exact clients delivered 216 bytes, proving that their embedded slot-7 raw call interprets the integer as bytes. The MinGW disassembly also shows the vector's byte count being placed unchanged in the raw call's length stack slot, so no calling-convention change is required. Production now passes `frame.size()` directly. The generic pinned RakNet source treats the corresponding raw argument as a bit count; that source therefore documents interface ancestry and ordering, but not the exact SA-MP fork's length unit.

### open.mp server adapter evidence

The open.mp `1.5.8.3134` build number maps through the project's CI build-count rule to source commit `876f2be913517e8f2e07da27b0e57774c2192aaf`. That commit embeds open.mp SDK `3ee7bc4ab20c22359c34c08c38f93815b44bffd5`, open.mp-network `dc3eac9d5dc30f96edcf4e7e64f33d8c241d49ff`, and RakNet `64d8ddb14d3341436217a9a8815b9511016bb2fa`. The component now compiles against those exact SDK/network revisions.

| Boundary | Verified behavior | Source | Status |
|---|---|---|---|
| Packet registration | `ICore::addPerPacketInEventHandler<ID>` iterates all registered networks and registers `SingleNetworkInEventHandler::onReceive` for one packet ID | [1.5.8.3134 SDK `core.hpp`](https://github.com/openmultiplayer/open.mp-sdk/blob/3ee7bc4ab20c22359c34c08c38f93815b44bffd5/include/core.hpp) | VERIFIED API boundary; component compile-tested |
| Registration timing | Network components are inserted into `Core::networks` when their libraries are loaded; all component `onLoad` callbacks run later | [1.5.8.3134 `core_impl.hpp`](https://github.com/openmultiplayer/open.mp/blob/876f2be913517e8f2e07da27b0e57774c2192aaf/Server/Source/core_impl.hpp) | VERIFIED; CustomModel `onLoad` sees LegacyNetwork regardless of callback iteration order |
| Receive view | LegacyNetwork reads byte zero as the packet type, resets the stream to bit offset 8 before every global and per-ID handler, stops at the first `false`, and deallocates the RakNet packet after dispatch | [1.5.8.3134 LegacyNetwork receive loop](https://github.com/openmultiplayer/open.mp/blob/876f2be913517e8f2e07da27b0e57774c2192aaf/Server/Components/LegacyNetwork/legacy_network_impl.cpp) | VERIFIED source behavior |
| Player gate | The receive loop dispatches packet handlers only when `playerFromRakIndex[pkt->playerIndex]` is non-null | Same 1.5.8.3134 LegacyNetwork source | VERIFIED; early pre-ClientJoin packets do not reach components |
| Bitstream bounds | `GetReadOffset`, `GetNumberOfUnreadBits`, and `GetData` expose the bounded payload view without requiring cursor mutation | [1.5.8.3134 open.mp-network bitstream](https://github.com/openmultiplayer/open.mp-network/blob/dc3eac9d5dc30f96edcf4e7e64f33d8c241d49ff/bitstream.hpp) | VERIFIED API boundary; component compile-tested |
| Response send | `IPlayer::sendPacket` delegates to the peer's active network; the span length is in bits | [1.5.8.3134 SDK `player.hpp`](https://github.com/openmultiplayer/open.mp-sdk/blob/3ee7bc4ab20c22359c34c08c38f93815b44bffd5/include/player.hpp), [SDK `network.hpp`](https://github.com/openmultiplayer/open.mp-sdk/blob/3ee7bc4ab20c22359c34c08c38f93815b44bffd5/include/network.hpp) | VERIFIED API boundary; component compile-tested |
| Player lifecycle | The official player connect dispatcher supplies connect and disconnect callbacks; the component resets or erases tracker state there | Same SDK `player.hpp` | VERIFIED API boundary; host lifecycle-tested |

The server component does not deallocate network packets. Returning `false` only stops later handlers in the active dispatcher; the LegacyNetwork receive loop performs the one server-side deallocation regardless of handler result. A global handler returning `false` prevents the per-ID dispatcher from running at all.

No handler for packet ID `0x5E` exists in the reviewed open.mp `1.5.8.3134` source. Pawn.RakNet registers a global `NetworkInEventHandler`, not a per-ID `0x5E` handler. Its default path returns `true`, but a Pawn `OnIncomingPacket`/`IPacket` callback can return false and stop later dispatch. CustomModel's read-only observer runs at `EventPriority_Highest`, always returns true, and makes that case visible without changing packet data or ownership. There is no source evidence of an inherent component collision.

The resulting ELF32 component was loaded by the official open.mp `1.5.8.3079` Linux x86 release from signed commit `c6759bd8d265171ae3d86598895a23d5a8d92a3b`. The downloaded release archive matched the publisher's SHA-256 `5df708898cbbb97f6c299ad5c1ff663a52ad4d3d39b3d561f1fe08a32df424f4`; the server accepted the component and logged `server transport initialized`. The source compiles as ELF32 i386 against the exact `1.5.8.3134` SDK/network revisions. The `3134` component load and complete Hello/Welcome paths on exact R1 and R3-1 are RUNTIME-VERIFIED from maintainer tests.

## Phase 3 dependency evidence

Phase 3 adds no SA-MP address, vtable, calling-convention, Packet-layout, GTA, or RenderWare claim.

| Component | Fact used | Source | Status |
|---|---|---|---|
| WinHTTP | Native Windows HTTP client API with HTTP/HTTPS support | [Microsoft WinHTTP overview](https://learn.microsoft.com/en-us/windows/win32/winhttp/about-winhttp) | VERIFIED platform API |
| WinHTTP | `WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS` bounds automatic redirects; redirect policy can disallow HTTPS-to-HTTP | [Microsoft WinHTTP option flags](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags) | VERIFIED platform API |
| WinHTTP | Secure requests use Schannel certificate validation unless applications explicitly set insecure flags; CustomModel sets none | Microsoft WinHTTP documentation and implemented default behavior | VERIFIED design boundary; live TLS endpoint not tested in this repository |
| PicoSHA2 | Header-only SHA-256 under MIT license | [PicoSHA2 repository at pinned commit](https://github.com/okdshin/PicoSHA2/tree/161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29) | PUBLIC-REFERENCE; standard `abc` vector host-tested |
| Wine 11.17 WinHTTP | Session option handler implements redirect policy but not `MAX_HTTP_AUTOMATIC_REDIRECTS`; the request handler implements the redirect-count option | [Wine 11.17 `session.c`](https://gitlab.winehq.org/wine/wine/-/blob/wine-11.17/dlls/winhttp/session.c) | VERIFIED source behavior and RUNTIME-VERIFIED in the GTA Wine prefix |
| Wine 11.17 WinHTTP | Session-scope redirect count fails with error 12009; request-scope succeeds and both fixture downloads return exact bytes | Project WinHTTP scope probe and `CustomModelWinHttpProbe` | RUNTIME-VERIFIED on Wine 11.17 |

### Other reviewed references

[samp-compat](https://github.com/AGraber/samp-compat/tree/7f4a59ef3c74811664bb2d3de615d66715dc7d05) documents server-side SA-MP 0.3.7 R2 and 0.3.DL RPC handling and is useful for protocol behavior. Its addresses belong to the server executable, not the client `samp.dll`, so none was copied into the client registry. [chandling's hook implementation](https://github.com/dotSILENT/chandling/blob/6ade726e174fe3fc1793d96791f1b32adc1d4b33/chandling/Hooks.cpp) demonstrates a pattern-based RakClient `Receive` hook across its source-labelled revisions, but does not provide the per-entry-point proof required by this project. The reviewed SAMP-API multiversion tree contains R1 and R3-1 client mappings, not R2 or R4 client APIs. [OpenMPPlus](https://github.com/Rohatcengizhanbucak/OpenMPPlus/tree/7cee168fe2279e643d99c5c9689337c4a2c80d8d) adds an exact `0x0CBCB0` code profile, but its R4 path uses registered RPC transport and is excluded from its published release matrix; it does not validate this project's raw Receive/deallocation path.

## UNVERIFIED

- The exact R4 or R4-2 identity of the runtime-observed `0x0CBCD0` binary.
- The PE entry point for plain 0.3.7 R3, if such a separately distributed binary is in scope.
- Whether additional minor binaries share a marketing revision name but have different PE entry points.
- R2 and `0x0CBCD0` R4-family RakClient resolution, vtable identity, and transport ABI. These builds remain disabled.
- Runtime proof of raw Send slot 7, Receive slot 8, DeallocatePacket slot 9, IsConnected slot 18, packed Packet layout, and ownership behavior on the exact `0x0CBCB0` binary. Its public `CNetGame*` and member-pointer path is insufficient to enable transport.
- All RPC handler RVAs; Phase 2B does not need or populate them.
- Phase 3 WinHTTP download, cache-hit reconnect, corruption replacement, and AssetReady reporting on exact R1/R3-1 clients. These paths are host-tested with a fake fetch backend but not yet runtime verified in GTA.
- Coexistence and shutdown ordering when another ASI hooks the same `Receive` slot. Runtime hot-unload after another hook chains through CustomModel is unsupported.

## TODO

1. Record cryptographic hashes for each runtime-tested `samp.dll` so entry points remain tied to exact binaries.
2. Independently identify whether the `0x0CBCD0` binary is R4, R4-2, or another R4-family distribution before changing its label.
3. Obtain and runtime-test the `0x0CBCB0` public-reference binary, including the complete raw Receive/deallocation path, before enabling it.
4. Run the fixture manifest live on R1 and R3-1 and record download, verification, cache-hit reconnect, and server AssetReady logs.
5. Repeat with one intentionally corrupted response and confirm bounded AssetError behavior.
6. If the global observer logs but the per-ID handler does not, audit the live Pawn.RakNet Pawn callbacks for a false return on packet `0x5E`.
7. Record component and `samp.dll` hashes used by those runtime tests.

## Reference order

Use these sources in order for future targeted work:

1. BlastHackNet/SAMP-API;
2. AGraber/samp-compat;
3. dotSILENT/chandling and its version database;
4. public SA-MP offset collections and multi-version ASI projects;
5. GTA SA plugin-sdk and RenderWare reverse-engineering projects;
6. targeted analysis of the exact binary that blocks the next feature.

R5 architecture may be studied, but R5 production support remains out of scope.
