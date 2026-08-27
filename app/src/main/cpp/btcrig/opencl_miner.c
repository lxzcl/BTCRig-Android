#include "opencl_miner.h"
#include "opencl_loader.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define clGetPlatformIDs btcrig_opencl_api()->clGetPlatformIDs
#define clGetDeviceIDs btcrig_opencl_api()->clGetDeviceIDs
#define clGetDeviceInfo btcrig_opencl_api()->clGetDeviceInfo
#define clCreateContext btcrig_opencl_api()->clCreateContext
#define clCreateCommandQueue btcrig_opencl_api()->clCreateCommandQueue
#define clCreateBuffer btcrig_opencl_api()->clCreateBuffer
#define clCreateProgramWithSource btcrig_opencl_api()->clCreateProgramWithSource
#define clBuildProgram btcrig_opencl_api()->clBuildProgram
#define clGetProgramBuildInfo btcrig_opencl_api()->clGetProgramBuildInfo
#define clCreateKernel btcrig_opencl_api()->clCreateKernel
#define clReleaseKernel btcrig_opencl_api()->clReleaseKernel
#define clReleaseProgram btcrig_opencl_api()->clReleaseProgram
#define clReleaseMemObject btcrig_opencl_api()->clReleaseMemObject
#define clReleaseCommandQueue btcrig_opencl_api()->clReleaseCommandQueue
#define clReleaseContext btcrig_opencl_api()->clReleaseContext
#define clEnqueueWriteBuffer btcrig_opencl_api()->clEnqueueWriteBuffer
#define clSetKernelArg btcrig_opencl_api()->clSetKernelArg
#define clEnqueueNDRangeKernel btcrig_opencl_api()->clEnqueueNDRangeKernel
#define clEnqueueReadBuffer btcrig_opencl_api()->clEnqueueReadBuffer

#define OPENCL_DEFAULT_BATCH_SIZE 1048576U
#define OPENCL_DEFAULT_MAX_RESULTS 256U
#define OPENCL_DEFAULT_NONCES_PER_WORK_ITEM 1U
#define OPENCL_MAX_NONCES_PER_WORK_ITEM 64U
#define OPENCL_LEGACY_NOATOMIC_MAX_BATCH_SIZE 1048576U
#define OPENCL_LEGACY_PARAM_WORDS 19U

struct opencl_miner {
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    cl_mem params_buf;
    cl_mem count_buf;
    cl_mem matches_buf;
    uint32_t batch_size;
    uint32_t local_work_size;
    uint32_t nonces_per_work_item;
    uint32_t max_results;
    uint32_t result_slots;
    uint32_t *match_flags;
    uint32_t *matches;
    int backend_variant;
    int kernel_variant;
    int legacy_noatomic;
    char device_name[128];
    char device_vendor[128];
    char device_version[128];
    char backend_name[32];
};

static const char *k_opencl_kernel_compact =
"#ifdef cl_khr_global_int32_base_atomics\n"
"#pragma OPENCL EXTENSION cl_khr_global_int32_base_atomics : enable\n"
"#endif\n"
"typedef unsigned int u32;\n"
"__constant u32 K256[64] = {\n"
"0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,\n"
"0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,\n"
"0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,\n"
"0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,\n"
"0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,\n"
"0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,\n"
"0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,\n"
"0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};\n"
"inline u32 rotr32(u32 x, u32 n) { return (x >> n) | (x << (32U - n)); }\n"
"inline u32 bswap32(u32 v) { return ((v & 0x000000ffU) << 24) | ((v & 0x0000ff00U) << 8) | ((v & 0x00ff0000U) >> 8) | ((v & 0xff000000U) >> 24); }\n"
"#define SS0(x) (rotr32((x), 7) ^ rotr32((x), 18) ^ ((x) >> 3))\n"
"#define SS1(x) (rotr32((x), 17) ^ rotr32((x), 19) ^ ((x) >> 10))\n"
"#define BS0(x) (rotr32((x), 2) ^ rotr32((x), 13) ^ rotr32((x), 22))\n"
"#define BS1(x) (rotr32((x), 6) ^ rotr32((x), 11) ^ rotr32((x), 25))\n"
"#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))\n"
"#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))\n"
"inline void compress(u32 st[8], u32 w[16]) {\n"
"  u32 a=st[0], b=st[1], c=st[2], d=st[3], e=st[4], f=st[5], g=st[6], h=st[7];\n"
"  for (int i = 0; i < 64; ++i) {\n"
"    u32 wi;\n"
"    if (i < 16) {\n"
"      wi = w[i];\n"
"    } else {\n"
"      int j = i & 15;\n"
"      wi = w[j] + SS1(w[(j + 14) & 15]) + w[(j + 9) & 15] + SS0(w[(j + 1) & 15]);\n"
"      w[j] = wi;\n"
"    }\n"
"    u32 t1 = h + BS1(e) + CH(e, f, g) + K256[i] + wi;\n"
"    u32 t2 = BS0(a) + MAJ(a, b, c);\n"
"    h = g; g = f; f = e; e = d + t1;\n"
"    d = c; c = b; b = a; a = t1 + t2;\n"
"  }\n"
"  st[0]+=a; st[1]+=b; st[2]+=c; st[3]+=d; st[4]+=e; st[5]+=f; st[6]+=g; st[7]+=h;\n"
"}\n";

static const char *k_opencl_kernel =
"#ifdef cl_khr_global_int32_base_atomics\n"
"#pragma OPENCL EXTENSION cl_khr_global_int32_base_atomics : enable\n"
"#endif\n"
"typedef unsigned int u32;\n"
"inline u32 rotr32(u32 x, u32 n) { return (x >> n) | (x << (32U - n)); }\n"
"inline u32 bswap32(u32 v) { return ((v & 0x000000ffU) << 24) | ((v & 0x0000ff00U) << 8) | ((v & 0x00ff0000U) >> 8) | ((v & 0xff000000U) >> 24); }\n"
"#define SS0(x) (rotr32((x), 7) ^ rotr32((x), 18) ^ ((x) >> 3))\n"
"#define SS1(x) (rotr32((x), 17) ^ rotr32((x), 19) ^ ((x) >> 10))\n"
"#define BS0(x) (rotr32((x), 2) ^ rotr32((x), 13) ^ rotr32((x), 22))\n"
"#define BS1(x) (rotr32((x), 6) ^ rotr32((x), 11) ^ rotr32((x), 25))\n"
"#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))\n"
"#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))\n"
"#define MSG(w0,w1,w9,w14) ((w0) += SS0(w1) + (w9) + SS1(w14))\n"
"#define ROUND(a,b,c,d,e,f,g,h,w,k) do { u32 t1 = (h) + BS1(e) + CH(e,f,g) + (k) + (w); u32 t2 = BS0(a) + MAJ(a,b,c); (d) += t1; (h) = t1 + t2; } while (0)\n"
"inline void compress(u32 st[8], u32 w[16]) {\n"
"  u32 a=st[0], b=st[1], c=st[2], d=st[3], e=st[4], f=st[5], g=st[6], h=st[7];\n"
"  u32 w0=w[0], w1=w[1], w2=w[2], w3=w[3], w4=w[4], w5=w[5], w6=w[6], w7=w[7];\n"
"  u32 w8=w[8], w9=w[9], w10=w[10], w11=w[11], w12=w[12], w13=w[13], w14=w[14], w15=w[15];\n"
"  ROUND(a,b,c,d,e,f,g,h,w0,0x428a2f98U);\n"
"  ROUND(h,a,b,c,d,e,f,g,w1,0x71374491U);\n"
"  ROUND(g,h,a,b,c,d,e,f,w2,0xb5c0fbcfU);\n"
"  ROUND(f,g,h,a,b,c,d,e,w3,0xe9b5dba5U);\n"
"  ROUND(e,f,g,h,a,b,c,d,w4,0x3956c25bU);\n"
"  ROUND(d,e,f,g,h,a,b,c,w5,0x59f111f1U);\n"
"  ROUND(c,d,e,f,g,h,a,b,w6,0x923f82a4U);\n"
"  ROUND(b,c,d,e,f,g,h,a,w7,0xab1c5ed5U);\n"
"  ROUND(a,b,c,d,e,f,g,h,w8,0xd807aa98U);\n"
"  ROUND(h,a,b,c,d,e,f,g,w9,0x12835b01U);\n"
"  ROUND(g,h,a,b,c,d,e,f,w10,0x243185beU);\n"
"  ROUND(f,g,h,a,b,c,d,e,w11,0x550c7dc3U);\n"
"  ROUND(e,f,g,h,a,b,c,d,w12,0x72be5d74U);\n"
"  ROUND(d,e,f,g,h,a,b,c,w13,0x80deb1feU);\n"
"  ROUND(c,d,e,f,g,h,a,b,w14,0x9bdc06a7U);\n"
"  ROUND(b,c,d,e,f,g,h,a,w15,0xc19bf174U);\n"
"  ROUND(a,b,c,d,e,f,g,h,MSG(w0, w1, w9, w14),0xe49b69c1U);\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w1, w2, w10, w15),0xefbe4786U);\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w2, w3, w11, w0),0x0fc19dc6U);\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w3, w4, w12, w1),0x240ca1ccU);\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w4, w5, w13, w2),0x2de92c6fU);\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w5, w6, w14, w3),0x4a7484aaU);\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w6, w7, w15, w4),0x5cb0a9dcU);\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w7, w8, w0, w5),0x76f988daU);\n"
"  ROUND(a,b,c,d,e,f,g,h,MSG(w8, w9, w1, w6),0x983e5152U);\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w9, w10, w2, w7),0xa831c66dU);\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w10, w11, w3, w8),0xb00327c8U);\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w11, w12, w4, w9),0xbf597fc7U);\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w12, w13, w5, w10),0xc6e00bf3U);\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w13, w14, w6, w11),0xd5a79147U);\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w14, w15, w7, w12),0x06ca6351U);\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w15, w0, w8, w13),0x14292967U);\n";

static const char *k_opencl_kernel_rounds =
"  ROUND(a,b,c,d,e,f,g,h,MSG(w0, w1, w9, w14),0x27b70a85U);\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w1, w2, w10, w15),0x2e1b2138U);\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w2, w3, w11, w0),0x4d2c6dfcU);\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w3, w4, w12, w1),0x53380d13U);\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w4, w5, w13, w2),0x650a7354U);\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w5, w6, w14, w3),0x766a0abbU);\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w6, w7, w15, w4),0x81c2c92eU);\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w7, w8, w0, w5),0x92722c85U);\n"
"  ROUND(a,b,c,d,e,f,g,h,MSG(w8, w9, w1, w6),0xa2bfe8a1U);\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w9, w10, w2, w7),0xa81a664bU);\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w10, w11, w3, w8),0xc24b8b70U);\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w11, w12, w4, w9),0xc76c51a3U);\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w12, w13, w5, w10),0xd192e819U);\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w13, w14, w6, w11),0xd6990624U);\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w14, w15, w7, w12),0xf40e3585U);\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w15, w0, w8, w13),0x106aa070U);\n"
"  ROUND(a,b,c,d,e,f,g,h,MSG(w0, w1, w9, w14),0x19a4c116U);\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w1, w2, w10, w15),0x1e376c08U);\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w2, w3, w11, w0),0x2748774cU);\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w3, w4, w12, w1),0x34b0bcb5U);\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w4, w5, w13, w2),0x391c0cb3U);\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w5, w6, w14, w3),0x4ed8aa4aU);\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w6, w7, w15, w4),0x5b9cca4fU);\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w7, w8, w0, w5),0x682e6ff3U);\n"
"  ROUND(a,b,c,d,e,f,g,h,MSG(w8, w9, w1, w6),0x748f82eeU);\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w9, w10, w2, w7),0x78a5636fU);\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w10, w11, w3, w8),0x84c87814U);\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w11, w12, w4, w9),0x8cc70208U);\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w12, w13, w5, w10),0x90befffaU);\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w13, w14, w6, w11),0xa4506cebU);\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w14, w15, w7, w12),0xbef9a3f7U);\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w15, w0, w8, w13),0xc67178f2U);\n"
"  st[0]+=a; st[1]+=b; st[2]+=c; st[3]+=d; st[4]+=e; st[5]+=f; st[6]+=g; st[7]+=h;\n"
"}\n";

