# BTCRig Android

Android wrapper/runtime for BTCRig.

## Current target

- Modern APK first: Android 5.0+ (`minSdk 21`), `compileSdk 36`, ARMv8/`arm64-v8a` only.
- Keep the first shell small: native Android UI, foreground service, no extra UI framework.
- Native core boundary: JNI calls `btcrig_core` for start/stop/status/self-test, CPU SHA256d miner loop, Stratum status/share counters, and benchmark.
- Default config is copied to the app private files directory as `config.json`; `"cpu_threads": 0` means auto.
- Stratum V1 currently connects, subscribes, authorizes, parses `set_difficulty`/`notify`, builds CPU jobs, and submits CPU shares.
- Legacy Android 4.x support will be a separate target because it needs an old NDK/toolchain path.

## Build

```bash
source /home/xxx/def/android-dev/env.sh
gradle :app:assembleDebug
```

Debug APK:

```text
app/build/outputs/apk/debug/app-debug.apk
```
