#include "btcrig_core.h"
#include "btcrig_android_tls.h"

#include <jni.h>

typedef void (*copy_string_fn)(char *, size_t);

#if defined(__ANDROID__)
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    return btcrig_android_tls_onload(vm) == 0 ? JNI_VERSION_1_6 : JNI_ERR;
}
#endif

static jstring new_core_string(JNIEnv *env, copy_string_fn copy) {
    char text[256];
    copy(text, sizeof(text));
    return (*env)->NewStringUTF(env, text);
}

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

JNIEXPORT jboolean JNICALL
Java_com_btcrig_android_BtcrigNative_stratumConnected(JNIEnv *env, jclass ignored) {
    (void)env;
    (void)ignored;
    return btcrig_core_stratum_connected() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_com_btcrig_android_BtcrigNative_stratumJobs(JNIEnv *env, jclass ignored) {
    (void)env;
    (void)ignored;
    return (jlong)btcrig_core_stratum_jobs();
}

JNIEXPORT jlong JNICALL
Java_com_btcrig_android_BtcrigNative_stratumSubmits(JNIEnv *env, jclass ignored) {
    (void)env;
    (void)ignored;
    return (jlong)btcrig_core_stratum_submits();
}

JNIEXPORT jlong JNICALL
Java_com_btcrig_android_BtcrigNative_stratumAccepts(JNIEnv *env, jclass ignored) {
    (void)env;
    (void)ignored;
    return (jlong)btcrig_core_stratum_accepts();
}

JNIEXPORT jlong JNICALL
Java_com_btcrig_android_BtcrigNative_stratumRejects(JNIEnv *env, jclass ignored) {
    (void)env;
    (void)ignored;
    return (jlong)btcrig_core_stratum_rejects();
}

JNIEXPORT jstring JNICALL
Java_com_btcrig_android_BtcrigNative_pool(JNIEnv *env, jclass ignored) {
    (void)ignored;
    return new_core_string(env, btcrig_core_copy_pool);
}

JNIEXPORT jstring JNICALL
Java_com_btcrig_android_BtcrigNative_stratumStatus(JNIEnv *env, jclass ignored) {
    (void)ignored;
    return new_core_string(env, btcrig_core_copy_stratum_status);
}

JNIEXPORT jstring JNICALL
Java_com_btcrig_android_BtcrigNative_lastError(JNIEnv *env, jclass ignored) {
    (void)ignored;
    return new_core_string(env, btcrig_core_copy_last_error);
}

JNIEXPORT jstring JNICALL
Java_com_btcrig_android_BtcrigNative_openclStatus(JNIEnv *env, jclass ignored, jstring config_path) {
    (void)ignored;
    char text[2048];
    const char *path = config_path == NULL ? NULL : (*env)->GetStringUTFChars(env, config_path, NULL);
    btcrig_core_copy_opencl_status(path, text, sizeof(text));
    if (path != NULL) {
        (*env)->ReleaseStringUTFChars(env, config_path, path);
    }
    return (*env)->NewStringUTF(env, text);
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

JNIEXPORT jdouble JNICALL
Java_com_btcrig_android_BtcrigNative_benchmarkCpuBackend(JNIEnv *env,
                                                         jclass ignored,
                                                         jstring backend,
                                                         jint seconds,
                                                         jint threads) {
    (void)ignored;
    const char *name = backend == NULL ? NULL : (*env)->GetStringUTFChars(env, backend, NULL);
    double hps = btcrig_core_benchmark_cpu_backend(name, seconds, threads);
    if (name != NULL) {
        (*env)->ReleaseStringUTFChars(env, backend, name);
    }
    return hps;
}

JNIEXPORT jdouble JNICALL
Java_com_btcrig_android_BtcrigNative_benchmarkOpencl(JNIEnv *env,
                                                     jclass ignored,
                                                     jstring config_path,
                                                     jint seconds) {
    (void)ignored;
    const char *path = config_path == NULL ? NULL : (*env)->GetStringUTFChars(env, config_path, NULL);
    double hps = btcrig_core_benchmark_opencl(path, seconds);
    if (path != NULL) {
        (*env)->ReleaseStringUTFChars(env, config_path, path);
    }
    return hps;
}
