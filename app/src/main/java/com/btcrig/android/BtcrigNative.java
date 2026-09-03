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

    static native long stratumSubmits();

    static native long stratumAccepts();

    static native long stratumRejects();

    static native String pool();

    static native String stratumStatus();

    static native String lastError();

    static native String openclStatus(String configPath);

    static native double hashrate();

    static native double benchmarkCpu(int seconds, int threads);

    static native String benchmarkCpuChallenge(String seed, int seconds, int threads, double proofDifficulty);

    static native double benchmarkCpuBackend(String backend, int seconds, int threads);

    static native double benchmarkOpencl(String configPath, int seconds);

    static native String benchmarkOpenclChallenge(String configPath, String seed, int seconds, double proofDifficulty);

    static native double benchmarkCpuGpu(String configPath, int seconds, int threads);

    static native String benchmarkCpuGpuChallenge(String configPath, String seed, int seconds, int threads, double proofDifficulty);
}
