cmake_minimum_required(VERSION 3.22.1)
project(btcrig_core C)

set(BTCRIG_SRC
    btcrig_core_full.c
    btcrig_android_tls.c
    btcrig_jni.c
    compat/openssl_sha.c
    btcrig/console.c
    btcrig/cpu_info.c
    btcrig/miner.c
    btcrig/opencl_loader.c
    btcrig/opencl_miner.c
    btcrig/sha256d.c
    btcrig/stratum.c
    third_party/jansson/src/dtoa.c
    third_party/jansson/src/dump.c
    third_party/jansson/src/error.c
    third_party/jansson/src/hashtable.c
    third_party/jansson/src/hashtable_seed.c
    third_party/jansson/src/load.c
    third_party/jansson/src/memory.c
    third_party/jansson/src/pack_unpack.c
    third_party/jansson/src/strbuffer.c
    third_party/jansson/src/strconv.c
    third_party/jansson/src/utf.c
    third_party/jansson/src/value.c
    third_party/jansson/src/version.c
)

add_library(btcrig_core SHARED ${BTCRIG_SRC})

if(ANDROID_ABI STREQUAL "arm64-v8a")
    target_sources(btcrig_core PRIVATE btcrig/sha256d_arm_sha2.c)
    target_compile_definitions(btcrig_core PRIVATE BTC_MINER_ARM_SHA2)
    set_source_files_properties(
        btcrig/sha256d_arm_sha2.c
        PROPERTIES COMPILE_OPTIONS "-march=armv8-a+crypto"
    )
endif()

target_compile_features(btcrig_core PRIVATE c_std_11)
target_compile_options(btcrig_core PRIVATE -Wall -Wextra)
target_compile_definitions(btcrig_core PRIVATE
    BTC_MINER_OPENCL
    HAVE_CONFIG_H
)
target_include_directories(btcrig_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/compat
    ${CMAKE_CURRENT_SOURCE_DIR}/btcrig
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jansson/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jansson/private_include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/OpenCL
)
target_link_libraries(btcrig_core PRIVATE dl m)
