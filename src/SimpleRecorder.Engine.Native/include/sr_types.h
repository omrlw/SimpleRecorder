#pragma once

enum sr_result_code
{
    sr_result_ok = 0,
    sr_result_not_implemented = 1
};

enum sr_recorder_state
{
    sr_state_idle = 0,
    sr_state_recording = 1,
    sr_state_paused = 2,
    sr_state_stopping = 3
};
