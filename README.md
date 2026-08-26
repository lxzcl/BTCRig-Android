# BTCRig Android

Android wrapper/runtime for BTCRig.

## Current target

- Modern APK first: Android 5.0+ (`minSdk 21`), `compileSdk 36`, ARMv8/`arm64-v8a` only.
- Keep the first shell small: native Android UI, foreground service, no extra UI framework.
- Native core boundary: JNI calls `btcrig_core` for start/stop/status/self-test and benchmark.
- Native core imports BTCRig miner/Stratum sources, vendored Jansson JSON parsing, ARMv8 SHA2 CPU path, and the OpenCL runtime loader/miner path.
- Default config is copied to the app private files directory as `config.json`; `"cpu.threads": 0` means auto and `"opencl.enabled": true` tries vendor OpenCL at runtime.
- Stratum V1 TCP currently connects, subscribes, authorizes, parses `set_difficulty`/`notify`, builds jobs, and submits shares through the imported core.
- TLS Stratum is not linked in this APK yet; use `stratum+tcp://` pools for now.
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
