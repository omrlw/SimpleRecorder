#pragma once

#include <stdint.h>
#include "sr_types.h"

#ifdef SIMPLE_RECORDER_ENGINE_NATIVE_EXPORTS
#define SR_API __declspec(dllexport)
#else
#define SR_API __declspec(dllimport)
#endif

extern "C"
{
    SR_API int32_t __stdcall sr_engine_get_version();
    SR_API int32_t __stdcall sr_engine_initialize();
    SR_API int32_t __stdcall sr_engine_start();
    SR_API int32_t __stdcall sr_engine_pause();
    SR_API int32_t __stdcall sr_engine_resume();
    SR_API int32_t __stdcall sr_engine_stop();
}