static const char *k_opencl_kernel_tail_helpers =
"inline int meets_target(const u32 hash[8], u32 t0, u32 t1, u32 t2, u32 t3, u32 t4, u32 t5, u32 t6, u32 t7) {\n"
"  u32 h = bswap32(hash[7]); if (h < t7) return 1; if (h > t7) return 0;\n"
"  h = bswap32(hash[6]); if (h < t6) return 1; if (h > t6) return 0;\n"
"  h = bswap32(hash[5]); if (h < t5) return 1; if (h > t5) return 0;\n"
"  h = bswap32(hash[4]); if (h < t4) return 1; if (h > t4) return 0;\n"
"  h = bswap32(hash[3]); if (h < t3) return 1; if (h > t3) return 0;\n"
"  h = bswap32(hash[2]); if (h < t2) return 1; if (h > t2) return 0;\n"
"  h = bswap32(hash[1]); if (h < t1) return 1; if (h > t1) return 0;\n"
"  h = bswap32(hash[0]); if (h < t0) return 1; if (h > t0) return 0;\n"
"  return 1;\n"
"}\n"
"inline void store_match(u32 nonce, const u32 out[8], u32 max_results,\n"
"                        __global volatile u32 *result_count,\n"
"                        __global u32 *matches) {\n"
"  u32 idx = atomic_inc(result_count);\n"
"  if (idx < max_results) {\n"
"    u32 base = idx * 9U;\n"
"    matches[base] = nonce;\n"
"    matches[base + 1U] = out[0];\n"
"    matches[base + 2U] = out[1];\n"
"    matches[base + 3U] = out[2];\n"
"    matches[base + 4U] = out[3];\n"
"    matches[base + 5U] = out[4];\n"
"    matches[base + 6U] = out[5];\n"
"    matches[base + 7U] = out[6];\n"
"    matches[base + 8U] = out[7];\n"
"  }\n"
"}\n"
"inline void scan_one_nonce(u32 fast0, u32 fast1, u32 fast2, u32 fast3,\n"
"                           u32 fast4, u32 fast5, u32 fast6, u32 fast7,\n"
"                           u32 target0, u32 target1, u32 target2, u32 target3,\n"
"                           u32 target4, u32 target5, u32 target6, u32 target7,\n"
"                           u32 tail0, u32 tail1, u32 tail2, u32 nonce,\n"
"                           u32 max_results,\n"
"                           __global volatile u32 *result_count,\n"
"                           __global u32 *matches) {\n"
"  u32 st[8];\n"
"  u32 w[16];\n"
"  st[0] = fast0; st[1] = fast1; st[2] = fast2; st[3] = fast3;\n"
"  st[4] = fast4; st[5] = fast5; st[6] = fast6; st[7] = fast7;\n"
"  w[0] = tail0; w[1] = tail1; w[2] = tail2; w[3] = bswap32(nonce);\n"
"  w[4] = 0x80000000U; w[5] = 0U; w[6] = 0U; w[7] = 0U;\n"
"  w[8] = 0U; w[9] = 0U; w[10] = 0U; w[11] = 0U;\n"
"  w[12] = 0U; w[13] = 0U; w[14] = 0U; w[15] = 640U;\n"
"  compress(st, w);\n"
"  u32 out[8] = {0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};\n"
"  w[0] = st[0]; w[1] = st[1]; w[2] = st[2]; w[3] = st[3];\n"
"  w[4] = st[4]; w[5] = st[5]; w[6] = st[6]; w[7] = st[7];\n"
"  w[8] = 0x80000000U; w[9] = 0U; w[10] = 0U; w[11] = 0U;\n"
"  w[12] = 0U; w[13] = 0U; w[14] = 0U; w[15] = 256U;\n"
"  compress(out, w);\n"
"  if (meets_target(out, target0, target1, target2, target3, target4, target5, target6, target7)) {\n"
"    store_match(nonce, out, max_results, result_count, matches);\n"
"  }\n"
"}\n";

static const char *k_opencl_kernel_noatomic_tail_helpers =
"inline int meets_target(const u32 hash[8], u32 t0, u32 t1, u32 t2, u32 t3, u32 t4, u32 t5, u32 t6, u32 t7) {\n"
"  u32 h = bswap32(hash[7]); if (h < t7) return 1; if (h > t7) return 0;\n"
"  h = bswap32(hash[6]); if (h < t6) return 1; if (h > t6) return 0;\n"
"  h = bswap32(hash[5]); if (h < t5) return 1; if (h > t5) return 0;\n"
"  h = bswap32(hash[4]); if (h < t4) return 1; if (h > t4) return 0;\n"
"  h = bswap32(hash[3]); if (h < t3) return 1; if (h > t3) return 0;\n"
"  h = bswap32(hash[2]); if (h < t2) return 1; if (h > t2) return 0;\n"
"  h = bswap32(hash[1]); if (h < t1) return 1; if (h > t1) return 0;\n"
"  h = bswap32(hash[0]); if (h < t0) return 1; if (h > t0) return 0;\n"
"  return 1;\n"
"}\n"
"inline void store_match(u32 slot, u32 nonce, const u32 out[8], __global u32 *match_flags, __global u32 *matches) {\n"
"  match_flags[slot] = 1U;\n"
"  u32 base = slot * 9U;\n"
"  matches[base] = nonce;\n"
"  matches[base + 1U] = out[0];\n"
"  matches[base + 2U] = out[1];\n"
"  matches[base + 3U] = out[2];\n"
"  matches[base + 4U] = out[3];\n"
"  matches[base + 5U] = out[4];\n"
"  matches[base + 6U] = out[5];\n"
"  matches[base + 7U] = out[6];\n"
"  matches[base + 8U] = out[7];\n"
"}\n"
"inline void scan_one_nonce(u32 fast0, u32 fast1, u32 fast2, u32 fast3,\n"
"                           u32 fast4, u32 fast5, u32 fast6, u32 fast7,\n"
"                           u32 target0, u32 target1, u32 target2, u32 target3,\n"
"                           u32 target4, u32 target5, u32 target6, u32 target7,\n"
"                           u32 tail0, u32 tail1, u32 tail2, u32 nonce, u32 slot,\n"
"                           __global u32 *match_flags, __global u32 *matches) {\n"
"  u32 st[8];\n"
"  u32 w[16];\n"
"  st[0] = fast0; st[1] = fast1; st[2] = fast2; st[3] = fast3;\n"
"  st[4] = fast4; st[5] = fast5; st[6] = fast6; st[7] = fast7;\n"
"  w[0] = tail0; w[1] = tail1; w[2] = tail2; w[3] = bswap32(nonce);\n"
"  w[4] = 0x80000000U; w[5] = 0U; w[6] = 0U; w[7] = 0U;\n"
"  w[8] = 0U; w[9] = 0U; w[10] = 0U; w[11] = 0U;\n"
"  w[12] = 0U; w[13] = 0U; w[14] = 0U; w[15] = 640U;\n"
"  compress(st, w);\n"
"  u32 out[8] = {0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};\n"
"  w[0] = st[0]; w[1] = st[1]; w[2] = st[2]; w[3] = st[3];\n"
"  w[4] = st[4]; w[5] = st[5]; w[6] = st[6]; w[7] = st[7];\n"
"  w[8] = 0x80000000U; w[9] = 0U; w[10] = 0U; w[11] = 0U;\n"
"  w[12] = 0U; w[13] = 0U; w[14] = 0U; w[15] = 256U;\n"
"  compress(out, w);\n"
"  if (meets_target(out, target0, target1, target2, target3, target4, target5, target6, target7)) {\n"
"    store_match(slot, nonce, out, match_flags, matches);\n"
"  }\n"
"}\n";

static const char *k_opencl_kernel_modern_scalar_macro =
"#define SHA256_COMPRESS_SCALAR(s0,s1,s2,s3,s4,s5,s6,s7,w0,w1,w2,w3,w4,w5,w6,w7,w8,w9,w10,w11,w12,w13,w14,w15) do { \\\n"
"  u32 a=(s0), b=(s1), c=(s2), d=(s3), e=(s4), f=(s5), g=(s6), h=(s7); \\\n"
"  ROUND(a,b,c,d,e,f,g,h,w0,0x428a2f98U); \\\n"
"  ROUND(h,a,b,c,d,e,f,g,w1,0x71374491U); \\\n"
"  ROUND(g,h,a,b,c,d,e,f,w2,0xb5c0fbcfU); \\\n"
"  ROUND(f,g,h,a,b,c,d,e,w3,0xe9b5dba5U); \\\n"
"  ROUND(e,f,g,h,a,b,c,d,w4,0x3956c25bU); \\\n"
"  ROUND(d,e,f,g,h,a,b,c,w5,0x59f111f1U); \\\n"
"  ROUND(c,d,e,f,g,h,a,b,w6,0x923f82a4U); \\\n"
"  ROUND(b,c,d,e,f,g,h,a,w7,0xab1c5ed5U); \\\n"
"  ROUND(a,b,c,d,e,f,g,h,w8,0xd807aa98U); \\\n"
"  ROUND(h,a,b,c,d,e,f,g,w9,0x12835b01U); \\\n"
"  ROUND(g,h,a,b,c,d,e,f,w10,0x243185beU); \\\n"
"  ROUND(f,g,h,a,b,c,d,e,w11,0x550c7dc3U); \\\n"
"  ROUND(e,f,g,h,a,b,c,d,w12,0x72be5d74U); \\\n"
"  ROUND(d,e,f,g,h,a,b,c,w13,0x80deb1feU); \\\n"
"  ROUND(c,d,e,f,g,h,a,b,w14,0x9bdc06a7U); \\\n"
"  ROUND(b,c,d,e,f,g,h,a,w15,0xc19bf174U); \\\n"
"  ROUND(a,b,c,d,e,f,g,h,MSG(w0, w1, w9, w14),0xe49b69c1U); \\\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w1, w2, w10, w15),0xefbe4786U); \\\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w2, w3, w11, w0),0x0fc19dc6U); \\\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w3, w4, w12, w1),0x240ca1ccU); \\\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w4, w5, w13, w2),0x2de92c6fU); \\\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w5, w6, w14, w3),0x4a7484aaU); \\\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w6, w7, w15, w4),0x5cb0a9dcU); \\\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w7, w8, w0, w5),0x76f988daU); \\\n"
"  ROUND(a,b,c,d,e,f,g,h,MSG(w8, w9, w1, w6),0x983e5152U); \\\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w9, w10, w2, w7),0xa831c66dU); \\\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w10, w11, w3, w8),0xb00327c8U); \\\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w11, w12, w4, w9),0xbf597fc7U); \\\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w12, w13, w5, w10),0xc6e00bf3U); \\\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w13, w14, w6, w11),0xd5a79147U); \\\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w14, w15, w7, w12),0x06ca6351U); \\\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w15, w0, w8, w13),0x14292967U); \\\n"
"  ROUND(a,b,c,d,e,f,g,h,MSG(w0, w1, w9, w14),0x27b70a85U); \\\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w1, w2, w10, w15),0x2e1b2138U); \\\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w2, w3, w11, w0),0x4d2c6dfcU); \\\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w3, w4, w12, w1),0x53380d13U); \\\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w4, w5, w13, w2),0x650a7354U); \\\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w5, w6, w14, w3),0x766a0abbU); \\\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w6, w7, w15, w4),0x81c2c92eU); \\\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w7, w8, w0, w5),0x92722c85U); \\\n"
"  ROUND(a,b,c,d,e,f,g,h,MSG(w8, w9, w1, w6),0xa2bfe8a1U); \\\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w9, w10, w2, w7),0xa81a664bU); \\\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w10, w11, w3, w8),0xc24b8b70U); \\\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w11, w12, w4, w9),0xc76c51a3U); \\\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w12, w13, w5, w10),0xd192e819U); \\\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w13, w14, w6, w11),0xd6990624U); \\\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w14, w15, w7, w12),0xf40e3585U); \\\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w15, w0, w8, w13),0x106aa070U); \\\n"
"  ROUND(a,b,c,d,e,f,g,h,MSG(w0, w1, w9, w14),0x19a4c116U); \\\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w1, w2, w10, w15),0x1e376c08U); \\\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w2, w3, w11, w0),0x2748774cU); \\\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w3, w4, w12, w1),0x34b0bcb5U); \\\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w4, w5, w13, w2),0x391c0cb3U); \\\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w5, w6, w14, w3),0x4ed8aa4aU); \\\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w6, w7, w15, w4),0x5b9cca4fU); \\\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w7, w8, w0, w5),0x682e6ff3U); \\\n"
"  ROUND(a,b,c,d,e,f,g,h,MSG(w8, w9, w1, w6),0x748f82eeU); \\\n"
"  ROUND(h,a,b,c,d,e,f,g,MSG(w9, w10, w2, w7),0x78a5636fU); \\\n"
"  ROUND(g,h,a,b,c,d,e,f,MSG(w10, w11, w3, w8),0x84c87814U); \\\n"
"  ROUND(f,g,h,a,b,c,d,e,MSG(w11, w12, w4, w9),0x8cc70208U); \\\n"
"  ROUND(e,f,g,h,a,b,c,d,MSG(w12, w13, w5, w10),0x90befffaU); \\\n"
"  ROUND(d,e,f,g,h,a,b,c,MSG(w13, w14, w6, w11),0xa4506cebU); \\\n"
"  ROUND(c,d,e,f,g,h,a,b,MSG(w14, w15, w7, w12),0xbef9a3f7U); \\\n"
"  ROUND(b,c,d,e,f,g,h,a,MSG(w15, w0, w8, w13),0xc67178f2U); \\\n"
"  (s0)+=a; (s1)+=b; (s2)+=c; (s3)+=d; (s4)+=e; (s5)+=f; (s6)+=g; (s7)+=h; \\\n"
"} while (0)\n";

