#include "pch.h"
#include "../include/sr_api.h"

static sr_recorder_state g_state = sr_state_idle;

int32_t __stdcall sr_engine_get_version()
{
    return 1;
}

int32_t __stdcall sr_engine_initialize()
{
    g_state = sr_state_idle;
    return sr_result_ok;
}

int32_t __stdcall sr_engine_start()
{
    g_state = sr_state_recording;
    return sr_result_ok;
}

int32_t __stdcall sr_engine_pause()
{
    g_state = sr_state_paused;
    return sr_result_ok;
}

int32_t __stdcall sr_engine_resume()
{
    g_state = sr_state_recording;
    return sr_result_ok;
}

int32_t __stdcall sr_engine_stop()
{
    g_state = sr_state_idle;
    return sr_result_ok;
}
