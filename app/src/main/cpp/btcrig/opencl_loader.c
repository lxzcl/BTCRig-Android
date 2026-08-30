#include "opencl_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE opencl_library_t;
typedef FARPROC opencl_symbol_t;
#else
#include <dlfcn.h>
typedef void *opencl_library_t;
typedef void *opencl_symbol_t;
#endif

typedef struct {
    opencl_library_t library;
    int loaded;
    char error[256];
    btcrig_opencl_api_t api;
} btcrig_opencl_loader_t;

static btcrig_opencl_loader_t g_opencl;

static void set_loader_error(const char *message) {
    if (message == NULL) {
        message = "OpenCL runtime unavailable";
    }
    snprintf(g_opencl.error, sizeof(g_opencl.error), "%s", message);
}

static void copy_error(char *error, size_t error_size) {
    if (error == NULL || error_size == 0) {
        return;
    }
    snprintf(error, error_size, "%s", g_opencl.error[0] != '\0' ?
             g_opencl.error : "OpenCL runtime unavailable");
}

static void clear_error(char *error, size_t error_size) {
    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
}

static void set_open_failure(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return;
    }
#if defined(_WIN32)
    char message[256];
    snprintf(message, sizeof(message), "failed to load %s", name);
    set_loader_error(message);
#else
    const char *detail = dlerror();
    char message[256];
    snprintf(message, sizeof(message), "failed to load %s: %s", name, detail != NULL ? detail : "unknown error");
    set_loader_error(message);
#endif
}

static int try_open_library(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return -1;
    }
#if defined(_WIN32)
    g_opencl.library = LoadLibraryA(name);
#else
    (void)dlerror();
    g_opencl.library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
    if (g_opencl.library == NULL) {
        set_open_failure(name);
    }
    return g_opencl.library != NULL ? 0 : -1;
}

static void close_library(void) {
    if (g_opencl.library == NULL) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(g_opencl.library);
#else
    dlclose(g_opencl.library);
#endif
    g_opencl.library = NULL;
}

static opencl_symbol_t load_symbol(const char *name) {
    if (g_opencl.library == NULL) {
        return NULL;
    }
#if defined(_WIN32)
    return GetProcAddress(g_opencl.library, name);
#else
    return dlsym(g_opencl.library, name);
#endif
}

#define LOAD_OPENCL_SYMBOL(name) do { \
    opencl_symbol_t symbol__ = load_symbol(#name); \
    if (symbol__ == NULL) { \
        char message__[256]; \
        snprintf(message__, sizeof(message__), "OpenCL runtime is missing required symbol " #name); \
        set_loader_error(message__); \
        memset(&g_opencl.api, 0, sizeof(g_opencl.api)); \
        return -1; \
    } \
    memcpy(&g_opencl.api.name, &symbol__, sizeof(g_opencl.api.name)); \
} while (0)

static int load_required_symbols(void) {
    LOAD_OPENCL_SYMBOL(clGetPlatformIDs);
    LOAD_OPENCL_SYMBOL(clGetDeviceIDs);
    LOAD_OPENCL_SYMBOL(clGetDeviceInfo);
    LOAD_OPENCL_SYMBOL(clCreateContext);
    LOAD_OPENCL_SYMBOL(clCreateCommandQueue);
    LOAD_OPENCL_SYMBOL(clCreateBuffer);
    LOAD_OPENCL_SYMBOL(clCreateProgramWithSource);
    LOAD_OPENCL_SYMBOL(clBuildProgram);
    LOAD_OPENCL_SYMBOL(clGetProgramBuildInfo);
    LOAD_OPENCL_SYMBOL(clCreateKernel);
    LOAD_OPENCL_SYMBOL(clReleaseKernel);
    LOAD_OPENCL_SYMBOL(clReleaseProgram);
    LOAD_OPENCL_SYMBOL(clReleaseMemObject);
    LOAD_OPENCL_SYMBOL(clReleaseCommandQueue);
    LOAD_OPENCL_SYMBOL(clReleaseContext);
    LOAD_OPENCL_SYMBOL(clEnqueueWriteBuffer);
    LOAD_OPENCL_SYMBOL(clSetKernelArg);
    LOAD_OPENCL_SYMBOL(clEnqueueNDRangeKernel);
    LOAD_OPENCL_SYMBOL(clEnqueueReadBuffer);
    return 0;
}

static int try_load_library(const char *name) {
    if (try_open_library(name) != 0) {
        return -1;
    }
    memset(&g_opencl.api, 0, sizeof(g_opencl.api));
    if (load_required_symbols() == 0) {
        return 0;
    }
    close_library();
    return -1;
}

static int open_library(void) {
    const char *override = getenv("BTCRIG_OPENCL_LIBRARY");
    if (try_load_library(override) == 0) {
        return 0;
    }
#if defined(_WIN32)
    try_load_library("OpenCL.dll");
#elif defined(__APPLE__)
    if (try_load_library("/System/Library/Frameworks/OpenCL.framework/OpenCL") != 0) {
        try_load_library("OpenCL.framework/OpenCL");
    }
#else
#if defined(__ANDROID__)
    static const char *const android_opencl_names[] = {
        "libOpenCL.so",
        "libOpenCL.so.1",
        "libOpenCL_system.so",
        "libGLES_mali.so",
        "libPVROCL.so",
        "/system_ext/lib64/libOpenCL.so",
        "/system_ext/lib/libOpenCL.so",
        "/system_ext/lib64/libOpenCL_system.so",
        "/system_ext/lib/libOpenCL_system.so",
        "/vendor/lib64/libOpenCL.so",
        "/vendor/lib/libOpenCL.so",
        "/vendor/lib64/egl/libGLES_mali.so",
        "/vendor/lib/egl/libGLES_mali.so",
        "/vendor/lib64/libPVROCL.so",
        "/vendor/lib/libPVROCL.so",
        "/odm/lib64/libOpenCL.so",
        "/odm/lib/libOpenCL.so",
        "/product/lib64/libOpenCL.so",
        "/product/lib/libOpenCL.so",
        "/system/vendor/lib64/libOpenCL.so",
        "/system/vendor/lib/libOpenCL.so",
        "/system/lib64/egl/libGLES_mali.so",
        "/system/lib/egl/libGLES_mali.so",
        NULL
    };
    for (int i = 0; g_opencl.library == NULL && android_opencl_names[i] != NULL; ++i) {
        try_load_library(android_opencl_names[i]);
    }
#else
    if (g_opencl.library == NULL && try_load_library("libOpenCL.so.1") != 0) {
        try_load_library("libOpenCL.so");
    }
#endif
#endif
    if (g_opencl.library == NULL) {
        if (g_opencl.error[0] == '\0') {
            set_loader_error("OpenCL runtime library not found");
        }
        return -1;
    }
    return 0;
}

int btcrig_opencl_load(char *error, size_t error_size) {
    if (g_opencl.loaded) {
        clear_error(error, error_size);
        return 0;
    }
    if (open_library() != 0) {
        copy_error(error, error_size);
        return -1;
    }

    g_opencl.loaded = 1;
    g_opencl.error[0] = '\0';
    clear_error(error, error_size);
    return 0;
}

const btcrig_opencl_api_t *btcrig_opencl_api(void) {
    return &g_opencl.api;
}

#undef LOAD_OPENCL_SYMBOL