static const char *k_opencl_kernel_modern_tail_helpers =
"inline int meets_target_words(u32 h0, u32 h1, u32 h2, u32 h3, u32 h4, u32 h5, u32 h6, u32 h7,\n"
"                              u32 t0, u32 t1, u32 t2, u32 t3, u32 t4, u32 t5, u32 t6, u32 t7) {\n"
"  u32 h = bswap32(h7); if (h < t7) return 1; if (h > t7) return 0;\n"
"  h = bswap32(h6); if (h < t6) return 1; if (h > t6) return 0;\n"
"  h = bswap32(h5); if (h < t5) return 1; if (h > t5) return 0;\n"
"  h = bswap32(h4); if (h < t4) return 1; if (h > t4) return 0;\n"
"  h = bswap32(h3); if (h < t3) return 1; if (h > t3) return 0;\n"
"  h = bswap32(h2); if (h < t2) return 1; if (h > t2) return 0;\n"
"  h = bswap32(h1); if (h < t1) return 1; if (h > t1) return 0;\n"
"  h = bswap32(h0); if (h < t0) return 1; if (h > t0) return 0;\n"
"  return 1;\n"
"}\n"
"inline void store_match_words(u32 nonce, u32 h0, u32 h1, u32 h2, u32 h3, u32 h4, u32 h5, u32 h6, u32 h7,\n"
"                              u32 max_results, __global volatile u32 *result_count, __global u32 *matches) {\n"
"  u32 idx = atomic_inc(result_count);\n"
"  if (idx < max_results) {\n"
"    u32 base = idx * 9U;\n"
"    matches[base] = nonce;\n"
"    matches[base + 1U] = h0; matches[base + 2U] = h1; matches[base + 3U] = h2; matches[base + 4U] = h3;\n"
"    matches[base + 5U] = h4; matches[base + 6U] = h5; matches[base + 7U] = h6; matches[base + 8U] = h7;\n"
"  }\n"
"}\n"
"inline void scan_one_nonce(u32 fast0, u32 fast1, u32 fast2, u32 fast3,\n"
"                           u32 fast4, u32 fast5, u32 fast6, u32 fast7,\n"
"                           u32 target0, u32 target1, u32 target2, u32 target3,\n"
"                           u32 target4, u32 target5, u32 target6, u32 target7,\n"
"                           u32 tail0, u32 tail1, u32 tail2, u32 nonce,\n"
"                           u32 max_results,\n"
"                           __global volatile u32 *result_count,\n"
"                           __global u32 *matches) {\n"
"  u32 s0=fast0, s1=fast1, s2=fast2, s3=fast3, s4=fast4, s5=fast5, s6=fast6, s7=fast7;\n"
"  u32 w0=tail0, w1=tail1, w2=tail2, w3=bswap32(nonce), w4=0x80000000U, w5=0U, w6=0U, w7=0U;\n"
"  u32 w8=0U, w9=0U, w10=0U, w11=0U, w12=0U, w13=0U, w14=0U, w15=640U;\n"
"  SHA256_COMPRESS_SCALAR(s0,s1,s2,s3,s4,s5,s6,s7,w0,w1,w2,w3,w4,w5,w6,w7,w8,w9,w10,w11,w12,w13,w14,w15);\n"
"  u32 h0=0x6a09e667U, h1=0xbb67ae85U, h2=0x3c6ef372U, h3=0xa54ff53aU;\n"
"  u32 h4=0x510e527fU, h5=0x9b05688cU, h6=0x1f83d9abU, h7=0x5be0cd19U;\n"
"  w0=s0; w1=s1; w2=s2; w3=s3; w4=s4; w5=s5; w6=s6; w7=s7;\n"
"  w8=0x80000000U; w9=0U; w10=0U; w11=0U; w12=0U; w13=0U; w14=0U; w15=256U;\n"
"  SHA256_COMPRESS_SCALAR(h0,h1,h2,h3,h4,h5,h6,h7,w0,w1,w2,w3,w4,w5,w6,w7,w8,w9,w10,w11,w12,w13,w14,w15);\n"
"  if (meets_target_words(h0,h1,h2,h3,h4,h5,h6,h7,target0,target1,target2,target3,target4,target5,target6,target7)) {\n"
"    store_match_words(nonce,h0,h1,h2,h3,h4,h5,h6,h7,max_results,result_count,matches);\n"
"  }\n"
"}\n";

static const char *k_opencl_kernel_tail_kernels =
"#define SCAN_KERNEL_ARGS \\\n"
"  u32 fast0, u32 fast1, u32 fast2, u32 fast3, \\\n"
"  u32 fast4, u32 fast5, u32 fast6, u32 fast7, \\\n"
"  u32 target0, u32 target1, u32 target2, u32 target3, \\\n"
"  u32 target4, u32 target5, u32 target6, u32 target7, \\\n"
"  u32 tail0, u32 tail1, u32 tail2, \\\n"
"  u32 start_nonce, u32 nonce_count, \\\n"
"  u32 nonces_per_work_item, \\\n"
"  u32 max_results, \\\n"
"  __global volatile u32 *result_count, \\\n"
"  __global u32 *matches\n"
"#define SCAN_ONE(base_nonce) \\\n"
"  scan_one_nonce(fast0, fast1, fast2, fast3, fast4, fast5, fast6, fast7, \\\n"
"                 target0, target1, target2, target3, target4, target5, target6, target7, \\\n"
"                 tail0, tail1, tail2, start_nonce + (base_nonce), max_results, result_count, matches)\n"
"__kernel void scan_nonce_range_npi1(SCAN_KERNEL_ARGS) {\n"
"  (void)nonces_per_work_item;\n"
"  u32 gid = (u32)get_global_id(0);\n"
"  if (gid >= nonce_count) return;\n"
"  SCAN_ONE(gid);\n"
"}\n"
"__kernel void scan_nonce_range_npi2(SCAN_KERNEL_ARGS) {\n"
"  (void)nonces_per_work_item;\n"
"  u32 base_nonce = (u32)get_global_id(0) * 2U;\n"
"  if (base_nonce >= nonce_count) return;\n"
"  SCAN_ONE(base_nonce);\n"
"  if (base_nonce + 1U < nonce_count) SCAN_ONE(base_nonce + 1U);\n"
"}\n"
"__kernel void scan_nonce_range_npi4(SCAN_KERNEL_ARGS) {\n"
"  (void)nonces_per_work_item;\n"
"  u32 base_nonce = (u32)get_global_id(0) * 4U;\n"
"  if (base_nonce >= nonce_count) return;\n"
"  SCAN_ONE(base_nonce);\n"
"  if (base_nonce + 1U < nonce_count) SCAN_ONE(base_nonce + 1U);\n"
"  if (base_nonce + 2U < nonce_count) SCAN_ONE(base_nonce + 2U);\n"
"  if (base_nonce + 3U < nonce_count) SCAN_ONE(base_nonce + 3U);\n"
"}\n"
"__kernel void scan_nonce_range(u32 fast0, u32 fast1, u32 fast2, u32 fast3,\n"
"                               u32 fast4, u32 fast5, u32 fast6, u32 fast7,\n"
"                               u32 target0, u32 target1, u32 target2, u32 target3,\n"
"                               u32 target4, u32 target5, u32 target6, u32 target7,\n"
"                               u32 tail0, u32 tail1, u32 tail2,\n"
"                               u32 start_nonce, u32 nonce_count,\n"
"                               u32 nonces_per_work_item,\n"
"                               u32 max_results,\n"
"                               __global volatile u32 *result_count,\n"
"                               __global u32 *matches) {\n"
"  u32 gid = (u32)get_global_id(0);\n"
"  if (nonce_count == 0U) return;\n"
"  u32 work_items = ((nonce_count - 1U) / nonces_per_work_item) + 1U;\n"
"  if (gid >= work_items) return;\n"
"  u32 base_nonce = gid * nonces_per_work_item;\n"
"  u32 limit = nonces_per_work_item;\n"
"  if (base_nonce + limit > nonce_count) limit = nonce_count - base_nonce;\n"
"  for (u32 item = 0U; item < limit; ++item) {\n"
"    SCAN_ONE(base_nonce + item);\n"
"  }\n"
"}\n";

static const char *k_opencl_kernel_noatomic_tail_kernel =
"__kernel void legacy_scan_nonce_range(__global const u32 *params,\n"
"                                      u32 start_nonce, u32 nonce_count,\n"
"                                      u32 nonces_per_work_item,\n"
"                                      u32 max_results,\n"
"                                      __global u32 *match_flags,\n"
"                                      __global u32 *matches) {\n"
"  (void)max_results;\n"
"  u32 gid = (u32)get_global_id(0);\n"
"  if (nonce_count == 0U) return;\n"
"  u32 work_items = ((nonce_count - 1U) / nonces_per_work_item) + 1U;\n"
"  if (gid >= work_items) return;\n"
"  u32 base_nonce = gid * nonces_per_work_item;\n"
"  u32 limit = nonces_per_work_item;\n"
"  if (base_nonce + limit > nonce_count) limit = nonce_count - base_nonce;\n"
"  for (u32 item = 0U; item < limit; ++item) {\n"
"    u32 slot = base_nonce + item;\n"
"    scan_one_nonce(params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7],\n"
"                   params[8], params[9], params[10], params[11], params[12], params[13], params[14], params[15],\n"
"                   params[16], params[17], params[18], start_nonce + slot, slot, match_flags, matches);\n"
"  }\n"
"}\n";

