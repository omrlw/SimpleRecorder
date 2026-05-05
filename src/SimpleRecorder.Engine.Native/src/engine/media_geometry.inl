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

        ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(d3d.device.As(&multithread)))
        {
            multithread->SetMultithreadProtected(TRUE);
            d3d.multithread_protected = multithread->GetMultithreadProtected() != FALSE;
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
        result = d3d.adapter->GetDesc1(&adapter_desc);
        if (FAILED(result))
        {
            return result;
        }

        d3d.adapter_name = adapter_desc.Description;
        d3d.adapter_luid = adapter_desc.AdapterLuid;
        return S_OK;
    }
