// Internal implementation partition for SimpleRecorder.Engine.Native.
// D3D11 crop, scale, and BGRA-to-NV12 frame processing.
// Included by engine.cpp inside the native engine anonymous namespace.

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

        D3D11_VIDEO_PROCESSOR_COLOR_SPACE input_color_space{};
        input_color_space.RGB_Range = 0;
        input_color_space.YCbCr_Matrix = 1;
        input_color_space.YCbCr_xvYCC = 0;
        input_color_space.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;

        D3D11_VIDEO_PROCESSOR_COLOR_SPACE output_color_space{};
        output_color_space.RGB_Range = 0;
        output_color_space.YCbCr_Matrix = 1;
        output_color_space.YCbCr_xvYCC = 0;
        output_color_space.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;

        _d3d.video_context->VideoProcessorSetStreamColorSpace(_processor.Get(), 0, &input_color_space);
        _d3d.video_context->VideoProcessorSetOutputColorSpace(_processor.Get(), &output_color_space);

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