static const char *k_opencl_kernel_modern_fixed_kernels =
"#define SCAN_KERNEL_ARGS \\\n"
"  u32 fast0, u32 fast1, u32 fast2, u32 fast3, \\\n"
"  u32 fast4, u32 fast5, u32 fast6, u32 fast7, \\\n"
"  u32 target0, u32 target1, u32 target2, u32 target3, \\\n"
"  u32 target4, u32 target5, u32 target6, u32 target7, \\\n"
"  u32 tail0, u32 tail1, u32 tail2, \\\n"
"  u32 start_nonce, u32 nonce_count, \\\n"
"  u32 nonces_per_work_item, \\\n"
"  u32 max_results, \\\n"
"  __global volatile u32 *result_count, \\\n"
"  __global u32 *matches\n"
"#define SCAN_ONE(base_nonce) \\\n"
"  scan_one_nonce(fast0, fast1, fast2, fast3, fast4, fast5, fast6, fast7, \\\n"
"                 target0, target1, target2, target3, target4, target5, target6, target7, \\\n"
"                 tail0, tail1, tail2, start_nonce + (base_nonce), max_results, result_count, matches)\n"
"__kernel void modern_scan_nonce_range_fixed_npi1(SCAN_KERNEL_ARGS) {\n"
"  (void)nonces_per_work_item;\n"
"  u32 gid = (u32)get_global_id(0);\n"
"  if (gid >= nonce_count) return;\n"
"  SCAN_ONE(gid);\n"
"}\n"
"__kernel void modern_scan_nonce_range_fixed_npi2(SCAN_KERNEL_ARGS) {\n"
"  (void)nonces_per_work_item;\n"
"  u32 base_nonce = (u32)get_global_id(0) * 2U;\n"
"  if (base_nonce >= nonce_count) return;\n"
"  SCAN_ONE(base_nonce);\n"
"  if (base_nonce + 1U < nonce_count) SCAN_ONE(base_nonce + 1U);\n"
"}\n"
"__kernel void modern_scan_nonce_range_fixed_npi4(SCAN_KERNEL_ARGS) {\n"
"  (void)nonces_per_work_item;\n"
"  u32 base_nonce = (u32)get_global_id(0) * 4U;\n"
"  if (base_nonce >= nonce_count) return;\n"
"  SCAN_ONE(base_nonce);\n"
"  if (base_nonce + 1U < nonce_count) SCAN_ONE(base_nonce + 1U);\n"
"  if (base_nonce + 2U < nonce_count) SCAN_ONE(base_nonce + 2U);\n"
"  if (base_nonce + 3U < nonce_count) SCAN_ONE(base_nonce + 3U);\n"
"}\n";

static const char *k_opencl_kernel_modern_register_prefix =
"typedef uint2 u32v;\n"
"__constant u32 K256V[64] = {\n"
"0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,\n"
"0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,\n"
"0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,\n"
"0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,\n"
"0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,\n"
"0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,\n"
"0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,\n"
"0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};\n"
"inline u32v rotr32v(u32v x, u32 n) { return (x >> n) | (x << (32U - n)); }\n"
"inline u32v bswap32v(u32v v) { return ((v & (u32v)(0x000000ffU)) << 24) | ((v & (u32v)(0x0000ff00U)) << 8) | ((v & (u32v)(0x00ff0000U)) >> 8) | ((v & (u32v)(0xff000000U)) >> 24); }\n"
"#define SS0V(x) (rotr32v((x), 7) ^ rotr32v((x), 18) ^ ((x) >> 3))\n"
"#define SS1V(x) (rotr32v((x), 17) ^ rotr32v((x), 19) ^ ((x) >> 10))\n"
"#define BS0V(x) (rotr32v((x), 2) ^ rotr32v((x), 13) ^ rotr32v((x), 22))\n"
"#define BS1V(x) (rotr32v((x), 6) ^ rotr32v((x), 11) ^ rotr32v((x), 25))\n"
"#define CHV(x,y,z) (((x) & (y)) ^ (~(x) & (z)))\n"
"#define MAJV(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))\n"
"inline void compress_vec2(u32v st[8], u32v w[16]) {\n"
"  u32v a=st[0], b=st[1], c=st[2], d=st[3], e=st[4], f=st[5], g=st[6], h=st[7];\n"
"  for (int i = 0; i < 64; ++i) {\n"
"    u32v wi;\n"
"    if (i < 16) {\n"
"      wi = w[i];\n"
"    } else {\n"
"      int j = i & 15;\n"
"      wi = w[j] + SS1V(w[(j + 14) & 15]) + w[(j + 9) & 15] + SS0V(w[(j + 1) & 15]);\n"
"      w[j] = wi;\n"
"    }\n"
"    u32v t1 = h + BS1V(e) + CHV(e, f, g) + (u32v)(K256V[i]) + wi;\n"
"    u32v t2 = BS0V(a) + MAJV(a, b, c);\n"
"    h = g; g = f; f = e; e = d + t1;\n"
"    d = c; c = b; b = a; a = t1 + t2;\n"
"  }\n"
"  st[0]+=a; st[1]+=b; st[2]+=c; st[3]+=d; st[4]+=e; st[5]+=f; st[6]+=g; st[7]+=h;\n"
"}\n";

static const char *k_opencl_kernel_modern_register_scan =
"inline void scan_two_nonces_vec(u32 fast0, u32 fast1, u32 fast2, u32 fast3,\n"
"                                u32 fast4, u32 fast5, u32 fast6, u32 fast7,\n"
"                                u32 target0, u32 target1, u32 target2, u32 target3,\n"
"                                u32 target4, u32 target5, u32 target6, u32 target7,\n"
"                                u32 tail0, u32 tail1, u32 tail2, u32 nonce0, int second_valid,\n"
"                                u32 max_results,\n"
"                                __global volatile u32 *result_count,\n"
"                                __global u32 *matches) {\n"
"  u32v nonce = (u32v)(nonce0, nonce0 + 1U);\n"
"  u32v s[8];\n"
"  s[0]=(u32v)(fast0); s[1]=(u32v)(fast1); s[2]=(u32v)(fast2); s[3]=(u32v)(fast3);\n"
"  s[4]=(u32v)(fast4); s[5]=(u32v)(fast5); s[6]=(u32v)(fast6); s[7]=(u32v)(fast7);\n"
"  u32v w[16];\n"
"  w[0]=(u32v)(tail0); w[1]=(u32v)(tail1); w[2]=(u32v)(tail2); w[3]=bswap32v(nonce);\n"
"  w[4]=(u32v)(0x80000000U); w[5]=(u32v)(0U); w[6]=(u32v)(0U); w[7]=(u32v)(0U);\n"
"  w[8]=(u32v)(0U); w[9]=(u32v)(0U); w[10]=(u32v)(0U); w[11]=(u32v)(0U);\n"
"  w[12]=(u32v)(0U); w[13]=(u32v)(0U); w[14]=(u32v)(0U); w[15]=(u32v)(640U);\n"
"  compress_vec2(s, w);\n"
"  u32v h[8];\n"
"  h[0]=(u32v)(0x6a09e667U); h[1]=(u32v)(0xbb67ae85U); h[2]=(u32v)(0x3c6ef372U); h[3]=(u32v)(0xa54ff53aU);\n"
"  h[4]=(u32v)(0x510e527fU); h[5]=(u32v)(0x9b05688cU); h[6]=(u32v)(0x1f83d9abU); h[7]=(u32v)(0x5be0cd19U);\n"
"  w[0]=s[0]; w[1]=s[1]; w[2]=s[2]; w[3]=s[3]; w[4]=s[4]; w[5]=s[5]; w[6]=s[6]; w[7]=s[7];\n"
"  w[8]=(u32v)(0x80000000U); w[9]=(u32v)(0U); w[10]=(u32v)(0U); w[11]=(u32v)(0U);\n"
"  w[12]=(u32v)(0U); w[13]=(u32v)(0U); w[14]=(u32v)(0U); w[15]=(u32v)(256U);\n"
"  compress_vec2(h, w);\n"
"  if (meets_target_words(h[0].s0,h[1].s0,h[2].s0,h[3].s0,h[4].s0,h[5].s0,h[6].s0,h[7].s0,target0,target1,target2,target3,target4,target5,target6,target7)) {\n"
"    store_match_words(nonce0,h[0].s0,h[1].s0,h[2].s0,h[3].s0,h[4].s0,h[5].s0,h[6].s0,h[7].s0,max_results,result_count,matches);\n"
"  }\n"
"  if (second_valid && meets_target_words(h[0].s1,h[1].s1,h[2].s1,h[3].s1,h[4].s1,h[5].s1,h[6].s1,h[7].s1,target0,target1,target2,target3,target4,target5,target6,target7)) {\n"
"    store_match_words(nonce0 + 1U,h[0].s1,h[1].s1,h[2].s1,h[3].s1,h[4].s1,h[5].s1,h[6].s1,h[7].s1,max_results,result_count,matches);\n"
"  }\n"
"}\n";

static const char *k_opencl_kernel_modern_register_kernel =
"#define SCAN_KERNEL_ARGS \\\n"
"  u32 fast0, u32 fast1, u32 fast2, u32 fast3, \\\n"
"  u32 fast4, u32 fast5, u32 fast6, u32 fast7, \\\n"
"  u32 target0, u32 target1, u32 target2, u32 target3, \\\n"
"  u32 target4, u32 target5, u32 target6, u32 target7, \\\n"
"  u32 tail0, u32 tail1, u32 tail2, \\\n"
"  u32 start_nonce, u32 nonce_count, \\\n"
"  u32 nonces_per_work_item, \\\n"
"  u32 max_results, \\\n"
"  __global volatile u32 *result_count, \\\n"
"  __global u32 *matches\n"
"__kernel void modern_scan_nonce_range_register_heavy(SCAN_KERNEL_ARGS) {\n"
"  (void)nonces_per_work_item;\n"
"  u32 base_nonce = (u32)get_global_id(0) * 2U;\n"
"  if (base_nonce >= nonce_count) return;\n"
"  scan_two_nonces_vec(fast0, fast1, fast2, fast3, fast4, fast5, fast6, fast7,\n"
"                      target0, target1, target2, target3, target4, target5, target6, target7,\n"
"                      tail0, tail1, tail2, start_nonce + base_nonce, base_nonce + 1U < nonce_count,\n"
"                      max_results, result_count, matches);\n"
"}\n";

static void set_error(char *error, size_t error_size, const char *message, cl_int code) {
    if (error == NULL || error_size == 0) {
        return;
    }
    if (code == CL_SUCCESS) {
        snprintf(error, error_size, "%s", message);
    } else {
        snprintf(error, error_size, "%s (OpenCL error %d)", message, (int)code);
    }
}

static void append_text(char *out, size_t out_size, const char *fmt, ...) {
    if (out == NULL || out_size == 0) {
        return;
    }
    size_t used = strlen(out);
    if (used >= out_size - 1) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(out + used, out_size - used, fmt, args);
    va_end(args);
}

static uint32_t config_u32_or(uint32_t value, uint32_t fallback) {
    return value == 0 ? fallback : value;
}

static uint32_t clamp_nonces_per_work_item(uint32_t value) {
    if (value == 0) {
        return OPENCL_DEFAULT_NONCES_PER_WORK_ITEM;
    }
    if (value > OPENCL_MAX_NONCES_PER_WORK_ITEM) {
        return OPENCL_MAX_NONCES_PER_WORK_ITEM;
    }
    return value;
}

