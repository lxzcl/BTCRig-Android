# BTCRig Android

BTCRig Android is the Android client for BTCRig. It runs CPU / OpenCL mining on Android devices and includes local Benchmark and leaderboard support.

## Features

- CPU mining
- OpenCL / GPU mining
- CPU + GPU mixed mining
- Stratum TCP / TLS pool support
- Official and unknown certificate compatibility modes
- Background mining and keep-awake mode
- Local Benchmark
- Verified Benchmark score upload
- English and Chinese UI

## Usage

1. Download the latest APK from [Releases](https://github.com/lxzcl/BTCRig-Android/releases).
2. Install and open BTCRig.
3. Open Settings and configure pool URL, wallet address, password, CPU threads, and OpenCL options.
4. Return to Home and tap the status capsule to start or stop mining.
5. Open Info to view logs, run Benchmark, or upload a score.
6. Open Rank to view hashrate rankings by device and backend.

## Defaults

- Default pool: `stratum+tcp://public-pool.io:3333`
- CPU mining is enabled by default
- OpenCL availability depends on the device GPU and system driver
- Donation ratio defaults to `1%`; set it to `0%` to disable donation

## Build

```bash
./gradlew :app:assembleDebug
```

Debug APK output:

```text
app/build/outputs/apk/debug/app-debug.apk
```

Release APKs are built by GitHub Actions and published to Releases.

## License

BTCRig Android is licensed under GPL-3.0. See [LICENSE](LICENSE).
