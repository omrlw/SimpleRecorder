// Internal implementation partition for SimpleRecorder.Engine.Native.
// GDI compatibility capture resources and RGB32 Media Foundation writer.
// Included by engine.cpp inside the native engine anonymous namespace.

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
        session.gpu_start_condition.notify_all();
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
        session.gpu_start_condition.notify_all();
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

    HRESULT create_legacy_sink_writer(recording_session& session, ComPtr<IMFSinkWriter>& sink_writer, DWORD& stream_index, bool configure_encoder)
    {
        sink_writer.Reset();
        stream_index = 0;

        std::error_code create_error;
        std::filesystem::create_directories(session.final_output_path.parent_path(), create_error);
        if (create_error)
        {
            return E_FAIL;
        }

        std::error_code remove_error;
        std::filesystem::remove(session.final_output_path, remove_error);

        ComPtr<IMFAttributes> attributes;
        auto result = MFCreateAttributes(&attributes, configure_encoder ? 4 : 3);
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

        ComPtr<IMFAttributes> input_attributes;
        if (configure_encoder)
        {
            ComPtr<IPropertyStore> encoder_store;
            result = create_encoder_property_store(session.quality_config, encoder_store);
            if (FAILED(result))
            {
                return result;
            }

            result = attributes->SetUnknown(MF_SINK_WRITER_ENCODER_CONFIG, encoder_store.Get());
            if (FAILED(result))
            {
                return result;
            }

            result = create_encoder_input_attributes(session.quality_config, input_attributes);
            if (FAILED(result))
            {
                return result;
            }
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

        result = output_type->SetUINT32(MF_MT_AVG_BITRATE, session.quality_config.target_bitrate_bps);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetUINT32(MF_MT_MPEG2_PROFILE, resolve_h264_profile(session.options));
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

        result = sink_writer->SetInputMediaType(stream_index, input_type.Get(), input_attributes.Get());
        if (FAILED(result))
        {
            return result;
        }

        return sink_writer->BeginWriting();
    }
