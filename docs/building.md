# Building CustomModel

## Requirements on Arch Linux

Install CMake, Ninja, and a 32-bit MinGW-w64 compiler that provides `i686-w64-mingw32-g++` and `i686-w64-mingw32-windres`. The common Arch package names are:

```sh
sudo pacman -S --needed cmake ninja mingw-w64-gcc
```

Confirm that `i686-w64-mingw32-g++ --version` works before configuring. The toolchain file fails during configuration if that compiler is absent.

## Cross-compile the ASI

From the repository root:

```sh
cmake -S . -B build-mingw32 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF
cmake --build build-mingw32
```

The result is:

```text
build-mingw32/bin/CustomModel.asi
```

The CMake project rejects a client build when the selected toolchain is not Windows or uses pointers wider than 32 bits. The client source also contains a 32-bit compile-time assertion.

To inspect the artifact on Linux:

```sh
file build-mingw32/bin/CustomModel.asi
i686-w64-mingw32-objdump -f build-mingw32/bin/CustomModel.asi
```

The reported architecture must be PE i386.

The MinGW build statically links its GCC, libstdc++, and winpthreads runtime dependencies. Confirm the final import table with:

```sh
i686-w64-mingw32-objdump -p \
  build-mingw32/bin/CustomModel.asi \
  | grep "DLL Name"
```

The result must not include `libwinpthread-1.dll`, `libstdc++-6.dll`, or a `libgcc` runtime DLL. Windows system DLL and API-set imports are expected.

## Run host tests

The PE parser, build registry, wire protocol, server handshake state, manifest registry, cache, SHA-256, and downloader boundary can be tested without MinGW or internet access:

```sh
cmake -S . -B build-host -G Ninja \
  -DCUSTOMMODEL_BUILD_CLIENT=OFF \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## Install in a game directory

Copy `CustomModel.asi` into the GTA San Andreas directory used by an ASI loader. On startup, logs are written relative to the module location:

```text
CustomModel/logs/custommodel.log
```

The runtime detects the SA-MP revision and initializes the shared protocol. R1 and R3-1 select the runtime-verified RakNet adapter and initialize a single asset worker. R2 and both R4 records select the unavailable transport. DFF/TXD bytes can be downloaded and cached, but no build loads them into GTA.

Phase 3 uses the Windows WinHTTP system library for HTTP/TLS and a pinned header-only PicoSHA2 implementation for hashing. It does not require libcurl, OpenSSL, or another deployable DLL. `WINHTTP.dll` is therefore an expected Windows system import.

## Build the open.mp component

The component uses two small official dependencies because the stable SDK supplies component/player events while `open.mp-network` supplies the `NetworkBitStream` view used by packet callbacks. They are not vendored, fetched automatically, or used by the ASI. Use the reviewed commits:

```sh
git clone https://github.com/openmultiplayer/open.mp-sdk.git dependencies/open.mp-sdk
git -C dependencies/open.mp-sdk checkout 3ee7bc4ab20c22359c34c08c38f93815b44bffd5
git -C dependencies/open.mp-sdk submodule update --init --recursive

git clone https://github.com/openmultiplayer/open.mp-network.git dependencies/open.mp-network
git -C dependencies/open.mp-network checkout dc3eac9d5dc30f96edcf4e7e64f33d8c241d49ff
```

The official Linux release is x86, so build a matching 32-bit component. On Arch this also requires the normal multilib C++ development runtime:

```sh
cmake -S . -B build-openmp -G Ninja \
  -DCUSTOMMODEL_BUILD_CLIENT=OFF \
  -DCUSTOMMODEL_BUILD_OPENMP_COMPONENT=ON \
  -DCUSTOMMODEL_OMP_SDK_DIR="$PWD/dependencies/open.mp-sdk" \
  -DCUSTOMMODEL_OMP_NETWORK_DIR="$PWD/dependencies/open.mp-network" \
  -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS=-m32 \
  -DCMAKE_SHARED_LINKER_FLAGS=-m32 \
  -DCMAKE_EXE_LINKER_FLAGS=-m32
