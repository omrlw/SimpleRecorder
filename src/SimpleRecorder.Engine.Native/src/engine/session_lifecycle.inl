// Internal implementation partition for SimpleRecorder.Engine.Native.
// Thread joining, stop/failure coordination, and telemetry helper calculations.
// Included by engine.cpp inside the native engine anonymous namespace.

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

    double compute_effective_frame_rate(const recording_metrics& metrics)
    {
        if (metrics.encoded_frames == 0 || metrics.represented_duration_qpc <= 0)
        {
            return 0.0;
        }

        return static_cast<double>(metrics.encoded_frames) * static_cast<double>(qpc_frequency()) /
            static_cast<double>(metrics.represented_duration_qpc);
    }

    int64_t compute_wall_duration_qpc(const recording_session& session)
    {
        const auto completed_qpc = session.completed_qpc != 0 ? session.completed_qpc : qpc_now();
        return std::max<int64_t>(completed_qpc - session.started_qpc, 0);
    }

    double compute_represented_to_wall_duration_ratio(const recording_session& session)
    {
        const auto wall_duration_qpc = compute_wall_duration_qpc(session);
        if (wall_duration_qpc <= 0)
        {
            return 0.0;
        }

        return static_cast<double>(session.metrics.represented_duration_qpc) /
            static_cast<double>(wall_duration_qpc);
    }

    double compute_average_latency_millis(int64_t total_latency_qpc, uint64_t sample_count)
    {
        if (sample_count == 0)
        {
            return 0.0;
        }

        return qpc_to_millis(total_latency_qpc) / static_cast<double>(sample_count);
    }
