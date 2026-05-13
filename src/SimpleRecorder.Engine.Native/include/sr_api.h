#pragma once

#include "sr_types.h"

#ifdef SIMPLE_RECORDER_ENGINE_NATIVE_EXPORTS
#define SR_API __declspec(dllexport)
#else
#define SR_API __declspec(dllimport)
#endif

extern "C"
{
    SR_API int32_t __stdcall sr_engine_get_abi_version();
    SR_API sr_engine_handle __stdcall sr_engine_create();
    SR_API void __stdcall sr_engine_destroy(sr_engine_handle engine);
    SR_API int32_t __stdcall sr_engine_set_callback(sr_engine_handle engine, sr_status_callback callback, void* context);
    SR_API int32_t __stdcall sr_engine_initialize(sr_engine_handle engine);
    SR_API int32_t __stdcall sr_engine_prepare_recording_output(sr_engine_handle engine, const wchar_t* output_path);
    SR_API int32_t __stdcall sr_engine_write_compatibility_report(sr_engine_handle engine, const wchar_t* output_path);
    // Legacy ABI entry retained for version compatibility. Screenshot capture is not a product feature.
    SR_API int32_t __stdcall sr_engine_prepare_screenshot_output(sr_engine_handle engine, const wchar_t* output_path);
    SR_API int32_t __stdcall sr_engine_start(sr_engine_handle engine, const sr_capture_source* source, const sr_recording_options* options);
    SR_API int32_t __stdcall sr_engine_pause(sr_engine_handle engine);
    SR_API int32_t __stdcall sr_engine_resume(sr_engine_handle engine);
    SR_API int32_t __stdcall sr_engine_stop(sr_engine_handle engine);
    // Legacy ABI entry retained for version compatibility. It returns an unsupported failure result.
    SR_API int32_t __stdcall sr_engine_take_screenshot(sr_engine_handle engine, const sr_capture_source* source);
}