cmake --build build-openmp
```

The output is `build-openmp/server/components/CustomModelServer.so` on Linux or `CustomModelServer.dll` on Windows. Copy it into the open.mp server's `components` directory. The target is opt-in so configuring the normal host or MinGW build never needs these SDKs.

`open.mp-network` is explicitly non-stable. At load time the component compares its compiled bitstream ABI version with the server core and refuses to register the packet handler on a mismatch. Rebuild the component against the network revision required by that server rather than bypassing this check.

The 32-bit component was load-tested with official open.mp `1.5.8.3079` (`c6759bd8d265171ae3d86598895a23d5a8d92a3b`). The Phase 2B component passed its bitstream ABI check and initialized. The current source builds as ELF32 i386 against the SDK and network commits embedded by open.mp `1.5.8.3134` source commit `876f2be913517e8f2e07da27b0e57774c2192aaf`. Exact R1 and R3-1 clients have completed the corrected Hello/Welcome handshake. Phase 3 still needs the live asset test below.

Expected client send diagnostics are:

```text
[CustomModel:debug] CMOD bytes=26
[CustomModel:debug] transport bytes=27
[CustomModel:debug] RakNet Send length=27
[CustomModel:debug] preview=5E 43 4D 4F 44 00 01 00 01 00 00 00 0E ...
```

Expected server handshake diagnostics are:

```text
[CustomModel] server transport initialized
[CustomModel:debug] carrier observed before per-packet dispatch
[CustomModel:debug] remaining bits=208
[CustomModel:debug] remaining bytes=26
[CustomModel:debug] read offset=8
[CustomModel:debug] packet handler invoked
[CustomModel] Hello received from player 0
[CustomModel] build SA-MP 0.3.7 R1 accepted for player 0
[CustomModel] Welcome sent to player 0
```

The equivalent R3-1 line names `SA-MP 0.3.7 R3-1`. The client then logs `Welcome received` and `handshake complete`.

## Manual Phase 3 live test

The repository includes two 40-byte opaque fixtures. They exercise transport, HTTP, integrity, and caching; they are not valid GTA models.

1. Start a development HTTP server from the repository root:

   ```sh
   python -m http.server 8000 --directory tests/fixtures/assets
   ```

2. Copy `tests/fixtures/custommodel-assets.cfg` to the open.mp server working directory as `custommodel-assets.cfg`. If the game runs on another host, replace `127.0.0.1` with a reachable HTTP address without changing the fixture sizes or hashes.
3. Install the rebuilt `CustomModelServer.so` and `CustomModel.asi`, then start open.mp 1.5.8.3134.
4. Connect first with the exact R1 client and then with the exact R3-1 client.

Expected server progression for either client is:

```text
[CustomModel] Welcome sent to player 0
[CustomModel] manifest revision 3 sent to player 0 (2 assets)
[CustomModel] asset 1 ready for player 0
[CustomModel] asset 2 ready for player 0
```

The example revision is 3 because a fresh registry begins at 1 and each of its two registrations advances it once. Expected client progression is:

```text
[CustomModel] handshake complete
[CustomModel] manifest revision 3 received
[CustomModel] manifest contains 2 assets
[CustomModel] asset 1 queued
[CustomModel] asset 1 downloading
[CustomModel] asset 1 verified
[CustomModel] asset 1 ready
```

Asset 2 follows the same sequence. Verify that these files exist:

```text
CustomModel/cache/sha256/57/5724024f46ee4b392f3bf1482eb985f5bef00a1893f47b5a7cdd9d3893d0435d.dff
CustomModel/cache/sha256/57/57e66088326fbee6bd2258cfd991bf9cb9fdc6d9b60e9d1676df44a320fefb70.txd
```

Reconnect without deleting them. The client should log `cache hit` and Ready without another HTTP request. Then alter one cached file and reconnect; the lookup should remove it, redownload it, verify it, and report Ready. To test failure, serve bytes with the wrong size or hash and confirm AssetError on both client and server without a final cache file.

## Wine WinHTTP probe

The development-only probe compiles the same `winhttp_fetch.cpp` used by the ASI. It is disabled by default and is not a release artifact:

```sh
cmake -S . -B build-mingw32 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DCUSTOMMODEL_BUILD_WINHTTP_PROBE=ON
cmake --build build-mingw32

WINEPREFIX=/path/to/gta-prefix WINEDEBUG=-all \
  wine build-mingw32/tools/CustomModelWinHttpProbe.exe \
  http://127.0.0.1:8000/test_asset.dff \
  winhttp-probe.tmp \
  40
```

Expected result:

```text
[CustomModel:debug] WinHTTP request scheme=http host=127.0.0.1 port=8000 path=/test_asset.dff secure=false
fetch succeeded bytes=40 sha256=5724024f46ee4b392f3bf1482eb985f5bef00a1893f47b5a7cdd9d3893d0435d
```

Wine 11.17 may additionally report that its optional `REJECT_USERPWD_IN_URL` option is unavailable. That does not fail the request because CustomModel rejects credential-bearing manifest URLs itself. Any other diagnostic should name the exact failed WinHTTP operation and error number.
