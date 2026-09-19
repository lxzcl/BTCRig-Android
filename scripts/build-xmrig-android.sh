#!/usr/bin/env bash
set -euo pipefail

sdk_dir=$1
output_dir=$2
root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ndk_dir="$sdk_dir/ndk/29.0.14206865"
cmake="$sdk_dir/cmake/3.22.1/bin/cmake"
xmrig_src="$root_dir/app/src/main/cpp/third_party/xmrig"
libuv_src="$root_dir/app/src/main/cpp/third_party/libuv"
build_dir="$root_dir/app/build/xmrig"
libuv_build="$build_dir/libuv"
xmrig_build="$build_dir/xmrig"
xmrig_patch="$root_dir/scripts/xmrig-donate.patch"
xmrig_patched_src="$build_dir/xmrig-src"
dummy_libs="$build_dir/dummy-libs"
toolchain="$ndk_dir/build/cmake/android.toolchain.cmake"
openssl_version=3.5.8
openssl_sha256=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2
openssl_url="https://github.com/openssl/openssl/releases/download/openssl-$openssl_version/openssl-$openssl_version.tar.gz"
openssl_tar="$build_dir/openssl-$openssl_version.tar.gz"
openssl_src="$build_dir/openssl-src"
openssl_prefix="$build_dir/openssl"

test -f "$xmrig_src/CMakeLists.txt" || { echo "XMRig submodule is missing; run git submodule update --init --recursive" >&2; exit 1; }
test -f "$libuv_src/CMakeLists.txt" || { echo "libuv submodule is missing; run git submodule update --init --recursive" >&2; exit 1; }

"$cmake" -S "$libuv_src" -B "$libuv_build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_TESTING=OFF \
    -DLIBUV_BUILD_TESTS=OFF \
    -DLIBUV_BUILD_BENCH=OFF
"$cmake" --build "$libuv_build" --target uv_a

if [ ! -f "$openssl_prefix/lib/libssl.a" ] || [ ! -f "$openssl_prefix/lib/libcrypto.a" ]; then
    if [ ! -f "$openssl_tar" ]; then
        curl -fL --retry 3 -o "$openssl_tar" "$openssl_url"
    fi
    echo "$openssl_sha256  $openssl_tar" | sha256sum -c -
    rm -rf "$openssl_src"
    mkdir -p "$openssl_src"
    tar -xzf "$openssl_tar" -C "$openssl_src" --strip-components=1
    (
        cd "$openssl_src"
        export ANDROID_NDK_ROOT="$ndk_dir"
        export PATH="$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
        ./Configure android-arm64 -D__ANDROID_API__=24 no-shared no-tests no-apps --prefix="$openssl_prefix"
        make -j"$(nproc)"
        make install_sw
    )
fi

mkdir -p "$dummy_libs"
"$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" rcs "$dummy_libs/libpthread.a"
"$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" rcs "$dummy_libs/librt.a"

rm -rf "$xmrig_patched_src"
cp -r "$xmrig_src" "$xmrig_patched_src"
patch -p1 -N -d "$xmrig_patched_src" < "$xmrig_patch"
rm -rf "$xmrig_build"

"$cmake" -S "$xmrig_patched_src" -B "$xmrig_build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXE_LINKER_FLAGS="-L$dummy_libs" \
    -DUV_LIBRARY="$libuv_build/libuv.a" \
    -DUV_INCLUDE_DIR="$libuv_src/include" \
    -DWITH_TLS=ON \
    -DXMRIG_DEPS="$openssl_prefix" \
    -DOPENSSL_ROOT_DIR="$openssl_prefix" \
    -DOPENSSL_INCLUDE_DIR="$openssl_prefix/include" \
    -DOPENSSL_SSL_LIBRARY="$openssl_prefix/lib/libssl.a" \
    -DOPENSSL_CRYPTO_LIBRARY="$openssl_prefix/lib/libcrypto.a" \
    -DWITH_HWLOC=OFF \
    -DWITH_HTTP=OFF \
    -DWITH_OPENCL=OFF \
    -DWITH_CUDA=OFF \
    -DWITH_ASM=OFF \
    -DWITH_MSR=OFF \
    -DWITH_DMI=OFF \
    -DWITH_ENV_VARS=OFF \
    -DWITH_CN_LITE=ON \
    -DWITH_CN_HEAVY=ON \
    -DWITH_CN_PICO=ON \
    -DWITH_CN_FEMTO=ON \
    -DWITH_CN_GPU=ON \
    -DWITH_KAWPOW=OFF \
    -DWITH_GHOSTRIDER=ON \
    -DWITH_AVX2=OFF \
    -DWITH_SSE4_1=OFF
"$cmake" --build "$xmrig_build" --target xmrig

mkdir -p "$output_dir/arm64-v8a"
"$cmake" -E copy "$xmrig_build/xmrig" "$output_dir/arm64-v8a/libxmrig.so"
"$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" "$output_dir/arm64-v8a/libxmrig.so"
