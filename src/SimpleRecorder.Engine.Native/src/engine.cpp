#include "pch.h"
#include "../include/sr_api.h"

// The native engine is intentionally partitioned into internal implementation
// includes below while remaining one translation unit. That keeps ABI/export
// behavior and anonymous-namespace visibility unchanged while making capture,
// processing, encoding, manifest, and lifecycle code reviewable by area.

#include <codecapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <propvarutil.h>
#include <roapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using system_clock = std::chrono::system_clock;

    constexpr size_t capture_slot_count = 6;
    constexpr size_t encode_snapshot_pool_multiplier = 4;
    constexpr size_t encode_snapshot_pool_minimum = 24;
    constexpr size_t wgc_frame_queue_limit = 3;
    constexpr int32_t monitor_frame_rate_option = 0;
    constexpr int32_t product_monitor_frame_rate_limit = 120;
    constexpr int32_t h264_level_52_macroblocks_per_second = 2'073'600;
    constexpr uint32_t audio_sample_rate = 48'000;
    constexpr uint32_t audio_channels = 2;
    constexpr uint32_t audio_bits_per_sample = 16;
    constexpr uint32_t audio_bytes_per_sample = audio_bits_per_sample / 8;
    constexpr uint32_t audio_bytes_per_frame = audio_channels * audio_bytes_per_sample;
    constexpr uint32_t audio_aac_bitrate = 192'000;
    constexpr uint32_t audio_mix_chunk_frames = 480;
    constexpr int32_t minimum_dimension = 2;
    constexpr int32_t maximum_auto_output_height = 2160;
    constexpr HRESULT capture_timeout = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    constexpr int32_t wgc_startup_warmup_millis = 1500;

    enum class capture_fallback_reason
    {
        none,
        startup_timeout,
        startup_hresult,
        stream_change,
        runtime_hresult,
        unsupported
    };

    struct d3d_context;
    struct recording_session;
    class audio_capture_controller;

    struct encoder_capability_probe;

    const char* capture_fallback_reason_name(capture_fallback_reason reason) noexcept;
    bool can_fallback_to_dxgi(int32_t source_kind) noexcept;
    bool detect_hardware_graphics_adapter() noexcept;
    std::string narrow_utf8(const std::wstring& value);
    std::string format_hresult(HRESULT value);
    void record_capture_fallback(
        recording_session& session,
        std::string from,
        std::string to,
        capture_fallback_reason reason,
        HRESULT hresult);
    void record_capture_backend_failure(
        recording_session& session,
        std::string backend,
        capture_fallback_reason reason,
        HRESULT hresult);
    const char* audio_mode_name(int32_t audio_mode) noexcept;
    bool audio_mode_includes_system(int32_t audio_mode) noexcept;
    bool audio_mode_includes_microphone(int32_t audio_mode) noexcept;
    HRESULT configure_audio_stream(IMFSinkWriter* sink_writer, recording_session& session, DWORD& stream_index);
    int64_t compute_wall_duration_qpc(const recording_session& session);

    enum class pipeline_mode
    {
        gpu_first,
        legacy_gdi
    };

    struct legacy_capture_slot
    {
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ previous_bitmap = nullptr;
        uint8_t* pixels = nullptr;
        uint32_t pixel_bytes = 0;
        int64_t captured_at_qpc = 0;
        int64_t capture_duration_qpc = 0;
        uint64_t sequence = 0;
    };

    struct gpu_capture_slot
    {
        ComPtr<ID3D11Texture2D> bgra_texture;
        ComPtr<ID3D11Texture2D> nv12_texture;
        ComPtr<ID3D11VideoProcessorInputView> input_view;
        ComPtr<ID3D11VideoProcessorOutputView> output_view;
        ComPtr<IMFMediaBuffer> sample_buffer;
        ComPtr<IMFSample> sample;
        DWORD sample_buffer_length = 0;
        RECT source_rect{ 0, 0, 0, 0 };
        int64_t captured_at_qpc = 0;
        int64_t capture_duration_qpc = 0;
        int64_t convert_duration_qpc = 0;
        uint64_t sequence = 0;
    };

    struct encode_snapshot_slot
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<IMFMediaBuffer> sample_buffer;
        ComPtr<IMFSample> sample;
        DWORD sample_buffer_length = 0;
    };

    struct recording_metrics
    {
        uint64_t capture_attempts = 0;
        uint64_t captured_frames = 0;
        uint64_t encoded_frames = 0;
        uint64_t backpressure_drop_count = 0;
        uint64_t capture_failure_count = 0;
        uint64_t duplicated_frame_count = 0;
        uint64_t pacing_overrun_count = 0;
        int64_t represented_duration_qpc = 0;
        int64_t represented_duration_hns = 0;
        uint32_t peak_queue_depth = 0;
        int64_t total_capture_latency_qpc = 0;
        int64_t total_queue_latency_qpc = 0;
        int64_t total_convert_latency_qpc = 0;
        int64_t total_encode_latency_qpc = 0;
        int64_t first_captured_qpc = 0;
        int64_t last_captured_qpc = 0;
        int64_t last_sample_duration_qpc = 0;
        LONGLONG first_sample_timestamp_hns = -1;
        LONGLONG last_sample_timestamp_hns = 0;
        LONGLONG last_sample_duration_hns = 0;
        int32_t output_width = 0;
        int32_t output_height = 0;
        uint64_t audio_samples_written = 0;
        uint64_t audio_packets_written = 0;
        uint64_t audio_discontinuities = 0;
        uint64_t audio_underflows = 0;
        double audio_drift_millis = 0.0;
    };

    struct encoder_quality_config
    {
        uint32_t target_bitrate_bps = 10'000'000;
        uint32_t max_bitrate_bps = 16'000'000;
        uint32_t quality_vs_speed = 65;
        uint32_t gop_size = 60;
        uint32_t h264_level = eAVEncH264VLevel5_2;
        uint32_t quality_policy_version = 2;
        bool cabac_requested = true;
        bool use_b_frames = false;
        bool low_latency_requested = false;
        bool real_time_requested = false;
        bool allow_frame_drops = false;
        bool frame_rate_conversion_disabled = true;
        bool prefer_quality_video_processing = true;
        bool edge_enhancement_requested = true;
        const char* quality_preset_name = "Balanced";
        const char* rate_control_mode = "PeakConstrainedVBR";
        const char* h264_level_name = "5.2";
    };

    struct recording_session
    {
        std::mutex gate;
        std::condition_variable ready_condition;
        std::condition_variable gpu_start_condition;
        bool stop_requested = false;
        bool pause_requested = false;
        bool capture_finished = false;
        bool encode_finished = false;
        bool gpu_resources_ready = false;
        bool gpu_encoder_ready = false;
        HRESULT failure = S_OK;
        std::wstring failure_reason;
        pipeline_mode mode = pipeline_mode::legacy_gdi;

        std::filesystem::path session_directory;
        std::filesystem::path final_output_path;
        sr_capture_source source{ sr_struct_version, sr_capture_source_display, 0, { 0, 0, 0, 0 } };
        sr_recording_options options{ sr_struct_version, 30, 0, 0, 0, 0, 0, sr_encoder_preference_auto, sr_video_codec_h264, sr_audio_capture_off, nullptr };
        std::wstring microphone_device_id;
        int32_t target_frame_rate = 30;
        int32_t monitor_refresh_rate = 0;
        std::string fps_cap_reason = "none";
        int32_t output_width = 0;
        int32_t output_height = 0;
        int64_t started_at_unix_millis = 0;
        int64_t started_qpc = 0;
        int64_t completed_qpc = 0;
        uint64_t next_sequence = 0;

        std::string capture_backend = "gdi-live";
        std::string requested_capture_backend = "gdi-live";
        std::string encode_backend = "media-foundation-h264-rgb32";
        std::string capture_fallback_from;
        std::string capture_fallback_to;
        capture_fallback_reason capture_fallback_reason_code = capture_fallback_reason::none;
        HRESULT capture_fallback_hresult = S_OK;
        bool hardware_encode = false;
        std::string hardware_encode_status = "software-or-unverified";
        std::string encoder_preference = "auto";
        std::string encoder_selection_reason = "not-selected";
        std::string encoder_fallback_reason = "none";
        std::string encoder_vendor = "unknown";
        std::string encoder_name = "unknown";
        std::string encoder_codec = "h264";
        std::string encoder_pixel_format = "nv12";
        std::string audio_codec = "none";
        std::string audio_status = "off";
        std::string system_audio_device_name = "default";
        std::string microphone_device_name = "default";
        std::string audio_failure_reason;
        std::string adapter_luid;
        encoder_quality_config quality_config;
        std::string encoder_config_status = "not-configured";
        std::string video_processor_usage = "not-used";
        bool edge_enhancement_applied = false;
        bool d3d_multithread_protected = false;
        bool gpu_hardware_detected = false;
        bool cpu_fallback_allowed = false;
        bool cpu_fallback_blocked = false;
        std::string cpu_fallback_block_reason = "none";
        HRESULT gpu_initialization_hresult = S_OK;
        std::string copy_integrity_status = "not-observed";
        std::string copy_integrity_failure_reason;
        std::string crop_resize_mismatch_reason = "none";
        uint64_t copy_dimension_mismatch_count = 0;
        bool wgc_startup_attempted = false;
        uint64_t wgc_resize_count = 0;
        std::string wgc_resize_status = "not-observed";
        std::string wgc_resize_failure_reason;
        std::wstring adapter_name;
        int64_t wgc_first_frame_latency_qpc = 0;

        recording_metrics metrics;
        std::thread capture_thread;
        std::thread encode_thread;
        std::deque<size_t> free_slots;
        std::deque<size_t> ready_slots;

        HDC screen_dc = nullptr;
        RECT legacy_capture_rect{ 0, 0, 0, 0 };
        std::vector<legacy_capture_slot> legacy_slots;

        RECT capture_item_rect{ 0, 0, 0, 0 };
        RECT capture_crop_rect{ 0, 0, 0, 0 };
        HMONITOR capture_monitor = nullptr;
        HWND capture_window = nullptr;
        std::vector<gpu_capture_slot> gpu_slots;
        std::unique_ptr<d3d_context> gpu_d3d;
    };

    struct stub_engine
    {
        std::mutex gate;
        sr_recorder_state state = sr_state_idle;
        sr_capture_source active_source{ sr_struct_version, sr_capture_source_display, 0, { 0, 0, 0, 0 } };
        sr_recording_options active_options{ sr_struct_version, 30, 0, 0, 0, 0, 0, sr_encoder_preference_auto, sr_video_codec_h264, sr_audio_capture_off, nullptr };
        sr_status_callback callback = nullptr;
        void* callback_context = nullptr;
        bool initialized = false;
        std::wstring pending_recording_path;
        std::wstring current_recording_path;
        std::unique_ptr<recording_session> active_recording;
    };

    struct monitor_info
    {
        HMONITOR handle = nullptr;
        RECT bounds{ 0, 0, 0, 0 };
        bool is_primary = false;
        std::wstring device_name;
    };

    struct resolved_capture_geometry
    {
        RECT requested_bounds{ 0, 0, 0, 0 };
        RECT capture_item_bounds{ 0, 0, 0, 0 };
        RECT source_rect{ 0, 0, 0, 0 };
        HMONITOR monitor = nullptr;
        HWND window = nullptr;
        bool spans_multiple_monitors = false;
    };

    struct encoder_capability_probe
    {
        bool has_h264_nv12_hardware = false;
        std::string encoder_name = "unknown";
        std::string encoder_vendor = "unknown";
        std::string hardware_url;
        uint32_t hardware_encoder_count = 0;
    };

    struct copied_capture_frame
    {
        RECT source_rect{ 0, 0, 0, 0 };
        int64_t captured_at_qpc = 0;
    };

    struct d3d_context
    {
        ComPtr<IDXGIFactory1> factory;
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11VideoDevice> video_device;
        ComPtr<ID3D11VideoContext> video_context;
        ComPtr<IMFDXGIDeviceManager> dxgi_device_manager;
        UINT dxgi_reset_token = 0;
        bool multithread_protected = false;
        bool hardware_adapter_detected = false;
        std::wstring adapter_name;
        LUID adapter_luid{};
    };

    struct media_foundation_scope
    {
        HRESULT co_initialize_result = E_FAIL;
        HRESULT mf_startup_result = E_FAIL;

        media_foundation_scope();
        ~media_foundation_scope();
        bool is_ready() const;
    };

    class capture_backend
    {
    public:
        virtual ~capture_backend() = default;
        virtual HRESULT start() = 0;
        virtual HRESULT copy_next_frame_to(ID3D11Texture2D* destination, copied_capture_frame& copied_frame) = 0;
        virtual void stop() = 0;
        virtual uint64_t consume_backpressure_drops() noexcept { return 0; }
        virtual const char* backend_name() const noexcept = 0;
        virtual capture_fallback_reason last_failure_reason() const noexcept { return capture_fallback_reason::none; }
        virtual bool has_received_first_frame() const noexcept { return false; }
        virtual int64_t first_frame_arrived_qpc() const noexcept { return 0; }
        virtual bool consume_resize(resolved_capture_geometry&) { return false; }
        virtual bool current_geometry(resolved_capture_geometry&) const { return false; }
        virtual std::string consume_copy_mismatch_reason() { return {}; }
    };

    class audio_capture_controller
    {
    public:
        audio_capture_controller(recording_session& session, IMFSinkWriter* sink_writer, DWORD stream_index, std::mutex& writer_gate);
        ~audio_capture_controller();

        audio_capture_controller(const audio_capture_controller&) = delete;
        audio_capture_controller& operator=(const audio_capture_controller&) = delete;

        void start();
        void stop() noexcept;

        struct capture_source_state;

    private:
        void run() noexcept;
        HRESULT initialize_source(bool system_loopback, const wchar_t* requested_device_id, capture_source_state& source);
        HRESULT read_source(capture_source_state& source, std::vector<float>& mix, uint32_t frame_count, bool& had_data);
        HRESULT write_mix(const std::vector<float>& mix, uint64_t start_frame, uint32_t frame_count);

        recording_session& _session;
        ComPtr<IMFSinkWriter> _sink_writer;
        DWORD _stream_index = 0;
        std::mutex& _writer_gate;
        std::thread _thread;
        std::atomic_bool _stop_requested{ false };
    };

    class frame_graph
    {
    public:
        frame_graph(
            d3d_context& d3d,
            std::vector<gpu_capture_slot>& slots,
            const RECT& capture_item_rect,
            int32_t output_width,
            int32_t output_height,
            int32_t target_frame_rate,
            bool prefer_quality_video_processing,
            bool edge_enhancement_requested);

        void process_slot(gpu_capture_slot& slot, const RECT& source_rect);
        HRESULT recreate_for_capture_item_rect(const RECT& capture_item_rect) noexcept;
        const std::string& video_processor_usage() const noexcept;
        bool edge_enhancement_applied() const noexcept;

    private:
        HRESULT create_video_processor(
            const D3D11_VIDEO_PROCESSOR_CONTENT_DESC& content_desc,
            D3D11_VIDEO_USAGE usage);
        bool try_enable_edge_enhancement();
        void create_slots(UINT input_width, UINT input_height);

        d3d_context& _d3d;
        std::vector<gpu_capture_slot>& _slots;
        RECT _capture_item_rect{ 0, 0, 0, 0 };
        int32_t _output_width = 0;
        int32_t _output_height = 0;
        int32_t _target_frame_rate = 30;
        std::string _video_processor_usage = "playback-normal";
        bool _edge_enhancement_requested = false;
        bool _edge_enhancement_applied = false;
        ComPtr<ID3D11VideoProcessorEnumerator> _enumerator;
        ComPtr<ID3D11VideoProcessor> _processor;
    };

    class encoder_backend
    {
    public:
        encoder_backend(recording_session& session, d3d_context& d3d);
        ~encoder_backend();

        bool is_hardware_encode() const noexcept;
        const char* backend_name() const noexcept;
        const char* hardware_encode_status() const noexcept;
        void prepare_slot_samples();
        void write_slot(gpu_capture_slot& slot, LONGLONG sample_time, LONGLONG sample_duration);
        HRESULT finalize() noexcept;
        void stop_audio() noexcept;

    private:
        void initialize_sink_writer();
        void prepare_encode_snapshot_pool(const D3D11_TEXTURE2D_DESC& texture_desc);
        encode_snapshot_slot& next_encode_snapshot();
        HRESULT create_sink_writer(
            bool enable_d3d_manager,
            bool disable_converters,
            bool configure_encoder,
            bool enable_hardware_transforms);

        recording_session& _session;
        d3d_context& _d3d;
        ComPtr<IMFSinkWriter> _sink_writer;
        DWORD _stream_index = 0;
        DWORD _audio_stream_index = static_cast<DWORD>(-1);
        bool _hardware_encode = false;
        std::wstring _last_stage;
        std::vector<encode_snapshot_slot> _encode_snapshots;
        size_t _next_encode_snapshot = 0;
        std::mutex _writer_gate;
        std::unique_ptr<audio_capture_controller> _audio_capture;
    };

    class wgc_capture_backend final : public capture_backend
    {
    public:
        wgc_capture_backend(d3d_context& d3d, const resolved_capture_geometry& geometry, const sr_capture_source& source);
        ~wgc_capture_backend() override;

        HRESULT start() override;
        HRESULT copy_next_frame_to(ID3D11Texture2D* destination, copied_capture_frame& copied_frame) override;
        void stop() override;
        uint64_t consume_backpressure_drops() noexcept override;
        const char* backend_name() const noexcept override;
        capture_fallback_reason last_failure_reason() const noexcept override;
        bool has_received_first_frame() const noexcept override;
        int64_t first_frame_arrived_qpc() const noexcept override;
        bool consume_resize(resolved_capture_geometry& geometry) override;
        bool current_geometry(resolved_capture_geometry& geometry) const override;
        std::string consume_copy_mismatch_reason() override;

    private:
        struct queued_frame
        {
            ComPtr<ID3D11Texture2D> texture;
            int64_t captured_at_qpc = 0;
        };

        ComPtr<ID3D11Texture2D> acquire_owned_frame_texture(uint32_t width, uint32_t height);
        void recycle_owned_frame_texture(ComPtr<ID3D11Texture2D> texture) noexcept;
        void recycle_ready_frames_locked() noexcept;
        void fail_locked(HRESULT failure, capture_fallback_reason reason) noexcept;
        void normalize_geometry_for_frame_size(int32_t width, int32_t height) noexcept;
        void on_frame_arrived(
            winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
            winrt::Windows::Foundation::IInspectable const&);

        d3d_context& _d3d;
        resolved_capture_geometry _geometry;
        sr_capture_source _source;
        mutable std::mutex _gate;
        std::condition_variable _frame_arrived_condition;
        std::deque<queued_frame> _ready_frames;
        std::deque<ComPtr<ID3D11Texture2D>> _available_frame_textures;
        std::atomic<uint64_t> _queue_drop_count = 0;
        HRESULT _failure = S_OK;
        capture_fallback_reason _failure_reason = capture_fallback_reason::none;
        bool _stopped = false;
        bool _has_received_first_frame = false;
        bool _resize_pending = false;
        resolved_capture_geometry _pending_resize_geometry{};
        std::string _copy_mismatch_reason;
        int64_t _first_frame_arrived_qpc = 0;
        int64_t _startup_deadline_qpc = 0;

        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice _direct3d_device{ nullptr };
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem _item{ nullptr };
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool _frame_pool{ nullptr };
        winrt::Windows::Graphics::Capture::GraphicsCaptureSession _session{ nullptr };
        winrt::event_token _frame_arrived_token{};
    };

    class dxgi_duplication_capture_backend final : public capture_backend
    {
    public:
        dxgi_duplication_capture_backend(d3d_context& d3d, const resolved_capture_geometry& geometry);

        HRESULT start() override;
        HRESULT copy_next_frame_to(ID3D11Texture2D* destination, copied_capture_frame& copied_frame) override;
        void stop() override;
        const char* backend_name() const noexcept override;
        bool current_geometry(resolved_capture_geometry& geometry) const override;
        std::string consume_copy_mismatch_reason() override;

    private:
        d3d_context& _d3d;
        resolved_capture_geometry _geometry;
        std::string _copy_mismatch_reason;
        ComPtr<IDXGIOutputDuplication> _duplication;
    };

    stub_engine* as_engine(sr_engine_handle handle)
    {
        return static_cast<stub_engine*>(handle);
    }

    int64_t unix_time_millis()
    {
        const auto now = system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    int64_t qpc_frequency()
    {
        static const int64_t frequency = []
        {
            LARGE_INTEGER value{};
            QueryPerformanceFrequency(&value);
            return std::max<int64_t>(value.QuadPart, 1);
        }();

        return frequency;
    }

    int64_t qpc_now()
    {
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);
        return value.QuadPart;
    }

    double qpc_to_millis(int64_t qpc_ticks)
    {
        return static_cast<double>(qpc_ticks) * 1000.0 / static_cast<double>(qpc_frequency());
    }

    LONGLONG qpc_to_hns(int64_t qpc_ticks)
    {
        return static_cast<LONGLONG>(qpc_ticks * 10'000'000LL / qpc_frequency());
    }

    double hns_to_millis(int64_t hns_ticks)
    {
        return static_cast<double>(hns_ticks) / 10'000.0;
    }

    LONGLONG frame_timestamp_hns(uint64_t frame_index, int32_t frame_rate)
    {
        return static_cast<LONGLONG>(
            static_cast<long double>(frame_index) * 10'000'000.0L /
            static_cast<long double>(std::max(frame_rate, 1)));
    }

    LONGLONG frame_duration_hns(uint64_t frame_index, int32_t frame_rate)
    {
        return std::max<LONGLONG>(
            frame_timestamp_hns(frame_index + 1, frame_rate) - frame_timestamp_hns(frame_index, frame_rate),
            1);
    }

    bool has_format_support(ID3D11Device* device, DXGI_FORMAT format, UINT required_support) noexcept
    {
        if (device == nullptr)
        {
            return false;
        }

        UINT support = 0;
        return SUCCEEDED(device->CheckFormatSupport(format, &support)) &&
            (support & required_support) == required_support;
    }

    int64_t frame_duration_qpc(int32_t frame_rate)
    {
        return std::max<int64_t>(qpc_frequency() / std::max(frame_rate, 1), 1);
    }

    int32_t scale_coordinate(int32_t value, int32_t from_extent, int32_t to_extent) noexcept
    {
        if (from_extent <= 0 || to_extent <= 0)
        {
            return 0;
        }

        return static_cast<int32_t>(std::llround(
            static_cast<double>(value) * static_cast<double>(to_extent) /
            static_cast<double>(from_extent)));
    }

    bool requires_hardware_encode(const recording_session& session) noexcept
    {
        return session.options.encoder_preference != sr_encoder_preference_software_fallback &&
            session.options.frame_rate == monitor_frame_rate_option &&
            session.target_frame_rate > 60;
    }

    bool is_recording_state(sr_recorder_state state)
    {
        return state == sr_state_recording || state == sr_state_paused;
    }

    int32_t publish(stub_engine* engine, sr_recorder_state state, int32_t countdown_remaining_seconds = 0)
    {
        if (engine == nullptr)
        {
            return sr_result_invalid_argument;
        }

        sr_status_callback callback = nullptr;
        void* callback_context = nullptr;
        int32_t source_kind = sr_capture_source_display;

        {
            std::scoped_lock lock(engine->gate);
            engine->state = state;
            callback = engine->callback;
            callback_context = engine->callback_context;
            source_kind = engine->active_source.kind;
        }

        if (callback == nullptr)
        {
            return sr_result_ok;
        }

        const sr_status_event status
        {
            sr_struct_version,
            state,
            source_kind,
            countdown_remaining_seconds,
            unix_time_millis()
        };

        callback(callback_context, status);
        return sr_result_ok;
    }

    std::optional<RECT> display_mode_monitor_bounds(HMONITOR monitor);
    std::optional<RECT> dxgi_monitor_bounds(HMONITOR monitor);

    RECT virtual_screen_rect()
    {
        RECT bounds{};
        bool has_bounds = false;
        std::pair<RECT*, bool*> state{ &bounds, &has_bounds };

        EnumDisplayMonitors(
            nullptr,
            nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM parameter) -> BOOL
            {
                auto* state = reinterpret_cast<std::pair<RECT*, bool*>*>(parameter);
                MONITORINFOEXW info{};
                info.cbSize = sizeof(info);
                if (!GetMonitorInfoW(monitor, &info))
                {
                    return TRUE;
                }

                auto monitor_bounds = display_mode_monitor_bounds(monitor);
                if (!monitor_bounds.has_value())
                {
                    monitor_bounds = dxgi_monitor_bounds(monitor);
                }

                auto resolved_bounds = monitor_bounds.value_or(info.rcMonitor);
                if (resolved_bounds.right <= resolved_bounds.left || resolved_bounds.bottom <= resolved_bounds.top)
                {
                    resolved_bounds = info.rcMonitor;
                }

                if (!*state->second)
                {
                    *state->first = resolved_bounds;
                    *state->second = true;
                    return TRUE;
                }

                RECT merged{};
                UnionRect(&merged, state->first, &resolved_bounds);
                *state->first = merged;
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&state));

        if (has_bounds)
        {
            return bounds;
        }

        const auto left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const auto top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const auto width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const auto height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        return RECT{ left, top, left + width, top + height };
    }

    RECT intersect_with_virtual_screen(RECT rect)
    {
        RECT clipped = rect;
        const RECT bounds = virtual_screen_rect();
        IntersectRect(&clipped, &rect, &bounds);
        return clipped;
    }

    bool rect_has_area(const RECT& rect)
    {
        return rect.right > rect.left && rect.bottom > rect.top;
    }

    bool rect_contains_rect(const RECT& outer, const RECT& inner)
    {
        return inner.left >= outer.left &&
            inner.top >= outer.top &&
            inner.right <= outer.right &&
            inner.bottom <= outer.bottom;
    }

    int32_t normalize_even_dimension(int32_t value)
    {
        if (value <= minimum_dimension)
        {
            return minimum_dimension;
        }

        return value % 2 == 0 ? value : value - 1;
    }

    int32_t resolve_target_height(const sr_recording_options& options, int32_t source_height)
    {
        if (options.resolution == 480 ||
            options.resolution == 720 ||
            options.resolution == 1080 ||
            options.resolution == 1440 ||
            options.resolution == 2160)
        {
            return normalize_even_dimension(std::min(source_height, options.resolution));
        }

        return normalize_even_dimension(std::min(source_height, maximum_auto_output_height));
    }

    int32_t resolve_target_width(int32_t source_width, int32_t source_height, int32_t target_height)
    {
        auto target_width = std::max(static_cast<int32_t>(
            static_cast<int64_t>(source_width) * target_height / std::max(source_height, 1)),
            minimum_dimension);

        return normalize_even_dimension(target_width);
    }

    uint32_t resolve_h264_profile(const sr_recording_options& options)
    {
        return options.quality_preset == 2
            ? eAVEncH264VProfile_Main
            : eAVEncH264VProfile_High;
    }

    std::string format_luid(const LUID& value)
    {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0')
            << std::setw(8) << static_cast<uint32_t>(value.HighPart)
            << "-"
            << std::setw(8) << static_cast<uint32_t>(value.LowPart);
        return stream.str();
    }

    std::string infer_encoder_vendor(const std::string& encoder_name)
    {
        auto lower = encoder_name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        });

        if (lower.find("nvidia") != std::string::npos || lower.find("nvenc") != std::string::npos)
        {
            return "nvidia";
        }

        if (lower.find("amd") != std::string::npos || lower.find("advanced micro devices") != std::string::npos)
        {
            return "amd";
        }

        if (lower.find("intel") != std::string::npos || lower.find("quick sync") != std::string::npos)
        {
            return "intel";
        }

        if (lower.find("qualcomm") != std::string::npos)
        {
            return "qualcomm";
        }

        if (lower.find("microsoft") != std::string::npos)
        {
            return "microsoft";
        }

        return "unknown";
    }

    encoder_capability_probe probe_h264_nv12_hardware_encoder()
    {
        encoder_capability_probe probe{};
        IMFActivate** activations = nullptr;
        UINT32 activation_count = 0;

        MFT_REGISTER_TYPE_INFO input_type{};
        input_type.guidMajorType = MFMediaType_Video;
        input_type.guidSubtype = MFVideoFormat_NV12;

        MFT_REGISTER_TYPE_INFO output_type{};
        output_type.guidMajorType = MFMediaType_Video;
        output_type.guidSubtype = MFVideoFormat_H264;

        const auto result = MFTEnumEx(
            MFT_CATEGORY_VIDEO_ENCODER,
            MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
            &input_type,
            &output_type,
            &activations,
            &activation_count);

        if (FAILED(result) || activations == nullptr || activation_count == 0)
        {
            if (activations != nullptr)
            {
                CoTaskMemFree(activations);
            }

            return probe;
        }

        probe.has_h264_nv12_hardware = true;
        probe.hardware_encoder_count = activation_count;

        for (UINT32 index = 0; index < activation_count; ++index)
        {
            auto* activation = activations[index];
            if (activation == nullptr)
            {
                continue;
            }

            if (probe.encoder_name == "unknown")
            {
                wchar_t* friendly_name = nullptr;
                UINT32 friendly_name_length = 0;
                if (SUCCEEDED(activation->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendly_name, &friendly_name_length)) &&
                    friendly_name != nullptr)
                {
                    probe.encoder_name = narrow_utf8(std::wstring(friendly_name, friendly_name_length));
                    probe.encoder_vendor = infer_encoder_vendor(probe.encoder_name);
                    CoTaskMemFree(friendly_name);
                }
            }

            if (probe.hardware_url.empty())
            {
                wchar_t* hardware_url = nullptr;
                UINT32 hardware_url_length = 0;
                if (SUCCEEDED(activation->GetAllocatedString(MFT_ENUM_HARDWARE_URL_Attribute, &hardware_url, &hardware_url_length)) &&
                    hardware_url != nullptr)
                {
                    probe.hardware_url = narrow_utf8(std::wstring(hardware_url, hardware_url_length));
                    CoTaskMemFree(hardware_url);
                }
            }
        }

        for (UINT32 index = 0; index < activation_count; ++index)
        {
            if (activations[index] != nullptr)
            {
                activations[index]->Release();
            }
        }

        CoTaskMemFree(activations);
        return probe;
    }

    const char* encoder_preference_name(int32_t preference) noexcept
    {
        switch (preference)
        {
        case sr_encoder_preference_hardware_only:
            return "hardware-only";
        case sr_encoder_preference_software_fallback:
            return "software-fallback";
        default:
            return "auto";
        }
    }

    const char* video_codec_name(int32_t codec) noexcept
    {
        switch (codec)
        {
        case sr_video_codec_hevc:
            return "hevc";
        case sr_video_codec_av1:
            return "av1";
        default:
            return "h264";
        }
    }

    const char* resolve_quality_preset_name(const sr_recording_options& options)
    {
        switch (options.quality_preset)
        {
        case 1:
            return "Quality";
        case 2:
            return "Low";
        default:
            return "Balanced";
        }
    }

    double resolve_screen_bitrate_scale(int32_t output_width, int32_t output_height) noexcept
    {
        constexpr double reference_pixels = 1920.0 * 1080.0;
        const auto output_pixels = static_cast<double>(std::max(output_width, minimum_dimension)) *
            static_cast<double>(std::max(output_height, minimum_dimension));
        const auto pixel_ratio = output_pixels / reference_pixels;

        return std::clamp(std::pow(std::max(pixel_ratio, 0.01), 0.65), 0.45, 4.0);
    }

    encoder_quality_config resolve_encoder_quality_config(
        const sr_recording_options& options,
        int32_t target_frame_rate,
        int32_t output_width,
        int32_t output_height)
    {
        auto bitrate_kbps = 10'000;
        auto max_bitrate_multiplier = 1.6;
        uint32_t quality_vs_speed = 65;
        bool low_latency_requested = false;
        bool prefer_quality_video_processing = true;
        bool edge_enhancement_requested = true;
        const auto requested_resolution = options.resolution;

        switch (options.quality_preset)
        {
        case 2:
            if (requested_resolution == 480)
            {
                bitrate_kbps = 3'500;
            }
            else if (requested_resolution == 720)
            {
                bitrate_kbps = 5'000;
            }
            else if (requested_resolution == 1440)
            {
                bitrate_kbps = 16'000;
            }
            else if (requested_resolution == 2160)
            {
                bitrate_kbps = 28'000;
            }
            else
            {
                bitrate_kbps = 9'000;
            }
            max_bitrate_multiplier = 1.3;
            quality_vs_speed = 50;
            low_latency_requested = true;
            prefer_quality_video_processing = false;
            edge_enhancement_requested = false;
            break;
        case 1:
            if (requested_resolution == 480)
            {
                bitrate_kbps = 7'500;
            }
            else if (requested_resolution == 720)
            {
                bitrate_kbps = 12'000;
            }
            else if (requested_resolution == 1440)
            {
                bitrate_kbps = 38'000;
            }
            else if (requested_resolution == 2160)
            {
                bitrate_kbps = 68'000;
            }
            else
            {
                bitrate_kbps = 22'000;
            }
            quality_vs_speed = 75;
            break;
        default:
            if (requested_resolution == 480)
            {
                bitrate_kbps = 6'000;
            }
            else if (requested_resolution == 720)
            {
                bitrate_kbps = 10'000;
            }
            else if (requested_resolution == 1440)
            {
                bitrate_kbps = 30'000;
            }
            else if (requested_resolution == 2160)
            {
                bitrate_kbps = 52'000;
            }
            else
            {
                bitrate_kbps = 18'000;
            }
            max_bitrate_multiplier = 1.8;
            quality_vs_speed = 72;
            break;
        }

        bitrate_kbps = static_cast<int32_t>(
            std::lround(static_cast<double>(bitrate_kbps) * resolve_screen_bitrate_scale(output_width, output_height)));

        if (target_frame_rate > 60)
        {
            bitrate_kbps = static_cast<int32_t>(
                std::lround(static_cast<double>(bitrate_kbps) * (static_cast<double>(target_frame_rate) / 60.0) * 0.70));
            quality_vs_speed = std::min<uint32_t>(quality_vs_speed, 28);
            low_latency_requested = true;
            prefer_quality_video_processing = false;
            edge_enhancement_requested = false;
        }
        else if (target_frame_rate == 60)
        {
            bitrate_kbps = static_cast<int32_t>(bitrate_kbps * 1.6);
        }
        else if (target_frame_rate <= 24)
        {
            bitrate_kbps = static_cast<int32_t>(bitrate_kbps * 0.8);
        }

        encoder_quality_config config{};
        config.target_bitrate_bps = static_cast<uint32_t>(std::max(bitrate_kbps, 1) * 1000);
        config.max_bitrate_bps = static_cast<uint32_t>(static_cast<double>(config.target_bitrate_bps) * max_bitrate_multiplier);
        config.quality_vs_speed = quality_vs_speed;
        config.gop_size = static_cast<uint32_t>(std::max(target_frame_rate, 1) * 2);
        config.cabac_requested = options.quality_preset != 2;
        config.use_b_frames = false;
        config.low_latency_requested = low_latency_requested;
        config.real_time_requested = false;
        config.allow_frame_drops = false;
        config.frame_rate_conversion_disabled = true;
        config.prefer_quality_video_processing = prefer_quality_video_processing;
        config.edge_enhancement_requested = edge_enhancement_requested;
        config.quality_preset_name = resolve_quality_preset_name(options);
        return config;
    }

    PROPERTYKEY codec_property_key(const GUID& key) noexcept
    {
        return PROPERTYKEY{ key, 0 };
    }

    HRESULT set_property_store_uint32(IPropertyStore* store, const GUID& key, uint32_t value)
    {
        if (store == nullptr)
        {
            return E_POINTER;
        }

        PROPVARIANT variant{};
        auto result = InitPropVariantFromUInt32(value, &variant);
        if (SUCCEEDED(result))
        {
            const auto property_key = codec_property_key(key);
            result = store->SetValue(property_key, variant);
        }

        PropVariantClear(&variant);
        return result;
    }

    HRESULT set_property_store_bool(IPropertyStore* store, const GUID& key, bool value)
    {
        if (store == nullptr)
        {
            return E_POINTER;
        }

        PROPVARIANT variant{};
        auto result = InitPropVariantFromBoolean(value ? TRUE : FALSE, &variant);
        if (SUCCEEDED(result))
        {
            const auto property_key = codec_property_key(key);
            result = store->SetValue(property_key, variant);
        }

        PropVariantClear(&variant);
        return result;
    }

    HRESULT create_encoder_property_store(const encoder_quality_config& config, ComPtr<IPropertyStore>& store)
    {
        store.Reset();

        auto result = PSCreateMemoryPropertyStore(IID_PPV_ARGS(&store));
        if (FAILED(result))
        {
            return result;
        }

        result = set_property_store_uint32(store.Get(), CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_PeakConstrainedVBR);
        if (FAILED(result))
        {
            return result;
        }

        result = set_property_store_uint32(store.Get(), CODECAPI_AVEncCommonMeanBitRate, config.target_bitrate_bps);
        if (FAILED(result))
        {
            return result;
        }

        result = set_property_store_uint32(store.Get(), CODECAPI_AVEncCommonMaxBitRate, config.max_bitrate_bps);
        if (FAILED(result))
        {
            return result;
        }

        result = set_property_store_uint32(store.Get(), CODECAPI_AVEncCommonQualityVsSpeed, config.quality_vs_speed);
        if (FAILED(result))
        {
            return result;
        }

        result = set_property_store_uint32(store.Get(), CODECAPI_AVEncMPVGOPSize, config.gop_size);
        if (FAILED(result))
        {
            return result;
        }

        result = set_property_store_uint32(store.Get(), CODECAPI_AVEncMPVDefaultBPictureCount, config.use_b_frames ? 1 : 0);
        if (FAILED(result))
        {
            return result;
        }

        result = set_property_store_bool(store.Get(), CODECAPI_AVEncCommonRealTime, config.real_time_requested);
        if (FAILED(result))
        {
            return result;
        }

        result = set_property_store_uint32(store.Get(), CODECAPI_AVEncCommonAllowFrameDrops, config.allow_frame_drops ? TRUE : FALSE);
        if (FAILED(result))
        {
            return result;
        }

        result = set_property_store_uint32(
            store.Get(),
            CODECAPI_AVEncVideoOutputFrameRateConversion,
            config.frame_rate_conversion_disabled
                ? eAVEncVideoOutputFrameRateConversion_Disable
                : eAVEncVideoOutputFrameRateConversion_Enable);
        if (FAILED(result))
        {
            return result;
        }

        result = set_property_store_bool(store.Get(), CODECAPI_AVEncH264CABACEnable, config.cabac_requested);
        if (FAILED(result))
        {
            return result;
        }

        return store->Commit();
    }

    HRESULT create_encoder_input_attributes(const encoder_quality_config& config, ComPtr<IMFAttributes>& attributes)
    {
        attributes.Reset();
        auto result = MFCreateAttributes(&attributes, 10);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_PeakConstrainedVBR);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(CODECAPI_AVEncCommonMeanBitRate, config.target_bitrate_bps);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(CODECAPI_AVEncCommonMaxBitRate, config.max_bitrate_bps);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(CODECAPI_AVEncCommonQualityVsSpeed, config.quality_vs_speed);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(CODECAPI_AVEncMPVGOPSize, config.gop_size);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(CODECAPI_AVEncMPVDefaultBPictureCount, config.use_b_frames ? 1 : 0);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(CODECAPI_AVEncCommonRealTime, config.real_time_requested ? TRUE : FALSE);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(CODECAPI_AVEncCommonAllowFrameDrops, config.allow_frame_drops ? TRUE : FALSE);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(
            CODECAPI_AVEncVideoOutputFrameRateConversion,
            config.frame_rate_conversion_disabled
                ? eAVEncVideoOutputFrameRateConversion_Disable
                : eAVEncVideoOutputFrameRateConversion_Enable);
        if (FAILED(result))
        {
            return result;
        }

        return attributes->SetUINT32(CODECAPI_AVEncH264CABACEnable, config.cabac_requested ? TRUE : FALSE);
    }

    HRESULT apply_bt709_limited_video_color_metadata(IMFAttributes* attributes)
    {
        if (attributes == nullptr)
        {
            return E_POINTER;
        }

        auto result = attributes->SetUINT32(MF_MT_VIDEO_PRIMARIES, static_cast<UINT32>(MFVideoPrimaries_BT709));
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(MF_MT_TRANSFER_FUNCTION, static_cast<UINT32>(MFVideoTransFunc_709));
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(MF_MT_YUV_MATRIX, static_cast<UINT32>(MFVideoTransferMatrix_BT709));
        if (FAILED(result))
        {
            return result;
        }

        return attributes->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, static_cast<UINT32>(MFNominalRange_16_235));
    }

    int32_t detect_monitor_refresh_rate(HMONITOR monitor) noexcept
    {
        if (monitor == nullptr)
        {
            return 0;
        }

        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info) || info.szDevice[0] == L'\0')
        {
            return 0;
        }

        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode))
        {
            return 0;
        }

        return mode.dmDisplayFrequency > 1
            ? static_cast<int32_t>(mode.dmDisplayFrequency)
            : 0;
    }

    int32_t resolve_h264_level_frame_rate(int32_t width, int32_t height) noexcept
    {
        const auto macroblocks_wide = (std::max(width, 1) + 15) / 16;
        const auto macroblocks_high = (std::max(height, 1) + 15) / 16;
        const auto macroblocks_per_frame = std::max(macroblocks_wide * macroblocks_high, 1);
        const auto h264_limit = std::max(h264_level_52_macroblocks_per_second / macroblocks_per_frame, 1);
        return std::max(h264_limit, 1);
    }

    int32_t resolve_target_frame_rate(
        const sr_recording_options& options,
        int32_t output_width,
        int32_t output_height,
        HMONITOR monitor,
        int32_t& monitor_refresh_rate,
        std::string& cap_reason)
    {
        monitor_refresh_rate = detect_monitor_refresh_rate(monitor);
        cap_reason = "none";

        const auto h264_level_frame_rate = resolve_h264_level_frame_rate(output_width, output_height);
        if (options.frame_rate == monitor_frame_rate_option)
        {
            auto target = product_monitor_frame_rate_limit;
            if (target > h264_level_frame_rate)
            {
                target = h264_level_frame_rate;
                cap_reason = "H264Limit";
            }

            return std::max(target, 1);
        }

        if (options.frame_rate == 24 || options.frame_rate == 30 || options.frame_rate == 60)
        {
            if (options.frame_rate > h264_level_frame_rate)
            {
                cap_reason = "H264Limit";
                return h264_level_frame_rate;
            }

            return options.frame_rate;
        }

        cap_reason = "UnsupportedPreset";
        return std::min(60, h264_level_frame_rate);
    }

    std::filesystem::path derive_final_video_path(const std::wstring& recording_path)
    {
        auto final_path = std::filesystem::path(recording_path);
        final_path.replace_extension(L".mp4");
        return final_path;
    }

    #include "engine/media_geometry.inl"
    #include "engine/frame_graph.inl"
    #include "engine/encoder_mf.inl"
    #include "engine/capture_backends.inl"
    #include "engine/legacy_gdi_pipeline.inl"
    #include "engine/audio_capture.inl"
    #include "engine/capture_loops.inl"
    #include "engine/encode_loops.inl"
    #include "engine/session_lifecycle.inl"
    #include "engine/compatibility_report.inl"
    #include "engine/manifest_writer.inl"
    #include "engine/engine_initialization.inl"

}