static char ascii_tolower_char(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int string_contains_ci(const char *text, const char *needle) {
    size_t needle_len;

    if (text == NULL || needle == NULL) {
        return 0;
    }
    needle_len = strlen(needle);
    if (needle_len == 0) {
        return 1;
    }

    for (const char *p = text; *p != '\0'; ++p) {
        size_t i = 0;
        while (i < needle_len &&
               p[i] != '\0' &&
               ascii_tolower_char(p[i]) == ascii_tolower_char(needle[i])) {
            ++i;
        }
        if (i == needle_len) {
            return 1;
        }
    }
    return 0;
}

static int device_prefers_legacy_noatomic(const char *vendor,
                                          const char *name,
                                          const char *version) {
    (void)vendor;
    (void)version;
    return string_contains_ci(name, "caicos");
}

static void apply_legacy_noatomic_limits(miner_opencl_device_config_t *device) {
    if (device == NULL || device->kernel_variant != MINER_OPENCL_KERNEL_LEGACY_NOATOMIC) {
        return;
    }
    if (device->batch_size == 0 || device->batch_size > OPENCL_LEGACY_NOATOMIC_MAX_BATCH_SIZE) {
        device->batch_size = OPENCL_LEGACY_NOATOMIC_MAX_BATCH_SIZE;
    }
    if (device->local_work_size == 0) {
        device->local_work_size = 64U;
    }
}

static void apply_legacy_noatomic_device_info(miner_opencl_device_config_t *device,
                                              const char *vendor,
                                              const char *name,
                                              const char *version) {
    if (device == NULL) {
        return;
    }
    if (device->kernel_variant == MINER_OPENCL_KERNEL_AUTO &&
        device_prefers_legacy_noatomic(vendor, name, version)) {
        device->kernel_variant = MINER_OPENCL_KERNEL_LEGACY_NOATOMIC;
    }
    apply_legacy_noatomic_limits(device);
}

static uint32_t choose_local_work_size(uint32_t preferred, size_t max_work_group) {
    const uint32_t candidates[] = {512U, 256U, 128U, 64U, 32U, 16U, 8U, 4U, 2U, 1U};

    if (max_work_group == 0) {
        return 0U;
    }
    if (preferred > 0 && preferred <= max_work_group) {
        return preferred;
    }
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (candidates[i] <= max_work_group) {
            return candidates[i];
        }
    }
    return 0U;
}

static uint32_t default_local_work_size_for_device(const char *vendor,
                                                   const char *name,
                                                   size_t max_work_group) {
    uint32_t preferred = 256U;

    if (device_prefers_legacy_noatomic(vendor, name, NULL)) {
        preferred = 64U;
    } else if (string_contains_ci(vendor, "intel") || string_contains_ci(name, "intel")) {
        preferred = 128U;
    } else if (string_contains_ci(vendor, "amd") ||
               string_contains_ci(vendor, "advanced micro") ||
               string_contains_ci(name, "amd") ||
               string_contains_ci(name, "radeon")) {
        preferred = 256U;
    } else if (string_contains_ci(vendor, "nvidia") || string_contains_ci(name, "nvidia")) {
        preferred = 256U;
    }

    return choose_local_work_size(preferred, max_work_group);
}

static const char *kernel_name_for_npi(uint32_t nonces_per_work_item) {
    switch (nonces_per_work_item) {
    case 1U:
        return "scan_nonce_range_npi1";
    case 2U:
        return "scan_nonce_range_npi2";
    case 4U:
        return "scan_nonce_range_npi4";
    default:
        return "scan_nonce_range";
    }
}

static uint32_t kernel_variant_fixed_npi(int variant) {
    switch (variant) {
    case MINER_OPENCL_KERNEL_FIXED_NPI1:
        return 1U;
    case MINER_OPENCL_KERNEL_FIXED_NPI2:
        return 2U;
    case MINER_OPENCL_KERNEL_FIXED_NPI4:
        return 4U;
    default:
        return 0U;
    }
}

static uint32_t kernel_variant_forced_npi(int variant) {
    uint32_t fixed_npi = kernel_variant_fixed_npi(variant);
    if (fixed_npi != 0) {
        return fixed_npi;
    }
    if (variant == MINER_OPENCL_KERNEL_REGISTER_HEAVY) {
        return 2U;
    }
    return 0U;
}

static int kernel_variant_modern_only(int variant) {
    return kernel_variant_fixed_npi(variant) != 0 ||
        variant == MINER_OPENCL_KERNEL_REGISTER_HEAVY;
}

static const char *kernel_name_for_variant(int variant, uint32_t nonces_per_work_item) {
    switch (variant) {
    case MINER_OPENCL_KERNEL_FIXED_NPI1:
        return "modern_scan_nonce_range_fixed_npi1";
    case MINER_OPENCL_KERNEL_FIXED_NPI2:
        return "modern_scan_nonce_range_fixed_npi2";
    case MINER_OPENCL_KERNEL_FIXED_NPI4:
        return "modern_scan_nonce_range_fixed_npi4";
    case MINER_OPENCL_KERNEL_REGISTER_HEAVY:
        return "modern_scan_nonce_range_register_heavy";
    case MINER_OPENCL_KERNEL_LEGACY_NOATOMIC:
        return "legacy_scan_nonce_range";
    default:
        return kernel_name_for_npi(nonces_per_work_item);
    }
}

static int normalize_kernel_variant(int variant) {
    if (variant == MINER_OPENCL_KERNEL_COMPACT ||
        variant == MINER_OPENCL_KERNEL_UNROLLED ||
        variant == MINER_OPENCL_KERNEL_FIXED_NPI1 ||
        variant == MINER_OPENCL_KERNEL_FIXED_NPI2 ||
        variant == MINER_OPENCL_KERNEL_FIXED_NPI4 ||
        variant == MINER_OPENCL_KERNEL_REGISTER_HEAVY ||
        variant == MINER_OPENCL_KERNEL_LEGACY_NOATOMIC) {
        return variant;
    }
    return MINER_OPENCL_KERNEL_AUTO;
}

static int normalize_backend_variant(int variant) {
    if (variant == MINER_OPENCL_BACKEND_COMPAT10 || variant == MINER_OPENCL_BACKEND_MODERN) {
        return variant;
    }
    return MINER_OPENCL_BACKEND_AUTO;
}

static const char *opencl_backend_variant_name(int variant) {
    switch (variant) {
    case MINER_OPENCL_BACKEND_COMPAT10:
        return "compat10";
    case MINER_OPENCL_BACKEND_MODERN:
        return "modern";
    default:
        return "auto";
    }
}

static const char *opencl_kernel_variant_name(int variant) {
    switch (variant) {
    case MINER_OPENCL_KERNEL_COMPACT:
        return "compact";
    case MINER_OPENCL_KERNEL_UNROLLED:
        return "unrolled";
    case MINER_OPENCL_KERNEL_FIXED_NPI1:
        return "fixed-npi1";
    case MINER_OPENCL_KERNEL_FIXED_NPI2:
        return "fixed-npi2";
    case MINER_OPENCL_KERNEL_FIXED_NPI4:
        return "fixed-npi4";
    case MINER_OPENCL_KERNEL_REGISTER_HEAVY:
        return "register-heavy";
    case MINER_OPENCL_KERNEL_LEGACY_NOATOMIC:
        return "legacy-noatomic";
    default:
        return "auto";
    }
}

static int parse_opencl_version(const char *text, int *major, int *minor) {
    int parsed_major = 0;
    int parsed_minor = 0;
    if (text == NULL) {
        return -1;
    }
    if (sscanf(text, "OpenCL %d.%d", &parsed_major, &parsed_minor) != 2) {
        return -1;
    }
    if (major != NULL) {
        *major = parsed_major;
    }
    if (minor != NULL) {
        *minor = parsed_minor;
    }
    return 0;
}

static int opencl_version_at_least(const char *text, int major, int minor) {
    int parsed_major = 0;
    int parsed_minor = 0;
    if (parse_opencl_version(text, &parsed_major, &parsed_minor) != 0) {
        return 0;
    }
    return parsed_major > major || (parsed_major == major && parsed_minor >= minor);
}

static int extension_list_has(const char *extensions, const char *needle) {
    if (extensions == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }

    size_t needle_len = strlen(needle);
    const char *p = extensions;

    while ((p = strstr(p, needle)) != NULL) {
        int left_ok = p == extensions || p[-1] == ' ';
        char right = p[needle_len];
        int right_ok = right == '\0' || right == ' ';
        if (left_ok && right_ok) {
            return 1;
        }
        p += needle_len;
    }
    return 0;
}

static int device_has_extension(cl_device_id device, const char *needle) {
    size_t size = 0;
    if (clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, 0, NULL, &size) != CL_SUCCESS || size == 0) {
        return 0;
    }

    char *extensions = malloc(size);
    if (extensions == NULL) {
        return 0;
    }
    if (clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, size, extensions, NULL) != CL_SUCCESS) {
        free(extensions);
        return 0;
    }

    int found = extension_list_has(extensions, needle);
    free(extensions);
    return found;
}

static int validate_compat10_device(cl_device_id device,
                                    const char *device_version,
                                    char *error,
                                    size_t error_size) {
    if (!opencl_version_at_least(device_version, 1, 0)) {
        set_error(error, error_size, "selected OpenCL device did not report a usable OpenCL 1.x version", CL_SUCCESS);
        return -1;
    }

    if (!opencl_version_at_least(device_version, 1, 1) &&
        !device_has_extension(device, "cl_khr_global_int32_base_atomics")) {
        set_error(error,
                  error_size,
                  "compat10 backend requires OpenCL 1.1+ or cl_khr_global_int32_base_atomics",
                  CL_SUCCESS);
        return -1;
    }

    return 0;
}

static int validate_modern_device(const char *device_version,
                                  char *error,
                                  size_t error_size) {
    if (!opencl_version_at_least(device_version, 1, 2)) {
        set_error(error,
                  error_size,
                  "modern backend requires OpenCL 1.2 or newer",
                  CL_SUCCESS);
        return -1;
    }
    return 0;
}

static int resolve_backend_variant(cl_device_id device,
                                   const char *device_version,
                                   int requested,
                                   char *error,
                                   size_t error_size) {
    requested = normalize_backend_variant(requested);
    if (requested == MINER_OPENCL_BACKEND_AUTO) {
        requested = opencl_version_at_least(device_version, 1, 2) ?
            MINER_OPENCL_BACKEND_MODERN : MINER_OPENCL_BACKEND_COMPAT10;
    }

    if (requested == MINER_OPENCL_BACKEND_MODERN) {
        if (validate_modern_device(device_version, error, error_size) != 0) {
            return -1;
        }
        return MINER_OPENCL_BACKEND_MODERN;
    }

    if (validate_compat10_device(device, device_version, error, error_size) != 0) {
        return -1;
    }
    return MINER_OPENCL_BACKEND_COMPAT10;
}

static int select_platform_device(const miner_opencl_config_t *config,
                                  cl_platform_id *platform_out,
                                  cl_device_id *device_out,
                                  char *error,
                                  size_t error_size) {
    cl_uint platform_count = 0;
    cl_int rc = clGetPlatformIDs(0, NULL, &platform_count);
    if (rc != CL_SUCCESS || platform_count == 0) {
        set_error(error, error_size, "no OpenCL platforms found", rc);
        return -1;
    }

    cl_platform_id *platforms = calloc(platform_count, sizeof(*platforms));
    if (platforms == NULL) {
        set_error(error, error_size, "OpenCL platform allocation failed", CL_SUCCESS);
        return -1;
    }

    rc = clGetPlatformIDs(platform_count, platforms, NULL);
    if (rc != CL_SUCCESS) {
        free(platforms);
        set_error(error, error_size, "failed to enumerate OpenCL platforms", rc);
        return -1;
    }

    int platform_index = config != NULL ? config->platform : 0;
    if (platform_index < 0 || (cl_uint)platform_index >= platform_count) {
        free(platforms);
        set_error(error, error_size, "configured OpenCL platform index is out of range", CL_SUCCESS);
        return -1;
    }

    cl_platform_id platform = platforms[platform_index];
    free(platforms);

    cl_uint device_count = 0;
    rc = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, NULL, &device_count);
    if (rc != CL_SUCCESS || device_count == 0) {
        set_error(error, error_size, "no OpenCL devices found on selected platform", rc);
        return -1;
    }

    cl_device_id *devices = calloc(device_count, sizeof(*devices));
    if (devices == NULL) {
        set_error(error, error_size, "OpenCL device allocation failed", CL_SUCCESS);
        return -1;
    }

    rc = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, device_count, devices, NULL);
    if (rc != CL_SUCCESS) {
        free(devices);
        set_error(error, error_size, "failed to enumerate OpenCL devices", rc);
        return -1;
    }

    int device_index = config != NULL ? config->device : 0;
    if (device_index < 0 || (cl_uint)device_index >= device_count) {
        free(devices);
        set_error(error, error_size, "configured OpenCL device index is out of range", CL_SUCCESS);
        return -1;
    }

    *platform_out = platform;
    *device_out = devices[device_index];
    free(devices);
    return 0;
}

