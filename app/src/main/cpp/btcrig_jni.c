#include "btcrig_core.h"

#include <jni.h>

JNIEXPORT jstring JNICALL
Java_com_btcrig_android_BtcrigNative_backendName(JNIEnv *env, jclass ignored) {
    (void)ignored;
    return (*env)->NewStringUTF(env, btcrig_core_backend_name());
}

JNIEXPORT jboolean JNICALL
Java_com_btcrig_android_BtcrigNative_selfTest(JNIEnv *env, jclass ignored) {
    (void)env;
    (void)ignored;
    return btcrig_core_self_test() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_btcrig_android_BtcrigNative_start(JNIEnv *env, jclass ignored, jstring config_path) {
    (void)ignored;
    const char *path = config_path == NULL ? NULL : (*env)->GetStringUTFChars(env, config_path, NULL);
    int started = btcrig_core_start(path);
    if (path != NULL) {
        (*env)->ReleaseStringUTFChars(env, config_path, path);
    }
    return started ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_btcrig_android_BtcrigNative_stop(JNIEnv *env, jclass ignored) {
    (void)env;
    (void)ignored;
    btcrig_core_stop();
}

JNIEXPORT jboolean JNICALL
Java_com_btcrig_android_BtcrigNative_isRunning(JNIEnv *env, jclass ignored) {
    (void)env;
    (void)ignored;
    return btcrig_core_is_running() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_btcrig_android_BtcrigNative_workerCount(JNIEnv *env, jclass ignored) {
    (void)env;
    (void)ignored;
    return btcrig_core_worker_count();
}

JNIEXPORT jlong JNICALL
Java_com_btcrig_android_BtcrigNative_totalHashes(JNIEnv *env, jclass ignored) {
    (void)env;
    (void)ignored;
    return (jlong)btcrig_core_total_hashes();
}

JNIEXPORT jdouble JNICALL
Java_com_btcrig_android_BtcrigNative_hashrate(JNIEnv *env, jclass ignored) {
    (void)env;
    (void)ignored;
    return btcrig_core_hashrate();
}

JNIEXPORT jdouble JNICALL
Java_com_btcrig_android_BtcrigNative_benchmarkCpu(JNIEnv *env, jclass ignored, jint seconds, jint threads) {
    (void)env;
    (void)ignored;
    return btcrig_core_benchmark_cpu(seconds, threads);
}
