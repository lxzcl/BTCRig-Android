# BTCRig Android

Android wrapper/runtime for BTCRig.

## Current target

- Modern APK: Android 5.0+ (`minSdk 21`), `compileSdk 36`, ARMv8/`arm64-v8a`.
- Legacy APK baseline in this Gradle project: Android 5.0+ (`minSdk 21`), `armeabi-v7a`, portable CPU path. Android 4.x needs a separate old-NDK build path before claiming support.
- Modern UI uses Jetpack Compose + Material3 only. Miuix is intentionally not included.
- Legacy UI keeps the native Java View shell.
- Native core boundary: JNI calls `btcrig_core` for start/stop/status/self-test and benchmark.
- Native core imports BTCRig miner/Stratum sources, vendored Jansson JSON parsing, ARMv8 SHA2 CPU path, and the OpenCL runtime loader/miner path.
- Default config is copied to the app private files directory as `config.json`; `"cpu.threads": 0` means auto and `"opencl.enabled": true` tries vendor OpenCL at runtime.
- The APK declares optional `libOpenCL.so` access so Android linker namespaces can expose vendor OpenCL when the device publishes it.
- The modern UI is split into Home / Settings / Info pages with Material3 cards/navigation for core status, hashrate, OpenCL visibility, pool/share status, config summary, and log viewing.
- The modern Home page uses a compact hashrate-first layout with a clickable status pill and subtle breathing glow.
- The Settings page exposes pool/user/password/CPU/OpenCL directly; `config.json` is kept under Advanced JSON. Form saves preserve other JSON fields. Stop the service before saving changes.
- `"cpu.threads": 0` means CPU mining disabled in the Android shell; set a positive number to use CPU workers.
- Native stdout/stderr is appended to `files/btcrig.log` with one rotated `btcrig.log.1`.
- Stratum V1 TCP currently connects, subscribes, authorizes, parses `set_difficulty`/`notify`, builds jobs, and submits shares through the imported core.
- TLS Stratum is not linked in this APK yet; use `stratum+tcp://` pools for now.
- Legacy Android 4.x support will be a separate target because it needs an old NDK/toolchain path.

## Build

```bash
source /home/xxx/def/android-dev/env.sh
gradle :app:assembleModernDebug
```

Modern debug APK:

```text
app/build/outputs/apk/modern/debug/app-modern-debug.apk
```

Legacy armv7 debug APK:

```text
app/build/outputs/apk/legacy/debug/app-legacy-debug.apk
```
