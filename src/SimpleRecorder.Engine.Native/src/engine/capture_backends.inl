// Internal implementation partition for SimpleRecorder.Engine.Native.
// Windows Graphics Capture and DXGI Desktop Duplication capture backends.
// Included by engine.cpp inside the native engine anonymous namespace.

    HRESULT copy_texture_to_destination(
        d3d_context& d3d,
        ID3D11Texture2D* destination,
        ID3D11Texture2D* source,
        std::string& mismatch_reason)
    {
        mismatch_reason.clear();
        if (destination == nullptr || source == nullptr)
        {
            return E_INVALIDARG;
        }

        D3D11_TEXTURE2D_DESC destination_desc{};
        D3D11_TEXTURE2D_DESC source_desc{};
        destination->GetDesc(&destination_desc);
        source->GetDesc(&source_desc);

        if (destination_desc.Format != source_desc.Format)
        {
            mismatch_reason = "texture-format-mismatch";
            return MF_E_INVALIDMEDIATYPE;
        }

        if (destination_desc.Width == source_desc.Width && destination_desc.Height == source_desc.Height)
        {
            d3d.context->CopyResource(destination, source);
            return S_OK;
        }

        if (source_desc.Width >= destination_desc.Width && source_desc.Height >= destination_desc.Height)
        {
            const D3D11_BOX source_box
            {
                0,
                0,
                0,
                destination_desc.Width,
                destination_desc.Height,
                1
            };
            d3d.context->CopySubresourceRegion(destination, 0, 0, 0, 0, source, 0, &source_box);
            mismatch_reason = "source-texture-larger-than-capture-slot-copied-region";
            return S_OK;
        }

        mismatch_reason = "source-texture-smaller-than-capture-slot";
        return MF_E_INVALIDMEDIATYPE;
    }

    ComPtr<ID3D11Texture2D> wgc_capture_backend::acquire_owned_frame_texture(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            throw winrt::hresult_error(E_INVALIDARG, L"WGC frame content size is empty.");
        }

        {
            std::scoped_lock lock(_gate);
            while (!_available_frame_textures.empty())
            {
                auto texture = std::move(_available_frame_textures.front());
                _available_frame_textures.pop_front();
                if (texture == nullptr)
                {
                    continue;
                }

                D3D11_TEXTURE2D_DESC desc{};
                texture->GetDesc(&desc);
                if (desc.Width == width &&
                    desc.Height == height &&
                    desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM)
                {
                    return texture;
                }
            }
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;

        ComPtr<ID3D11Texture2D> texture;
        winrt::check_hresult(_d3d.device->CreateTexture2D(&desc, nullptr, &texture));
        return texture;
    }

    void wgc_capture_backend::recycle_owned_frame_texture(ComPtr<ID3D11Texture2D> texture) noexcept
    {
        if (texture == nullptr)
        {
            return;
        }

        std::scoped_lock lock(_gate);
        _available_frame_textures.push_back(std::move(texture));
    }

    void wgc_capture_backend::recycle_ready_frames_locked() noexcept
    {
        while (!_ready_frames.empty())
        {
            if (_ready_frames.front().texture != nullptr)
            {
                _available_frame_textures.push_back(std::move(_ready_frames.front().texture));
            }

            _ready_frames.pop_front();
        }
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
            normalize_geometry_for_frame_size(capture_size.Width, capture_size.Height);
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
        _ready_frames.pop_back();
        recycle_ready_frames_locked();
        lock.unlock();

        const auto copy_result = copy_texture_to_destination(_d3d, destination, next.texture.Get(), _copy_mismatch_reason);
        recycle_owned_frame_texture(std::move(next.texture));
        if (FAILED(copy_result))
        {
            return copy_result;
        }

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

        recycle_ready_frames_locked();
        _available_frame_textures.clear();
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

    bool wgc_capture_backend::consume_resize(resolved_capture_geometry& geometry)
    {
        std::scoped_lock lock(_gate);
        if (!_resize_pending)
        {
            return false;
        }

        geometry = _pending_resize_geometry;
        _resize_pending = false;
        return true;
    }

    bool wgc_capture_backend::current_geometry(resolved_capture_geometry& geometry) const
    {
        std::scoped_lock lock(_gate);
        geometry = _geometry;
        return rect_has_area(_geometry.capture_item_bounds);
    }

    std::string wgc_capture_backend::consume_copy_mismatch_reason()
    {
        std::scoped_lock lock(_gate);
        auto reason = std::move(_copy_mismatch_reason);
        _copy_mismatch_reason.clear();
        return reason;
    }

    void wgc_capture_backend::fail_locked(HRESULT failure, capture_fallback_reason reason) noexcept
    {
        _failure = failure;
        _failure_reason = reason;
        _frame_arrived_condition.notify_all();
    }

    void wgc_capture_backend::normalize_geometry_for_frame_size(int32_t width, int32_t height) noexcept
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }

        const auto previous_bounds = _geometry.capture_item_bounds;
        const auto previous_source = _geometry.source_rect;
        const auto previous_width = previous_bounds.right - previous_bounds.left;
        const auto previous_height = previous_bounds.bottom - previous_bounds.top;

        _geometry.capture_item_bounds = { 0, 0, width, height };
        if (_geometry.window != nullptr)
        {
            _geometry.source_rect = { 0, 0, width, height };
            return;
        }

        const auto source_is_unspecified_display =
            _source.kind == sr_capture_source_display &&
            _source.region.width <= 0 &&
            _source.region.height <= 0;
        const auto source_covers_previous_item =
            previous_width > 0 &&
            previous_height > 0 &&
            previous_source.left <= 0 &&
            previous_source.top <= 0 &&
            previous_source.right >= previous_width &&
            previous_source.bottom >= previous_height;

        if (source_is_unspecified_display || source_covers_previous_item)
        {
            _geometry.source_rect = { 0, 0, width, height };
            return;
        }

        if (previous_width > 0 && previous_height > 0)
        {
            _geometry.source_rect =
            {
                scale_coordinate(previous_source.left, previous_width, width),
                scale_coordinate(previous_source.top, previous_height, height),
                scale_coordinate(previous_source.right, previous_width, width),
                scale_coordinate(previous_source.bottom, previous_height, height)
            };
        }

        RECT frame_bounds{ 0, 0, width, height };
        IntersectRect(&_geometry.source_rect, &_geometry.source_rect, &frame_bounds);
        if (_geometry.source_rect.right <= _geometry.source_rect.left ||
            _geometry.source_rect.bottom <= _geometry.source_rect.top)
        {
            _geometry.source_rect = frame_bounds;
        }
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
                recycle_ready_frames_locked();
                _available_frame_textures.clear();

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
                    normalize_geometry_for_frame_size(content_size.Width, content_size.Height);
                    _pending_resize_geometry = _geometry;
                    _resize_pending = true;
                    _frame_arrived_condition.notify_all();
                }
                catch (const winrt::hresult_error& recreate_error)
                {
                    fail_locked(recreate_error.code(), capture_fallback_reason::stream_change);
                    return;
                }

                return;
            }

            auto access = frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> texture;
            winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(texture.GetAddressOf())));

            D3D11_TEXTURE2D_DESC source_desc{};
            texture->GetDesc(&source_desc);
            if (source_desc.Width < static_cast<UINT>(content_size.Width) ||
                source_desc.Height < static_cast<UINT>(content_size.Height) ||
                source_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
            {
                std::scoped_lock lock(_gate);
                _copy_mismatch_reason = source_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM
                    ? "wgc-frame-format-mismatch"
                    : "wgc-frame-smaller-than-content-size";
                fail_locked(MF_E_INVALIDMEDIATYPE, capture_fallback_reason::runtime_hresult);
                return;
            }

            auto owned_texture = acquire_owned_frame_texture(
                static_cast<uint32_t>(content_size.Width),
                static_cast<uint32_t>(content_size.Height));
            const D3D11_BOX content_box
            {
                0,
                0,
                0,
                static_cast<UINT>(content_size.Width),
                static_cast<UINT>(content_size.Height),
                1
            };
            _d3d.context->CopySubresourceRegion(owned_texture.Get(), 0, 0, 0, 0, texture.Get(), 0, &content_box);
            _d3d.context->Flush();

            std::scoped_lock lock(_gate);
            if (_ready_frames.size() >= wgc_frame_queue_limit)
            {
                if (_ready_frames.front().texture != nullptr)
                {
                    _available_frame_textures.push_back(std::move(_ready_frames.front().texture));
                }
                _ready_frames.pop_front();
                ++_queue_drop_count;
            }

            const auto captured_at_qpc = qpc_now();
            _ready_frames.push_back({ owned_texture, captured_at_qpc });
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
            result = copy_texture_to_destination(_d3d, destination, source_texture.Get(), _copy_mismatch_reason);
            if (FAILED(result))
            {
                (void)_duplication->ReleaseFrame();
                return result;
            }

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

    bool dxgi_duplication_capture_backend::current_geometry(resolved_capture_geometry& geometry) const
    {
        geometry = _geometry;
        return rect_has_area(_geometry.capture_item_bounds);
    }

    std::string dxgi_duplication_capture_backend::consume_copy_mismatch_reason()
    {
        auto reason = std::move(_copy_mismatch_reason);
        _copy_mismatch_reason.clear();
        return reason;
    }