static void opencl_device_config_apply_defaults(miner_opencl_device_config_t *device,
                                                const miner_opencl_config_t *config) {
    if (device == NULL || config == NULL) {
        return;
    }
    if (device->batch_size == 0) {
        device->batch_size = config_u32_or(config->batch_size, OPENCL_DEFAULT_BATCH_SIZE);
    }
    if (device->max_results == 0) {
        device->max_results = config_u32_or(config->max_results, OPENCL_DEFAULT_MAX_RESULTS);
    }
    if (device->local_work_size == 0) {
        device->local_work_size = config->local_work_size;
    }
    if (device->nonces_per_work_item == 0) {
        device->nonces_per_work_item = clamp_nonces_per_work_item(config->nonces_per_work_item);
    } else {
        device->nonces_per_work_item = clamp_nonces_per_work_item(device->nonces_per_work_item);
    }
    if (device->backend_variant == MINER_OPENCL_BACKEND_AUTO) {
        device->backend_variant = normalize_backend_variant(config->backend_variant);
    } else {
        device->backend_variant = normalize_backend_variant(device->backend_variant);
    }
    if (device->kernel_variant == MINER_OPENCL_KERNEL_AUTO) {
        device->kernel_variant = normalize_kernel_variant(config->kernel_variant);
    } else {
        device->kernel_variant = normalize_kernel_variant(device->kernel_variant);
    }
    apply_legacy_noatomic_limits(device);
}

int opencl_miner_resolve_devices(const miner_opencl_config_t *config,
                                 miner_opencl_device_config_t *devices_out,
                                 int max_devices,
                                 char *error,
                                 size_t error_size) {
    if (config == NULL || !config->enabled || devices_out == NULL || max_devices <= 0) {
        return 0;
    }

    if (config->device_count > 0) {
        int count = config->device_count < max_devices ? config->device_count : max_devices;
        for (int i = 0; i < count; ++i) {
            devices_out[i] = config->devices[i];
            opencl_device_config_apply_defaults(&devices_out[i], config);
        }
        return count;
    }

    if (!config->all_devices) {
        devices_out[0].platform = config->platform;
        devices_out[0].device = config->device;
        devices_out[0].batch_size = config_u32_or(config->batch_size, OPENCL_DEFAULT_BATCH_SIZE);
        devices_out[0].local_work_size = config->local_work_size;
        devices_out[0].nonces_per_work_item = clamp_nonces_per_work_item(config->nonces_per_work_item);
        devices_out[0].max_results = config_u32_or(config->max_results, OPENCL_DEFAULT_MAX_RESULTS);
        devices_out[0].backend_variant = normalize_backend_variant(config->backend_variant);
        devices_out[0].kernel_variant = normalize_kernel_variant(config->kernel_variant);
        if (btcrig_opencl_load(NULL, 0) == 0) {
            cl_platform_id platform = NULL;
            cl_device_id device = NULL;
            if (select_platform_device(config, &platform, &device, NULL, 0) == 0) {
                char name[128] = "";
                char vendor[128] = "";
                char version[128] = "";
                (void)clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name), name, NULL);
                (void)clGetDeviceInfo(device, CL_DEVICE_VENDOR, sizeof(vendor), vendor, NULL);
                (void)clGetDeviceInfo(device, CL_DEVICE_VERSION, sizeof(version), version, NULL);
                apply_legacy_noatomic_device_info(&devices_out[0], vendor, name, version);
            }
        }
        apply_legacy_noatomic_limits(&devices_out[0]);
        return 1;
    }

    if (btcrig_opencl_load(error, error_size) != 0) {
        return -1;
    }

    cl_uint platform_count = 0;
    cl_int rc = clGetPlatformIDs(0, NULL, &platform_count);
    if (rc != CL_SUCCESS || platform_count == 0) {
        set_error(error, error_size, "no OpenCL platforms found", rc);
        return -1;
    }

    cl_platform_id *platforms = calloc(platform_count, sizeof(*platforms));
    if (platforms == NULL) {
        set_error(error, error_size, "OpenCL platform allocation failed", CL_SUCCESS);
        return -1;
    }

    rc = clGetPlatformIDs(platform_count, platforms, NULL);
    if (rc != CL_SUCCESS) {
        free(platforms);
        set_error(error, error_size, "failed to enumerate OpenCL platforms", rc);
        return -1;
    }

    int found = 0;
    for (cl_uint p = 0; p < platform_count && found < max_devices; ++p) {
        cl_uint device_count = 0;
        rc = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_ALL, 0, NULL, &device_count);
        if (rc != CL_SUCCESS || device_count == 0) {
            continue;
        }

        cl_device_id *devices = calloc(device_count, sizeof(*devices));
        if (devices == NULL) {
            free(platforms);
            set_error(error, error_size, "OpenCL device allocation failed", CL_SUCCESS);
            return -1;
        }

        rc = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_ALL, device_count, devices, NULL);
        if (rc != CL_SUCCESS) {
            free(devices);
            continue;
        }

        for (cl_uint d = 0; d < device_count && found < max_devices; ++d) {
            cl_device_type type = 0;
            if (clGetDeviceInfo(devices[d], CL_DEVICE_TYPE, sizeof(type), &type, NULL) != CL_SUCCESS) {
                continue;
            }
            if ((type & CL_DEVICE_TYPE_GPU) == 0) {
                continue;
            }

            devices_out[found].platform = (int)p;
            devices_out[found].device = (int)d;
            devices_out[found].batch_size = config_u32_or(config->batch_size, OPENCL_DEFAULT_BATCH_SIZE);
            devices_out[found].local_work_size = config->local_work_size;
            devices_out[found].nonces_per_work_item = clamp_nonces_per_work_item(config->nonces_per_work_item);
            devices_out[found].max_results = config_u32_or(config->max_results, OPENCL_DEFAULT_MAX_RESULTS);
            devices_out[found].backend_variant = normalize_backend_variant(config->backend_variant);
            devices_out[found].kernel_variant = normalize_kernel_variant(config->kernel_variant);
            char name[128] = "";
            char vendor[128] = "";
            char version[128] = "";
            (void)clGetDeviceInfo(devices[d], CL_DEVICE_NAME, sizeof(name), name, NULL);
            (void)clGetDeviceInfo(devices[d], CL_DEVICE_VENDOR, sizeof(vendor), vendor, NULL);
            (void)clGetDeviceInfo(devices[d], CL_DEVICE_VERSION, sizeof(version), version, NULL);
            apply_legacy_noatomic_device_info(&devices_out[found], vendor, name, version);
            ++found;
        }

        free(devices);
    }

    free(platforms);
    if (found == 0) {
        set_error(error, error_size, "no OpenCL GPU devices found", CL_SUCCESS);
        return -1;
    }
    return found;
}

static const char *opencl_device_type_name(cl_device_type type) {
    if ((type & CL_DEVICE_TYPE_GPU) != 0) {
        return "GPU";
    }
    if ((type & CL_DEVICE_TYPE_CPU) != 0) {
        return "CPU";
    }
    if ((type & CL_DEVICE_TYPE_ACCELERATOR) != 0) {
        return "accelerator";
    }
    return "unknown";
}

int opencl_miner_describe_devices(const miner_opencl_config_t *config,
                                  char *out,
                                  size_t out_size) {
    if (out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';

    if (config == NULL || !config->enabled) {
        append_text(out, out_size, "Config: disabled\nRuntime: not probed\nMode: CPU only");
        return 0;
    }

    miner_opencl_device_config_t resolved[MINER_OPENCL_MAX_DEVICES];
    char error[512];
    error[0] = '\0';
    int count = opencl_miner_resolve_devices(config, resolved, MINER_OPENCL_MAX_DEVICES, error, sizeof(error));
    if (count <= 0) {
        append_text(out,
                    out_size,
                    "Config: enabled\nRuntime: unavailable\nMode: CPU only\nReason: %s",
                    error[0] != '\0' ? error : "no usable OpenCL GPU devices");
        return 0;
    }

    append_text(out,
                out_size,
                "Config: enabled\nRuntime: available\nDevices: %d\nMode: CPU + OpenCL when service starts",
                count);
    for (int i = 0; i < count; ++i) {
        miner_opencl_config_t single = *config;
        single.all_devices = 0;
        single.device_count = 0;
        single.platform = resolved[i].platform;
        single.device = resolved[i].device;

        cl_platform_id platform = NULL;
        cl_device_id device = NULL;
        error[0] = '\0';
        if (select_platform_device(&single, &platform, &device, error, sizeof(error)) != 0) {
            append_text(out,
                        out_size,
                        "\n#%d platform=%d device=%d unavailable: %s",
                        i,
                        resolved[i].platform,
                        resolved[i].device,
                        error[0] != '\0' ? error : "device query failed");
            continue;
        }

        char name[128] = "";
        char vendor[128] = "";
        char version[128] = "";
        cl_device_type type = 0;
        (void)clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name), name, NULL);
        (void)clGetDeviceInfo(device, CL_DEVICE_VENDOR, sizeof(vendor), vendor, NULL);
        (void)clGetDeviceInfo(device, CL_DEVICE_VERSION, sizeof(version), version, NULL);
        (void)clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(type), &type, NULL);

        char backend_error[256];
        backend_error[0] = '\0';
        int backend = resolve_backend_variant(device,
                                              version,
                                              resolved[i].backend_variant,
                                              backend_error,
                                              sizeof(backend_error));
        append_text(out,
                    out_size,
                    "\n#%d %s / %s / %s / %s / backend=%s / kernel=%s / batch=%u / local=%u / npi=%u",
                    i,
                    opencl_device_type_name(type),
                    vendor[0] != '\0' ? vendor : "unknown vendor",
                    name[0] != '\0' ? name : "unknown device",
                    version[0] != '\0' ? version : "unknown version",
                    backend >= 0 ? opencl_backend_variant_name(backend) : opencl_backend_variant_name(resolved[i].backend_variant),
                    opencl_kernel_variant_name(resolved[i].kernel_variant),
                    resolved[i].batch_size,
                    resolved[i].local_work_size,
                    resolved[i].nonces_per_work_item);
        if (backend < 0 && backend_error[0] != '\0') {
            append_text(out, out_size, " / backend warning: %s", backend_error);
        }
    }
    return count;
}

