package com.btcrig.android;

final class BtcrigNative {
    static {
        System.loadLibrary("btcrig_core");
    }

    private BtcrigNative() {
    }

    static native String backendName();

    static native boolean selfTest();

    static native boolean start();

    static native void stop();

    static native boolean isRunning();

    static native double benchmarkCpu(int seconds, int threads);
}
