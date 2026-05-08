// Internal implementation partition for SimpleRecorder.Engine.Native.
// Media Foundation startup, monitor enumeration, source geometry, and D3D context setup.
// Included by engine.cpp inside the native engine anonymous namespace.

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

    bool is_hardware_adapter(IDXGIAdapter1* adapter) noexcept
    {
        if (adapter == nullptr)
        {
            return false;
        }

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)))
        {
            return false;
        }

        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            return false;
        }

        const std::wstring name = desc.Description;
        return name.find(L"Microsoft Basic Render") == std::wstring::npos &&
            name.find(L"Microsoft Basic Display") == std::wstring::npos &&
            name.find(L"WARP") == std::wstring::npos;
    }

    bool adapter_outputs_monitor(IDXGIAdapter1* adapter, HMONITOR monitor) noexcept
    {
        if (adapter == nullptr || monitor == nullptr)
        {
            return false;
        }

        for (UINT output_index = 0;; ++output_index)
        {
            ComPtr<IDXGIOutput> output;
            auto result = adapter->EnumOutputs(output_index, &output);
            if (result == DXGI_ERROR_NOT_FOUND)
            {
                return false;
            }

            if (FAILED(result))
            {
                return false;
            }

            DXGI_OUTPUT_DESC output_desc{};
            if (SUCCEEDED(output->GetDesc(&output_desc)) && output_desc.Monitor == monitor)
            {
                return true;
            }
        }
    }

    bool same_adapter_luid(IDXGIAdapter1* left, IDXGIAdapter1* right) noexcept
    {
        if (left == nullptr || right == nullptr)
        {
            return false;
        }

        DXGI_ADAPTER_DESC1 left_desc{};
        DXGI_ADAPTER_DESC1 right_desc{};
        if (FAILED(left->GetDesc1(&left_desc)) || FAILED(right->GetDesc1(&right_desc)))
        {
            return false;
        }

        return left_desc.AdapterLuid.HighPart == right_desc.AdapterLuid.HighPart &&
            left_desc.AdapterLuid.LowPart == right_desc.AdapterLuid.LowPart;
    }

    void add_unique_adapter(std::vector<ComPtr<IDXGIAdapter1>>& adapters, ComPtr<IDXGIAdapter1> candidate)
    {
        if (candidate == nullptr)
        {
            return;
        }

        for (const auto& adapter : adapters)
        {
            if (same_adapter_luid(adapter.Get(), candidate.Get()))
            {
                return;
            }
        }

        adapters.push_back(candidate);
    }

    std::vector<ComPtr<IDXGIAdapter1>> enumerate_hardware_adapters(IDXGIFactory1* factory, HMONITOR preferred_monitor)
    {
        std::vector<ComPtr<IDXGIAdapter1>> adapters;
        if (factory == nullptr)
        {
            return adapters;
        }

        for (UINT adapter_index = 0;; ++adapter_index)
        {
            ComPtr<IDXGIAdapter1> candidate;
            auto result = factory->EnumAdapters1(adapter_index, &candidate);
            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            if (FAILED(result) || !is_hardware_adapter(candidate.Get()))
            {
                continue;
            }

            if (adapter_outputs_monitor(candidate.Get(), preferred_monitor))
            {
                add_unique_adapter(adapters, candidate);
            }
        }

        for (UINT adapter_index = 0;; ++adapter_index)
        {
            ComPtr<IDXGIAdapter1> candidate;
            auto result = factory->EnumAdapters1(adapter_index, &candidate);
            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            if (FAILED(result) || !is_hardware_adapter(candidate.Get()))
            {
                continue;
            }

            add_unique_adapter(adapters, candidate);
        }

        return adapters;
    }

    bool detect_hardware_graphics_adapter() noexcept
    {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        {
            return false;
        }

        return !enumerate_hardware_adapters(factory.Get(), nullptr).empty();
    }

    HRESULT create_d3d_context(HMONITOR preferred_monitor, d3d_context& d3d)
    {
        HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(&d3d.factory));
        if (FAILED(result))
        {
            return result;
        }

        auto candidates = enumerate_hardware_adapters(d3d.factory.Get(), preferred_monitor);
        d3d.hardware_adapter_detected = !candidates.empty();
        if (candidates.empty())
        {
            return DXGI_ERROR_NOT_FOUND;
        }

        static const D3D_FEATURE_LEVEL feature_levels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0
        };

        auto create_for_adapter = [&](IDXGIAdapter1* adapter) -> HRESULT
        {
            d3d.adapter = adapter;
            d3d.device.Reset();
            d3d.context.Reset();
            d3d.video_device.Reset();
            d3d.video_context.Reset();

            UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#if defined(_DEBUG)
            creation_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

            D3D_FEATURE_LEVEL actual_feature_level{};
            auto create_result = D3D11CreateDevice(
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
            if (FAILED(create_result) && (creation_flags & D3D11_CREATE_DEVICE_DEBUG) != 0)
            {
                creation_flags &= ~D3D11_CREATE_DEVICE_DEBUG;
                create_result = D3D11CreateDevice(
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

            if (FAILED(create_result))
            {
                return create_result;
            }

            ComPtr<ID3D10Multithread> multithread;
            if (SUCCEEDED(d3d.device.As(&multithread)))
            {
                multithread->SetMultithreadProtected(TRUE);
                d3d.multithread_protected = multithread->GetMultithreadProtected() != FALSE;
            }

            create_result = d3d.device.As(&d3d.video_device);
            if (FAILED(create_result))
            {
                return create_result;
            }

            create_result = d3d.context.As(&d3d.video_context);
            if (FAILED(create_result))
            {
                return create_result;
            }

            if (!has_format_support(
                    d3d.device.Get(),
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT) ||
                !has_format_support(
                    d3d.device.Get(),
                    DXGI_FORMAT_NV12,
                    D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT))
            {
                return MF_E_INVALIDMEDIATYPE;
            }

            DXGI_ADAPTER_DESC1 adapter_desc{};
            create_result = d3d.adapter->GetDesc1(&adapter_desc);
            if (FAILED(create_result))
            {
                return create_result;
            }

            d3d.adapter_name = adapter_desc.Description;
            d3d.adapter_luid = adapter_desc.AdapterLuid;
            return S_OK;
        };

        HRESULT first_failure = E_FAIL;
        for (const auto& candidate : candidates)
        {
            result = create_for_adapter(candidate.Get());
            if (SUCCEEDED(result))
            {
                return S_OK;
            }

            if (first_failure == E_FAIL)
            {
                first_failure = result;
            }
        }

        d3d.adapter.Reset();
        d3d.device.Reset();
        d3d.context.Reset();
        d3d.video_device.Reset();
        d3d.video_context.Reset();
        return first_failure;
    }
