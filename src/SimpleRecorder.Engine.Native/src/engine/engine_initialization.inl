// Internal implementation partition for SimpleRecorder.Engine.Native.
// Recording session backend initialization.
// Included by engine.cpp inside the native engine anonymous namespace.

    bool try_initialize_gpu_recording(recording_session& session)
    {
        const auto geometry = resolve_capture_geometry(session.source);
        if (!geometry.has_value() || geometry->spans_multiple_monitors)
        {
            session.gpu_hardware_detected = detect_hardware_graphics_adapter();
            session.cpu_fallback_blocked = session.gpu_hardware_detected;
            session.cpu_fallback_block_reason = session.gpu_hardware_detected
                ? "gpu-detected-but-source-geometry-is-not-gpu-compatible"
                : "no-hardware-gpu-detected";
            session.gpu_initialization_hresult = E_INVALIDARG;
            session.encoder_selection_reason = session.cpu_fallback_block_reason;
            return false;
        }

        if (session.source.kind == sr_capture_source_window && geometry->window == nullptr)
        {
            session.gpu_hardware_detected = detect_hardware_graphics_adapter();
            session.cpu_fallback_blocked = session.gpu_hardware_detected;
            session.cpu_fallback_block_reason = session.gpu_hardware_detected
                ? "gpu-detected-but-window-handle-is-unavailable"
                : "no-hardware-gpu-detected";
            session.gpu_initialization_hresult = E_INVALIDARG;
            session.encoder_selection_reason = session.cpu_fallback_block_reason;
            return false;
        }

        session.mode = pipeline_mode::gpu_first;
        session.capture_item_rect = geometry->capture_item_bounds;
        session.capture_crop_rect = geometry->source_rect;
        session.capture_monitor = geometry->monitor;
        session.capture_window = geometry->window;
        session.requested_capture_backend = "windows-graphics-capture";
        session.capture_backend = "pending-gpu-backend";
        session.encode_backend = "pending-gpu-encoder";
        session.encoder_config_status = "pending";
        session.encoder_preference = encoder_preference_name(session.options.encoder_preference);
        session.encoder_codec = "h264";
        session.encoder_pixel_format = "nv12";
        if (session.options.video_codec != sr_video_codec_h264)
        {
            session.encoder_fallback_reason = "requested-codec-not-implemented";
            session.encoder_selection_reason = "using-h264-current-slice-default";
        }
        session.capture_fallback_reason_code = capture_fallback_reason::none;
        session.capture_fallback_hresult = S_OK;
        session.capture_fallback_from.clear();
        session.capture_fallback_to.clear();
        session.cpu_fallback_allowed = false;
        session.cpu_fallback_blocked = false;
        session.cpu_fallback_block_reason = "none";

        if (session.options.encoder_preference == sr_encoder_preference_hardware_only)
        {
            media_foundation_scope media_foundation;
            if (!media_foundation.is_ready())
            {
                session.encoder_selection_reason = "media-foundation-unavailable";
                return false;
            }

            auto probe = probe_h264_nv12_hardware_encoder();
            session.encoder_name = probe.encoder_name;
            session.encoder_vendor = probe.encoder_vendor;
            if (!probe.has_h264_nv12_hardware)
            {
                session.encoder_selection_reason = "no-h264-nv12-hardware-mft";
                session.hardware_encode_status = "hardware-required-unavailable";
                return false;
            }

            session.encoder_selection_reason = "h264-nv12-hardware-mft-available";
        }

        session.gpu_d3d = std::make_unique<d3d_context>();
        const auto d3d_result = create_d3d_context(session.capture_monitor, *session.gpu_d3d);
        session.gpu_hardware_detected = session.gpu_d3d->hardware_adapter_detected;
        session.gpu_initialization_hresult = d3d_result;
        if (FAILED(d3d_result))
        {
            session.gpu_d3d.reset();
            session.cpu_fallback_blocked = session.gpu_hardware_detected;
            session.cpu_fallback_block_reason = session.gpu_hardware_detected
                ? "gpu-detected-but-d3d11-video-initialization-failed"
                : "no-hardware-gpu-detected";
            session.encoder_selection_reason = session.cpu_fallback_block_reason;
            return false;
        }

        session.adapter_name = session.gpu_d3d->adapter_name;
        session.adapter_luid = format_luid(session.gpu_d3d->adapter_luid);
        session.d3d_multithread_protected = session.gpu_d3d->multithread_protected;
        return true;
    }

    bool try_initialize_legacy_recording(recording_session& session)
    {
        session.mode = pipeline_mode::legacy_gdi;
        session.requested_capture_backend = "gdi-compatibility";
        session.capture_backend = "gdi-compatibility";
        session.encode_backend = "media-foundation-h264-rgb32";
        session.hardware_encode = false;
        session.hardware_encode_status = "hardware-transform-requested-unverified";
        session.encoder_preference = encoder_preference_name(session.options.encoder_preference);
        session.encoder_selection_reason = "legacy-gdi-compatibility";
        session.encoder_fallback_reason = session.encoder_fallback_reason == "none"
            ? "gpu-pipeline-unavailable"
            : session.encoder_fallback_reason;
        session.encoder_vendor = "unknown";
        session.encoder_name = "media-foundation-h264-rgb32";
        session.encoder_codec = "h264";
        session.encoder_pixel_format = "rgb32";
        session.encoder_config_status = "pending";
        session.d3d_multithread_protected = false;
        session.capture_fallback_reason_code = capture_fallback_reason::none;
        session.capture_fallback_hresult = S_OK;
        session.capture_fallback_from.clear();
        session.capture_fallback_to.clear();
        session.cpu_fallback_allowed = true;
        session.cpu_fallback_blocked = false;
        session.cpu_fallback_block_reason = "no-hardware-gpu-detected";

        auto geometry = resolve_capture_geometry(session.source);
        if (!geometry.has_value())
        {
            return false;
        }

        session.legacy_capture_rect = geometry->requested_bounds;
        return initialize_legacy_capture_resources(session);
    }