static int build_program(opencl_miner_t *miner, char *error, size_t error_size) {
    cl_int rc = CL_SUCCESS;
    const char *sources[8];
    size_t lengths[8];
    cl_uint source_count = 0;
    int use_modern_unrolled =
        miner->backend_variant == MINER_OPENCL_BACKEND_MODERN &&
        miner->kernel_variant == MINER_OPENCL_KERNEL_UNROLLED;
    int use_modern_fixed =
        miner->backend_variant == MINER_OPENCL_BACKEND_MODERN &&
        kernel_variant_fixed_npi(miner->kernel_variant) != 0;
    int use_modern_register =
        miner->backend_variant == MINER_OPENCL_BACKEND_MODERN &&
        miner->kernel_variant == MINER_OPENCL_KERNEL_REGISTER_HEAVY;
    int use_modern_scalar = use_modern_unrolled || use_modern_fixed || use_modern_register;
    int use_legacy_noatomic = miner->kernel_variant == MINER_OPENCL_KERNEL_LEGACY_NOATOMIC;

    if (use_legacy_noatomic) {
        sources[source_count] = k_opencl_kernel_compact;
        lengths[source_count++] = strlen(k_opencl_kernel_compact);
        sources[source_count] = k_opencl_kernel_noatomic_tail_helpers;
        lengths[source_count++] = strlen(k_opencl_kernel_noatomic_tail_helpers);
        sources[source_count] = k_opencl_kernel_noatomic_tail_kernel;
        lengths[source_count++] = strlen(k_opencl_kernel_noatomic_tail_kernel);
    } else if (miner->kernel_variant == MINER_OPENCL_KERNEL_COMPACT) {
        sources[source_count] = k_opencl_kernel_compact;
        lengths[source_count++] = strlen(k_opencl_kernel_compact);
    } else {
        sources[source_count] = k_opencl_kernel;
        lengths[source_count++] = strlen(k_opencl_kernel);
        sources[source_count] = k_opencl_kernel_rounds;
        lengths[source_count++] = strlen(k_opencl_kernel_rounds);
    }
    if (!use_legacy_noatomic && use_modern_scalar) {
        sources[source_count] = k_opencl_kernel_modern_scalar_macro;
        lengths[source_count++] = strlen(k_opencl_kernel_modern_scalar_macro);
        sources[source_count] = k_opencl_kernel_modern_tail_helpers;
        lengths[source_count++] = strlen(k_opencl_kernel_modern_tail_helpers);
    } else if (!use_legacy_noatomic) {
        sources[source_count] = k_opencl_kernel_tail_helpers;
        lengths[source_count++] = strlen(k_opencl_kernel_tail_helpers);
    }
    if (use_modern_register) {
        sources[source_count] = k_opencl_kernel_modern_register_prefix;
        lengths[source_count++] = strlen(k_opencl_kernel_modern_register_prefix);
        sources[source_count] = k_opencl_kernel_modern_register_scan;
        lengths[source_count++] = strlen(k_opencl_kernel_modern_register_scan);
    }
    if (!use_legacy_noatomic) {
        const char *tail_kernels = use_modern_register ?
            k_opencl_kernel_modern_register_kernel :
            (use_modern_fixed ? k_opencl_kernel_modern_fixed_kernels : k_opencl_kernel_tail_kernels);
        sources[source_count] = tail_kernels;
        lengths[source_count++] = strlen(tail_kernels);
    }

    miner->program = clCreateProgramWithSource(miner->context, source_count, sources, lengths, &rc);
    if (rc != CL_SUCCESS) {
        set_error(error, error_size, "failed to create OpenCL program", rc);
        return -1;
    }

    rc = clBuildProgram(miner->program, 1, &miner->device, NULL, NULL, NULL);
    if (rc != CL_SUCCESS) {
        char log[2048];
        size_t log_size = 0;
        log[0] = '\0';
        (void)clGetProgramBuildInfo(miner->program,
                                    miner->device,
                                    CL_PROGRAM_BUILD_LOG,
                                    sizeof(log) - 1,
                                    log,
                                    &log_size);
        if (log_size >= sizeof(log)) {
            log_size = sizeof(log) - 1;
        }
        log[log_size] = '\0';
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "failed to build OpenCL kernel: %.1800s", log);
        }
        return -1;
    }

    miner->kernel = clCreateKernel(miner->program,
                                   kernel_name_for_variant(miner->kernel_variant,
                                                           miner->nonces_per_work_item),
                                   &rc);
    if (rc != CL_SUCCESS) {
        set_error(error, error_size, "failed to create OpenCL kernel", rc);
        return -1;
    }

    return 0;
}

static void release_program_objects(opencl_miner_t *miner) {
    if (miner == NULL) {
        return;
    }
    if (miner->kernel != NULL) {
        clReleaseKernel(miner->kernel);
        miner->kernel = NULL;
    }
    if (miner->program != NULL) {
        clReleaseProgram(miner->program);
        miner->program = NULL;
    }
}

opencl_miner_t *opencl_miner_create(const miner_opencl_config_t *config,
                                    char *error,
                                    size_t error_size) {
    cl_int rc = CL_SUCCESS;
    if (btcrig_opencl_load(error, error_size) != 0) {
        return NULL;
    }

    opencl_miner_t *miner = calloc(1, sizeof(*miner));
    if (miner == NULL) {
        set_error(error, error_size, "OpenCL miner allocation failed", CL_SUCCESS);
        return NULL;
    }

    miner->batch_size = config != NULL ? config_u32_or(config->batch_size, OPENCL_DEFAULT_BATCH_SIZE) : OPENCL_DEFAULT_BATCH_SIZE;
    miner->local_work_size = config != NULL ? config->local_work_size : 0;
    miner->nonces_per_work_item = config != NULL ?
        clamp_nonces_per_work_item(config->nonces_per_work_item) :
        OPENCL_DEFAULT_NONCES_PER_WORK_ITEM;
    miner->max_results = config != NULL ? config_u32_or(config->max_results, OPENCL_DEFAULT_MAX_RESULTS) : OPENCL_DEFAULT_MAX_RESULTS;
    int requested_kernel_variant = config != NULL ?
        normalize_kernel_variant(config->kernel_variant) :
        MINER_OPENCL_KERNEL_AUTO;
    int requested_backend_variant = config != NULL ?
        normalize_backend_variant(config->backend_variant) :
        MINER_OPENCL_BACKEND_AUTO;
    miner->kernel_variant = requested_kernel_variant == MINER_OPENCL_KERNEL_AUTO ?
        MINER_OPENCL_KERNEL_UNROLLED : requested_kernel_variant;
    if (miner->batch_size < 1024U) {
        miner->batch_size = 1024U;
    }
    if (miner->max_results < 1U) {
        miner->max_results = 1U;
    }

    if (select_platform_device(config, &miner->platform, &miner->device, error, error_size) != 0) {
        opencl_miner_destroy(miner);
        return NULL;
    }

    (void)clGetDeviceInfo(miner->device, CL_DEVICE_NAME, sizeof(miner->device_name), miner->device_name, NULL);
    (void)clGetDeviceInfo(miner->device, CL_DEVICE_VENDOR, sizeof(miner->device_vendor), miner->device_vendor, NULL);
    (void)clGetDeviceInfo(miner->device, CL_DEVICE_VERSION, sizeof(miner->device_version), miner->device_version, NULL);
    if (miner->device_name[0] == '\0') {
        snprintf(miner->device_name, sizeof(miner->device_name), "unknown");
    }
    if (miner->device_vendor[0] == '\0') {
        snprintf(miner->device_vendor, sizeof(miner->device_vendor), "unknown");
    }
    if (miner->device_version[0] == '\0') {
        snprintf(miner->device_version, sizeof(miner->device_version), "unknown");
    }
    if (device_prefers_legacy_noatomic(miner->device_vendor,
                                       miner->device_name,
                                       miner->device_version)) {
        if (requested_kernel_variant == MINER_OPENCL_KERNEL_AUTO) {
            miner->kernel_variant = MINER_OPENCL_KERNEL_LEGACY_NOATOMIC;
        } else if (requested_kernel_variant != MINER_OPENCL_KERNEL_LEGACY_NOATOMIC) {
            set_error(error,
                      error_size,
                      "legacy OpenCL device requires --opencl-kernel legacy-noatomic",
                      CL_SUCCESS);
            opencl_miner_destroy(miner);
            return NULL;
        }
    }
    if (miner->kernel_variant == MINER_OPENCL_KERNEL_LEGACY_NOATOMIC &&
        miner->batch_size > OPENCL_LEGACY_NOATOMIC_MAX_BATCH_SIZE) {
        miner->batch_size = OPENCL_LEGACY_NOATOMIC_MAX_BATCH_SIZE;
    }
    int resolved_backend = resolve_backend_variant(miner->device,
                                                   miner->device_version,
                                                   requested_backend_variant,
                                                   error,
                                                   error_size);
    if (resolved_backend < 0) {
        opencl_miner_destroy(miner);
        return NULL;
    }
    miner->backend_variant = resolved_backend;
    if (kernel_variant_modern_only(miner->kernel_variant) &&
        miner->backend_variant != MINER_OPENCL_BACKEND_MODERN) {
        set_error(error,
                  error_size,
                  "modern-only OpenCL kernels require backend=modern",
                  CL_SUCCESS);
        opencl_miner_destroy(miner);
        return NULL;
    }
    uint32_t forced_npi = kernel_variant_forced_npi(miner->kernel_variant);
    if (forced_npi != 0) {
        miner->nonces_per_work_item = forced_npi;
    }

    size_t max_work_group = 0;
    (void)clGetDeviceInfo(miner->device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(max_work_group), &max_work_group, NULL);
    if (miner->local_work_size == 0) {
        miner->local_work_size = default_local_work_size_for_device(miner->device_vendor,
                                                                    miner->device_name,
                                                                    max_work_group);
    } else if (max_work_group > 0 && miner->local_work_size > max_work_group) {
        miner->local_work_size = (uint32_t)max_work_group;
    }

    cl_context_properties context_properties[] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties)miner->platform,
        0
    };
    miner->context = clCreateContext(context_properties, 1, &miner->device, NULL, NULL, &rc);
    if (rc != CL_SUCCESS) {
        set_error(error, error_size, "failed to create OpenCL context", rc);
        opencl_miner_destroy(miner);
        return NULL;
    }

    miner->queue = clCreateCommandQueue(miner->context, miner->device, 0, &rc);
    if (rc != CL_SUCCESS) {
        set_error(error, error_size, "failed to create OpenCL command queue", rc);
        opencl_miner_destroy(miner);
        return NULL;
    }

    if (build_program(miner, error, error_size) != 0) {
        if (requested_kernel_variant == MINER_OPENCL_KERNEL_AUTO &&
            miner->kernel_variant == MINER_OPENCL_KERNEL_UNROLLED) {
            release_program_objects(miner);
            miner->kernel_variant = MINER_OPENCL_KERNEL_COMPACT;
            if (error != NULL && error_size > 0) {
                error[0] = '\0';
            }
            if (build_program(miner, error, error_size) == 0) {
                goto program_built;
            }
        }
        opencl_miner_destroy(miner);
        return NULL;
    }

program_built:
    miner->legacy_noatomic = miner->kernel_variant == MINER_OPENCL_KERNEL_LEGACY_NOATOMIC;
    miner->result_slots = miner->legacy_noatomic ? miner->batch_size : miner->max_results;
    snprintf(miner->backend_name,
             sizeof(miner->backend_name),
             "%s-%s",
             opencl_backend_variant_name(miner->backend_variant),
             opencl_kernel_variant_name(miner->kernel_variant));

    size_t match_words = (size_t)miner->result_slots * 9U;
    if (miner->result_slots == 0 || match_words / 9U != (size_t)miner->result_slots) {
        set_error(error, error_size, "OpenCL result buffer is too large", CL_SUCCESS);
        opencl_miner_destroy(miner);
        return NULL;
    }

    size_t count_size = miner->legacy_noatomic ?
        (size_t)miner->result_slots * sizeof(uint32_t) :
        sizeof(uint32_t);
    if (miner->legacy_noatomic) {
        miner->params_buf = clCreateBuffer(miner->context,
                                           CL_MEM_READ_ONLY,
                                           sizeof(uint32_t) * OPENCL_LEGACY_PARAM_WORDS,
                                           NULL,
                                           &rc);
        if (rc != CL_SUCCESS) {
            set_error(error, error_size, "failed to create OpenCL legacy parameter buffer", rc);
            opencl_miner_destroy(miner);
            return NULL;
        }
    }
    miner->count_buf = clCreateBuffer(miner->context, CL_MEM_READ_WRITE, count_size, NULL, &rc);
    if (rc != CL_SUCCESS) {
        set_error(error, error_size, "failed to create OpenCL result count buffer", rc);
        opencl_miner_destroy(miner);
        return NULL;
    }
    miner->matches_buf = clCreateBuffer(miner->context,
                                        CL_MEM_WRITE_ONLY,
                                        sizeof(uint32_t) * match_words,
                                        NULL,
                                        &rc);
    if (rc != CL_SUCCESS) {
        set_error(error, error_size, "failed to create OpenCL matches buffer", rc);
        opencl_miner_destroy(miner);
        return NULL;
    }

    miner->matches = calloc(match_words, sizeof(*miner->matches));
    if (miner->matches == NULL) {
        set_error(error, error_size, "OpenCL host result allocation failed", CL_SUCCESS);
        opencl_miner_destroy(miner);
        return NULL;
    }
    if (miner->legacy_noatomic) {
        miner->match_flags = calloc((size_t)miner->result_slots, sizeof(*miner->match_flags));
        if (miner->match_flags == NULL) {
            set_error(error, error_size, "OpenCL host result flag allocation failed", CL_SUCCESS);
            opencl_miner_destroy(miner);
            return NULL;
        }
    }

    return miner;
}

