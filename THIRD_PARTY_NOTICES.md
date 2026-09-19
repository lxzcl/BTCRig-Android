# Third-party software

BTCRig Android bundles the following software:

- [MoneroOcean XMRig 6.26.0-mo5](https://github.com/MoneroOcean/xmrig/tree/0ce9664d3f540b72624e3733a592ddfdf02662a5), licensed under the GNU General Public License v3.0. Its source is included as the `app/src/main/cpp/third_party/xmrig` submodule.
- [libuv 1.51.0](https://github.com/libuv/libuv/tree/v1.51.0), licensed under the MIT License. Its source and license notice are included as the `app/src/main/cpp/third_party/libuv` submodule.
- [OpenSSL 3.5.8](https://github.com/openssl/openssl/releases/tag/openssl-3.5.8), licensed under the Apache License 2.0. `scripts/build-xmrig-android.sh` downloads the pinned source tarball (verified by SHA-256) and builds static `libssl`/`libcrypto` for Android arm64-v8a to enable XMRig TLS support.

The exact source revisions and Android build instructions are recorded by `.gitmodules`, the submodule commits, and `scripts/build-xmrig-android.sh`.

Local modification: `scripts/xmrig-donate.patch` replaces the built-in XMRig/MoneroOcean developer donation with the BTCRig donation: the donation strategy mines on the configured pool with the BTCRig donation wallet, at a minimum level of 1%. The build script applies this patch to a copy of the submodule source before compiling.