int32_t __stdcall sr_engine_get_abi_version()
{
    return static_cast<int32_t>(sr_abi_version);
}

sr_engine_handle __stdcall sr_engine_create()
{
    return new (std::nothrow) stub_engine{};
}

void __stdcall sr_engine_destroy(sr_engine_handle engine)
{
    auto* stub = as_engine(engine);
    if (stub == nullptr)
    {
        return;
    }

    std::unique_ptr<recording_session> active_recording;
    {
        std::scoped_lock lock(stub->gate);
        active_recording = std::move(stub->active_recording);
    }

    if (active_recording != nullptr)
    {
        join_recording_threads(*active_recording);
        release_legacy_capture_resources(*active_recording);
    }

    delete stub;
}

int32_t __stdcall sr_engine_set_callback(sr_engine_handle engine, sr_status_callback callback, void* context)
{
    auto* stub = as_engine(engine);
    if (stub == nullptr)
    {
        return sr_result_invalid_argument;
    }

    std::scoped_lock lock(stub->gate);
    stub->callback = callback;
    stub->callback_context = context;
    return sr_result_ok;
}

int32_t __stdcall sr_engine_initialize(sr_engine_handle engine)
{
    auto* stub = as_engine(engine);
    if (stub == nullptr)
    {
        return sr_result_invalid_argument;
    }

    std::unique_ptr<recording_session> active_recording;
    {
        std::scoped_lock lock(stub->gate);
        active_recording = std::move(stub->active_recording);
        stub->initialized = true;
        stub->state = sr_state_idle;
        stub->current_recording_path.clear();
    }

    if (active_recording != nullptr)
    {
        join_recording_threads(*active_recording);
        release_legacy_capture_resources(*active_recording);
    }

    return publish(stub, sr_state_idle);
}

