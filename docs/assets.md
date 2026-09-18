# Phase 3 assets

Phase 3 distributes and verifies opaque DFF/TXD files. It does not parse them or register them with GTA.

## Responsibilities

The server registry validates asset metadata, assigns a monotonically advancing in-process manifest revision, and emits entries in ascending asset-ID order. The open.mp adapter publishes the stream only after the R1/R3-1 handshake succeeds. It records Ready/Error reports per player and clears all state on disconnect.

The client validates the complete stream before queueing work. One worker checks the cache, downloads misses, enforces size, hashes the temporary file, and commits it. The RakNet thread drains bounded result records and sends AssetReady or AssetError. The worker never calls SA-MP, GTA, or RenderWare.

## Limits

| Limit | Value |
|---|---:|
| Assets per manifest | 256 |
| Logical name | 96 bytes |
| URL | 2048 bytes |
| Individual file | 64 MiB |
| Total announced files | 512 MiB |
| Total manifest metadata | 1 MiB |
| CMOD packet including header | 4096 bytes |
| HTTP redirects | 5 |
| Connection/read timeout | 10 seconds per WinHTTP operation |
| Overall transfer deadline | 120 seconds |

All incoming lengths, counts, enum values, revisions, indexes, and totals are validated before use. The announced file size controls both HTTP output and later filesystem verification. The downloader stops before writing a byte beyond that size.

## Registry configuration

The open.mp component looks for `custommodel-assets.cfg` in the server working directory. The file has six whitespace-separated fields per non-comment line:

```text
asset_id TYPE logical_name URL file_size sha256
```

Example using the development fixtures:

```text
1 DFF dev/test_asset_dff http://127.0.0.1:8000/test_asset.dff 40 5724024f46ee4b392f3bf1482eb985f5bef00a1893f47b5a7cdd9d3893d0435d
2 TXD dev/test_asset_txd http://127.0.0.1:8000/test_asset.txd 40 57e66088326fbee6bd2258cfd991bf9cb9fdc6d9b60e9d1676df44a320fefb70
```

TYPE is exactly `DFF` or `TXD`. The digest is exactly 64 lowercase hexadecimal characters. URLs or names containing whitespace cannot be represented by this intentionally small development configuration format. A malformed line prevents the component from registering its network handlers rather than publishing a partial manifest.

Registry revisions start at 1 and advance after every successful add, remove, or nonempty clear. They identify the manifest content for one component process; they are not derived from filesystem timestamps and are not promised to persist across server restarts.

## Cache layout

The cache is relative to `CustomModel.asi`:

```text
CustomModel/cache/
├── sha256/
│   └── 57/
│       ├── 572402...d0435d.dff
│       └── 57e660...efb70.txd
└── tmp/
    └── <sha256>.<type>.tmp
```

Server logical names and URL paths never become filesystem paths. A final path consists only of a lowercase digest, a two-character digest prefix, and a fixed `.dff` or `.txd` suffix selected from the validated asset type. This prevents absolute-path and traversal escapes.

Every cache lookup verifies regular-file status, exact size, and SHA-256. A corrupt entry is removed and treated as a miss. A download is written only to the fixed temporary directory, then checked in this order:

```text
transfer complete
-> exact announced size
-> SHA-256 match
-> rename within the cache filesystem
-> AssetReady
```

Failure removes the temporary file and emits one bounded AssetError. A second asset ID with the same type and digest finds the committed entry and avoids another request.

## HTTP policy

The Windows client uses synchronous WinHTTP on its one worker thread. Only HTTP and HTTPS URLs pass shared validation. Local paths, UNC paths, `file:`, `ftp:`, `data:`, embedded credentials, fragments, and backslashes are rejected. HTTPS certificate validation uses Windows defaults; certificate errors are not bypassed. Automatic redirects are capped and HTTPS-to-HTTP downgrade redirects are disallowed.

The redirect policy is configured on the WinHTTP session. The maximum redirect count is configured on the request handle. Microsoft supports the latter option on either handle, but Wine 11.17 returns `ERROR_WINHTTP_INVALID_OPTION` (`12009`) for the session form and implements the request form. Moving the option does not change its value or make it optional. Wine does not implement `WINHTTP_OPTION_REJECT_USERPWD_IN_URL`; that additional defense is best-effort because shared URL validation already rejects credentials before WinHTTP. Its absence does not disable certificate validation or permit an HTTPS-to-HTTP redirect.

Every failing WinHTTP call records its operation, numeric `GetLastError()` value, and bounded `FormatMessage` text when the platform provides it. Parsed request diagnostics include scheme, host, port, path, and secure state, but exclude query strings.

WinHTTP is a Windows system API, so the ASI gains a `WINHTTP.dll` system import but no redistributable runtime DLL. The implementation never invokes curl, wget, PowerShell, or another process.

## Session behavior

Disconnect and reconnect advance a client session generation and clear queued work, events, manifest state, and per-ID states. A transfer already inside WinHTTP is not forcibly killed, but its generation is stale: it cannot report to the new server session or change its states. Any valid committed content remains reusable because the cache is content-addressed and reverified.

Server player state follows:

```text
HandshakePending -> Compatible -> ManifestSending -> ManifestComplete -> AssetsReady
                                                            \-> asset failure recorded
```

An empty manifest becomes AssetsReady immediately. Duplicate Ready messages are idempotent. A later Error for an expected asset removes that asset from the ready set.

## Dependency choice

PicoSHA2 is vendored at commit `161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29` under its MIT license. It removes the risk of creating an ad-hoc cryptographic implementation and adds no binary dependency. WinHTTP removes the need to ship libcurl and a separate TLS stack while retaining OS proxy and certificate handling.

## Explicit exclusions

Phase 3 does not add model packages, DFF/TXD parsing, model IDs, custom skins, custom objects, GTA streaming, RenderWare hooks, HTTP execution, Pawn natives, or RPC 179 emulation. Cached bytes are inert until a separately verified Phase 4 loader consumes them.
