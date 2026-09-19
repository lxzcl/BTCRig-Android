<div align="center">

# BTCRig Android

**Android client for BTCRig: SHA256d mining on CPU and GPU, an optional multi-algorithm XMRig CPU engine, local benchmarks, and a verified leaderboard.**

[Releases](https://github.com/lxzcl/BTCRig-Android/releases)

![Release](https://img.shields.io/github/v/release/lxzcl/BTCRig-Android?style=for-the-badge&color=00b894)
![Platform](https://img.shields.io/badge/platform-Android%205.0%2B-00b894?style=for-the-badge)
![License](https://img.shields.io/badge/license-GPL--3.0-00b894?style=for-the-badge)

</div>

BTCRig Android runs on Android 5.0+ devices with arm64-v8a or armeabi-v7a and ships two independent mining engines. The BTCRig engine mines SHA256d (BTC) on CPU and OpenCL GPUs; the optional XMRig engine (Android 7.0+ / arm64-v8a) mines multi-algorithm CPU workloads and can run alongside GPU BTC mining so both processors stay busy.

## Engines

| Engine | Algorithms | Backends | Pools |
| --- | --- | --- | --- |
| BTCRig | SHA256d | CPU, OpenCL GPU, CPU + GPU | stratum+tcp, stratum+tls, certificate compatibility mode |
| XMRig (optional) | MoneroOcean XMRig 6.26.0 multi-algorithm | CPU only | stratum+tcp, stratum+tls |
| XMRig + GPU companion | XMRig on CPU, SHA256d on OpenCL GPU | CPU + GPU | Two pools, one per engine |

## Features

- CPU mining, OpenCL / GPU mining, and mixed CPU + GPU mining
- Optional XMRig CPU engine with pool-selected or fixed algorithms and on-device algorithm calibration
- Optional GPU BTC companion mining while XMRig mines on the CPU
- Stratum TCP and TLS pools, with compatibility mode for unknown certificates
- Local Benchmark with signed results, verified score upload, and a leaderboard by device and backend
- Foreground service with keep-awake, background stability handling, and auto-start when the app opens
- Optional Home app mode
- English and Chinese UI

## Usage

1. Download the latest APK from [Releases](https://github.com/lxzcl/BTCRig-Android/releases).
2. Install and open BTCRig.
3. Open Settings and configure the engine, pool URL, wallet address, password, threads, and GPU options.
4. Return to Home and tap the status capsule to start or stop mining.
5. Open Info to view logs, run Benchmark, or upload a score.
6. Open Rank to view hashrate rankings by device and backend.

## Defaults

- BTCRig pool: `stratum+tcp://public-pool.io:3333`
- XMRig pool: `stratum+tcp://gulf.moneroocean.stream:10004`
- CPU mining is enabled by default; OpenCL availability depends on the device GPU and system driver
- The XMRig engine requires Android 7.0+ on arm64-v8a

## Build

```bash
git submodule update --init --recursive
./gradlew :app:assembleDebug
```

The first build compiles the bundled XMRig, libuv, and OpenSSL sources for arm64-v8a. Debug APK output:

```text
app/build/outputs/apk/debug/app-debug.apk
```

Release APKs are built by GitHub Actions and published to Releases.

## Project layout

| Area | Files |
| --- | --- |
| UI (Compose) | `app/src/main/java/com/btcrig/android/ModernActivity.kt`, `ModernUi.kt`, `ModernModels.kt`, `ModernUtils.kt` |
| Service and config | `BtcrigService.java`, `BtcrigConfig.java`, `XmrigRunner.java` |
| Native core | `app/src/main/cpp/btcrig_core_full.c`, `app/src/main/cpp/btcrig/*.c` |
| Third-party sources | `app/src/main/cpp/third_party/xmrig`, `third_party/libuv` |
| Build | `scripts/build-xmrig-android.sh`, `.github/workflows` |

## License

BTCRig Android is licensed under GPL-3.0. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The app includes a 1% developer donation by default, adjustable in Settings; the XMRig engine keeps a 1% minimum. BTC: `bc1qqz0wutk9kk5mmaf7fu4dm5w4fq4fhaah9hpzr3`
