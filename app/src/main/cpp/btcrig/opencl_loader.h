#ifndef BTCRIG_OPENCL_LOADER_H
#define BTCRIG_OPENCL_LOADER_H

#include <stddef.h>

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 100
#endif

#if defined(__APPLE__)
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

typedef struct {
    cl_int (CL_API_CALL *clGetPlatformIDs)(cl_uint num_entries,
                                           cl_platform_id *platforms,
                                           cl_uint *num_platforms);
    cl_int (CL_API_CALL *clGetDeviceIDs)(cl_platform_id platform,
                                         cl_device_type device_type,
                                         cl_uint num_entries,
                                         cl_device_id *devices,
                                         cl_uint *num_devices);
    cl_int (CL_API_CALL *clGetDeviceInfo)(cl_device_id device,
                                          cl_device_info param_name,
                                          size_t param_value_size,
                                          void *param_value,
                                          size_t *param_value_size_ret);
    cl_context (CL_API_CALL *clCreateContext)(const cl_context_properties *properties,
                                              cl_uint num_devices,
                                              const cl_device_id *devices,
                                              void (CL_CALLBACK *pfn_notify)(const char *errinfo,
                                                                             const void *private_info,
                                                                             size_t cb,
                                                                             void *user_data),
                                              void *user_data,
                                              cl_int *errcode_ret);
    cl_command_queue (CL_API_CALL *clCreateCommandQueue)(cl_context context,
                                                         cl_device_id device,
                                                         cl_command_queue_properties properties,
                                                         cl_int *errcode_ret);
    cl_mem (CL_API_CALL *clCreateBuffer)(cl_context context,
                                         cl_mem_flags flags,
                                         size_t size,
                                         void *host_ptr,
                                         cl_int *errcode_ret);
    cl_program (CL_API_CALL *clCreateProgramWithSource)(cl_context context,
                                                        cl_uint count,
                                                        const char **strings,
                                                        const size_t *lengths,
                                                        cl_int *errcode_ret);
    cl_int (CL_API_CALL *clBuildProgram)(cl_program program,
                                         cl_uint num_devices,
                                         const cl_device_id *device_list,
                                         const char *options,
                                         void (CL_CALLBACK *pfn_notify)(cl_program program,
                                                                        void *user_data),
                                         void *user_data);
    cl_int (CL_API_CALL *clGetProgramBuildInfo)(cl_program program,
                                                cl_device_id device,
                                                cl_program_build_info param_name,
                                                size_t param_value_size,
                                                void *param_value,
                                                size_t *param_value_size_ret);
    cl_kernel (CL_API_CALL *clCreateKernel)(cl_program program,
                                            const char *kernel_name,
                                            cl_int *errcode_ret);
    cl_int (CL_API_CALL *clReleaseKernel)(cl_kernel kernel);
    cl_int (CL_API_CALL *clReleaseProgram)(cl_program program);
    cl_int (CL_API_CALL *clReleaseMemObject)(cl_mem memobj);
    cl_int (CL_API_CALL *clReleaseCommandQueue)(cl_command_queue command_queue);
    cl_int (CL_API_CALL *clReleaseContext)(cl_context context);
    cl_int (CL_API_CALL *clEnqueueWriteBuffer)(cl_command_queue command_queue,
                                               cl_mem buffer,
                                               cl_bool blocking_write,
                                               size_t offset,
                                               size_t size,
                                               const void *ptr,
                                               cl_uint num_events_in_wait_list,
                                               const cl_event *event_wait_list,
                                               cl_event *event);
    cl_int (CL_API_CALL *clSetKernelArg)(cl_kernel kernel,
                                         cl_uint arg_index,
                                         size_t arg_size,
                                         const void *arg_value);
    cl_int (CL_API_CALL *clEnqueueNDRangeKernel)(cl_command_queue command_queue,
                                                 cl_kernel kernel,
                                                 cl_uint work_dim,
                                                 const size_t *global_work_offset,
                                                 const size_t *global_work_size,
                                                 const size_t *local_work_size,
                                                 cl_uint num_events_in_wait_list,
                                                 const cl_event *event_wait_list,
                                                 cl_event *event);
    cl_int (CL_API_CALL *clEnqueueReadBuffer)(cl_command_queue command_queue,
                                              cl_mem buffer,
                                              cl_bool blocking_read,
                                              size_t offset,
                                              size_t size,
                                              void *ptr,
                                              cl_uint num_events_in_wait_list,
                                              const cl_event *event_wait_list,
                                              cl_event *event);
} btcrig_opencl_api_t;

int btcrig_opencl_load(char *error, size_t error_size);
const btcrig_opencl_api_t *btcrig_opencl_api(void);

#endif
