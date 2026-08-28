#include "btcrig_android_tls.h"

#if defined(__ANDROID__)

#include <string.h>
#include <stdio.h>

static JavaVM *g_vm = NULL;
static jclass g_tls_class = NULL;
static jmethodID g_open = NULL;
static jmethodID g_write = NULL;
static jmethodID g_read = NULL;
static jmethodID g_pending = NULL;
static jmethodID g_close = NULL;
static jmethodID g_cipher = NULL;
static jmethodID g_last_error = NULL;

static JNIEnv *get_env(int *detach) {
    JNIEnv *env = NULL;
    *detach = 0;
    if (g_vm == NULL) {
        return NULL;
    }
    if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) {
        return NULL;
    }
    *detach = 1;
    return env;
}

static void put_env(int detach) {
    if (detach && g_vm != NULL) {
        (*g_vm)->DetachCurrentThread(g_vm);
    }
}

static void clear_exception(JNIEnv *env) {
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

static void copy_jstring(JNIEnv *env, jstring value, char *out, size_t out_size) {
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (value == NULL) {
        return;
    }
    const char *text = (*env)->GetStringUTFChars(env, value, NULL);
    if (text != NULL) {
        snprintf(out, out_size, "%s", text);
        (*env)->ReleaseStringUTFChars(env, value, text);
    }
}

int btcrig_android_tls_onload(JavaVM *vm) {
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }

    jclass local = (*env)->FindClass(env, "com/btcrig/android/BtcrigTls");
    if (local == NULL) {
        clear_exception(env);
        return -1;
    }

    g_tls_class = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    if (g_tls_class == NULL) {
        return -1;
    }

    g_open = (*env)->GetStaticMethodID(env, g_tls_class, "open", "(Ljava/lang/String;Ljava/lang/String;Z)I");
    g_write = (*env)->GetStaticMethodID(env, g_tls_class, "write", "(I[BI)I");
    g_read = (*env)->GetStaticMethodID(env, g_tls_class, "read", "(I[BI)I");
    g_pending = (*env)->GetStaticMethodID(env, g_tls_class, "pending", "(I)I");
    g_close = (*env)->GetStaticMethodID(env, g_tls_class, "close", "(I)V");
    g_cipher = (*env)->GetStaticMethodID(env, g_tls_class, "cipher", "(I)Ljava/lang/String;");
    g_last_error = (*env)->GetStaticMethodID(env, g_tls_class, "lastError", "()Ljava/lang/String;");
    if (g_open == NULL || g_write == NULL || g_read == NULL || g_pending == NULL ||
        g_close == NULL || g_cipher == NULL || g_last_error == NULL) {
        clear_exception(env);
        return -1;
    }

    g_vm = vm;
    return 0;
}

int btcrig_android_tls_open(const char *host, const char *port, int verify) {
    int detach = 0;
    JNIEnv *env = get_env(&detach);
    if (env == NULL || g_tls_class == NULL) {
        return -1;
    }
    jstring jhost = (*env)->NewStringUTF(env, host != NULL ? host : "");
    jstring jport = (*env)->NewStringUTF(env, port != NULL ? port : "");
    int id = -1;
    if (jhost != NULL && jport != NULL) {
        id = (*env)->CallStaticIntMethod(env, g_tls_class, g_open, jhost, jport, verify ? JNI_TRUE : JNI_FALSE);
        clear_exception(env);
    }
    if (jhost != NULL) {
        (*env)->DeleteLocalRef(env, jhost);
    }
    if (jport != NULL) {
        (*env)->DeleteLocalRef(env, jport);
    }
    put_env(detach);
    return id;
}

int btcrig_android_tls_write(int id, const void *data, size_t len) {
    int detach = 0;
    JNIEnv *env = get_env(&detach);
    if (env == NULL || g_tls_class == NULL || data == NULL || len > 2147483647u) {
        return -1;
    }
    jbyteArray bytes = (*env)->NewByteArray(env, (jsize)len);
    int rc = -1;
    if (bytes != NULL) {
        (*env)->SetByteArrayRegion(env, bytes, 0, (jsize)len, (const jbyte *)data);
        rc = (*env)->CallStaticIntMethod(env, g_tls_class, g_write, (jint)id, bytes, (jint)len);
        clear_exception(env);
        (*env)->DeleteLocalRef(env, bytes);
    } else {
        clear_exception(env);
    }
    put_env(detach);
    return rc;
}

int btcrig_android_tls_read(int id, void *data, size_t len) {
    int detach = 0;
    JNIEnv *env = get_env(&detach);
    if (env == NULL || g_tls_class == NULL || data == NULL || len == 0) {
        return -1;
    }
    if (len > 2147483647u) {
        len = 2147483647u;
    }
    jbyteArray bytes = (*env)->NewByteArray(env, (jsize)len);
    int n = -1;
    if (bytes != NULL) {
        n = (*env)->CallStaticIntMethod(env, g_tls_class, g_read, (jint)id, bytes, (jint)len);
        clear_exception(env);
        if (n > 0) {
            (*env)->GetByteArrayRegion(env, bytes, 0, (jsize)n, (jbyte *)data);
            clear_exception(env);
        }
        (*env)->DeleteLocalRef(env, bytes);
    } else {
        clear_exception(env);
    }
    put_env(detach);
    return n;
}

int btcrig_android_tls_pending(int id) {
    int detach = 0;
    JNIEnv *env = get_env(&detach);
    int rc = env == NULL || g_tls_class == NULL ? 0 : (*env)->CallStaticIntMethod(env, g_tls_class, g_pending, (jint)id);
    if (env != NULL && g_tls_class != NULL) {
        clear_exception(env);
    }
    put_env(detach);
    return rc;
}

void btcrig_android_tls_close(int id) {
    int detach = 0;
    JNIEnv *env = get_env(&detach);
    if (env != NULL && g_tls_class != NULL) {
        (*env)->CallStaticVoidMethod(env, g_tls_class, g_close, (jint)id);
        clear_exception(env);
    }
    put_env(detach);
}

void btcrig_android_tls_cipher(int id, char *out, size_t out_size) {
    int detach = 0;
    JNIEnv *env = get_env(&detach);
    jstring value = NULL;
    if (env != NULL && g_tls_class != NULL) {
        value = (jstring)(*env)->CallStaticObjectMethod(env, g_tls_class, g_cipher, (jint)id);
        clear_exception(env);
        copy_jstring(env, value, out, out_size);
        if (value != NULL) {
            (*env)->DeleteLocalRef(env, value);
        }
    }
    put_env(detach);
}

void btcrig_android_tls_last_error(char *out, size_t out_size) {
    int detach = 0;
    JNIEnv *env = get_env(&detach);
    jstring value = NULL;
    if (env != NULL) {
        value = (jstring)(*env)->CallStaticObjectMethod(env, g_tls_class, g_last_error);
        clear_exception(env);
        copy_jstring(env, value, out, out_size);
        if (value != NULL) {
            (*env)->DeleteLocalRef(env, value);
        }
    }
    put_env(detach);
}

#endif
