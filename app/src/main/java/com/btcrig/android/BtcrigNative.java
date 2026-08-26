package com.btcrig.android;

final class BtcrigNative {
    static {
        System.loadLibrary("btcrig_core");
    }

    private BtcrigNative() {
    }

    static native String backendName();

    static native boolean selfTest();

    static native boolean start(String configPath);

    static native void stop();

    static native boolean isRunning();

    static native int workerCount();

    static native long totalHashes();

    static native boolean stratumConnected();

    static native long stratumJobs();

    static native String pool();

    static native String stratumStatus();

    static native String lastError();

    static native double hashrate();

    static native double benchmarkCpu(int seconds, int threads);
}
