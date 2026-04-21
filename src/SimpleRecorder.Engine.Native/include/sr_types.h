#pragma once

#include <stdint.h>

static const uint32_t sr_abi_version = 2;
static const uint32_t sr_struct_version = 1;

enum sr_result_code
{
    sr_result_ok = 0,
    sr_result_invalid_argument = 1,
    sr_result_invalid_state = 2,
    sr_result_not_initialized = 3
};

enum sr_recorder_state
{
    sr_state_idle = 0,
    sr_state_source_selected = 1,
    sr_state_countdown = 2,
    sr_state_recording = 3,
    sr_state_paused = 4,
    sr_state_stopping_saving = 5,
    sr_state_screenshot_success = 6,
    sr_state_error_non_blocking = 7
};

enum sr_capture_source_kind
{
    sr_capture_source_display = 0,
    sr_capture_source_window = 1,
    sr_capture_source_region = 2
};

struct sr_rect
{
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

struct sr_capture_source
{
    uint32_t version;
    int32_t kind;
    int64_t window_handle;
    sr_rect region;
};

struct sr_recording_options
{
    uint32_t version;
    int32_t frame_rate;
    int32_t resolution;
    int32_t quality_preset;
    int32_t countdown_seconds;
    int32_t include_system_audio;
    int32_t include_microphone;
};

struct sr_status_event
{
    uint32_t version;
    int32_t state;
    int32_t active_source_kind;
    int32_t countdown_remaining_seconds;
    int64_t timestamp_unix_millis;
};

typedef void* sr_engine_handle;
typedef void(__stdcall* sr_status_callback)(void* context, sr_status_event status);
