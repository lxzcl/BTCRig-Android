# BTCRig Android

Android wrapper/runtime for BTCRig.

## Current target

- Modern APK first: Android 5.0+ (`minSdk 21`), `compileSdk 36`.
- Keep the first shell small: native Android UI, foreground service, no extra UI framework.
- First native bridge: JNI CPU SHA256d benchmark using the BTCRig fast-c path shape, without OpenSSL/Jansson.
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