void opencl_miner_destroy(opencl_miner_t *miner) {
    if (miner == NULL) {
        return;
    }
    free(miner->match_flags);
    free(miner->matches);
    if (miner->matches_buf != NULL) {
        clReleaseMemObject(miner->matches_buf);
    }
    if (miner->count_buf != NULL) {
        clReleaseMemObject(miner->count_buf);
    }
    if (miner->params_buf != NULL) {
        clReleaseMemObject(miner->params_buf);
    }
    if (miner->kernel != NULL) {
        clReleaseKernel(miner->kernel);
    }
    if (miner->program != NULL) {
        clReleaseProgram(miner->program);
    }
    if (miner->queue != NULL) {
        clReleaseCommandQueue(miner->queue);
    }
    if (miner->context != NULL) {
        clReleaseContext(miner->context);
    }
    free(miner);
}

uint32_t opencl_miner_batch_size(const opencl_miner_t *miner) {
    return miner != NULL ? miner->batch_size : OPENCL_DEFAULT_BATCH_SIZE;
}

uint32_t opencl_miner_local_work_size(const opencl_miner_t *miner) {
    return miner != NULL ? miner->local_work_size : 0U;
}

uint32_t opencl_miner_nonces_per_work_item(const opencl_miner_t *miner) {
    return miner != NULL ? miner->nonces_per_work_item : OPENCL_DEFAULT_NONCES_PER_WORK_ITEM;
}

const char *opencl_miner_backend_name(const opencl_miner_t *miner) {
    return miner != NULL ? miner->backend_name : "unavailable";
}

const char *opencl_miner_device_name(const opencl_miner_t *miner) {
    return miner != NULL ? miner->device_name : "unavailable";
}

const char *opencl_miner_device_version(const opencl_miner_t *miner) {
    return miner != NULL ? miner->device_version : "unavailable";
}

static void store_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t load_be32(const uint8_t *p) {
#if defined(__GNUC__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return __builtin_bswap32(v);
#else
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
#endif
}

static void make_self_test_header(uint8_t header[80]) {
    memset(header, 0, 80);
    header[0] = 0x01;
    header[68] = 0xff;
    header[69] = 0xff;
    header[70] = 0x00;
    header[71] = 0x1d;
}

typedef struct {
    uint8_t header[80];
    uint32_t start_nonce;
    uint32_t nonce_count;
    uint32_t seen_mask;
    int failed;
} opencl_self_test_context_t;

static void opencl_self_test_match(void *opaque, uint32_t nonce, const uint32_t hash_words[8]) {
    opencl_self_test_context_t *ctx = (opencl_self_test_context_t *)opaque;
    uint8_t cpu_hash[32];
    uint8_t opencl_hash[32];

    if (ctx == NULL || hash_words == NULL) {
        return;
    }
    if (nonce < ctx->start_nonce || nonce >= ctx->start_nonce + ctx->nonce_count) {
        ctx->failed = 1;
        return;
    }

    uint32_t offset = nonce - ctx->start_nonce;
    uint32_t bit = 1U << offset;
    if ((ctx->seen_mask & bit) != 0) {
        ctx->failed = 1;
        return;
    }
    ctx->seen_mask |= bit;

    store_le32(&ctx->header[76], nonce);
    sha256d_80(ctx->header, cpu_hash);
    sha256d_words_to_hash(hash_words, opencl_hash);
    if (memcmp(cpu_hash, opencl_hash, sizeof(cpu_hash)) != 0) {
        ctx->failed = 1;
    }
}

int opencl_miner_self_test(const miner_opencl_config_t *config,
                           opencl_self_test_result_t *result,
                           char *error,
                           size_t error_size) {
    const uint32_t nonce_count = 16U;
    uint8_t header[80];
    sha256_midstate_t midstate;
    uint32_t tail_words[4];
    uint32_t target_words[8];
    miner_opencl_config_t test_config;
    opencl_self_test_context_t context;

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }

    if (config != NULL) {
        test_config = *config;
    } else {
        miner_opencl_config_defaults(&test_config);
    }
    test_config.batch_size = 1024U;
    test_config.max_results = 32U;

    char create_error[2048];
    create_error[0] = '\0';
    opencl_miner_t *miner = opencl_miner_create(&test_config, create_error, sizeof(create_error));
    if (miner == NULL) {
        set_error(error, error_size, create_error[0] != '\0' ? create_error : "OpenCL self-test setup failed", CL_SUCCESS);
        return -1;
    }
    if (result != NULL) {
        snprintf(result->backend, sizeof(result->backend), "%s", opencl_miner_backend_name(miner));
        snprintf(result->device_name, sizeof(result->device_name), "%s", opencl_miner_device_name(miner));
        snprintf(result->device_version, sizeof(result->device_version), "%s", opencl_miner_device_version(miner));
        result->checked_nonces = nonce_count;
    }

    make_self_test_header(header);
    sha256d_80_midstate_prepare(&midstate, header);
    for (int i = 0; i < 4; ++i) {
        tail_words[i] = load_be32(header + 64 + i * 4);
    }
    for (int i = 0; i < 8; ++i) {
        target_words[i] = UINT32_MAX;
    }

    memset(&context, 0, sizeof(context));
    memcpy(context.header, header, sizeof(context.header));
    context.start_nonce = 0x13579b00U;
    context.nonce_count = nonce_count;

    if (opencl_miner_scan(miner,
                          &midstate,
                          tail_words,
                          target_words,
                          context.start_nonce,
                          nonce_count,
                          &context,
                          opencl_self_test_match) != 0) {
        opencl_miner_destroy(miner);
        set_error(error, error_size, "OpenCL self-test scan failed", CL_SUCCESS);
        return -1;
    }

    opencl_miner_destroy(miner);

    uint32_t expected_mask = (1U << nonce_count) - 1U;
    if (context.failed || context.seen_mask != expected_mask) {
        set_error(error, error_size, "OpenCL self-test hash verification failed", CL_SUCCESS);
        return -1;
    }

    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    return 0;
}

int opencl_miner_scan(opencl_miner_t *miner,
                      const sha256_midstate_t *state,
                      const uint32_t tail_words[4],
                      const uint32_t target_words[8],
                      uint32_t start_nonce,
                      uint32_t nonce_count,
                      void *opaque,
                      sha256d_scan_match_func_t on_match) {
    if (miner == NULL || state == NULL || tail_words == NULL || target_words == NULL) {
        return -1;
    }
    if (nonce_count == 0) {
        return 0;
    }
    if (miner->legacy_noatomic && nonce_count > miner->result_slots) {
        return -1;
    }

    cl_int rc = CL_SUCCESS;
    if (miner->legacy_noatomic) {
        uint32_t params[OPENCL_LEGACY_PARAM_WORDS];
        memcpy(params, state->fast_state, sizeof(uint32_t) * 8U);
        memcpy(params + 8U, target_words, sizeof(uint32_t) * 8U);
        memcpy(params + 16U, tail_words, sizeof(uint32_t) * 3U);
        rc = clEnqueueWriteBuffer(miner->queue,
                                  miner->params_buf,
                                  CL_TRUE,
                                  0,
                                  sizeof(params),
                                  params,
                                  0,
                                  NULL,
                                  NULL);
        if (rc != CL_SUCCESS) {
            return -1;
        }
        memset(miner->match_flags, 0, sizeof(uint32_t) * (size_t)nonce_count);
        rc = clEnqueueWriteBuffer(miner->queue,
                                  miner->count_buf,
                                  CL_TRUE,
                                  0,
                                  sizeof(uint32_t) * (size_t)nonce_count,
                                  miner->match_flags,
                                  0,
                                  NULL,
                                  NULL);
        if (rc != CL_SUCCESS) {
            return -1;
        }
    }
    if (!miner->legacy_noatomic) {
        uint32_t zero = 0;
        rc = clEnqueueWriteBuffer(miner->queue,
                                  miner->count_buf,
                                  CL_TRUE,
                                  0,
                                  sizeof(zero),
                                  &zero,
                                  0,
                                  NULL,
                                  NULL);
        if (rc != CL_SUCCESS) {
            return -1;
        }
    }

    int arg = 0;
    uint32_t max_results_arg = miner->legacy_noatomic ? miner->result_slots : miner->max_results;
    if (miner->legacy_noatomic) {
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(miner->params_buf), &miner->params_buf);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &start_nonce);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &nonce_count);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &miner->nonces_per_work_item);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &max_results_arg);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(miner->count_buf), &miner->count_buf);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(miner->matches_buf), &miner->matches_buf);
    } else {
        for (int i = 0; i < 8; ++i) {
            rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &state->fast_state[i]);
        }
        for (int i = 0; i < 8; ++i) {
            rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &target_words[i]);
        }
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &tail_words[0]);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &tail_words[1]);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &tail_words[2]);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &start_nonce);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &nonce_count);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &miner->nonces_per_work_item);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(uint32_t), &max_results_arg);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(miner->count_buf), &miner->count_buf);
        rc |= clSetKernelArg(miner->kernel, arg++, sizeof(miner->matches_buf), &miner->matches_buf);
    }
    if (rc != CL_SUCCESS) {
        return -1;
    }

    size_t global = ((size_t)nonce_count + miner->nonces_per_work_item - 1U) / miner->nonces_per_work_item;
    size_t local = miner->local_work_size;
    if (local > 0) {
        size_t rem = global % local;
        if (rem != 0) {
            global += local - rem;
        }
    }
    rc = clEnqueueNDRangeKernel(miner->queue,
                                miner->kernel,
                                1,
                                NULL,
                                &global,
                                local > 0 ? &local : NULL,
                                0,
                                NULL,
                                NULL);
    if (rc != CL_SUCCESS) {
        return -1;
    }
    if (miner->legacy_noatomic) {
        if (on_match == NULL) {
            return 0;
        }
        rc = clEnqueueReadBuffer(miner->queue,
                                 miner->count_buf,
                                 CL_TRUE,
                                 0,
                                 sizeof(uint32_t) * (size_t)nonce_count,
                                 miner->match_flags,
                                 0,
                                 NULL,
                                 NULL);
        if (rc != CL_SUCCESS) {
            return -1;
        }

        uint32_t count = 0;
        for (uint32_t i = 0; i < nonce_count; ++i) {
            if (miner->match_flags[i] != 0) {
                ++count;
            }
        }
        if (count == 0) {
            return 0;
        }

        rc = clEnqueueReadBuffer(miner->queue,
                                 miner->matches_buf,
                                 CL_TRUE,
                                 0,
                                 sizeof(uint32_t) * 9U * (size_t)nonce_count,
                                 miner->matches,
                                 0,
                                 NULL,
                                 NULL);
        if (rc != CL_SUCCESS) {
            return -1;
        }

        uint32_t emitted = 0;
        for (uint32_t i = 0; i < nonce_count && emitted < miner->max_results; ++i) {
            if (miner->match_flags[i] == 0) {
                continue;
            }
            uint32_t *entry = miner->matches + (size_t)i * 9U;
            on_match(opaque, entry[0], entry + 1);
            ++emitted;
        }
        return 0;
    }

    uint32_t count = 0;
    rc = clEnqueueReadBuffer(miner->queue,
                             miner->count_buf,
                             CL_TRUE,
                             0,
                             sizeof(count),
                             &count,
                             0,
                             NULL,
                             NULL);
    if (rc != CL_SUCCESS) {
        return -1;
    }

    if (count == 0 || on_match == NULL) {
        return 0;
    }

    uint32_t stored = count > miner->max_results ? miner->max_results : count;
    rc = clEnqueueReadBuffer(miner->queue,
                             miner->matches_buf,
                             CL_TRUE,
                             0,
                             sizeof(uint32_t) * 9U * stored,
                             miner->matches,
                             0,
                             NULL,
                             NULL);
    if (rc != CL_SUCCESS) {
        return -1;
    }

    for (uint32_t i = 0; i < stored; ++i) {
        uint32_t *entry = miner->matches + (size_t)i * 9U;
        on_match(opaque, entry[0], entry + 1);
    }

    return 0;
}
