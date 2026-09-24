# CustomModel

> Work in Progress / Experimental

CustomModel is an experimental compatibility layer for bringing
SA-MP 0.3.DL-style custom model functionality to older SA-MP clients.

Current status:
- R1 transport: working
- R3-1 transport: working
- CMOD protocol: working
- HTTP/SHA-256/cache pipeline: working
- open.mp server component: working
- Artwork registry foundation: working
- GTA PED runtime: experimental/incomplete
- Custom PED rendering: not production-ready
- R2/R4: unsupported (progress)

## Runtime proof

The following logs were captured from live tests and show the parts of
CustomModel that are currently working.

### SA-MP 0.3.7 R1

```
[CustomModel] SA-MP 0.3.7 R1 detected
[CustomModel] RakClient resolved
[CustomModel] transport initialized
[CustomModel] RakNet connection established
[CustomModel] Hello sent
[CustomModel] Welcome received
[CustomModel] handshake complete
[CustomModel] manifest revision 3 received
[CustomModel] manifest contains 2 assets
[CustomModel] asset 1001 cache hit
[CustomModel] asset 1001 ready
[CustomModel] asset 1002 cache hit
[CustomModel] asset 1002 ready
```

Server side:
```
[CustomModel] Hello received from player 0
[CustomModel] build SA-MP 0.3.7 R1 accepted for player 0
[CustomModel] Welcome sent to player 0
[CustomModel] manifest revision 3 sent to player 0 (2 assets)
[CustomModel] asset 1001 ready for player 0
[CustomModel] asset 1002 ready for player 0
```
### SA-MP 0.3.7 R3-1
```
[CustomModel] SA-MP 0.3.7 R3-1 detected
[CustomModel] RakClient resolved
[CustomModel] transport initialized
[CustomModel] RakNet connection established
[CustomModel] Hello sent
[CustomModel] Welcome received
[CustomModel] handshake complete
[CustomModel] manifest revision 3 received
[CustomModel] manifest contains 2 assets
[CustomModel] asset 1001 downloading
[CustomModel] asset 1001 verifying
[CustomModel] asset 1001 verified
[CustomModel] asset 1001 ready
[CustomModel] asset 1002 downloading
[CustomModel] asset 1002 verifying
[CustomModel] asset 1002 verified
[CustomModel] asset 1002 ready
```
### Server side:
```
[CustomModel] Hello received from player 0
[CustomModel] build SA-MP 0.3.7 R3-1 accepted for player 0
[CustomModel] Welcome sent to player 0
[CustomModel] manifest revision 3 sent to player 0 (2 assets)
[CustomModel] asset 1001 ready for player 0
[CustomModel] asset 1002 ready for player 0
Verified so far
R1 handshake
R3-1 handshake
CMOD transport
manifest delivery
HTTP asset download
SHA-256 verification
content-addressed cache
cache-hit reuse
AssetReady reporting
open.mp server component
Not complete yet
```
The GTA PED runtime is still experimental.

Custom PED rendering, TXD/DFF runtime integration, R3-1 PED application,
download overlay rendering, and full 0.3.DL artwork interoperability are
not yet production-ready.
