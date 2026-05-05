// Internal implementation partition for SimpleRecorder.Engine.Native.
// Media Foundation H.264 sink writer setup and GPU sample submission.
// Included by engine.cpp inside the native engine anonymous namespace.

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

    const char* encoder_backend::hardware_encode_status() const noexcept
    {
        return _session.hardware_encode_status.c_str();
    }

    void encoder_backend::prepare_slot_samples()
    {
        auto check = [](HRESULT hr, const wchar_t* stage)
        {
            if (FAILED(hr))
            {
                throw winrt::hresult_error(hr, stage);
            }
        };

        for (auto& slot : _session.gpu_slots)
        {
            if (slot.nv12_texture == nullptr)
            {
                throw winrt::hresult_error(E_POINTER, L"GPU slot has no NV12 texture.");
            }

            check(MFCreateDXGISurfaceBuffer(
                __uuidof(ID3D11Texture2D),
                slot.nv12_texture.Get(),
                0,
                FALSE,
                &slot.sample_buffer), L"MFCreateDXGISurfaceBuffer");

            check(slot.sample_buffer->GetMaxLength(&slot.sample_buffer_length), L"IMFMediaBuffer::GetMaxLength");
            if (slot.sample_buffer_length == 0)
            {
                slot.sample_buffer_length = static_cast<DWORD>(_session.output_width * _session.output_height * 3 / 2);
            }

            check(MFCreateSample(&slot.sample), L"MFCreateSample");
            check(slot.sample->AddBuffer(slot.sample_buffer.Get()), L"IMFSample::AddBuffer");
        }
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

        if (slot.sample_buffer == nullptr || slot.sample == nullptr || slot.sample_buffer_length == 0)
        {
            throw winrt::hresult_error(E_POINTER, L"GPU slot sample resources were not prepared.");
        }

        check(slot.sample_buffer->SetCurrentLength(slot.sample_buffer_length), L"IMFMediaBuffer::SetCurrentLength");
        check(slot.sample->SetSampleTime(sample_time), L"IMFSample::SetSampleTime");
        check(slot.sample->SetSampleDuration(std::max<LONGLONG>(sample_duration, 1)), L"IMFSample::SetSampleDuration");
        check(_sink_writer->WriteSample(_stream_index, slot.sample.Get()), L"IMFSinkWriter::WriteSample");
    }

    HRESULT encoder_backend::finalize() noexcept
    {
        if (_sink_writer == nullptr)
        {
            return E_FAIL;
        }

        return _sink_writer->Finalize();
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

        media_foundation_scope media_foundation;
        if (!media_foundation.is_ready())
        {
            throw winrt::hresult_error(E_FAIL, L"Media Foundation could not be initialized for encoder capability probing.");
        }

        auto probe = probe_h264_nv12_hardware_encoder();
        if (probe.has_h264_nv12_hardware)
        {
            _session.encoder_name = probe.encoder_name;
            _session.encoder_vendor = probe.encoder_vendor;
        }

        if (_session.options.encoder_preference == sr_encoder_preference_hardware_only && !probe.has_h264_nv12_hardware)
        {
            _session.encoder_selection_reason = "no-h264-nv12-hardware-mft";
            _session.hardware_encode_status = "hardware-required-unavailable";
            throw winrt::hresult_error(MF_E_TOPO_CODEC_NOT_FOUND, L"No H.264/NV12 hardware Media Foundation encoder is available.");
        }

        HRESULT result = E_FAIL;
        std::wstring strict_stage;
        std::wstring unconfigured_strict_stage;

        if (probe.has_h264_nv12_hardware)
        {
            result = create_sink_writer(true, true, true, true);
            if (SUCCEEDED(result))
            {
                _hardware_encode = true;
                _session.encoder_config_status = "configured";
                _session.hardware_encode_status = "verified-hardware";
                _session.encoder_selection_reason = "h264-nv12-hardware-mft-d3d11-negotiated";
                _session.encoder_fallback_reason = "none";
                return;
            }

            strict_stage = _last_stage;
            result = create_sink_writer(true, true, false, true);
            if (SUCCEEDED(result))
            {
                _hardware_encode = true;
                _session.encoder_config_status = "fallback-without-encoder-config";
                _session.hardware_encode_status = "verified-hardware";
                _session.encoder_selection_reason = "h264-nv12-hardware-mft-d3d11-negotiated-without-config";
                _session.encoder_fallback_reason = "encoder-config-rejected";
                return;
            }

            unconfigured_strict_stage = _last_stage;
        }

        if (_session.options.encoder_preference == sr_encoder_preference_hardware_only)
        {
            _session.encoder_selection_reason = "hardware-only-sink-writer-negotiation-failed";
            _session.encoder_fallback_reason = narrow_utf8(strict_stage);
            _session.hardware_encode_status = "hardware-required-unavailable";
            return winrt::throw_hresult(result);
        }

        result = create_sink_writer(true, true, true, true);
        if (SUCCEEDED(result))
        {
            _hardware_encode = false;
            _session.encoder_config_status = "configured";
            _session.encoder_selection_reason = "hardware-requested-unverified";
            _session.encoder_fallback_reason = "hardware-only-negotiation-failed";
            _session.hardware_encode_status = "unverified-hardware-requested";
            return;
        }

        const auto unverified_configured_stage = _last_stage;
        result = create_sink_writer(true, true, false, true);
        if (SUCCEEDED(result))
        {
            _hardware_encode = false;
            _session.encoder_config_status = "fallback-without-encoder-config";
            _session.encoder_selection_reason = "hardware-requested-unverified-without-config";
            _session.encoder_fallback_reason = "encoder-config-rejected";
            _session.hardware_encode_status = "unverified-hardware-requested";
            return;
        }

        const auto unverified_unconfigured_stage = _last_stage;
        result = create_sink_writer(true, false, false, false);
        if (SUCCEEDED(result))
        {
            _hardware_encode = false;
            _session.encoder_config_status = "fallback-compatible";
            _session.encoder_selection_reason = "software-compatible-d3d11";
            _session.encoder_fallback_reason = "hardware-transform-negotiation-failed";
            _session.hardware_encode_status = "verified-software";
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
        if (!unverified_configured_stage.empty())
        {
            message << L"; unverified-configured-stage=" << unverified_configured_stage;
        }
        if (!unverified_unconfigured_stage.empty())
        {
            message << L"; unverified-unconfigured-stage=" << unverified_unconfigured_stage;
        }
        if (!unconfigured_strict_stage.empty())
        {
            message << L"; unconfigured-strict-stage=" << unconfigured_strict_stage;
        }

        throw winrt::hresult_error(result, message.str());
    }

    HRESULT encoder_backend::create_sink_writer(
        bool enable_d3d_manager,
        bool disable_converters,
        bool configure_encoder,
        bool enable_hardware_transforms)
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
        auto result = MFCreateAttributes(&attributes, configure_encoder ? 9 : 8);
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

        if (enable_hardware_transforms)
        {
            result = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
            if (FAILED(result))
            {
                return fail(result, L"Set(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS)");
            }
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

        ComPtr<IMFAttributes> input_attributes;
        if (configure_encoder)
        {
            ComPtr<IPropertyStore> encoder_store;
            result = create_encoder_property_store(_session.quality_config, encoder_store);
            if (FAILED(result))
            {
                return fail(result, L"create_encoder_property_store");
            }

            result = attributes->SetUnknown(MF_SINK_WRITER_ENCODER_CONFIG, encoder_store.Get());
            if (FAILED(result))
            {
                return fail(result, L"Set(MF_SINK_WRITER_ENCODER_CONFIG)");
            }

            result = create_encoder_input_attributes(_session.quality_config, input_attributes);
            if (FAILED(result))
            {
                return fail(result, L"create_encoder_input_attributes");
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

        result = output_type->SetUINT32(MF_MT_AVG_BITRATE, _session.quality_config.target_bitrate_bps);
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

        result = apply_bt709_limited_video_color_metadata(output_type.Get());
        if (FAILED(result))
        {
            return fail(result, L"apply_bt709_limited_video_color_metadata(output)");
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

        result = apply_bt709_limited_video_color_metadata(input_type.Get());
        if (FAILED(result))
        {
            return fail(result, L"apply_bt709_limited_video_color_metadata(input)");
        }

        result = _sink_writer->SetInputMediaType(_stream_index, input_type.Get(), input_attributes.Get());
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
