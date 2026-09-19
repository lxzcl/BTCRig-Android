# Third-party software

BTCRig Android bundles the following software:

- [MoneroOcean XMRig 6.26.0-mo5](https://github.com/MoneroOcean/xmrig/tree/0ce9664d3f540b72624e3733a592ddfdf02662a5), licensed under the GNU General Public License v3.0. Its source is included as the `app/src/main/cpp/third_party/xmrig` submodule.
- [libuv 1.51.0](https://github.com/libuv/libuv/tree/v1.51.0), licensed under the MIT License. Its source and license notice are included as the `app/src/main/cpp/third_party/libuv` submodule.

The exact source revisions and Android build instructions are recorded by `.gitmodules`, the submodule commits, and `scripts/build-xmrig-android.sh`.

Local modification: `scripts/xmrig-disable-donate.patch` disables the built-in XMRig/MoneroOcean developer donation path (`Pools::load` forces a 0% donate level). The build script applies this patch to a copy of the submodule source before compiling.
