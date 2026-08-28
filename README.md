# BTCRig Android

Android app shell for BTCRig with native CPU/OpenCL mining support.

## Status

- Version: `0.1.0`
- Android: `minSdk 21`, `targetSdk 36`, `compileSdk 36`
- ABI: `arm64-v8a`, `armeabi-v7a`
- UI: Jetpack Compose / Material3
- Native core: JNI wrapper around imported BTCRig miner, Stratum V1, CPU backends, Jansson config parsing, and vendor OpenCL runtime loading.
- Config: copied to app private storage as `config.json`; normal settings are exposed in-app, advanced JSON is still available.
- Donation: default `1%`, same pool, donation period only swaps the wallet/user address.

## Build

```bash
./gradlew :app:assembleDebug
```

Debug APK:

```text
app/build/outputs/apk/debug/app-debug.apk
```

## GitHub Actions

`Android Test Build` runs on every `main` push and pull request.

`Android Release` publishes a signed APK directly to GitHub Releases when `VERSION` changes on `main`.
