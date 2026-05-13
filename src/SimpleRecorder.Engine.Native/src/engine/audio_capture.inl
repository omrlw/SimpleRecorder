// Internal implementation partition for SimpleRecorder.Engine.Native.
// WASAPI shared-mode audio capture, mixing, and AAC sink writer submission.
// Included by engine.cpp inside the native engine anonymous namespace.

    const char* audio_mode_name(int32_t audio_mode) noexcept
    {
        switch (audio_mode)
        {
        case sr_audio_capture_system:
            return "system";
        case sr_audio_capture_microphone:
            return "microphone";
        case sr_audio_capture_system_and_microphone:
            return "system-and-microphone";
        default:
            return "off";
        }
    }

    bool audio_mode_includes_system(int32_t audio_mode) noexcept
    {
        return audio_mode == sr_audio_capture_system || audio_mode == sr_audio_capture_system_and_microphone;
    }

    bool audio_mode_includes_microphone(int32_t audio_mode) noexcept
    {
        return audio_mode == sr_audio_capture_microphone || audio_mode == sr_audio_capture_system_and_microphone;
    }

    HRESULT configure_audio_stream(IMFSinkWriter* sink_writer, recording_session& session, DWORD& stream_index)
    {
        stream_index = static_cast<DWORD>(-1);
        if (sink_writer == nullptr || session.options.audio_mode == sr_audio_capture_off)
        {
            session.audio_codec = "none";
            session.audio_status = "off";
            return S_OK;
        }

        ComPtr<IMFMediaType> output_type;
        auto result = MFCreateMediaType(&output_type);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio_channels);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio_sample_rate);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, audio_bits_per_sample);
        if (FAILED(result))
        {
            return result;
        }

        result = output_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, audio_aac_bitrate / 8);
        if (FAILED(result))
        {
            return result;
        }

        result = sink_writer->AddStream(output_type.Get(), &stream_index);
        if (FAILED(result))
        {
            session.audio_status = "aac-stream-add-failed";
            return result;
        }

        ComPtr<IMFMediaType> input_type;
        result = MFCreateMediaType(&input_type);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio_channels);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio_sample_rate);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, audio_bits_per_sample);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, audio_bytes_per_frame);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, audio_sample_rate * audio_bytes_per_frame);
        if (FAILED(result))
        {
            return result;
        }

        result = input_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        if (FAILED(result))
        {
            return result;
        }

        result = sink_writer->SetInputMediaType(stream_index, input_type.Get(), nullptr);
        if (FAILED(result))
        {
            session.audio_status = "pcm-input-type-rejected";
            return result;
        }

        session.audio_codec = "aac";
        session.audio_status = "configured";
        return S_OK;
    }

    struct audio_capture_controller::capture_source_state
    {
        bool enabled = false;
        bool system_loopback = false;
        HANDLE sample_event = nullptr;
        WAVEFORMATEX* mix_format = nullptr;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> audio_client;
        ComPtr<IAudioCaptureClient> capture_client;
        std::vector<float> pending_frames;
        uint32_t channels = 0;
        uint32_t sample_rate = 0;
        uint32_t bits_per_sample = 0;
        GUID subtype{};
        std::string display_name = "default";
    };

    audio_capture_controller::audio_capture_controller(
        recording_session& session,
        IMFSinkWriter* sink_writer,
        DWORD stream_index,
        std::mutex& writer_gate)
        : _session(session),
          _sink_writer(sink_writer),
          _stream_index(stream_index),
          _writer_gate(writer_gate)
    {
    }

    audio_capture_controller::~audio_capture_controller()
    {
        stop();
    }

    void audio_capture_controller::start()
    {
        _thread = std::thread(&audio_capture_controller::run, this);
    }

    void audio_capture_controller::stop() noexcept
    {
        _stop_requested = true;
        if (_thread.joinable())
        {
            _thread.join();
        }
    }

    static std::string read_audio_device_name(IMMDevice* device)
    {
        if (device == nullptr)
        {
            return "default";
        }

        LPWSTR id = nullptr;
        std::string result = "default";
        if (SUCCEEDED(device->GetId(&id)) && id != nullptr)
        {
            result = narrow_utf8(id);
        }

        if (id != nullptr)
        {
            CoTaskMemFree(id);
        }

        return result;
    }

    static GUID audio_subtype(const WAVEFORMATEX* format) noexcept
    {
        if (format == nullptr)
        {
            return GUID{};
        }

        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        {
            return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->SubFormat;
        }

        if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        {
            return KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        }

        if (format->wFormatTag == WAVE_FORMAT_PCM)
        {
            return KSDATAFORMAT_SUBTYPE_PCM;
        }

        return GUID{};
    }

    HRESULT audio_capture_controller::initialize_source(
        bool system_loopback,
        const wchar_t* requested_device_id,
        capture_source_state& source)
    {
        source.enabled = true;
        source.system_loopback = system_loopback;

        ComPtr<IMMDeviceEnumerator> enumerator;
        auto result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator));
        if (FAILED(result))
        {
            return result;
        }

        if (!system_loopback && requested_device_id != nullptr && requested_device_id[0] != L'\0')
        {
            result = enumerator->GetDevice(requested_device_id, &source.device);
        }
        else
        {
            result = enumerator->GetDefaultAudioEndpoint(
                system_loopback ? eRender : eCapture,
                eConsole,
                &source.device);
        }

        if (FAILED(result))
        {
            return result;
        }

        source.display_name = read_audio_device_name(source.device.Get());
        result = source.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &source.audio_client);
        if (FAILED(result))
        {
            return result;
        }

        result = source.audio_client->GetMixFormat(&source.mix_format);
        if (FAILED(result))
        {
            return result;
        }

        source.channels = std::max<uint32_t>(source.mix_format->nChannels, 1);
        source.sample_rate = std::max<uint32_t>(source.mix_format->nSamplesPerSec, 1);
        source.bits_per_sample = source.mix_format->wBitsPerSample;
        source.subtype = audio_subtype(source.mix_format);

        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
            (system_loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
        result = source.audio_client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            flags,
            0,
            0,
            source.mix_format,
            nullptr);
        if (FAILED(result))
        {
            return result;
        }

        source.sample_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (source.sample_event == nullptr)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        result = source.audio_client->SetEventHandle(source.sample_event);
        if (FAILED(result))
        {
            return result;
        }

        result = source.audio_client->GetService(IID_PPV_ARGS(&source.capture_client));
        if (FAILED(result))
        {
            return result;
        }

        return source.audio_client->Start();
    }

    static void append_resampled_packet(
        const BYTE* data,
        uint32_t input_frames,
        DWORD flags,
        const audio_capture_controller::capture_source_state& source,
        std::vector<float>& pending)
    {
        if (input_frames == 0)
        {
            return;
        }

        const auto output_frames = std::max<uint32_t>(
            1,
            static_cast<uint32_t>((static_cast<uint64_t>(input_frames) * audio_sample_rate + source.sample_rate - 1) / source.sample_rate));
        const auto gain = 1.0f;
        const auto silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr;
        const auto is_float = source.subtype == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        const auto is_pcm = source.subtype == KSDATAFORMAT_SUBTYPE_PCM;

        pending.reserve(pending.size() + static_cast<size_t>(output_frames) * audio_channels);
        for (uint32_t out = 0; out < output_frames; ++out)
        {
            const auto in = std::min<uint32_t>(
                input_frames - 1,
                static_cast<uint32_t>((static_cast<uint64_t>(out) * source.sample_rate) / audio_sample_rate));
            float left = 0.0f;
            float right = 0.0f;

            if (!silent)
            {
                if (is_float && source.bits_per_sample == 32)
                {
                    const auto* samples = reinterpret_cast<const float*>(data);
                    left = samples[static_cast<size_t>(in) * source.channels];
                    right = source.channels > 1 ? samples[static_cast<size_t>(in) * source.channels + 1] : left;
                }
                else if (is_pcm && source.bits_per_sample == 16)
                {
                    const auto* samples = reinterpret_cast<const int16_t*>(data);
                    left = static_cast<float>(samples[static_cast<size_t>(in) * source.channels]) / 32768.0f;
                    right = source.channels > 1
                        ? static_cast<float>(samples[static_cast<size_t>(in) * source.channels + 1]) / 32768.0f
                        : left;
                }
                else if (is_pcm && source.bits_per_sample == 24)
                {
                    const auto frame = data + static_cast<size_t>(in) * source.channels * 3;
                    auto read24 = [](const BYTE* bytes)
                    {
                        int32_t value = static_cast<int32_t>(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16));
                        if ((value & 0x00800000) != 0)
                        {
                            value |= 0xFF000000;
                        }
                        return static_cast<float>(value) / 8388608.0f;
                    };
                    left = read24(frame);
                    right = source.channels > 1 ? read24(frame + 3) : left;
                }
                else if (is_pcm && source.bits_per_sample == 32)
                {
                    const auto* samples = reinterpret_cast<const int32_t*>(data);
                    left = static_cast<float>(static_cast<double>(samples[static_cast<size_t>(in) * source.channels]) / 2147483648.0);
                    right = source.channels > 1
                        ? static_cast<float>(static_cast<double>(samples[static_cast<size_t>(in) * source.channels + 1]) / 2147483648.0)
                        : left;
                }
            }

            pending.push_back(std::clamp(left * gain, -1.0f, 1.0f));
            pending.push_back(std::clamp(right * gain, -1.0f, 1.0f));
        }
    }

    HRESULT audio_capture_controller::read_source(
        capture_source_state& source,
        std::vector<float>& mix,
        uint32_t frame_count,
        bool& had_data)
    {
        had_data = false;
        if (!source.enabled || source.capture_client == nullptr)
        {
            return S_OK;
        }

        WaitForSingleObject(source.sample_event, 0);
        UINT32 packet_frames = 0;
        auto result = source.capture_client->GetNextPacketSize(&packet_frames);
        if (FAILED(result))
        {
            return result;
        }

        while (packet_frames > 0)
        {
            BYTE* data = nullptr;
            DWORD flags = 0;
            UINT64 device_position = 0;
            UINT64 qpc_position = 0;
            result = source.capture_client->GetBuffer(&data, &packet_frames, &flags, &device_position, &qpc_position);
            if (FAILED(result))
            {
                return result;
            }

            append_resampled_packet(data, packet_frames, flags, source, source.pending_frames);
            source.capture_client->ReleaseBuffer(packet_frames);
            result = source.capture_client->GetNextPacketSize(&packet_frames);
            if (FAILED(result))
            {
                return result;
            }
        }

        const auto available_frames = static_cast<uint32_t>(source.pending_frames.size() / audio_channels);
        const auto frames_to_mix = std::min(frame_count, available_frames);
        if (frames_to_mix == 0)
        {
            return S_OK;
        }

        const float source_gain = _session.options.audio_mode == sr_audio_capture_system_and_microphone ? 0.70f : 1.0f;
        for (uint32_t frame = 0; frame < frames_to_mix; ++frame)
        {
            mix[static_cast<size_t>(frame) * 2] += source.pending_frames[static_cast<size_t>(frame) * 2] * source_gain;
            mix[static_cast<size_t>(frame) * 2 + 1] += source.pending_frames[static_cast<size_t>(frame) * 2 + 1] * source_gain;
        }

        source.pending_frames.erase(
            source.pending_frames.begin(),
            source.pending_frames.begin() + static_cast<std::ptrdiff_t>(frames_to_mix * audio_channels));
        had_data = true;
        return S_OK;
    }

    HRESULT audio_capture_controller::write_mix(const std::vector<float>& mix, uint64_t start_frame, uint32_t frame_count)
    {
        ComPtr<IMFMediaBuffer> buffer;
        const auto byte_count = frame_count * audio_bytes_per_frame;
        auto result = MFCreateMemoryBuffer(byte_count, &buffer);
        if (FAILED(result))
        {
            return result;
        }

        BYTE* destination = nullptr;
        result = buffer->Lock(&destination, nullptr, nullptr);
        if (FAILED(result))
        {
            return result;
        }

        auto* samples = reinterpret_cast<int16_t*>(destination);
        for (uint32_t frame = 0; frame < frame_count; ++frame)
        {
            const auto left = std::clamp(mix[static_cast<size_t>(frame) * 2], -1.0f, 1.0f);
            const auto right = std::clamp(mix[static_cast<size_t>(frame) * 2 + 1], -1.0f, 1.0f);
            samples[static_cast<size_t>(frame) * 2] = static_cast<int16_t>(std::lround(left * 32767.0f));
            samples[static_cast<size_t>(frame) * 2 + 1] = static_cast<int16_t>(std::lround(right * 32767.0f));
        }

        buffer->Unlock();
        result = buffer->SetCurrentLength(byte_count);
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

        const auto sample_time = static_cast<LONGLONG>(
            (static_cast<long double>(start_frame) * 10'000'000.0L) / static_cast<long double>(audio_sample_rate));
        const auto sample_duration = static_cast<LONGLONG>(
            (static_cast<long double>(frame_count) * 10'000'000.0L) / static_cast<long double>(audio_sample_rate));
        sample->SetSampleTime(sample_time);
        sample->SetSampleDuration(std::max<LONGLONG>(sample_duration, 1));

        {
            std::scoped_lock writer_lock(_writer_gate);
            result = _sink_writer->WriteSample(_stream_index, sample.Get());
        }

        if (SUCCEEDED(result))
        {
            std::scoped_lock lock(_session.gate);
            _session.metrics.audio_samples_written += frame_count;
            ++_session.metrics.audio_packets_written;
        }

        return result;
    }

    void audio_capture_controller::run() noexcept
    {
        const auto co_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        DWORD avrt_task_index = 0;
        HANDLE avrt_handle = AvSetMmThreadCharacteristicsW(L"Audio", &avrt_task_index);

        capture_source_state system_source;
        capture_source_state microphone_source;
        auto cleanup = [&]
        {
            if (system_source.audio_client != nullptr)
            {
                system_source.audio_client->Stop();
            }
            if (microphone_source.audio_client != nullptr)
            {
                microphone_source.audio_client->Stop();
            }
            if (system_source.sample_event != nullptr)
            {
                CloseHandle(system_source.sample_event);
            }
            if (microphone_source.sample_event != nullptr)
            {
                CloseHandle(microphone_source.sample_event);
            }
            if (system_source.mix_format != nullptr)
            {
                CoTaskMemFree(system_source.mix_format);
            }
            if (microphone_source.mix_format != nullptr)
            {
                CoTaskMemFree(microphone_source.mix_format);
            }
            if (avrt_handle != nullptr)
            {
                AvRevertMmThreadCharacteristics(avrt_handle);
            }
            if (SUCCEEDED(co_result))
            {
                CoUninitialize();
            }
        };

        if (FAILED(co_result))
        {
            {
                std::scoped_lock lock(_session.gate);
                _session.audio_status = "com-initialization-failed";
                _session.audio_failure_reason = format_hresult(co_result);
            }
            fail_session(_session, co_result, L"Audio COM initialization failed.");
            cleanup();
            return;
        }

        HRESULT result = S_OK;
        if (audio_mode_includes_system(_session.options.audio_mode))
        {
            result = initialize_source(true, nullptr, system_source);
            if (FAILED(result))
            {
                {
                    std::scoped_lock lock(_session.gate);
                    _session.audio_status = "system-audio-failed";
                    _session.audio_failure_reason = format_hresult(result);
                }
                fail_session(_session, result, L"System audio loopback capture failed to initialize.");
                cleanup();
                return;
            }

            std::scoped_lock lock(_session.gate);
            _session.system_audio_device_name = system_source.display_name;
        }

        if (audio_mode_includes_microphone(_session.options.audio_mode))
        {
            const auto* requested_id = _session.microphone_device_id.empty() ? nullptr : _session.microphone_device_id.c_str();
            result = initialize_source(false, requested_id, microphone_source);
            if (FAILED(result))
            {
                {
                    std::scoped_lock lock(_session.gate);
                    _session.audio_status = "microphone-failed";
                    _session.audio_failure_reason = format_hresult(result);
                }
                fail_session(_session, result, L"Microphone capture failed to initialize.");
                cleanup();
                return;
            }

            std::scoped_lock lock(_session.gate);
            _session.microphone_device_name = microphone_source.display_name;
        }

        {
            std::scoped_lock lock(_session.gate);
            _session.audio_status = "capturing";
        }

        std::vector<float> mix(static_cast<size_t>(audio_mix_chunk_frames) * audio_channels);
        uint64_t next_audio_frame = 0;
        auto next_tick = std::chrono::steady_clock::now();

        while (!_stop_requested && SUCCEEDED(_session.failure))
        {
            bool is_paused = false;
            {
                std::scoped_lock lock(_session.gate);
                is_paused = _session.pause_requested;
            }

            if (is_paused)
            {
                next_tick = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            std::fill(mix.begin(), mix.end(), 0.0f);
            bool system_had_data = false;
            bool microphone_had_data = false;

            result = read_source(system_source, mix, audio_mix_chunk_frames, system_had_data);
            if (FAILED(result))
            {
                break;
            }

            result = read_source(microphone_source, mix, audio_mix_chunk_frames, microphone_had_data);
            if (FAILED(result))
            {
                break;
            }

            if (!system_had_data && !microphone_had_data)
            {
                std::scoped_lock lock(_session.gate);
                ++_session.metrics.audio_underflows;
            }

            result = write_mix(mix, next_audio_frame, audio_mix_chunk_frames);
            if (FAILED(result))
            {
                break;
            }

            next_audio_frame += audio_mix_chunk_frames;
            next_tick += std::chrono::milliseconds(10);
            std::this_thread::sleep_until(next_tick);
        }

        if (FAILED(result))
        {
            {
                std::scoped_lock lock(_session.gate);
                _session.audio_status = "capture-failed";
                _session.audio_failure_reason = format_hresult(result);
                ++_session.metrics.audio_discontinuities;
            }
            fail_session(_session, result, L"Audio capture failed while recording.");
        }
        else
        {
            std::scoped_lock lock(_session.gate);
            const auto wall_duration_qpc = compute_wall_duration_qpc(_session);
            const auto audio_duration_ms = static_cast<double>(_session.metrics.audio_samples_written) *
                1000.0 / static_cast<double>(audio_sample_rate);
            _session.metrics.audio_drift_millis = audio_duration_ms - qpc_to_millis(wall_duration_qpc);
            _session.audio_status = _session.metrics.audio_samples_written > 0 ? "captured" : _session.audio_status;
        }

        cleanup();
    }

    encoder_backend::~encoder_backend() = default;
