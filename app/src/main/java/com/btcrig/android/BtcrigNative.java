package com.btcrig.android;

final class BtcrigNative {
    static {
        System.loadLibrary("btcrig_core");
    }

    private BtcrigNative() {
    }

    static native String backendName();

    static native boolean selfTest();

    static native double benchmarkCpu(int seconds, int threads);
}