int32_t __stdcall sr_engine_prepare_recording_output(sr_engine_handle engine, const wchar_t* output_path)
{
    auto* stub = as_engine(engine);
    if (stub == nullptr || output_path == nullptr || output_path[0] == L'\0')
    {
        return sr_result_invalid_argument;
    }

    std::scoped_lock lock(stub->gate);
    stub->pending_recording_path = output_path;
    return sr_result_ok;
}

int32_t __stdcall sr_engine_write_compatibility_report(sr_engine_handle engine, const wchar_t* output_path)
{
    auto* stub = as_engine(engine);
    if (stub == nullptr || output_path == nullptr || output_path[0] == L'\0')
    {
        return sr_result_invalid_argument;
    }

    const auto result = write_compatibility_report_json(std::filesystem::path(output_path));
    return SUCCEEDED(result) ? sr_result_ok : sr_result_invalid_state;
}

int32_t __stdcall sr_engine_prepare_screenshot_output(sr_engine_handle engine, const wchar_t* output_path)
{
    auto* stub = as_engine(engine);
    if (stub == nullptr || output_path == nullptr || output_path[0] == L'\0')
    {
        return sr_result_invalid_argument;
    }

    return sr_result_invalid_state;
}

int32_t __stdcall sr_engine_start(sr_engine_handle engine, const sr_capture_source* source, const sr_recording_options* options)
{
    auto* stub = as_engine(engine);
    if (stub == nullptr || source == nullptr || options == nullptr)
    {
        return sr_result_invalid_argument;
    }

    std::wstring recording_path;
    {
        std::scoped_lock lock(stub->gate);
        if (!stub->initialized)
        {
            return sr_result_not_initialized;
        }

        if (is_recording_state(stub->state) || stub->active_recording != nullptr)
        {
            return sr_result_invalid_state;
        }

        if (stub->pending_recording_path.empty())
        {
            return sr_result_invalid_state;
        }

        stub->active_source = *source;
        stub->active_options = *options;
        stub->current_recording_path = stub->pending_recording_path;
        stub->pending_recording_path.clear();
        recording_path = stub->current_recording_path;
    }

    auto geometry = resolve_capture_geometry(*source);
    if (!geometry.has_value() || !rect_has_area(geometry->requested_bounds))
    {
        return sr_result_invalid_state;
    }

    const auto source_width = geometry->requested_bounds.right - geometry->requested_bounds.left;
    const auto source_height = geometry->requested_bounds.bottom - geometry->requested_bounds.top;
    if (source_width <= 0 || source_height <= 0)
    {
        return sr_result_invalid_state;
    }

    auto session = std::make_unique<recording_session>();
    session->session_directory = std::filesystem::path(recording_path);
    session->final_output_path = derive_final_video_path(recording_path);
    session->source = *source;
    session->options = *options;
    if (session->options.version < 3)
    {
        session->options.audio_mode = (session->options.include_system_audio != 0 && session->options.include_microphone != 0)
            ? sr_audio_capture_system_and_microphone
            : session->options.include_system_audio != 0
                ? sr_audio_capture_system
                : session->options.include_microphone != 0
                    ? sr_audio_capture_microphone
                    : sr_audio_capture_off;
        session->options.microphone_device_id = nullptr;
    }
    else if (session->options.audio_mode < sr_audio_capture_off || session->options.audio_mode > sr_audio_capture_system_and_microphone)
    {
        session->options.audio_mode = sr_audio_capture_off;
    }
    if (session->options.microphone_device_id != nullptr)
    {
        session->microphone_device_id = session->options.microphone_device_id;
        session->options.microphone_device_id = session->microphone_device_id.c_str();
    }
    session->encoder_preference = encoder_preference_name(options->encoder_preference);
    session->encoder_codec = video_codec_name(options->video_codec);
    if (options->video_codec != sr_video_codec_h264)
    {
        session->encoder_fallback_reason = "requested-codec-not-implemented";
        session->encoder_codec = "h264";
    }
    session->output_height = resolve_target_height(*options, source_height);
    session->output_width = resolve_target_width(source_width, source_height, session->output_height);
    session->target_frame_rate = resolve_target_frame_rate(
        *options,
        session->output_width,
        session->output_height,
        geometry->monitor,
        session->monitor_refresh_rate,
        session->fps_cap_reason);
    session->quality_config = resolve_encoder_quality_config(
        *options,
        session->target_frame_rate,
        session->output_width,
        session->output_height);
    session->metrics.output_width = session->output_width;
    session->metrics.output_height = session->output_height;
    session->started_at_unix_millis = unix_time_millis();
    session->started_qpc = qpc_now();

    std::error_code create_error;
    std::filesystem::create_directories(session->session_directory, create_error);
    if (create_error)
    {
        return sr_result_invalid_state;
    }

    const auto using_gpu_path = try_initialize_gpu_recording(*session);
    if (!using_gpu_path && session->gpu_hardware_detected)
    {
        session->cpu_fallback_allowed = false;
        session->cpu_fallback_blocked = true;
        if (session->cpu_fallback_block_reason == "none")
        {
            session->cpu_fallback_block_reason = "gpu-detected-cpu-fallback-blocked";
        }

        fail_session(
            *session,
            FAILED(session->gpu_initialization_hresult) ? session->gpu_initialization_hresult : E_FAIL,
            L"GPU hardware was detected, so CPU/GDI fallback was blocked. The GPU recording path must succeed.");
        write_manifest(*session, false);
        release_legacy_capture_resources(*session);
        return sr_result_invalid_state;
    }

    if (!using_gpu_path && options->encoder_preference == sr_encoder_preference_hardware_only)
    {
        session->cpu_fallback_allowed = false;
        session->cpu_fallback_blocked = true;
        session->cpu_fallback_block_reason = "hardware-only-request-without-compatible-gpu";
        fail_session(*session, E_FAIL, L"Hardware-only recording was requested but no compatible GPU path is available.");
        write_manifest(*session, false);
        release_legacy_capture_resources(*session);
        return sr_result_invalid_state;
    }

    if (!using_gpu_path && !try_initialize_legacy_recording(*session))
    {
        return sr_result_invalid_state;
    }

    try
    {
        if (session->mode == pipeline_mode::gpu_first)
        {
            session->capture_thread = std::thread(gpu_capture_loop, session.get());
            session->encode_thread = std::thread(gpu_encode_loop, session.get());
        }
        else
        {
            session->capture_thread = std::thread(legacy_capture_loop, session.get());
            session->encode_thread = std::thread(legacy_encode_loop, session.get());
        }
    }
    catch (...)
    {
        join_recording_threads(*session);
        release_legacy_capture_resources(*session);
        return sr_result_invalid_state;
    }

    if (session->mode == pipeline_mode::gpu_first)
    {
        bool startup_ready = false;
        bool startup_failed = false;
        {
            std::unique_lock lock(session->gate);
            startup_ready = session->gpu_start_condition.wait_for(lock, std::chrono::seconds(10), [&session]
            {
                return session->gpu_encoder_ready || session->capture_finished || FAILED(session->failure);
            });

            startup_failed = !startup_ready || !session->gpu_encoder_ready || FAILED(session->failure);
        }

        if (startup_failed)
        {
            if (!startup_ready)
            {
                fail_session(*session, E_FAIL, L"GPU recorder startup timed out before the encoder became ready.");
            }

            join_recording_threads(*session);
            write_manifest(*session, false);
            release_legacy_capture_resources(*session);
            return sr_result_invalid_state;
        }
    }

    {
        std::scoped_lock lock(stub->gate);
        stub->active_recording = std::move(session);
    }

    return publish(stub, sr_state_recording);
}

