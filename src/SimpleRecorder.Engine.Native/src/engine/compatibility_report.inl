// Internal implementation partition for SimpleRecorder.Engine.Native.
// Hardware/API compatibility probe report writer.
// Included by engine.cpp inside the native engine anonymous namespace.

    struct compatibility_profile_result
    {
        const char* name = "";
        int32_t width = 0;
        int32_t height = 0;
        int32_t frame_rate = 0;
        HRESULT hresult = E_FAIL;
        std::wstring stage;
    };

    std::string adapter_vendor_name(UINT vendor_id, const std::wstring& adapter_name)
    {
        switch (vendor_id)
        {
        case 0x1002:
        case 0x1022:
            return "amd";
        case 0x10DE:
            return "nvidia";
        case 0x8086:
            return "intel";
        case 0x1414:
            return "microsoft";
        default:
            break;
        }

        return infer_encoder_vendor(narrow_utf8(adapter_name));
    }

    std::wstring widen_ascii(std::string_view value)
    {
        std::wstring widened;
        widened.reserve(value.size());
        for (const auto character : value)
        {
            widened.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
        }

        return widened;
    }

    void write_json_string_or_null(std::ostringstream& json, const char* name, const std::string& value, bool trailing_comma)
    {
        json << "  \"" << name << "\": ";
        if (value.empty())
        {
            json << "null";
        }
        else
        {
            json << "\"" << escape_json(value) << "\"";
        }

        if (trailing_comma)
        {
            json << ",";
        }

        json << "\n";
    }

    void write_json_hresult(std::ostringstream& json, const char* name, HRESULT value, bool trailing_comma)
    {
        json << "  \"" << name << "\": ";
        if (SUCCEEDED(value))
        {
            json << "null";
        }
        else
        {
            json << "\"" << format_hresult(value) << "\"";
        }

        if (trailing_comma)
        {
            json << ",";
        }

        json << "\n";
    }

    HRESULT create_probe_sink_writer(
        d3d_context& d3d,
        const std::filesystem::path& output_path,
        int32_t width,
        int32_t height,
        int32_t frame_rate,
        std::wstring& stage)
    {
        stage.clear();
        auto fail = [&stage](HRESULT result, const wchar_t* failed_stage) noexcept
        {
            stage = failed_stage;
            return result;
        };

        ComPtr<IMFAttributes> attributes;
        auto result = MFCreateAttributes(&attributes, 4);
        if (FAILED(result))
        {
            return fail(result, L"MFCreateAttributes");
        }

        result = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        if (FAILED(result))
        {
            return fail(result, L"Set(MF_SINK_WRITER_DISABLE_THROTTLING)");
        }

        result = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (FAILED(result))
        {
            return fail(result, L"Set(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS)");
        }

        if (d3d.dxgi_device_manager == nullptr)
        {
            result = MFCreateDXGIDeviceManager(&d3d.dxgi_reset_token, &d3d.dxgi_device_manager);
            if (FAILED(result))
            {
                return fail(result, L"MFCreateDXGIDeviceManager");
            }

            result = d3d.dxgi_device_manager->ResetDevice(d3d.device.Get(), d3d.dxgi_reset_token);
            if (FAILED(result))
            {
                return fail(result, L"IMFDXGIDeviceManager::ResetDevice");
            }
        }

        result = attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, d3d.dxgi_device_manager.Get());
        if (FAILED(result))
        {
            return fail(result, L"Set(MF_SINK_WRITER_D3D_MANAGER)");
        }

        ComPtr<IMFSinkWriter> sink_writer;
        result = MFCreateSinkWriterFromURL(output_path.c_str(), nullptr, attributes.Get(), &sink_writer);
        if (FAILED(result))
        {
            return fail(result, L"MFCreateSinkWriterFromURL");
        }

        const auto bitrate = static_cast<UINT32>(std::max<int32_t>(8'000'000, width * height * std::max(frame_rate, 1) / 8));

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

        result = output_type->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        if (FAILED(result))
        {
            return fail(result, L"output.SetUINT32(MF_MT_AVG_BITRATE)");
        }

        result = output_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(result))
        {
            return fail(result, L"output.SetUINT32(MF_MT_INTERLACE_MODE)");
        }

        result = output_type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
        if (FAILED(result))
        {
            return fail(result, L"output.SetUINT32(MF_MT_MPEG2_PROFILE)");
        }

        result = output_type->SetUINT32(MF_MT_MPEG2_LEVEL, eAVEncH264VLevel5_2);
        if (FAILED(result))
        {
            return fail(result, L"output.SetUINT32(MF_MT_MPEG2_LEVEL)");
        }

        result = MFSetAttributeSize(output_type.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(width), static_cast<UINT32>(height));
        if (FAILED(result))
        {
            return fail(result, L"MFSetAttributeSize(output, MF_MT_FRAME_SIZE)");
        }

        result = MFSetAttributeRatio(output_type.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(frame_rate), 1);
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

        DWORD stream_index = 0;
        result = sink_writer->AddStream(output_type.Get(), &stream_index);
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

        result = MFSetAttributeSize(input_type.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(width), static_cast<UINT32>(height));
        if (FAILED(result))
        {
            return fail(result, L"MFSetAttributeSize(input, MF_MT_FRAME_SIZE)");
        }

        result = MFSetAttributeRatio(input_type.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(frame_rate), 1);
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

        result = sink_writer->SetInputMediaType(stream_index, input_type.Get(), nullptr);
        if (FAILED(result))
        {
            return fail(result, L"IMFSinkWriter::SetInputMediaType");
        }

        result = sink_writer->BeginWriting();
        if (FAILED(result))
        {
            return fail(result, L"IMFSinkWriter::BeginWriting");
        }

        return S_OK;
    }

    std::vector<compatibility_profile_result> probe_h264_profiles(
        d3d_context* d3d,
        const std::filesystem::path& report_path)
    {
        struct profile_definition
        {
            const char* name;
            int32_t width;
            int32_t height;
            int32_t frame_rate;
        };

        constexpr profile_definition profiles[] =
        {
            { "1080p30", 1920, 1080, 30 },
            { "1080p60", 1920, 1080, 60 },
            { "1440p60", 2560, 1440, 60 },
            { "2160p60", 3840, 2160, 60 },
            { "1080p120", 1920, 1080, 120 }
        };

        std::vector<compatibility_profile_result> results;
        results.reserve(std::size(profiles));

        for (const auto& profile : profiles)
        {
            compatibility_profile_result item{};
            item.name = profile.name;
            item.width = profile.width;
            item.height = profile.height;
            item.frame_rate = profile.frame_rate;

            if (d3d == nullptr || d3d->device == nullptr)
            {
                item.hresult = E_POINTER;
                item.stage = L"D3D context unavailable";
            }
            else
            {
                const auto temp_path = report_path.parent_path() /
                    (std::wstring(L"sr-compat-") + widen_ascii(profile.name) + L".tmp.mp4");
                std::error_code remove_before;
                std::filesystem::remove(temp_path, remove_before);

                item.hresult = create_probe_sink_writer(
                    *d3d,
                    temp_path,
                    profile.width,
                    profile.height,
                    profile.frame_rate,
                    item.stage);

                std::error_code remove_after;
                std::filesystem::remove(temp_path, remove_after);
            }

            results.push_back(std::move(item));
        }

        return results;
    }

    HRESULT write_compatibility_report_json(const std::filesystem::path& report_path)
    {
        std::error_code create_error;
        std::filesystem::create_directories(report_path.parent_path(), create_error);
        if (create_error)
        {
            return E_FAIL;
        }

        media_foundation_scope media_foundation;
        const auto media_foundation_ready = media_foundation.is_ready();

        ComPtr<IDXGIFactory1> factory;
        const auto factory_result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));

        std::vector<DXGI_ADAPTER_DESC1> adapter_descriptions;
        if (SUCCEEDED(factory_result))
        {
            for (UINT adapter_index = 0;; ++adapter_index)
            {
                ComPtr<IDXGIAdapter1> adapter;
                auto enum_result = factory->EnumAdapters1(adapter_index, &adapter);
                if (enum_result == DXGI_ERROR_NOT_FOUND)
                {
                    break;
                }

                if (FAILED(enum_result) || adapter == nullptr)
                {
                    continue;
                }

                DXGI_ADAPTER_DESC1 desc{};
                if (SUCCEEDED(adapter->GetDesc1(&desc)))
                {
                    adapter_descriptions.push_back(desc);
                }
            }
        }

        const auto primary = find_primary_monitor();
        d3d_context d3d;
        const auto d3d_result = create_d3d_context(primary.has_value() ? primary->handle : nullptr, d3d);
        const auto bgra_input_supported = SUCCEEDED(d3d_result) &&
            has_format_support(d3d.device.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT);
        const auto nv12_output_supported = SUCCEEDED(d3d_result) &&
            has_format_support(d3d.device.Get(), DXGI_FORMAT_NV12, D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT);

        const auto wgc_supported = winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();

        HRESULT dxgi_duplication_result = E_FAIL;
        if (SUCCEEDED(d3d_result) && primary.has_value())
        {
            sr_capture_source display_source{ sr_struct_version, sr_capture_source_display, 0, { 0, 0, 0, 0 } };
            auto geometry = resolve_capture_geometry(display_source);
            if (geometry.has_value() && !geometry->spans_multiple_monitors)
            {
                dxgi_duplication_capture_backend dxgi(d3d, *geometry);
                dxgi_duplication_result = dxgi.start();
                dxgi.stop();
            }
            else
            {
                dxgi_duplication_result = E_INVALIDARG;
            }
        }
        else
        {
            dxgi_duplication_result = FAILED(d3d_result) ? d3d_result : E_FAIL;
        }

        encoder_capability_probe encoder_probe{};
        std::vector<compatibility_profile_result> profile_results;
        if (media_foundation_ready)
        {
            encoder_probe = probe_h264_nv12_hardware_encoder();
            profile_results = probe_h264_profiles(SUCCEEDED(d3d_result) ? &d3d : nullptr, report_path);
        }
        else
        {
            profile_results = probe_h264_profiles(nullptr, report_path);
        }

        const auto h264_level_52_4k_limit = resolve_h264_level_frame_rate(3840, 2160);

        std::ostringstream json;
        json << "{\n";
        json << "  \"schemaVersion\": 1,\n";
        json << "  \"artifactType\": \"simple-recorder-compatibility-report\",\n";
        json << "  \"generatedAtUnixMillis\": " << unix_time_millis() << ",\n";
        json << "  \"abiVersion\": " << sr_abi_version << ",\n";
        json << "  \"structVersion\": " << sr_struct_version << ",\n";
        json << "  \"mediaFoundationReady\": " << (media_foundation_ready ? "true" : "false") << ",\n";
        write_json_hresult(json, "mediaFoundationHresult", media_foundation.mf_startup_result, true);
        json << "  \"dxgiFactoryCreated\": " << (SUCCEEDED(factory_result) ? "true" : "false") << ",\n";
        write_json_hresult(json, "dxgiFactoryHresult", factory_result, true);
        json << "  \"hardwareAdapterDetected\": " << (d3d.hardware_adapter_detected ? "true" : "false") << ",\n";
        json << "  \"d3dContextCreated\": " << (SUCCEEDED(d3d_result) ? "true" : "false") << ",\n";
        write_json_hresult(json, "d3dHresult", d3d_result, true);
        json << "  \"d3dMultithreadProtected\": " << (d3d.multithread_protected ? "true" : "false") << ",\n";
        json << "  \"bgraVideoProcessorInputSupported\": " << (bgra_input_supported ? "true" : "false") << ",\n";
        json << "  \"nv12VideoProcessorOutputSupported\": " << (nv12_output_supported ? "true" : "false") << ",\n";
        json << "  \"wgcSupported\": " << (wgc_supported ? "true" : "false") << ",\n";
        json << "  \"dxgiDuplicationAvailable\": " << (SUCCEEDED(dxgi_duplication_result) ? "true" : "false") << ",\n";
        write_json_hresult(json, "dxgiDuplicationHresult", dxgi_duplication_result, true);
        json << "  \"h264Nv12HardwareEncoderFound\": " << (encoder_probe.has_h264_nv12_hardware ? "true" : "false") << ",\n";
        json << "  \"h264HardwareEncoderCount\": " << encoder_probe.hardware_encoder_count << ",\n";
        json << "  \"h264HardwareEncoderName\": \"" << escape_json(encoder_probe.encoder_name) << "\",\n";
        json << "  \"h264HardwareEncoderVendor\": \"" << escape_json(encoder_probe.encoder_vendor) << "\",\n";
        write_json_string_or_null(json, "h264HardwareUrl", encoder_probe.hardware_url, true);
        json << "  \"h264Level52Max4kFrameRate\": " << h264_level_52_4k_limit << ",\n";
        json << "  \"selectedAdapterName\": \"" << escape_json(narrow_utf8(d3d.adapter_name)) << "\",\n";
        json << "  \"selectedAdapterLuid\": \"" << escape_json(format_luid(d3d.adapter_luid)) << "\",\n";
        json << "  \"adapters\": [\n";
        for (size_t index = 0; index < adapter_descriptions.size(); ++index)
        {
            const auto& desc = adapter_descriptions[index];
            const auto is_software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            json << "    {\n";
            json << "      \"name\": \"" << escape_json(narrow_utf8(desc.Description)) << "\",\n";
            json << "      \"vendorId\": " << desc.VendorId << ",\n";
            json << "      \"deviceId\": " << desc.DeviceId << ",\n";
            json << "      \"vendor\": \"" << escape_json(adapter_vendor_name(desc.VendorId, desc.Description)) << "\",\n";
            json << "      \"isSoftware\": " << (is_software ? "true" : "false") << ",\n";
            json << "      \"dedicatedVideoMemoryBytes\": " << static_cast<uint64_t>(desc.DedicatedVideoMemory) << ",\n";
            json << "      \"adapterLuid\": \"" << escape_json(format_luid(desc.AdapterLuid)) << "\"\n";
            json << "    }" << (index + 1 < adapter_descriptions.size() ? "," : "") << "\n";
        }
        json << "  ],\n";
        json << "  \"h264NegotiationProfiles\": [\n";
        for (size_t index = 0; index < profile_results.size(); ++index)
        {
            const auto& profile = profile_results[index];
            json << "    {\n";
            json << "      \"name\": \"" << escape_json(profile.name) << "\",\n";
            json << "      \"width\": " << profile.width << ",\n";
            json << "      \"height\": " << profile.height << ",\n";
            json << "      \"frameRate\": " << profile.frame_rate << ",\n";
            json << "      \"passed\": " << (SUCCEEDED(profile.hresult) ? "true" : "false") << ",\n";
            json << "      \"hresult\": ";
            if (SUCCEEDED(profile.hresult))
            {
                json << "null,\n";
            }
            else
            {
                json << "\"" << format_hresult(profile.hresult) << "\",\n";
            }
            json << "      \"stage\": ";
            if (profile.stage.empty())
            {
                json << "null\n";
            }
            else
            {
                json << "\"" << escape_json(narrow_utf8(profile.stage)) << "\"\n";
            }
            json << "    }" << (index + 1 < profile_results.size() ? "," : "") << "\n";
        }
        json << "  ]\n";
        json << "}\n";

        std::ofstream report_stream(report_path, std::ios::binary | std::ios::trunc);
        if (!report_stream.good())
        {
            return E_FAIL;
        }

        report_stream << json.str();
        return report_stream.good() ? S_OK : E_FAIL;
    }
