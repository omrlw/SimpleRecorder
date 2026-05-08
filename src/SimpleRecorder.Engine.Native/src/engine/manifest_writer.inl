// Internal implementation partition for SimpleRecorder.Engine.Native.
// Session manifest JSON writer.
// Included by engine.cpp inside the native engine anonymous namespace.

    void write_manifest(const recording_session& session, bool export_succeeded)
    {
        std::ostringstream json;
        const auto final_video_name_utf8 = session.final_output_path.filename().u8string();
        const auto final_video_name = std::string(final_video_name_utf8.begin(), final_video_name_utf8.end());
        const auto dropped_frame_count = session.metrics.backpressure_drop_count + session.metrics.capture_failure_count;
        const auto wall_duration_qpc = compute_wall_duration_qpc(session);
        const RECT output_rect{ 0, 0, session.metrics.output_width, session.metrics.output_height };

        json << std::fixed << std::setprecision(2);
        json << "{\n";
        json << "  \"schemaVersion\": 10,\n";
        json << "  \"artifactType\": \"streaming-mp4-session\",\n";
        json << "  \"requestedCaptureBackend\": \"" << escape_json(session.requested_capture_backend) << "\",\n";
        json << "  \"captureBackend\": \"" << escape_json(session.capture_backend) << "\",\n";
        json << "  \"captureFallbackFrom\": ";
        if (session.capture_fallback_from.empty())
        {
            json << "null,\n";
        }
        else
        {
            json << "\"" << escape_json(session.capture_fallback_from) << "\",\n";
        }
        json << "  \"captureFallbackTo\": ";
        if (session.capture_fallback_to.empty())
        {
            json << "null,\n";
        }
        else
        {
            json << "\"" << escape_json(session.capture_fallback_to) << "\",\n";
        }
        json << "  \"captureFallbackReason\": \"" << capture_fallback_reason_name(session.capture_fallback_reason_code) << "\",\n";
        json << "  \"captureFallbackHresult\": ";
        if (session.capture_fallback_reason_code == capture_fallback_reason::none)
        {
            json << "null,\n";
        }
        else
        {
            json << "\"" << format_hresult(session.capture_fallback_hresult) << "\",\n";
        }
        json << "  \"encodeBackend\": \"" << escape_json(session.encode_backend) << "\",\n";
        json << "  \"hardwareEncode\": " << (session.hardware_encode ? "true" : "false") << ",\n";
        json << "  \"hardwareEncodeStatus\": \"" << escape_json(session.hardware_encode_status) << "\",\n";
        json << "  \"encoderPreference\": \"" << escape_json(session.encoder_preference) << "\",\n";
        json << "  \"encoderSelectionReason\": \"" << escape_json(session.encoder_selection_reason) << "\",\n";
        json << "  \"encoderFallbackReason\": \"" << escape_json(session.encoder_fallback_reason) << "\",\n";
        json << "  \"encoderVendor\": \"" << escape_json(session.encoder_vendor) << "\",\n";
        json << "  \"encoderName\": \"" << escape_json(session.encoder_name) << "\",\n";
        json << "  \"adapterLuid\": \"" << escape_json(session.adapter_luid) << "\",\n";
        json << "  \"videoCodec\": \"" << escape_json(session.encoder_codec) << "\",\n";
        json << "  \"encoderPixelFormat\": \"" << escape_json(session.encoder_pixel_format) << "\",\n";
        json << "  \"qualityPresetName\": \"" << escape_json(session.quality_config.quality_preset_name) << "\",\n";
        json << "  \"qualityPolicyVersion\": " << session.quality_config.quality_policy_version << ",\n";
        json << "  \"targetBitrateBps\": " << session.quality_config.target_bitrate_bps << ",\n";
        json << "  \"maxBitrateBps\": " << session.quality_config.max_bitrate_bps << ",\n";
        json << "  \"rateControlMode\": \"" << escape_json(session.quality_config.rate_control_mode) << "\",\n";
        json << "  \"qualityVsSpeed\": " << session.quality_config.quality_vs_speed << ",\n";
        json << "  \"gopSize\": " << session.quality_config.gop_size << ",\n";
        json << "  \"cabacRequested\": " << (session.quality_config.cabac_requested ? "true" : "false") << ",\n";
        json << "  \"encoderLowLatency\": " << (session.quality_config.low_latency_requested ? "true" : "false") << ",\n";
        json << "  \"encoderRealTime\": " << (session.quality_config.real_time_requested ? "true" : "false") << ",\n";
        json << "  \"encoderAllowFrameDrops\": " << (session.quality_config.allow_frame_drops ? "true" : "false") << ",\n";
        json << "  \"encoderFrameRateConversion\": \"" << (session.quality_config.frame_rate_conversion_disabled ? "disabled" : "enabled") << "\",\n";
        json << "  \"encoderConfigStatus\": \"" << escape_json(session.encoder_config_status) << "\",\n";
        json << "  \"colorPrimaries\": \"BT.709\",\n";
        json << "  \"transferFunction\": \"BT.709\",\n";
        json << "  \"yuvMatrix\": \"BT.709\",\n";
        json << "  \"nominalRange\": \"16-235\",\n";
        json << "  \"d3dInputColorSpace\": \"RGB full-range BT.709/sRGB\",\n";
        json << "  \"d3dOutputColorSpace\": \"YCbCr BT.709 limited-range\",\n";
        json << "  \"videoProcessorUsage\": \"" << escape_json(session.video_processor_usage) << "\",\n";
        json << "  \"edgeEnhancementRequested\": " << (session.quality_config.edge_enhancement_requested ? "true" : "false") << ",\n";
        json << "  \"edgeEnhancementApplied\": " << (session.edge_enhancement_applied ? "true" : "false") << ",\n";
        json << "  \"gpuPipelineMode\": \"" << (session.mode == pipeline_mode::gpu_first ? "gpu-first" : "legacy-gdi") << "\",\n";
        json << "  \"captureSlotCount\": " << capture_slot_count << ",\n";
        json << "  \"captureQueueLimit\": " << wgc_frame_queue_limit << ",\n";
        json << "  \"d3dMultithreadProtected\": " << (session.d3d_multithread_protected ? "true" : "false") << ",\n";
        json << "  \"gpuHardwareDetected\": " << (session.gpu_hardware_detected ? "true" : "false") << ",\n";
        json << "  \"cpuFallbackAllowed\": " << (session.cpu_fallback_allowed ? "true" : "false") << ",\n";
        json << "  \"cpuFallbackBlocked\": " << (session.cpu_fallback_blocked ? "true" : "false") << ",\n";
        json << "  \"cpuFallbackBlockReason\": \"" << escape_json(session.cpu_fallback_block_reason) << "\",\n";
        json << "  \"gpuInitializationHresult\": ";
        if (SUCCEEDED(session.gpu_initialization_hresult))
        {
            json << "null,\n";
        }
        else
        {
            json << "\"" << format_hresult(session.gpu_initialization_hresult) << "\",\n";
        }
        json << "  \"copyIntegrityStatus\": \"" << escape_json(session.copy_integrity_status) << "\",\n";
        json << "  \"copyDimensionMismatchCount\": " << session.copy_dimension_mismatch_count << ",\n";
        json << "  \"copyIntegrityFailureReason\": ";
        if (session.copy_integrity_failure_reason.empty())
        {
            json << "null,\n";
        }
        else
        {
            json << "\"" << escape_json(session.copy_integrity_failure_reason) << "\",\n";
        }
        json << "  \"cropResizeMismatchReason\": \"" << escape_json(session.crop_resize_mismatch_reason) << "\",\n";
        write_json_rect(json, "captureItemRect", session.capture_item_rect, true);
        write_json_rect(json, "sourceRect", session.capture_crop_rect, true);
        write_json_rect(json, "outputRect", output_rect, true);
        json << "  \"wgcStartupAttempted\": " << (session.wgc_startup_attempted ? "true" : "false") << ",\n";
        json << "  \"wgcFirstFrameLatencyMs\": " << qpc_to_millis(session.wgc_first_frame_latency_qpc) << ",\n";
        json << "  \"wgcResizeCount\": " << session.wgc_resize_count << ",\n";
        json << "  \"wgcResizeStatus\": \"" << escape_json(session.wgc_resize_status) << "\",\n";
        json << "  \"wgcResizeFailureReason\": ";
        if (session.wgc_resize_failure_reason.empty())
        {
            json << "null,\n";
        }
        else
        {
            json << "\"" << escape_json(session.wgc_resize_failure_reason) << "\",\n";
        }
        if (!session.adapter_name.empty())
        {
            json << "  \"adapterName\": \"" << escape_json(narrow_utf8(session.adapter_name)) << "\",\n";
        }
        json << "  \"fileOutputMode\": \"live-mp4\",\n";
        json << "  \"sourceKind\": \"" << source_kind_name(session.source.kind) << "\",\n";
        json << "  \"requestedFrameRate\": " << session.options.frame_rate << ",\n";
        json << "  \"isMonitorFrameRateMode\": " << (session.options.frame_rate == monitor_frame_rate_option ? "true" : "false") << ",\n";
        json << "  \"monitorFrameRateLimit\": " << product_monitor_frame_rate_limit << ",\n";
        json << "  \"targetFrameRate\": " << session.target_frame_rate << ",\n";
        json << "  \"effectiveFrameRate\": " << compute_effective_frame_rate(session.metrics) << ",\n";
        json << "  \"monitorRefreshRate\": " << session.monitor_refresh_rate << ",\n";
        json << "  \"wasMonitorFrameRateCapped\": " << (session.options.frame_rate == monitor_frame_rate_option && session.monitor_refresh_rate > session.target_frame_rate ? "true" : "false") << ",\n";
        json << "  \"frameRatePolicy\": \"constant-output-cadence-max-120\",\n";
        json << "  \"fpsCapReason\": \"" << escape_json(session.fps_cap_reason) << "\",\n";
        json << "  \"captureFrameRate\": " << compute_capture_frame_rate(session.metrics) << ",\n";
        json << "  \"encodeContainerFrameRate\": " << session.target_frame_rate << ",\n";
        json << "  \"duplicatedFrameRatio\": " << compute_duplicated_frame_ratio(session.metrics) << ",\n";
        json << "  \"h264Level\": \"" << escape_json(session.quality_config.h264_level_name) << "\",\n";
        json << "  \"h264LevelValue\": " << session.quality_config.h264_level << ",\n";
        json << "  \"requestedResolution\": " << session.options.resolution << ",\n";
        json << "  \"outputWidth\": " << session.metrics.output_width << ",\n";
        json << "  \"outputHeight\": " << session.metrics.output_height << ",\n";
        json << "  \"includeSystemAudio\": " << (session.options.include_system_audio != 0 ? "true" : "false") << ",\n";
        json << "  \"includeMicrophone\": " << (session.options.include_microphone != 0 ? "true" : "false") << ",\n";
        json << "  \"startedAtUnixMillis\": " << session.started_at_unix_millis << ",\n";
        json << "  \"completedAtUnixMillis\": " << unix_time_millis() << ",\n";
        json << "  \"wallDurationMs\": " << qpc_to_millis(wall_duration_qpc) << ",\n";
        json << "  \"representedDurationMs\": " << (session.metrics.represented_duration_hns > 0
            ? hns_to_millis(session.metrics.represented_duration_hns)
            : qpc_to_millis(session.metrics.represented_duration_qpc)) << ",\n";
        json << "  \"representedToWallDurationRatio\": " << compute_represented_to_wall_duration_ratio(session) << ",\n";
        json << "  \"firstSampleTimestampHns\": " << std::max<LONGLONG>(session.metrics.first_sample_timestamp_hns, 0) << ",\n";
        json << "  \"lastSampleTimestampHns\": " << session.metrics.last_sample_timestamp_hns << ",\n";
        json << "  \"lastSampleDurationHns\": " << session.metrics.last_sample_duration_hns << ",\n";
        json << "  \"finalVideoFile\": \"" << escape_json(final_video_name) << "\",\n";
        json << "  \"finalVideoContainer\": \"mp4\",\n";
        json << "  \"finalVideoCodec\": \"h264\",\n";
        json << "  \"finalVideoExported\": " << (export_succeeded ? "true" : "false") << ",\n";
        json << "  \"captureAttemptCount\": " << session.metrics.capture_attempts << ",\n";
        json << "  \"capturedFrameCount\": " << session.metrics.captured_frames << ",\n";
        json << "  \"encodedFrameCount\": " << session.metrics.encoded_frames << ",\n";
        json << "  \"droppedFrameCount\": " << dropped_frame_count << ",\n";
        json << "  \"duplicatedFrameCount\": " << session.metrics.duplicated_frame_count << ",\n";
        json << "  \"backpressureDropCount\": " << session.metrics.backpressure_drop_count << ",\n";
        json << "  \"captureFailureCount\": " << session.metrics.capture_failure_count << ",\n";
        json << "  \"pacingOverrunCount\": " << session.metrics.pacing_overrun_count << ",\n";
        json << "  \"peakQueueDepth\": " << session.metrics.peak_queue_depth << ",\n";
        json << "  \"averageEncodedFramesPerSecond\": " << compute_average_frames_per_second(session.metrics) << ",\n";
        json << "  \"averageCaptureLatencyMs\": " << compute_average_latency_millis(session.metrics.total_capture_latency_qpc, session.metrics.captured_frames) << ",\n";
        json << "  \"averageQueueLatencyMs\": " << compute_average_latency_millis(session.metrics.total_queue_latency_qpc, session.metrics.encoded_frames) << ",\n";
        json << "  \"averageConvertLatencyMs\": " << compute_average_latency_millis(session.metrics.total_convert_latency_qpc, session.metrics.captured_frames) << ",\n";
        json << "  \"averageEncodeLatencyMs\": " << compute_average_latency_millis(session.metrics.total_encode_latency_qpc, session.metrics.encoded_frames);

        if (!session.failure_reason.empty())
        {
            json << ",\n  \"failureReason\": \"" << escape_json(narrow_utf8(session.failure_reason)) << "\"";
        }

        if (FAILED(session.failure))
        {
            json << ",\n  \"failureHresult\": \"" << format_hresult(session.failure) << "\"";
        }

        json << "\n}\n";

        std::ofstream manifest_stream(session.session_directory / "manifest.json", std::ios::binary | std::ios::trunc);
        manifest_stream << json.str();
    }