int32_t __stdcall sr_engine_pause(sr_engine_handle engine)
{
    auto* stub = as_engine(engine);
    if (stub == nullptr)
    {
        return sr_result_invalid_argument;
    }

    {
        std::scoped_lock lock(stub->gate);
        if (!stub->initialized)
        {
            return sr_result_not_initialized;
        }

        if (stub->state != sr_state_recording || stub->active_recording == nullptr)
        {
            return sr_result_invalid_state;
        }

        std::scoped_lock recording_lock(stub->active_recording->gate);
        stub->active_recording->pause_requested = true;
    }

    return publish(stub, sr_state_paused);
}

int32_t __stdcall sr_engine_resume(sr_engine_handle engine)
{
    auto* stub = as_engine(engine);
    if (stub == nullptr)
    {
        return sr_result_invalid_argument;
    }

    {
        std::scoped_lock lock(stub->gate);
        if (!stub->initialized)
        {
            return sr_result_not_initialized;
        }

        if (stub->state != sr_state_paused || stub->active_recording == nullptr)
        {
            return sr_result_invalid_state;
        }

        std::scoped_lock recording_lock(stub->active_recording->gate);
        stub->active_recording->pause_requested = false;
    }

    return publish(stub, sr_state_recording);
}

int32_t __stdcall sr_engine_stop(sr_engine_handle engine)
{
    auto* stub = as_engine(engine);
    if (stub == nullptr)
    {
        return sr_result_invalid_argument;
    }

    std::unique_ptr<recording_session> session;
    {
        std::scoped_lock lock(stub->gate);
        if (!stub->initialized)
        {
            return sr_result_not_initialized;
        }

        if (!is_recording_state(stub->state) || stub->active_recording == nullptr)
        {
            return sr_result_invalid_state;
        }

        session = std::move(stub->active_recording);
    }

    const auto stopping_result = publish(stub, sr_state_stopping_saving);
    if (stopping_result != sr_result_ok)
    {
        {
            std::scoped_lock lock(stub->gate);
            stub->active_recording = std::move(session);
        }

        return stopping_result;
    }

    join_recording_threads(*session);

    const auto export_succeeded = SUCCEEDED(session->failure) && session->metrics.encoded_frames > 0;
    if (!export_succeeded)
    {
        std::error_code remove_error;
        std::filesystem::remove(session->final_output_path, remove_error);
    }

    write_manifest(*session, export_succeeded);
    release_legacy_capture_resources(*session);

    {
        std::scoped_lock lock(stub->gate);
        stub->current_recording_path.clear();
    }

    const auto ready_result = publish(stub, sr_state_source_selected);
    if (ready_result != sr_result_ok)
    {
        return ready_result;
    }

    return export_succeeded ? sr_result_ok : sr_result_invalid_state;
}

int32_t __stdcall sr_engine_take_screenshot(sr_engine_handle engine, const sr_capture_source* source)
{
    auto* stub = as_engine(engine);
    if (stub == nullptr || source == nullptr)
    {
        return sr_result_invalid_argument;
    }

    return sr_result_invalid_state;
}
