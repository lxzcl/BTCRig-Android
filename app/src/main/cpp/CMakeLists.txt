cmake_minimum_required(VERSION 3.22.1)
project(btcrig_core C)

add_library(btcrig_core SHARED btcrig_core.c btcrig_jni.c)

target_compile_features(btcrig_core PRIVATE c_std_11)
target_compile_options(btcrig_core PRIVATE -Wall -Wextra -Werror)
