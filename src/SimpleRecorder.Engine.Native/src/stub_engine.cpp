#include "pch.h"
#include "../include/sr_api.h"

#include <codecapi.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
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
#include <atomic>
#include <chrono>
#include <condition_variable>
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
#include <thread>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using system_clock = std::chrono::system_clock;

    constexpr size_t capture_slot_count = 6;
    constexpr size_t wgc_frame_queue_limit = 3;
    constexpr int32_t max_supported_frame_rate = 120;
    constexpr int32_t minimum_dimension = 2;
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

    const char* capture_fallback_reason_name(capture_fallback_reason reason) noexcept;
    bool can_fallback_to_dxgi(int32_t source_kind) noexcept;
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
        RECT source_rect{ 0, 0, 0, 0 };
        int64_t captured_at_qpc = 0;
        int64_t capture_duration_qpc = 0;
        int64_t convert_duration_qpc = 0;
        uint64_t sequence = 0;
    };

    struct recording_metrics
    {
        uint64_t capture_attempts = 0;
        uint64_t captured_frames = 0;
        uint64_t encoded_frames = 0;
        uint64_t backpressure_drop_count = 0;
        uint64_t capture_failure_count = 0;
        uint64_t pacing_overrun_count = 0;
        uint32_t peak_queue_depth = 0;
        int64_t total_capture_latency_qpc = 0;
        int64_t total_queue_latency_qpc = 0;
        int64_t total_convert_latency_qpc = 0;
        int64_t total_encode_latency_qpc = 0;
        int64_t first_captured_qpc = 0;
        int64_t last_captured_qpc = 0;
        int64_t last_sample_duration_qpc = 0;
        int32_t output_width = 0;
        int32_t output_height = 0;
    };

    struct recording_session
    {
        std::mutex gate;
        std::condition_variable ready_condition;
        bool stop_requested = false;
        bool pause_requested = false;
        bool capture_finished = false;
        bool encode_finished = false;
        HRESULT failure = S_OK;
        std::wstring failure_reason;
        pipeline_mode mode = pipeline_mode::legacy_gdi;

        std::filesystem::path session_directory;
        std::filesystem::path final_output_path;
        sr_capture_source source{ sr_struct_version, sr_capture_source_display, 0, { 0, 0, 0, 0 } };
        sr_recording_options options{ sr_struct_version, 30, 0, 0, 0, 1, 0 };
        int32_t target_frame_rate = 30;
        int32_t output_width = 0;
        int32_t output_height = 0;
        int64_t started_at_unix_millis = 0;
        int64_t started_qpc = 0;
        uint64_t next_sequence = 0;

        std::string capture_backend = "gdi-live";
        std::string requested_capture_backend = "gdi-live";
        std::string encode_backend = "media-foundation-h264-rgb32";
        std::string capture_fallback_from;
        std::string capture_fallback_to;
        capture_fallback_reason capture_fallback_reason_code = capture_fallback_reason::none;
        HRESULT capture_fallback_hresult = S_OK;
        bool hardware_encode = false;
        bool wgc_startup_attempted = false;
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
        sr_recording_options active_options{ sr_struct_version, 30, 0, 0, 0, 1, 0 };
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
        std::wstring adapter_name;
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
            int32_t target_frame_rate);

        void process_slot(gpu_capture_slot& slot, const RECT& source_rect);

    private:
        void create_slots(UINT input_width, UINT input_height);

        d3d_context& _d3d;
        std::vector<gpu_capture_slot>& _slots;
        RECT _capture_item_rect{ 0, 0, 0, 0 };
        int32_t _output_width = 0;
        int32_t _output_height = 0;
        ComPtr<ID3D11VideoProcessorEnumerator> _enumerator;
        ComPtr<ID3D11VideoProcessor> _processor;
    };

    class encoder_backend
    {
    public:
        encoder_backend(recording_session& session, d3d_context& d3d);
        ~encoder_backend() = default;

        bool is_hardware_encode() const noexcept;
        const char* backend_name() const noexcept;
        void write_slot(gpu_capture_slot& slot, LONGLONG sample_time, LONGLONG sample_duration);
        HRESULT finalize() noexcept;

    private:
        void initialize_sink_writer();
        HRESULT create_sink_writer(bool enable_d3d_manager, bool disable_converters);
        static uint32_t resolve_h264_profile(const sr_recording_options& options);
        static uint32_t resolve_target_bitrate_bps(const sr_recording_options& options);

        recording_session& _session;
        d3d_context& _d3d;
        ComPtr<IMFSinkWriter> _sink_writer;
        DWORD _stream_index = 0;
        bool _hardware_encode = false;
        std::wstring _last_stage;
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

    private:
        struct queued_frame
        {
            ComPtr<ID3D11Texture2D> texture;
            int64_t captured_at_qpc = 0;
        };

        void fail_locked(HRESULT failure, capture_fallback_reason reason) noexcept;
        void on_frame_arrived(
            winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
            winrt::Windows::Foundation::IInspectable const&);

        d3d_context& _d3d;
        resolved_capture_geometry _geometry;
        sr_capture_source _source;
        mutable std::mutex _gate;
        std::condition_variable _frame_arrived_condition;
        std::deque<queued_frame> _ready_frames;
        std::atomic<uint64_t> _queue_drop_count = 0;
        HRESULT _failure = S_OK;
        capture_fallback_reason _failure_reason = capture_fallback_reason::none;
        bool _stopped = false;
        bool _has_received_first_frame = false;
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

    private:
        d3d_context& _d3d;
        resolved_capture_geometry _geometry;
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

    int64_t frame_duration_qpc(int32_t frame_rate)
    {
        return std::max<int64_t>(qpc_frequency() / std::max(frame_rate, 1), 1);
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

    RECT virtual_screen_rect()
    {
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
        if (options.resolution == 720 || options.resolution == 1080)
        {
            return normalize_even_dimension(std::min(source_height, options.resolution));
        }

        return normalize_even_dimension(source_height);
    }

    int32_t resolve_target_width(int32_t source_width, int32_t source_height, int32_t target_height)
    {
        auto target_width = std::max(static_cast<int32_t>(
            static_cast<int64_t>(source_width) * target_height / std::max(source_height, 1)),
            minimum_dimension);

        return normalize_even_dimension(target_width);
    }

    int32_t resolve_target_frame_rate(const sr_recording_options& options)
    {
        return std::clamp(std::max(options.frame_rate, 1), 1, max_supported_frame_rate);
    }

    std::filesystem::path derive_final_video_path(const std::wstring& recording_path)
    {
        auto final_path = std::filesystem::path(recording_path);
        final_path.replace_extension(L".mp4");
        return final_path;
    }

    media_foundation_scope::media_foundation_scope()
    {
        co_initialize_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(co_initialize_result) && co_initialize_result != RPC_E_CHANGED_MODE)
        {
            return;
        }

        mf_startup_result = MFStartup(MF_VERSION);
    }

    media_foundation_scope::~media_foundation_scope()
    {
        if (SUCCEEDED(mf_startup_result))
        {
            MFShutdown();
        }

        if (SUCCEEDED(co_initialize_result))
        {
            CoUninitialize();
        }
    }

    bool media_foundation_scope::is_ready() const
    {
        return (SUCCEEDED(co_initialize_result) || co_initialize_result == RPC_E_CHANGED_MODE) &&
            SUCCEEDED(mf_startup_result);
    }

    std::vector<monitor_info> enumerate_monitors()
    {
        std::vector<monitor_info> monitors;

        EnumDisplayMonitors(
            nullptr,
            nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM parameter) -> BOOL
            {
                auto* collection = reinterpret_cast<std::vector<monitor_info>*>(parameter);
                MONITORINFOEXW info{};
                info.cbSize = sizeof(info);
                if (!GetMonitorInfoW(monitor, &info))
                {
                    return TRUE;
                }

                collection->push_back({
                    monitor,
                    info.rcMonitor,
                    (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
                    info.szDevice
                });
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&monitors));

        return monitors;
    }

    std::optional<monitor_info> find_primary_monitor()
    {
        const auto monitors = enumerate_monitors();
        for (const auto& monitor : monitors)
        {
            if (monitor.is_primary)
            {
                return monitor;
            }
        }

        if (!monitors.empty())
        {
            return monitors.front();
        }

        return std::nullopt;
    }

    std::optional<monitor_info> find_single_monitor_for_rect(const RECT& rect)
    {
        std::optional<monitor_info> match;
        const auto monitors = enumerate_monitors();
        for (const auto& monitor : monitors)
        {
            if (!rect_contains_rect(monitor.bounds, rect))
            {
                continue;
            }

            if (match.has_value())
            {
                return std::nullopt;
            }

            match = monitor;
        }

        return match;
    }

    std::optional<resolved_capture_geometry> resolve_capture_geometry(const sr_capture_source& source)
    {
        RECT requested = virtual_screen_rect();
        HWND resolved_window = nullptr;

        if (source.kind == sr_capture_source_window)
        {
            resolved_window = reinterpret_cast<HWND>(source.window_handle);
            if (resolved_window == nullptr)
            {
                resolved_window = GetForegroundWindow();
            }

            RECT window_rect{};
            if (resolved_window != nullptr && GetWindowRect(resolved_window, &window_rect))
            {
                requested = window_rect;
            }
            else if (source.region.width > 0 && source.region.height > 0)
            {
                requested =
                {
                    source.region.x,
                    source.region.y,
                    source.region.x + source.region.width,
                    source.region.y + source.region.height
                };
            }
        }
        else if (source.region.width > 0 && source.region.height > 0)
        {
            requested =
            {
                source.region.x,
                source.region.y,
                source.region.x + source.region.width,
                source.region.y + source.region.height
            };
        }
        else if (source.kind == sr_capture_source_display)
        {
            if (const auto primary = find_primary_monitor())
            {
                requested = primary->bounds;
            }
        }

        requested = intersect_with_virtual_screen(requested);
        if (!rect_has_area(requested))
        {
            return std::nullopt;
        }

        resolved_capture_geometry geometry{};
        geometry.requested_bounds = requested;

        if (source.kind == sr_capture_source_window && resolved_window != nullptr)
        {
            geometry.window = resolved_window;
            geometry.capture_item_bounds = requested;
            geometry.source_rect =
            {
                0,
                0,
                requested.right - requested.left,
                requested.bottom - requested.top
            };
            geometry.monitor = MonitorFromWindow(resolved_window, MONITOR_DEFAULTTONEAREST);
            return geometry;
        }

        auto monitor = find_single_monitor_for_rect(requested);
        if (!monitor.has_value())
        {
            geometry.spans_multiple_monitors = true;
            return geometry;
        }

        geometry.monitor = monitor->handle;
        geometry.capture_item_bounds = monitor->bounds;
        geometry.source_rect =
        {
            requested.left - monitor->bounds.left,
            requested.top - monitor->bounds.top,
            requested.right - monitor->bounds.left,
            requested.bottom - monitor->bounds.top
        };

        return geometry;
    }

    HRESULT create_d3d_context(HMONITOR preferred_monitor, d3d_context& d3d)
    {
        HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(&d3d.factory));
        if (FAILED(result))
        {
            return result;
        }

        if (preferred_monitor != nullptr)
        {
            for (UINT adapter_index = 0;; ++adapter_index)
            {
                ComPtr<IDXGIAdapter1> candidate_adapter;
                result = d3d.factory->EnumAdapters1(adapter_index, &candidate_adapter);
                if (result == DXGI_ERROR_NOT_FOUND)
                {
                    break;
                }

                if (FAILED(result))
                {
                    return result;
                }

                for (UINT output_index = 0;; ++output_index)
                {
                    ComPtr<IDXGIOutput> candidate_output;
                    result = candidate_adapter->EnumOutputs(output_index, &candidate_output);
                    if (result == DXGI_ERROR_NOT_FOUND)
                    {
                        break;
                    }

                    if (FAILED(result))
                    {
                        return result;
                    }

                    DXGI_OUTPUT_DESC output_desc{};
                    result = candidate_output->GetDesc(&output_desc);
                    if (FAILED(result))
                    {
                        return result;
                    }

                    if (output_desc.Monitor == preferred_monitor)
                    {
                        d3d.adapter = candidate_adapter;
                        break;
                    }
                }

                if (d3d.adapter != nullptr)
                {
                    break;
                }
            }
        }

        if (d3d.adapter == nullptr)
        {
            result = d3d.factory->EnumAdapters1(0, &d3d.adapter);
            if (FAILED(result))
            {
                return result;
            }
        }

        static const D3D_FEATURE_LEVEL feature_levels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0
        };

        UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#if defined(_DEBUG)
        creation_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        D3D_FEATURE_LEVEL actual_feature_level{};
        result = D3D11CreateDevice(
            d3d.adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            creation_flags,
            feature_levels,
            static_cast<UINT>(std::size(feature_levels)),
            D3D11_SDK_VERSION,
            &d3d.device,
            &actual_feature_level,
            &d3d.context);
        if (FAILED(result))
        {
            if ((creation_flags & D3D11_CREATE_DEVICE_DEBUG) != 0)
            {
                creation_flags &= ~D3D11_CREATE_DEVICE_DEBUG;
                result = D3D11CreateDevice(
                    d3d.adapter.Get(),
                    D3D_DRIVER_TYPE_UNKNOWN,
                    nullptr,
                    creation_flags,
                    feature_levels,
                    static_cast<UINT>(std::size(feature_levels)),
                    D3D11_SDK_VERSION,
                    &d3d.device,
                    &actual_feature_level,
                    &d3d.context);
            }
        }

        if (FAILED(result))
        {
            return result;
        }

        result = d3d.device.As(&d3d.video_device);
        if (FAILED(result))
        {
            return result;
        }

        result = d3d.context.As(&d3d.video_context);
        if (FAILED(result))
        {
            return result;
        }

        DXGI_ADAPTER_DESC1 adapter_desc{};
        result = d3d.adapter->GetDesc1(&adapter_desc);
        if (FAILED(result))
        {
            return result;
        }

        d3d.adapter_name = adapter_desc.Description;
        return S_OK;
    }

    frame_graph::frame_graph(
        d3d_context& d3d,
        std::vector<gpu_capture_slot>& slots,
        const RECT& capture_item_rect,
        int32_t output_width,
        int32_t output_height,
        int32_t target_frame_rate)
        : _d3d(d3d),
          _slots(slots),
          _capture_item_rect(capture_item_rect),
          _output_width(output_width),
          _output_height(output_height)
    {
        const auto input_width = static_cast<UINT>(capture_item_rect.right - capture_item_rect.left);
        const auto input_height = static_cast<UINT>(capture_item_rect.bottom - capture_item_rect.top);

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content_desc{};
        content_desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content_desc.InputFrameRate.Numerator = static_cast<UINT>(std::max(target_frame_rate, 1));
        content_desc.InputFrameRate.Denominator = 1;
        content_desc.InputWidth = input_width;
        content_desc.InputHeight = input_height;
        content_desc.OutputFrameRate.Numerator = static_cast<UINT>(std::max(target_frame_rate, 1));
        content_desc.OutputFrameRate.Denominator = 1;
        content_desc.OutputWidth = static_cast<UINT>(output_width);
        content_desc.OutputHeight = static_cast<UINT>(output_height);
        content_desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        winrt::check_hresult(_d3d.video_device->CreateVideoProcessorEnumerator(&content_desc, &_enumerator));
        winrt::check_hresult(_d3d.video_device->CreateVideoProcessor(_enumerator.Get(), 0, &_processor));

        create_slots(input_width, input_height);
    }

    void frame_graph::create_slots(UINT input_width, UINT input_height)
    {
        _slots.resize(capture_slot_count);

        for (auto& slot : _slots)
        {
            D3D11_TEXTURE2D_DESC bgra_desc{};
            bgra_desc.Width = input_width;
            bgra_desc.Height = input_height;
            bgra_desc.MipLevels = 1;
            bgra_desc.ArraySize = 1;
            bgra_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            bgra_desc.SampleDesc.Count = 1;
            bgra_desc.Usage = D3D11_USAGE_DEFAULT;

            winrt::check_hresult(_d3d.device->CreateTexture2D(&bgra_desc, nullptr, &slot.bgra_texture));

            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
            input_desc.FourCC = 0;
            input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            input_desc.Texture2D.ArraySlice = 0;
            winrt::check_hresult(_d3d.video_device->CreateVideoProcessorInputView(
                slot.bgra_texture.Get(),
                _enumerator.Get(),
                &input_desc,
                &slot.input_view));

            D3D11_TEXTURE2D_DESC nv12_desc{};
            nv12_desc.Width = static_cast<UINT>(_output_width);
            nv12_desc.Height = static_cast<UINT>(_output_height);
            nv12_desc.MipLevels = 1;
            nv12_desc.ArraySize = 1;
            nv12_desc.Format = DXGI_FORMAT_NV12;
            nv12_desc.SampleDesc.Count = 1;
            nv12_desc.Usage = D3D11_USAGE_DEFAULT;
            nv12_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

            winrt::check_hresult(_d3d.device->CreateTexture2D(&nv12_desc, nullptr, &slot.nv12_texture));

            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc{};
            output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            output_desc.Texture2D.MipSlice = 0;
            winrt::check_hresult(_d3d.video_device->CreateVideoProcessorOutputView(
                slot.nv12_texture.Get(),
                _enumerator.Get(),
                &output_desc,
                &slot.output_view));
        }
    }

    void frame_graph::process_slot(gpu_capture_slot& slot, const RECT& source_rect)
    {
        RECT clamped_source = source_rect;
        const RECT full_input
        {
            0,
            0,
            _capture_item_rect.right - _capture_item_rect.left,
            _capture_item_rect.bottom - _capture_item_rect.top
        };

        IntersectRect(&clamped_source, &source_rect, &full_input);
        if (clamped_source.right <= clamped_source.left || clamped_source.bottom <= clamped_source.top)
        {
            clamped_source = full_input;
        }

        RECT destination_rect{ 0, 0, _output_width, _output_height };

        _d3d.video_context->VideoProcessorSetStreamFrameFormat(_processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        _d3d.video_context->VideoProcessorSetStreamSourceRect(_processor.Get(), 0, TRUE, &clamped_source);
        _d3d.video_context->VideoProcessorSetStreamDestRect(_processor.Get(), 0, TRUE, &destination_rect);
        _d3d.video_context->VideoProcessorSetOutputTargetRect(_processor.Get(), TRUE, &destination_rect);
        _d3d.video_context->VideoProcessorSetOutputBackgroundColor(_processor.Get(), FALSE, nullptr);

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = slot.input_view.Get();

        winrt::check_hresult(_d3d.video_context->VideoProcessorBlt(
            _processor.Get(),
            slot.output_view.Get(),
            0,
            1,
            &stream));
    }

    encoder_backend::encoder_backend(recording_session& session, d3d_context& d3d)
        : _session(session),
          _d3d(d3d)
    {
        initialize_sink_writer();
    }

    bool encoder_backend::is_hardware_encode() const noexcept
    {
        return _hardware_encode;
    }

    const char* encoder_backend::backend_name() const noexcept
    {
        return _hardware_encode
            ? "media-foundation-h264-d3d11-hardware"
            : "media-foundation-h264-d3d11";
    }

    void encoder_backend::write_slot(gpu_capture_slot& slot, LONGLONG sample_time, LONGLONG sample_duration)
    {
        auto check = [](HRESULT hr, const wchar_t* stage)
        {
            if (FAILED(hr))
            {
                throw winrt::hresult_error(hr, stage);
            }
        };

        ComPtr<IMFMediaBuffer> buffer;
        check(MFCreateDXGISurfaceBuffer(
            __uuidof(ID3D11Texture2D),
            slot.nv12_texture.Get(),
            0,
            FALSE,
            &buffer), L"MFCreateDXGISurfaceBuffer");

        DWORD buffer_length = 0;
        check(buffer->GetMaxLength(&buffer_length), L"IMFMediaBuffer::GetMaxLength");
        if (buffer_length == 0)
        {
            buffer_length = static_cast<DWORD>(_session.output_width * _session.output_height * 3 / 2);
        }

        check(buffer->SetCurrentLength(buffer_length), L"IMFMediaBuffer::SetCurrentLength");

        ComPtr<IMFSample> sample;
        check(MFCreateSample(&sample), L"MFCreateSample");
        check(sample->AddBuffer(buffer.Get()), L"IMFSample::AddBuffer");
        check(sample->SetSampleTime(sample_time), L"IMFSample::SetSampleTime");
        check(sample->SetSampleDuration(std::max<LONGLONG>(sample_duration, 1)), L"IMFSample::SetSampleDuration");
        check(_sink_writer->WriteSample(_stream_index, sample.Get()), L"IMFSinkWriter::WriteSample");
    }

    HRESULT encoder_backend::finalize() noexcept
    {
        if (_sink_writer == nullptr)
        {
            return E_FAIL;
        }

        return _sink_writer->Finalize();
    }

    uint32_t encoder_backend::resolve_h264_profile(const sr_recording_options& options)
    {
        return options.quality_preset == 2
            ? eAVEncH264VProfile_Main
            : eAVEncH264VProfile_High;
    }

    uint32_t encoder_backend::resolve_target_bitrate_bps(const sr_recording_options& options)
    {
        auto bitrate_kbps = 10'000;

        switch (options.quality_preset)
        {
        case 2:
            bitrate_kbps = options.resolution == 720 ? 4'000 : 8'000;
            break;
        case 1:
            bitrate_kbps = options.resolution == 720 ? 8'000 : 14'000;
            break;
        default:
            bitrate_kbps = options.resolution == 720 ? 6'000 : 10'000;
            break;
        }

        if (options.frame_rate >= 120)
        {
            bitrate_kbps = static_cast<int32_t>(bitrate_kbps * 2.1);
        }
        else if (options.frame_rate >= 60)
        {
            bitrate_kbps = static_cast<int32_t>(bitrate_kbps * 1.6);
        }
        else if (options.frame_rate <= 24)
        {
            bitrate_kbps = static_cast<int32_t>(bitrate_kbps * 0.8);
        }

        return static_cast<uint32_t>(std::max(bitrate_kbps, 1) * 1000);
    }

    void encoder_backend::initialize_sink_writer()
    {
        std::error_code create_error;
        std::filesystem::create_directories(_session.final_output_path.parent_path(), create_error);
        if (create_error)
        {
            throw winrt::hresult_error(E_FAIL, L"Failed to prepare the output folder.");
        }

        std::error_code remove_error;
        std::filesystem::remove(_session.final_output_path, remove_error);

        _last_stage.clear();

        auto result = create_sink_writer(true, true);
        if (SUCCEEDED(result))
        {
            _hardware_encode = false;
            return;
        }

        const auto strict_stage = _last_stage;
        result = create_sink_writer(true, false);
        if (SUCCEEDED(result))
        {
            _hardware_encode = false;
            return;
        }

        std::wstringstream message;
        message << L"Failed to initialize the D3D11 sink writer.";
        if (!strict_stage.empty())
        {
            message << L" strict-stage=" << strict_stage << L";";
        }
        if (!_last_stage.empty())
        {
            message << L" negotiated-stage=" << _last_stage;
        }

        throw winrt::hresult_error(result, message.str());
    }

    HRESULT encoder_backend::create_sink_writer(bool enable_d3d_manager, bool disable_converters)
    {
        _sink_writer.Reset();
        _stream_index = 0;
        _last_stage.clear();

        auto fail = [this](HRESULT hr, const wchar_t* stage) noexcept
        {
            _last_stage = stage;
            return hr;
        };

        ComPtr<IMFAttributes> attributes;
        auto result = MFCreateAttributes(&attributes, 6);
        if (FAILED(result))
        {
            return fail(result, L"MFCreateAttributes");
        }

        result = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        if (FAILED(result))
        {
            return fail(result, L"Set(MF_SINK_WRITER_DISABLE_THROTTLING)");
        }

        result = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        if (FAILED(result))
        {
            return fail(result, L"Set(MF_LOW_LATENCY)");
        }

        if (disable_converters)
        {
            result = attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
            if (FAILED(result))
            {
                return fail(result, L"Set(MF_READWRITE_DISABLE_CONVERTERS)");
            }
        }

        result = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (FAILED(result))
        {
            return fail(result, L"Set(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS)");
        }

        if (enable_d3d_manager)
        {
            if (_d3d.dxgi_device_manager == nullptr)
            {
                result = MFCreateDXGIDeviceManager(&_d3d.dxgi_reset_token, &_d3d.dxgi_device_manager);
                if (FAILED(result))
                {
                    return fail(result, L"MFCreateDXGIDeviceManager");
                }

                result = _d3d.dxgi_device_manager->ResetDevice(_d3d.device.Get(), _d3d.dxgi_reset_token);
                if (FAILED(result))
                {
                    return fail(result, L"IMFDXGIDeviceManager::ResetDevice");
                }
            }

            result = attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, _d3d.dxgi_device_manager.Get());
            if (FAILED(result))
            {
                return fail(result, L"Set(MF_SINK_WRITER_D3D_MANAGER)");
            }
        }

        result = MFCreateSinkWriterFromURL(_session.final_output_path.c_str(), nullptr, attributes.Get(), &_sink_writer);
        if (FAILED(result))
        {
            return fail(result, L"MFCreateSinkWriterFromURL");
        }

        ComPtr<IMFMediaType> output_type;
        result = MFCreateMediaType(&output_type);
        if (FAILED(result))
        {
            return fail(result, L"MFCreateMediaType(output)");
        }

        result = output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(result))
        {
            return fail(result, L"output.SetGUID(MF_MT_MAJOR_TYPE)");
        }

        result = output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        if (FAILED(result))
        {
            return fail(result, L"output.SetGUID(MF_MT_SUBTYPE)");
        }

        result = output_type->SetUINT32(MF_MT_AVG_BITRATE, resolve_target_bitrate_bps(_session.options));
        if (FAILED(result))
        {
            return fail(result, L"output.SetUINT32(MF_MT_AVG_BITRATE)");
        }

        result = output_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(result))
        {
            return fail(result, L"output.SetUINT32(MF_MT_INTERLACE_MODE)");
        }

        result = output_type->SetUINT32(MF_MT_MPEG2_PROFILE, resolve_h264_profile(_session.options));
        if (FAILED(result))
        {
            return fail(result, L"output.SetUINT32(MF_MT_MPEG2_PROFILE)");
        }

        result = MFSetAttributeSize(
            output_type.Get(),
            MF_MT_FRAME_SIZE,
            static_cast<UINT32>(_session.output_width),
            static_cast<UINT32>(_session.output_height));
        if (FAILED(result))
        {
            return fail(result, L"MFSetAttributeSize(output, MF_MT_FRAME_SIZE)");
        }

        result = MFSetAttributeRatio(output_type.Get(), MF_MT_FRAME_RATE, _session.target_frame_rate, 1);
        if (FAILED(result))
        {
            return fail(result, L"MFSetAttributeRatio(output, MF_MT_FRAME_RATE)");
        }

        result = MFSetAttributeRatio(output_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(result))
        {
            return fail(result, L"MFSetAttributeRatio(output, MF_MT_PIXEL_ASPECT_RATIO)");
        }

        result = _sink_writer->AddStream(output_type.Get(), &_stream_index);
        if (FAILED(result))
        {
            return fail(result, L"IMFSinkWriter::AddStream");
        }

        ComPtr<IMFMediaType> input_type;
        result = MFCreateMediaType(&input_type);
        if (FAILED(result))
        {
            return fail(result, L"MFCreateMediaType(input)");
        }

        result = input_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(result))
        {
            return fail(result, L"input.SetGUID(MF_MT_MAJOR_TYPE)");
        }

        result = input_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (FAILED(result))
        {
            return fail(result, L"input.SetGUID(MF_MT_SUBTYPE)");
        }

        result = input_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(result))
        {
            return fail(result, L"input.SetUINT32(MF_MT_INTERLACE_MODE)");
        }

        result = input_type->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
        if (FAILED(result))
        {
            return fail(result, L"input.SetUINT32(MF_MT_FIXED_SIZE_SAMPLES)");
        }

        result = input_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        if (FAILED(result))
        {
            return fail(result, L"input.SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT)");
        }

        result = MFSetAttributeSize(
            input_type.Get(),
            MF_MT_FRAME_SIZE,
            static_cast<UINT32>(_session.output_width),
            static_cast<UINT32>(_session.output_height));
        if (FAILED(result))
        {
            return fail(result, L"MFSetAttributeSize(input, MF_MT_FRAME_SIZE)");
        }

        result = MFSetAttributeRatio(input_type.Get(), MF_MT_FRAME_RATE, _session.target_frame_rate, 1);
        if (FAILED(result))
        {
            return fail(result, L"MFSetAttributeRatio(input, MF_MT_FRAME_RATE)");
        }

        result = MFSetAttributeRatio(input_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(result))
        {
            return fail(result, L"MFSetAttributeRatio(input, MF_MT_PIXEL_ASPECT_RATIO)");
        }

        result = _sink_writer->SetInputMediaType(_stream_index, input_type.Get(), nullptr);
        if (FAILED(result))
        {
            return fail(result, L"IMFSinkWriter::SetInputMediaType");
        }

        result = _sink_writer->BeginWriting();
        if (FAILED(result))
        {
            return fail(result, L"IMFSinkWriter::BeginWriting");
        }

        return result;
    }

    wgc_capture_backend::wgc_capture_backend(d3d_context& d3d, const resolved_capture_geometry& geometry, const sr_capture_source& source)
        : _d3d(d3d),
          _geometry(geometry),
          _source(source)
    {
    }

    wgc_capture_backend::~wgc_capture_backend()
    {
        stop();
    }

    HRESULT wgc_capture_backend::start()
    {
        try
        {
            if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported())
            {
                _failure_reason = capture_fallback_reason::unsupported;
                return E_NOTIMPL;
            }

            ComPtr<IDXGIDevice> dxgi_device;
            winrt::check_hresult(_d3d.device.As(&dxgi_device));

            winrt::com_ptr<IInspectable> inspectable_device;
            winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(), inspectable_device.put()));
            _direct3d_device = inspectable_device.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

            auto interop = winrt::get_activation_factory<
                winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                IGraphicsCaptureItemInterop>();

            if (_geometry.window != nullptr)
            {
                winrt::check_hresult(interop->CreateForWindow(
                    _geometry.window,
                    winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
                    winrt::put_abi(_item)));
            }
            else
            {
                winrt::check_hresult(interop->CreateForMonitor(
                    _geometry.monitor,
                    winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
                    winrt::put_abi(_item)));
            }

            const auto capture_size = _item.Size();
            _frame_pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
                _direct3d_device,
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                static_cast<int32_t>(wgc_frame_queue_limit),
                capture_size);

            _frame_arrived_token = _frame_pool.FrameArrived({ this, &wgc_capture_backend::on_frame_arrived });
            _session = _frame_pool.CreateCaptureSession(_item);
            _session.StartCapture();
            _startup_deadline_qpc = qpc_now() + (qpc_frequency() * wgc_startup_warmup_millis / 1000);
            return S_OK;
        }
        catch (const winrt::hresult_error& error)
        {
            _failure_reason = capture_fallback_reason::startup_hresult;
            return error.code();
        }
    }

    HRESULT wgc_capture_backend::copy_next_frame_to(ID3D11Texture2D* destination, copied_capture_frame& copied_frame)
    {
        if (destination == nullptr)
        {
            return E_INVALIDARG;
        }

        std::unique_lock lock(_gate);
        const auto ready_or_failed = [this]
        {
            return !_ready_frames.empty() || _stopped || FAILED(_failure);
        };

        if (_ready_frames.empty() && !_stopped && SUCCEEDED(_failure) && !_has_received_first_frame)
        {
            while (_ready_frames.empty() && !_stopped && SUCCEEDED(_failure) && !_has_received_first_frame)
            {
                const auto remaining_qpc = _startup_deadline_qpc - qpc_now();
                if (remaining_qpc <= 0)
                {
                    _failure_reason = capture_fallback_reason::startup_timeout;
                    return capture_timeout;
                }

                const auto wait_millis = std::max<int32_t>(static_cast<int32_t>(qpc_to_millis(remaining_qpc)), 1);
                _frame_arrived_condition.wait_for(lock, std::chrono::milliseconds(wait_millis), ready_or_failed);
            }
        }
        else if (_ready_frames.empty() && !_stopped && SUCCEEDED(_failure))
        {
            (void)_frame_arrived_condition.wait_for(lock, std::chrono::milliseconds(5), ready_or_failed);
        }

        if (FAILED(_failure))
        {
            return _failure;
        }

        if (_ready_frames.empty())
        {
            return capture_timeout;
        }

        auto next = std::move(_ready_frames.back());
        _ready_frames.clear();
        lock.unlock();

        _d3d.context->CopyResource(destination, next.texture.Get());
        copied_frame.source_rect = _geometry.source_rect;
        copied_frame.captured_at_qpc = next.captured_at_qpc;
        return S_OK;
    }

    void wgc_capture_backend::stop()
    {
        std::scoped_lock lock(_gate);
        if (_stopped)
        {
            return;
        }

        _stopped = true;
        if (_frame_pool)
        {
            _frame_pool.FrameArrived(_frame_arrived_token);
        }

        if (_session)
        {
            _session.Close();
            _session = nullptr;
        }

        if (_frame_pool)
        {
            _frame_pool.Close();
            _frame_pool = nullptr;
        }

        _item = nullptr;
        _frame_arrived_condition.notify_all();
    }

    uint64_t wgc_capture_backend::consume_backpressure_drops() noexcept
    {
        return _queue_drop_count.exchange(0);
    }

    const char* wgc_capture_backend::backend_name() const noexcept
    {
        return "windows-graphics-capture";
    }

    capture_fallback_reason wgc_capture_backend::last_failure_reason() const noexcept
    {
        std::scoped_lock lock(_gate);
        return _failure_reason;
    }

    bool wgc_capture_backend::has_received_first_frame() const noexcept
    {
        std::scoped_lock lock(_gate);
        return _has_received_first_frame;
    }

    int64_t wgc_capture_backend::first_frame_arrived_qpc() const noexcept
    {
        std::scoped_lock lock(_gate);
        return _first_frame_arrived_qpc;
    }

    void wgc_capture_backend::fail_locked(HRESULT failure, capture_fallback_reason reason) noexcept
    {
        _failure = failure;
        _failure_reason = reason;
        _frame_arrived_condition.notify_all();
    }

    void wgc_capture_backend::on_frame_arrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const&)
    {
        try
        {
            auto frame = sender.TryGetNextFrame();
            const auto content_size = frame.ContentSize();
            const auto expected_width = _geometry.capture_item_bounds.right - _geometry.capture_item_bounds.left;
            const auto expected_height = _geometry.capture_item_bounds.bottom - _geometry.capture_item_bounds.top;
            if (content_size.Width != expected_width || content_size.Height != expected_height)
            {
                std::scoped_lock lock(_gate);
                _ready_frames.clear();

                auto updated_geometry = resolve_capture_geometry(_source);
                if (!updated_geometry.has_value() || updated_geometry->spans_multiple_monitors)
                {
                    fail_locked(MF_E_TRANSFORM_STREAM_CHANGE, capture_fallback_reason::stream_change);
                    return;
                }

                try
                {
                    sender.Recreate(
                        _direct3d_device,
                        winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                        static_cast<int32_t>(wgc_frame_queue_limit),
                        content_size);
                    _geometry = *updated_geometry;
                }
                catch (const winrt::hresult_error& recreate_error)
                {
                    fail_locked(recreate_error.code(), capture_fallback_reason::stream_change);
                    return;
                }

                fail_locked(MF_E_TRANSFORM_STREAM_CHANGE, capture_fallback_reason::stream_change);
                return;
            }

            auto access = frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> texture;
            winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(texture.GetAddressOf())));

            std::scoped_lock lock(_gate);
            if (_ready_frames.size() >= wgc_frame_queue_limit)
            {
                _ready_frames.pop_front();
                ++_queue_drop_count;
            }

            const auto captured_at_qpc = qpc_now();
            _ready_frames.push_back({ texture, captured_at_qpc });
            if (!_has_received_first_frame)
            {
                _has_received_first_frame = true;
                _first_frame_arrived_qpc = captured_at_qpc;
            }

            _frame_arrived_condition.notify_all();
        }
        catch (const winrt::hresult_error& error)
        {
            std::scoped_lock lock(_gate);
            fail_locked(
                error.code(),
                _has_received_first_frame
                    ? capture_fallback_reason::runtime_hresult
                    : capture_fallback_reason::startup_hresult);
        }
    }

    dxgi_duplication_capture_backend::dxgi_duplication_capture_backend(d3d_context& d3d, const resolved_capture_geometry& geometry)
        : _d3d(d3d),
          _geometry(geometry)
    {
    }

    HRESULT dxgi_duplication_capture_backend::start()
    {
        if (_geometry.monitor == nullptr || _d3d.adapter == nullptr)
        {
            return E_INVALIDARG;
        }

        for (UINT output_index = 0;; ++output_index)
        {
            ComPtr<IDXGIOutput> output;
            auto result = _d3d.adapter->EnumOutputs(output_index, &output);
            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            if (FAILED(result))
            {
                return result;
            }

            DXGI_OUTPUT_DESC output_desc{};
            result = output->GetDesc(&output_desc);
            if (FAILED(result))
            {
                return result;
            }

            if (output_desc.Monitor != _geometry.monitor)
            {
                continue;
            }

            ComPtr<IDXGIOutput1> output1;
            result = output.As(&output1);
            if (FAILED(result))
            {
                return result;
            }

            return output1->DuplicateOutput(_d3d.device.Get(), &_duplication);
        }

        return DXGI_ERROR_NOT_FOUND;
    }

    HRESULT dxgi_duplication_capture_backend::copy_next_frame_to(ID3D11Texture2D* destination, copied_capture_frame& copied_frame)
    {
        if (_duplication == nullptr || destination == nullptr)
        {
            return E_INVALIDARG;
        }

        DXGI_OUTDUPL_FRAME_INFO frame_info{};
        ComPtr<IDXGIResource> resource;
        auto result = _duplication->AcquireNextFrame(0, &frame_info, &resource);
        if (result == DXGI_ERROR_WAIT_TIMEOUT)
        {
            return capture_timeout;
        }

        if (FAILED(result))
        {
            return result;
        }

        ComPtr<ID3D11Texture2D> source_texture;
        result = resource.As(&source_texture);
        if (SUCCEEDED(result))
        {
            _d3d.context->CopyResource(destination, source_texture.Get());
            copied_frame.source_rect = _geometry.source_rect;
            copied_frame.captured_at_qpc = qpc_now();
        }

        const auto release_result = _duplication->ReleaseFrame();
        return FAILED(result) ? result : release_result;
    }

    void dxgi_duplication_capture_backend::stop()
    {
        _duplication.Reset();
    }

    const char* dxgi_duplication_capture_backend::backend_name() const noexcept
    {
        return "dxgi-desktop-duplication";
    }

    void destroy_legacy_capture_slot(legacy_capture_slot& slot)
    {
        if (slot.dc != nullptr && slot.previous_bitmap != nullptr)
        {
            SelectObject(slot.dc, slot.previous_bitmap);
            slot.previous_bitmap = nullptr;
        }

        if (slot.bitmap != nullptr)
        {
            DeleteObject(slot.bitmap);
            slot.bitmap = nullptr;
        }

        if (slot.dc != nullptr)
        {
            DeleteDC(slot.dc);
            slot.dc = nullptr;
        }

        slot.pixels = nullptr;
        slot.pixel_bytes = 0;
        slot.captured_at_qpc = 0;
        slot.capture_duration_qpc = 0;
        slot.sequence = 0;
    }

    bool prepare_legacy_capture_slot(HDC screen_dc, int32_t width, int32_t height, legacy_capture_slot& slot)
    {
        slot.dc = CreateCompatibleDC(screen_dc);
        if (slot.dc == nullptr)
        {
            return false;
        }

        BITMAPINFO bitmap_info{};
        bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap_info.bmiHeader.biWidth = width;
        bitmap_info.bmiHeader.biHeight = -height;
        bitmap_info.bmiHeader.biPlanes = 1;
        bitmap_info.bmiHeader.biBitCount = 32;
        bitmap_info.bmiHeader.biCompression = BI_RGB;

        void* pixels = nullptr;
        slot.bitmap = CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (slot.bitmap == nullptr || pixels == nullptr)
        {
            destroy_legacy_capture_slot(slot);
            return false;
        }

        slot.previous_bitmap = SelectObject(slot.dc, slot.bitmap);
        slot.pixels = static_cast<uint8_t*>(pixels);
        slot.pixel_bytes = static_cast<uint32_t>(width * height * 4);
        SetStretchBltMode(slot.dc, COLORONCOLOR);
        return true;
    }

    bool capture_legacy_frame(recording_session& session, legacy_capture_slot& slot)
    {
        const auto source_width = session.legacy_capture_rect.right - session.legacy_capture_rect.left;
        const auto source_height = session.legacy_capture_rect.bottom - session.legacy_capture_rect.top;
        if (source_width <= 0 || source_height <= 0 || session.screen_dc == nullptr)
        {
            return false;
        }

        if (source_width == session.output_width && source_height == session.output_height)
        {
            return BitBlt(
                slot.dc,
                0,
                0,
                session.output_width,
                session.output_height,
                session.screen_dc,
                session.legacy_capture_rect.left,
                session.legacy_capture_rect.top,
                SRCCOPY | CAPTUREBLT) != FALSE;
        }

        return StretchBlt(
            slot.dc,
            0,
            0,
            session.output_width,
            session.output_height,
            session.screen_dc,
            session.legacy_capture_rect.left,
            session.legacy_capture_rect.top,
            source_width,
            source_height,
            SRCCOPY | CAPTUREBLT) != FALSE;
    }

    bool initialize_legacy_capture_resources(recording_session& session)
    {
        session.screen_dc = GetDC(nullptr);
        if (session.screen_dc == nullptr)
        {
            return false;
        }

        session.legacy_slots.resize(capture_slot_count);
        for (size_t index = 0; index < capture_slot_count; ++index)
        {
            if (!prepare_legacy_capture_slot(session.screen_dc, session.output_width, session.output_height, session.legacy_slots[index]))
            {
                return false;
            }

            session.free_slots.push_back(index);
        }

        return true;
    }

    void release_legacy_capture_resources(recording_session& session)
    {
        for (auto& slot : session.legacy_slots)
        {
            destroy_legacy_capture_slot(slot);
        }

        session.legacy_slots.clear();
        if (session.screen_dc != nullptr)
        {
            ReleaseDC(nullptr, session.screen_dc);
            session.screen_dc = nullptr;
        }
    }

    void request_stop(recording_session& session)
    {
        {
            std::scoped_lock lock(session.gate);
            session.stop_requested = true;
        }

        session.ready_condition.notify_all();
    }

    void fail_session(recording_session& session, HRESULT failure, std::wstring reason)
    {
        {
            std::scoped_lock lock(session.gate);
            if (FAILED(session.failure))
            {
                return;
            }

            session.failure = failure;
            session.failure_reason = std::move(reason);
            session.stop_requested = true;
        }

        session.ready_condition.notify_all();
    }

    void recycle_slot(recording_session& session, size_t slot_index)
    {
        std::scoped_lock lock(session.gate);
        session.free_slots.push_back(slot_index);
    }

    bool is_capture_timeout(HRESULT result)
    {
        return result == capture_timeout || result == DXGI_ERROR_WAIT_TIMEOUT;
    }

    HRESULT write_legacy_sample(
        IMFSinkWriter* sink_writer,
        DWORD stream_index,
        const legacy_capture_slot& slot,
        LONGLONG sample_time,
        LONGLONG sample_duration)
    {
        if (sink_writer == nullptr || slot.pixels == nullptr || slot.pixel_bytes == 0)
        {
            return E_INVALIDARG;
        }

        ComPtr<IMFMediaBuffer> buffer;
        auto result = MFCreateMemoryBuffer(slot.pixel_bytes, &buffer);
        if (FAILED(result))
        {
            return result;
        }

        BYTE* destination = nullptr;
        DWORD max_length = 0;
        DWORD current_length = 0;
        result = buffer->Lock(&destination, &max_length, &current_length);
        if (FAILED(result))
        {
            return result;
        }

        if (max_length < slot.pixel_bytes)
        {
            buffer->Unlock();
            return E_OUTOFMEMORY;
        }

        std::memcpy(destination, slot.pixels, slot.pixel_bytes);
        result = buffer->Unlock();
        if (FAILED(result))
        {
            return result;
        }

        result = buffer->SetCurrentLength(slot.pixel_bytes);
        if (FAILED(result))
        {
            return result;
        }

        ComPtr<IMFSample> sample;
        result = MFCreateSample(&sample);
        if (FAILED(result))
        {
            return result;
        }

        result = sample->AddBuffer(buffer.Get());
        if (FAILED(result))
        {
            return result;
        }

        result = sample->SetSampleTime(sample_time);
        if (FAILED(result))
        {
            return result;
        }

        result = sample->SetSampleDuration(std::max<LONGLONG>(sample_duration, 1));
        if (FAILED(result))
        {
            return result;
        }

        return sink_writer->WriteSample(stream_index, sample.Get());
    }

    HRESULT create_legacy_sink_writer(recording_session& session, ComPtr<IMFSinkWriter>& sink_writer, DWORD& stream_index)
    {
        std::error_code create_error;
        std::filesystem::create_directories(session.final_output_path.parent_path(), create_error);
        if (create_error)
        {
            return E_FAIL;
        }

        std::error_code remove_error;
        std::filesystem::remove(session.final_output_path, remove_error);

        ComPtr<IMFAttributes> attributes;
        auto result = MFCreateAttributes(&attributes, 3);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        if (FAILED(result))
        {
            return result;
        }

        result = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (FAILED(result))
        {
            return result;
        }

        result = MFCreateSinkWriterFromURL(session.final_output_path.c_str(), nullptr, attributes.Get(), &sink_writer);
        if (FAILED(result))
        {
            return result;
        }

        ComPtr<IMFMediaType> output_type;
        result = MFCreateMediaType(&output_type);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetUINT32(MF_MT_AVG_BITRATE, 10'000'000);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
        if (FAILED(result))
        {
            return result;
        }

        result = MFSetAttributeSize(output_type.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(session.output_width), static_cast<UINT32>(session.output_height));
        if (FAILED(result))
        {
            return result;
        }

        result = MFSetAttributeRatio(output_type.Get(), MF_MT_FRAME_RATE, session.target_frame_rate, 1);
        if (FAILED(result))
        {
            return result;
        }

        result = MFSetAttributeRatio(output_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(result))
        {
            return result;
        }

        result = sink_writer->AddStream(output_type.Get(), &stream_index);
        if (FAILED(result))
        {
            return result;
        }

        ComPtr<IMFMediaType> input_type;
        result = MFCreateMediaType(&input_type);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(session.output_width * 4));
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetUINT32(MF_MT_SAMPLE_SIZE, static_cast<UINT32>(session.output_width * session.output_height * 4));
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        if (FAILED(result))
        {
            return result;
        }

        result = MFSetAttributeSize(input_type.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(session.output_width), static_cast<UINT32>(session.output_height));
        if (FAILED(result))
        {
            return result;
        }

        result = MFSetAttributeRatio(input_type.Get(), MF_MT_FRAME_RATE, session.target_frame_rate, 1);
        if (FAILED(result))
        {
            return result;
        }

        result = MFSetAttributeRatio(input_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(result))
        {
            return result;
        }

        result = sink_writer->SetInputMediaType(stream_index, input_type.Get(), nullptr);
        if (FAILED(result))
        {
            return result;
        }

        return sink_writer->BeginWriting();
    }

    void gpu_capture_loop(recording_session* session)
    {
        if (session == nullptr)
        {
            return;
        }

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        auto* d3d = session->gpu_d3d.get();
        if (d3d == nullptr)
        {
            fail_session(*session, E_FAIL, L"GPU D3D context was not prepared.");
            std::scoped_lock lock(session->gate);
            session->capture_finished = true;
            return;
        }
        HRESULT result = S_OK;

        auto geometry = resolve_capture_geometry(session->source);
        if (!geometry.has_value())
        {
            fail_session(*session, E_INVALIDARG, L"Failed to resolve the capture geometry.");
            std::scoped_lock lock(session->gate);
            session->capture_finished = true;
            return;
        }

        capture_fallback_reason wgc_failure_reason = capture_fallback_reason::none;
        HRESULT wgc_failure_hresult = S_OK;
        constexpr const char* dxgi_backend_name = "dxgi-desktop-duplication";
        auto start_capture_backend = [&](bool allow_wgc) -> std::unique_ptr<capture_backend>
        {
            std::unique_ptr<capture_backend> started_backend;
            result = E_FAIL;

            if (allow_wgc)
            {
                session->wgc_startup_attempted = true;
                auto wgc = std::make_unique<wgc_capture_backend>(*d3d, *geometry, session->source);
                result = wgc->start();
                if (SUCCEEDED(result))
                {
                    session->capture_backend = wgc->backend_name();
                    started_backend = std::move(wgc);
                }
                else
                {
                    session->capture_backend = wgc->backend_name();
                    wgc_failure_reason = wgc->last_failure_reason();
                    wgc_failure_hresult = result;
                }
            }

            if (started_backend == nullptr &&
                geometry->monitor != nullptr &&
                can_fallback_to_dxgi(session->source.kind))
            {
                if (allow_wgc &&
                    (wgc_failure_reason != capture_fallback_reason::none || FAILED(wgc_failure_hresult)))
                {
                    record_capture_fallback(
                        *session,
                        "windows-graphics-capture",
                        dxgi_backend_name,
                        wgc_failure_reason == capture_fallback_reason::none
                            ? capture_fallback_reason::startup_hresult
                            : wgc_failure_reason,
                        wgc_failure_hresult);
                }

                auto dxgi = std::make_unique<dxgi_duplication_capture_backend>(*d3d, *geometry);
                result = dxgi->start();
                if (SUCCEEDED(result))
                {
                    session->capture_backend = dxgi->backend_name();
                    started_backend = std::move(dxgi);
                }
            }

            return started_backend;
        };

        auto backend = start_capture_backend(true);

        if (backend == nullptr)
        {
            if (session->wgc_startup_attempted &&
                !can_fallback_to_dxgi(session->source.kind) &&
                (wgc_failure_reason != capture_fallback_reason::none || FAILED(wgc_failure_hresult)))
            {
                record_capture_backend_failure(
                    *session,
                    "windows-graphics-capture",
                    wgc_failure_reason == capture_fallback_reason::none
                        ? capture_fallback_reason::startup_hresult
                        : wgc_failure_reason,
                    wgc_failure_hresult);
            }

            std::wstringstream message;
            if (session->wgc_startup_attempted && std::strcmp(session->capture_backend.c_str(), "windows-graphics-capture") == 0)
            {
                message << L"WGC capture startup failed. HRESULT=0x"
                        << std::hex
                        << static_cast<uint32_t>(wgc_failure_hresult);
                if (wgc_failure_reason != capture_fallback_reason::none)
                {
                    const auto reason_name = capture_fallback_reason_name(wgc_failure_reason);
                    message << L" (" << std::wstring(reason_name, reason_name + std::strlen(reason_name)) << L")";
                }
            }
            else
            {
                message << L"GPU capture backends could not be initialized.";
            }

            fail_session(*session, FAILED(result) ? result : E_FAIL, message.str());
            std::scoped_lock lock(session->gate);
            session->capture_finished = true;
            return;
        }

        session->capture_backend = backend->backend_name();
        if (backend->has_received_first_frame())
        {
            session->wgc_first_frame_latency_qpc = std::max<int64_t>(backend->first_frame_arrived_qpc() - session->started_qpc, 0);
        }

        try
        {
            frame_graph graph(*d3d, session->gpu_slots, geometry->capture_item_bounds, session->output_width, session->output_height, session->target_frame_rate);

            {
                std::scoped_lock lock(session->gate);
                for (size_t index = 0; index < session->gpu_slots.size(); ++index)
                {
                    session->free_slots.push_back(index);
                }
            }

            const auto target_period_qpc = frame_duration_qpc(session->target_frame_rate);
            auto next_capture_deadline = qpc_now();

            while (true)
            {
                bool should_stop = false;
                bool is_paused = false;
                {
                    std::scoped_lock lock(session->gate);
                    should_stop = session->stop_requested;
                    is_paused = session->pause_requested;
                }

                if (should_stop)
                {
                    break;
                }

                if (is_paused)
                {
                    next_capture_deadline = qpc_now();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }

                auto now = qpc_now();
                if (now < next_capture_deadline)
                {
                    const auto sleep_millis = static_cast<int32_t>(qpc_to_millis(next_capture_deadline - now));
                    if (sleep_millis > 1)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_millis - 1));
                    }

                    while ((now = qpc_now()) < next_capture_deadline)
                    {
                        SwitchToThread();
                    }
                }
                else if (now - next_capture_deadline >= target_period_qpc)
                {
                    const auto skipped_periods = static_cast<uint64_t>((now - next_capture_deadline) / target_period_qpc);
                    std::scoped_lock lock(session->gate);
                    session->metrics.pacing_overrun_count += skipped_periods;
                    next_capture_deadline += static_cast<int64_t>(skipped_periods) * target_period_qpc;
                }

                size_t slot_index = 0;
                {
                    std::scoped_lock lock(session->gate);
                    ++session->metrics.capture_attempts;
                    if (session->free_slots.empty())
                    {
                        ++session->metrics.backpressure_drop_count;
                        next_capture_deadline += target_period_qpc;
                        continue;
                    }

                    slot_index = session->free_slots.front();
                    session->free_slots.pop_front();
                }

                auto& slot = session->gpu_slots[slot_index];
                copied_capture_frame copied_frame{};
                const auto capture_started_qpc = qpc_now();
                result = backend->copy_next_frame_to(slot.bgra_texture.Get(), copied_frame);
                const auto capture_finished_qpc = qpc_now();

                {
                    std::scoped_lock lock(session->gate);
                    session->metrics.backpressure_drop_count += backend->consume_backpressure_drops();
                }

                if (is_capture_timeout(result))
                {
                    const auto failure_reason = backend->last_failure_reason();
                    const auto using_wgc = std::strcmp(backend->backend_name(), "windows-graphics-capture") == 0;
                    const auto can_fallback =
                        using_wgc &&
                        geometry->monitor != nullptr &&
                        can_fallback_to_dxgi(session->source.kind);
                    const auto startup_timed_out =
                        using_wgc &&
                        !backend->has_received_first_frame() &&
                        failure_reason == capture_fallback_reason::startup_timeout;

                    if (startup_timed_out)
                    {
                        if (can_fallback)
                        {
                            record_capture_fallback(
                                *session,
                                backend->backend_name(),
                                dxgi_backend_name,
                                capture_fallback_reason::startup_timeout,
                                result);
                            backend->stop();
                            auto fallback_backend = start_capture_backend(false);
                            if (fallback_backend != nullptr)
                            {
                                backend = std::move(fallback_backend);
                                session->capture_backend = backend->backend_name();
                                recycle_slot(*session, slot_index);
                                next_capture_deadline += target_period_qpc;
                                continue;
                            }
                        }
                        else
                        {
                            record_capture_backend_failure(
                                *session,
                                backend->backend_name(),
                                capture_fallback_reason::startup_timeout,
                                result);
                        }

                        {
                            std::scoped_lock lock(session->gate);
                            ++session->metrics.capture_failure_count;
                        }

                        std::wstringstream message;
                        const auto backend_name = backend->backend_name();
                        const std::wstring backend_name_wide(backend_name, backend_name + std::strlen(backend_name));
                        message << L"GPU capture startup timed out on " << backend_name_wide
                                << L". HRESULT=0x"
                                << std::hex
                                << static_cast<uint32_t>(result)
                                << L" (startup-timeout)";
                        fail_session(*session, result, message.str());
                        recycle_slot(*session, slot_index);
                        break;
                    }

                    if (backend->has_received_first_frame() && session->wgc_first_frame_latency_qpc == 0)
                    {
                        session->wgc_first_frame_latency_qpc =
                            std::max<int64_t>(backend->first_frame_arrived_qpc() - session->started_qpc, 0);
                    }

                    recycle_slot(*session, slot_index);
                    next_capture_deadline += target_period_qpc;
                    continue;
                }

                if (FAILED(result))
                {
                    const auto failure_reason = backend->last_failure_reason();
                    const auto using_wgc = std::strcmp(backend->backend_name(), "windows-graphics-capture") == 0;
                    const auto can_fallback =
                        using_wgc &&
                        geometry->monitor != nullptr &&
                        can_fallback_to_dxgi(session->source.kind);

                    if (backend->has_received_first_frame() && session->wgc_first_frame_latency_qpc == 0)
                    {
                        session->wgc_first_frame_latency_qpc =
                            std::max<int64_t>(backend->first_frame_arrived_qpc() - session->started_qpc, 0);
                    }

                    if (can_fallback)
                    {
                        record_capture_fallback(
                            *session,
                            backend->backend_name(),
                            dxgi_backend_name,
                            failure_reason == capture_fallback_reason::none
                                ? capture_fallback_reason::runtime_hresult
                                : failure_reason,
                            result);
                        backend->stop();
                        auto fallback_backend = start_capture_backend(false);
                        if (fallback_backend != nullptr)
                        {
                            backend = std::move(fallback_backend);
                            session->capture_backend = backend->backend_name();
                            recycle_slot(*session, slot_index);
                            continue;
                        }
                    }
                    else if (using_wgc && failure_reason != capture_fallback_reason::none)
                    {
                        record_capture_backend_failure(
                            *session,
                            backend->backend_name(),
                            failure_reason,
                            result);
                    }

                    {
                        std::scoped_lock lock(session->gate);
                        ++session->metrics.capture_failure_count;
                    }

                    const auto backend_name = backend->backend_name();
                    const std::wstring backend_name_wide(backend_name, backend_name + std::strlen(backend_name));
                    std::wstringstream message;
                    message << L"GPU capture failed on " << backend_name_wide
                            << L". HRESULT=0x"
                            << std::hex
                            << static_cast<uint32_t>(result);
                    if (failure_reason != capture_fallback_reason::none)
                    {
                        const auto reason_name = capture_fallback_reason_name(failure_reason);
                        message << L" (" << std::wstring(reason_name, reason_name + std::strlen(reason_name)) << L")";
                    }
                    fail_session(*session, result, message.str());
                    recycle_slot(*session, slot_index);
                    break;
                }

                if (backend->has_received_first_frame() && session->wgc_first_frame_latency_qpc == 0)
                {
                    session->wgc_first_frame_latency_qpc =
                        std::max<int64_t>(backend->first_frame_arrived_qpc() - session->started_qpc, 0);
                }

                const auto convert_started_qpc = qpc_now();
                try
                {
                    graph.process_slot(slot, copied_frame.source_rect);
                }
                catch (const winrt::hresult_error& error)
                {
                    fail_session(*session, error.code(), L"GPU crop/scale/convert failed.");
                    recycle_slot(*session, slot_index);
                    break;
                }
                const auto convert_finished_qpc = qpc_now();

                {
                    std::scoped_lock lock(session->gate);
                    slot.captured_at_qpc = copied_frame.captured_at_qpc != 0 ? copied_frame.captured_at_qpc : capture_finished_qpc;
                    slot.capture_duration_qpc = capture_finished_qpc - capture_started_qpc;
                    slot.convert_duration_qpc = convert_finished_qpc - convert_started_qpc;
                    slot.sequence = ++session->next_sequence;
                    session->ready_slots.push_back(slot_index);
                    ++session->metrics.captured_frames;
                    session->metrics.total_capture_latency_qpc += slot.capture_duration_qpc;
                    session->metrics.total_convert_latency_qpc += slot.convert_duration_qpc;
                    session->metrics.first_captured_qpc = session->metrics.first_captured_qpc == 0 ? slot.captured_at_qpc : session->metrics.first_captured_qpc;
                    session->metrics.last_captured_qpc = slot.captured_at_qpc;
                    session->metrics.peak_queue_depth = std::max<uint32_t>(session->metrics.peak_queue_depth, static_cast<uint32_t>(session->ready_slots.size()));
                }

                session->ready_condition.notify_one();
                next_capture_deadline += target_period_qpc;
            }
        }
        catch (const winrt::hresult_error& error)
        {
            fail_session(*session, error.code(), L"GPU frame-graph initialization failed.");
        }

        backend->stop();
        {
            std::scoped_lock lock(session->gate);
            session->capture_finished = true;
        }

        session->ready_condition.notify_all();
    }

    void legacy_capture_loop(recording_session* session)
    {
        if (session == nullptr)
        {
            return;
        }

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        const auto target_period_qpc = frame_duration_qpc(session->target_frame_rate);
        auto next_capture_deadline = qpc_now();

        while (true)
        {
            bool should_stop = false;
            bool is_paused = false;
            {
                std::scoped_lock lock(session->gate);
                should_stop = session->stop_requested;
                is_paused = session->pause_requested;
            }

            if (should_stop)
            {
                break;
            }

            if (is_paused)
            {
                next_capture_deadline = qpc_now();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            auto now = qpc_now();
            if (now < next_capture_deadline)
            {
                const auto sleep_millis = static_cast<int32_t>(qpc_to_millis(next_capture_deadline - now));
                if (sleep_millis > 1)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_millis - 1));
                }

                while ((now = qpc_now()) < next_capture_deadline)
                {
                    SwitchToThread();
                }
            }
            else if (now - next_capture_deadline >= target_period_qpc)
            {
                const auto skipped_periods = static_cast<uint64_t>((now - next_capture_deadline) / target_period_qpc);
                std::scoped_lock lock(session->gate);
                session->metrics.pacing_overrun_count += skipped_periods;
                next_capture_deadline += static_cast<int64_t>(skipped_periods) * target_period_qpc;
            }

            size_t slot_index = 0;
            {
                std::scoped_lock lock(session->gate);
                ++session->metrics.capture_attempts;
                if (session->free_slots.empty())
                {
                    ++session->metrics.backpressure_drop_count;
                    next_capture_deadline += target_period_qpc;
                    continue;
                }

                slot_index = session->free_slots.front();
                session->free_slots.pop_front();
            }

            auto& slot = session->legacy_slots[slot_index];
            const auto capture_started_qpc = qpc_now();
            const auto captured = capture_legacy_frame(*session, slot);
            const auto capture_finished_qpc = qpc_now();

            if (!captured)
            {
                {
                    std::scoped_lock lock(session->gate);
                    ++session->metrics.capture_failure_count;
                    session->free_slots.push_back(slot_index);
                }

                next_capture_deadline += target_period_qpc;
                continue;
            }

            {
                std::scoped_lock lock(session->gate);
                slot.captured_at_qpc = capture_finished_qpc;
                slot.capture_duration_qpc = capture_finished_qpc - capture_started_qpc;
                slot.sequence = ++session->next_sequence;
                session->ready_slots.push_back(slot_index);
                ++session->metrics.captured_frames;
                session->metrics.total_capture_latency_qpc += slot.capture_duration_qpc;
                session->metrics.first_captured_qpc = session->metrics.first_captured_qpc == 0 ? slot.captured_at_qpc : session->metrics.first_captured_qpc;
                session->metrics.last_captured_qpc = slot.captured_at_qpc;
                session->metrics.peak_queue_depth = std::max<uint32_t>(session->metrics.peak_queue_depth, static_cast<uint32_t>(session->ready_slots.size()));
            }

            session->ready_condition.notify_one();
            next_capture_deadline += target_period_qpc;
        }

        {
            std::scoped_lock lock(session->gate);
            session->capture_finished = true;
        }

        session->ready_condition.notify_all();
    }

    void gpu_encode_loop(recording_session* session)
    {
        if (session == nullptr)
        {
            return;
        }

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

        media_foundation_scope media_foundation;
        if (!media_foundation.is_ready())
        {
            fail_session(*session, E_FAIL, L"Media Foundation could not be initialized.");
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
            return;
        }
        auto* d3d = session->gpu_d3d.get();
        if (d3d == nullptr)
        {
            fail_session(*session, E_FAIL, L"GPU D3D context was not prepared.");
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
            return;
        }
        auto result = S_OK;

        try
        {
            encoder_backend encoder(*session, *d3d);
            {
                std::scoped_lock lock(session->gate);
                session->encode_backend = encoder.backend_name();
                session->hardware_encode = encoder.is_hardware_encode();
            }

            const auto default_duration_qpc = frame_duration_qpc(session->target_frame_rate);
            size_t pending_slot_index = static_cast<size_t>(-1);
            auto last_duration_qpc = default_duration_qpc;

            while (true)
            {
                size_t ready_slot_index = static_cast<size_t>(-1);
                {
                    std::unique_lock lock(session->gate);
                    session->ready_condition.wait(lock, [&session]
                    {
                        return !session->ready_slots.empty() || session->capture_finished || FAILED(session->failure);
                    });

                    if (session->ready_slots.empty())
                    {
                        if (session->capture_finished || FAILED(session->failure))
                        {
                            break;
                        }

                        continue;
                    }

                    ready_slot_index = session->ready_slots.front();
                    session->ready_slots.pop_front();
                }

                if (pending_slot_index == static_cast<size_t>(-1))
                {
                    pending_slot_index = ready_slot_index;
                    continue;
                }

                auto& pending_slot = session->gpu_slots[pending_slot_index];
                const auto current_capture_qpc = session->gpu_slots[ready_slot_index].captured_at_qpc;
                const auto sample_duration_qpc = std::max<int64_t>(current_capture_qpc - pending_slot.captured_at_qpc, 1);
                const auto encode_started_qpc = qpc_now();

                encoder.write_slot(pending_slot, qpc_to_hns(pending_slot.captured_at_qpc - session->started_qpc), qpc_to_hns(sample_duration_qpc));
                const auto encode_finished_qpc = qpc_now();

                {
                    std::scoped_lock lock(session->gate);
                    ++session->metrics.encoded_frames;
                    session->metrics.total_queue_latency_qpc += std::max<int64_t>(0, encode_started_qpc - pending_slot.captured_at_qpc);
                    session->metrics.total_encode_latency_qpc += encode_finished_qpc - encode_started_qpc;
                    session->metrics.last_sample_duration_qpc = sample_duration_qpc;
                }

                recycle_slot(*session, pending_slot_index);
                pending_slot_index = ready_slot_index;
                last_duration_qpc = sample_duration_qpc;
            }

            if (pending_slot_index != static_cast<size_t>(-1) && SUCCEEDED(session->failure))
            {
                auto& pending_slot = session->gpu_slots[pending_slot_index];
                const auto encode_started_qpc = qpc_now();
                encoder.write_slot(pending_slot, qpc_to_hns(pending_slot.captured_at_qpc - session->started_qpc), qpc_to_hns(last_duration_qpc));
                const auto encode_finished_qpc = qpc_now();

                {
                    std::scoped_lock lock(session->gate);
                    ++session->metrics.encoded_frames;
                    session->metrics.total_queue_latency_qpc += std::max<int64_t>(0, encode_started_qpc - pending_slot.captured_at_qpc);
                    session->metrics.total_encode_latency_qpc += encode_finished_qpc - encode_started_qpc;
                    session->metrics.last_sample_duration_qpc = last_duration_qpc;
                }

                recycle_slot(*session, pending_slot_index);
            }

            if (SUCCEEDED(session->failure))
            {
                result = encoder.finalize();
                if (FAILED(result))
                {
                    fail_session(*session, result, L"Failed to finalize the MP4 file.");
                }
            }
        }
        catch (const winrt::hresult_error& error)
        {
            std::wstringstream message;
            message << L"GPU encode path failed. HRESULT=0x" << std::hex << static_cast<uint32_t>(error.code().value);
            const auto detail = std::wstring(error.message().c_str());
            if (!detail.empty())
            {
                message << L". " << detail;
            }
            fail_session(*session, error.code(), message.str());
        }

        {
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
        }
    }

    void legacy_encode_loop(recording_session* session)
    {
        if (session == nullptr)
        {
            return;
        }

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        media_foundation_scope media_foundation;
        if (!media_foundation.is_ready())
        {
            fail_session(*session, E_FAIL, L"Media Foundation could not be initialized.");
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
            return;
        }

        ComPtr<IMFSinkWriter> sink_writer;
        DWORD stream_index = 0;
        const auto writer_result = create_legacy_sink_writer(*session, sink_writer, stream_index);
        if (FAILED(writer_result))
        {
            fail_session(*session, writer_result, L"Failed to create the legacy MP4 sink writer.");
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
            return;
        }

        const auto default_duration_qpc = frame_duration_qpc(session->target_frame_rate);
        size_t pending_slot_index = static_cast<size_t>(-1);
        auto last_duration_qpc = default_duration_qpc;

        while (true)
        {
            size_t ready_slot_index = static_cast<size_t>(-1);
            {
                std::unique_lock lock(session->gate);
                session->ready_condition.wait(lock, [&session]
                {
                    return !session->ready_slots.empty() || session->capture_finished || FAILED(session->failure);
                });

                if (session->ready_slots.empty())
                {
                    if (session->capture_finished || FAILED(session->failure))
                    {
                        break;
                    }

                    continue;
                }

                ready_slot_index = session->ready_slots.front();
                session->ready_slots.pop_front();
            }

            if (pending_slot_index == static_cast<size_t>(-1))
            {
                pending_slot_index = ready_slot_index;
                continue;
            }

            auto& pending_slot = session->legacy_slots[pending_slot_index];
            const auto current_capture_qpc = session->legacy_slots[ready_slot_index].captured_at_qpc;
            const auto sample_duration_qpc = std::max<int64_t>(current_capture_qpc - pending_slot.captured_at_qpc, 1);
            const auto encode_started_qpc = qpc_now();
            const auto result = write_legacy_sample(sink_writer.Get(), stream_index, pending_slot, qpc_to_hns(pending_slot.captured_at_qpc - session->started_qpc), qpc_to_hns(sample_duration_qpc));
            const auto encode_finished_qpc = qpc_now();
            if (FAILED(result))
            {
                fail_session(*session, result, L"Failed to write a legacy video sample.");
                recycle_slot(*session, pending_slot_index);
                recycle_slot(*session, ready_slot_index);
                break;
            }

            {
                std::scoped_lock lock(session->gate);
                ++session->metrics.encoded_frames;
                session->metrics.total_queue_latency_qpc += std::max<int64_t>(0, encode_started_qpc - pending_slot.captured_at_qpc);
                session->metrics.total_encode_latency_qpc += encode_finished_qpc - encode_started_qpc;
                session->metrics.last_sample_duration_qpc = sample_duration_qpc;
            }

            recycle_slot(*session, pending_slot_index);
            pending_slot_index = ready_slot_index;
            last_duration_qpc = sample_duration_qpc;
        }

        if (pending_slot_index != static_cast<size_t>(-1) && SUCCEEDED(session->failure))
        {
            auto& pending_slot = session->legacy_slots[pending_slot_index];
            const auto encode_started_qpc = qpc_now();
            const auto result = write_legacy_sample(sink_writer.Get(), stream_index, pending_slot, qpc_to_hns(pending_slot.captured_at_qpc - session->started_qpc), qpc_to_hns(last_duration_qpc));
            const auto encode_finished_qpc = qpc_now();
            if (FAILED(result))
            {
                fail_session(*session, result, L"Failed to write the final legacy video sample.");
                recycle_slot(*session, pending_slot_index);
            }
            else
            {
                {
                    std::scoped_lock lock(session->gate);
                    ++session->metrics.encoded_frames;
                    session->metrics.total_queue_latency_qpc += std::max<int64_t>(0, encode_started_qpc - pending_slot.captured_at_qpc);
                    session->metrics.total_encode_latency_qpc += encode_finished_qpc - encode_started_qpc;
                    session->metrics.last_sample_duration_qpc = last_duration_qpc;
                }

                recycle_slot(*session, pending_slot_index);
            }
        }

        if (SUCCEEDED(session->failure))
        {
            const auto finalize_result = sink_writer->Finalize();
            if (FAILED(finalize_result))
            {
                fail_session(*session, finalize_result, L"Failed to finalize the legacy MP4 file.");
            }
        }

        {
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
        }
    }

    void join_recording_threads(recording_session& session)
    {
        request_stop(session);

        if (session.capture_thread.joinable())
        {
            session.capture_thread.join();
        }

        {
            std::scoped_lock lock(session.gate);
            session.capture_finished = true;
        }

        session.ready_condition.notify_all();

        if (session.encode_thread.joinable())
        {
            session.encode_thread.join();
        }
    }

    std::string narrow_utf8(const std::wstring& value)
    {
        const auto utf8 = std::filesystem::path(value).u8string();
        return std::string(utf8.begin(), utf8.end());
    }

    std::string escape_json(std::string value)
    {
        std::string escaped;
        escaped.reserve(value.size());

        for (const auto character : value)
        {
            switch (character)
            {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += character;
                break;
            }
        }

        return escaped;
    }

    const char* source_kind_name(int32_t source_kind)
    {
        switch (source_kind)
        {
        case sr_capture_source_window:
            return "Window";
        case sr_capture_source_region:
            return "Region";
        default:
            return "Display";
        }
    }

    const char* capture_fallback_reason_name(capture_fallback_reason reason) noexcept
    {
        switch (reason)
        {
        case capture_fallback_reason::startup_timeout:
            return "startup-timeout";
        case capture_fallback_reason::startup_hresult:
            return "startup-hresult";
        case capture_fallback_reason::stream_change:
            return "stream-change";
        case capture_fallback_reason::runtime_hresult:
            return "runtime-hresult";
        case capture_fallback_reason::unsupported:
            return "unsupported";
        default:
            return "none";
        }
    }

    bool can_fallback_to_dxgi(int32_t source_kind) noexcept
    {
        return source_kind == sr_capture_source_display || source_kind == sr_capture_source_region;
    }

    std::string format_hresult(HRESULT value)
    {
        std::ostringstream stream;
        stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << static_cast<uint32_t>(value);
        return stream.str();
    }

    void record_capture_fallback(
        recording_session& session,
        std::string from,
        std::string to,
        capture_fallback_reason reason,
        HRESULT hresult)
    {
        session.capture_fallback_from = std::move(from);
        session.capture_fallback_to = std::move(to);
        session.capture_fallback_reason_code = reason;
        session.capture_fallback_hresult = hresult;
    }

    void record_capture_backend_failure(
        recording_session& session,
        std::string backend,
        capture_fallback_reason reason,
        HRESULT hresult)
    {
        session.capture_fallback_from = std::move(backend);
        session.capture_fallback_to.clear();
        session.capture_fallback_reason_code = reason;
        session.capture_fallback_hresult = hresult;
    }

    double compute_average_frames_per_second(const recording_metrics& metrics)
    {
        if (metrics.encoded_frames == 0 || metrics.first_captured_qpc == 0 || metrics.last_captured_qpc == 0)
        {
            return 0.0;
        }

        const auto represented_duration_qpc =
            (metrics.last_captured_qpc - metrics.first_captured_qpc) + std::max<int64_t>(metrics.last_sample_duration_qpc, 1);
        if (represented_duration_qpc <= 0)
        {
            return 0.0;
        }

        return static_cast<double>(metrics.encoded_frames) * static_cast<double>(qpc_frequency()) /
            static_cast<double>(represented_duration_qpc);
    }

    double compute_average_latency_millis(int64_t total_latency_qpc, uint64_t sample_count)
    {
        if (sample_count == 0)
        {
            return 0.0;
        }

        return qpc_to_millis(total_latency_qpc) / static_cast<double>(sample_count);
    }

    void write_manifest(const recording_session& session, bool export_succeeded)
    {
        std::ostringstream json;
        const auto final_video_name_utf8 = session.final_output_path.filename().u8string();
        const auto final_video_name = std::string(final_video_name_utf8.begin(), final_video_name_utf8.end());
        const auto dropped_frame_count = session.metrics.backpressure_drop_count + session.metrics.capture_failure_count;

        json << std::fixed << std::setprecision(2);
        json << "{\n";
        json << "  \"schemaVersion\": 4,\n";
        json << "  \"artifactType\": \"streaming-mp4-session\",\n";
        json << "  \"requestedCaptureBackend\": \"" << escape_json(session.requested_capture_backend) << "\",\n";
        json << "  \"captureBackend\": \"" << escape_json(session.capture_backend) << "\",\n";
        json << "  \"captureFallbackFrom\": ";
        if (session.capture_fallback_from.empty())
        {
            json << "null,\n";
        }
        else
        {
            json << "\"" << escape_json(session.capture_fallback_from) << "\",\n";
        }
        json << "  \"captureFallbackTo\": ";
        if (session.capture_fallback_to.empty())
        {
            json << "null,\n";
        }
        else
        {
            json << "\"" << escape_json(session.capture_fallback_to) << "\",\n";
        }
        json << "  \"captureFallbackReason\": \"" << capture_fallback_reason_name(session.capture_fallback_reason_code) << "\",\n";
        json << "  \"captureFallbackHresult\": ";
        if (session.capture_fallback_reason_code == capture_fallback_reason::none)
        {
            json << "null,\n";
        }
        else
        {
            json << "\"" << format_hresult(session.capture_fallback_hresult) << "\",\n";
        }
        json << "  \"encodeBackend\": \"" << escape_json(session.encode_backend) << "\",\n";
        json << "  \"hardwareEncode\": " << (session.hardware_encode ? "true" : "false") << ",\n";
        json << "  \"wgcStartupAttempted\": " << (session.wgc_startup_attempted ? "true" : "false") << ",\n";
        json << "  \"wgcFirstFrameLatencyMs\": " << qpc_to_millis(session.wgc_first_frame_latency_qpc) << ",\n";
        if (!session.adapter_name.empty())
        {
            json << "  \"adapterName\": \"" << escape_json(narrow_utf8(session.adapter_name)) << "\",\n";
        }
        json << "  \"fileOutputMode\": \"live-mp4\",\n";
        json << "  \"sourceKind\": \"" << source_kind_name(session.source.kind) << "\",\n";
        json << "  \"requestedFrameRate\": " << session.options.frame_rate << ",\n";
        json << "  \"targetFrameRate\": " << session.target_frame_rate << ",\n";
        json << "  \"requestedResolution\": " << session.options.resolution << ",\n";
        json << "  \"outputWidth\": " << session.metrics.output_width << ",\n";
        json << "  \"outputHeight\": " << session.metrics.output_height << ",\n";
        json << "  \"includeSystemAudio\": " << (session.options.include_system_audio != 0 ? "true" : "false") << ",\n";
        json << "  \"includeMicrophone\": " << (session.options.include_microphone != 0 ? "true" : "false") << ",\n";
        json << "  \"startedAtUnixMillis\": " << session.started_at_unix_millis << ",\n";
        json << "  \"completedAtUnixMillis\": " << unix_time_millis() << ",\n";
        json << "  \"finalVideoFile\": \"" << escape_json(final_video_name) << "\",\n";
        json << "  \"finalVideoContainer\": \"mp4\",\n";
        json << "  \"finalVideoCodec\": \"h264\",\n";
        json << "  \"finalVideoExported\": " << (export_succeeded ? "true" : "false") << ",\n";
        json << "  \"captureAttemptCount\": " << session.metrics.capture_attempts << ",\n";
        json << "  \"capturedFrameCount\": " << session.metrics.captured_frames << ",\n";
        json << "  \"encodedFrameCount\": " << session.metrics.encoded_frames << ",\n";
        json << "  \"droppedFrameCount\": " << dropped_frame_count << ",\n";
        json << "  \"backpressureDropCount\": " << session.metrics.backpressure_drop_count << ",\n";
        json << "  \"captureFailureCount\": " << session.metrics.capture_failure_count << ",\n";
        json << "  \"pacingOverrunCount\": " << session.metrics.pacing_overrun_count << ",\n";
        json << "  \"peakQueueDepth\": " << session.metrics.peak_queue_depth << ",\n";
        json << "  \"averageEncodedFramesPerSecond\": " << compute_average_frames_per_second(session.metrics) << ",\n";
        json << "  \"averageCaptureLatencyMs\": " << compute_average_latency_millis(session.metrics.total_capture_latency_qpc, session.metrics.captured_frames) << ",\n";
        json << "  \"averageQueueLatencyMs\": " << compute_average_latency_millis(session.metrics.total_queue_latency_qpc, session.metrics.encoded_frames) << ",\n";
        json << "  \"averageConvertLatencyMs\": " << compute_average_latency_millis(session.metrics.total_convert_latency_qpc, session.metrics.captured_frames) << ",\n";
        json << "  \"averageEncodeLatencyMs\": " << compute_average_latency_millis(session.metrics.total_encode_latency_qpc, session.metrics.encoded_frames);

        if (!session.failure_reason.empty())
        {
            json << ",\n  \"failureReason\": \"" << escape_json(narrow_utf8(session.failure_reason)) << "\"";
        }

        json << "\n}\n";

        std::ofstream manifest_stream(session.session_directory / "manifest.json", std::ios::binary | std::ios::trunc);
        manifest_stream << json.str();
    }

    bool try_initialize_gpu_recording(recording_session& session)
    {
        const auto geometry = resolve_capture_geometry(session.source);
        if (!geometry.has_value() || geometry->spans_multiple_monitors)
        {
            return false;
        }

        if (session.source.kind == sr_capture_source_window && geometry->window == nullptr)
        {
            return false;
        }

        session.mode = pipeline_mode::gpu_first;
        session.capture_item_rect = geometry->capture_item_bounds;
        session.capture_crop_rect = geometry->source_rect;
        session.capture_monitor = geometry->monitor;
        session.capture_window = geometry->window;
        session.requested_capture_backend = "windows-graphics-capture";
        session.capture_backend = "pending-gpu-backend";
        session.encode_backend = "pending-gpu-encoder";
        session.capture_fallback_reason_code = capture_fallback_reason::none;
        session.capture_fallback_hresult = S_OK;
        session.capture_fallback_from.clear();
        session.capture_fallback_to.clear();
        session.gpu_d3d = std::make_unique<d3d_context>();
        if (FAILED(create_d3d_context(session.capture_monitor, *session.gpu_d3d)))
        {
            session.gpu_d3d.reset();
            return false;
        }

        session.adapter_name = session.gpu_d3d->adapter_name;
        return true;
    }

    bool try_initialize_legacy_recording(recording_session& session)
    {
        session.mode = pipeline_mode::legacy_gdi;
        session.requested_capture_backend = "gdi-compatibility";
        session.capture_backend = "gdi-compatibility";
        session.encode_backend = "media-foundation-h264-rgb32";
        session.hardware_encode = false;
        session.capture_fallback_reason_code = capture_fallback_reason::none;
        session.capture_fallback_hresult = S_OK;
        session.capture_fallback_from.clear();
        session.capture_fallback_to.clear();

        auto geometry = resolve_capture_geometry(session.source);
        if (!geometry.has_value())
        {
            return false;
        }

        session.legacy_capture_rect = geometry->requested_bounds;
        return initialize_legacy_capture_resources(session);
    }
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
    session->target_frame_rate = resolve_target_frame_rate(*options);
    session->output_height = resolve_target_height(*options, source_height);
    session->output_width = resolve_target_width(source_width, source_height, session->output_height);
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
