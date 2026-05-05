// Internal implementation partition for SimpleRecorder.Engine.Native.
// Windows Graphics Capture and DXGI Desktop Duplication capture backends.
// Included by engine.cpp inside the native engine anonymous namespace.

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
