// Internal implementation partition for SimpleRecorder.Engine.Native.
// GPU and GDI capture worker loops and pacing/backpressure accounting.
// Included by engine.cpp inside the native engine anonymous namespace.

    void gpu_capture_loop(recording_session* session)
    {
        if (session == nullptr)
        {
            return;
        }

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        auto* d3d = session->gpu_d3d.get();
        if (d3d == nullptr)
        {
            fail_session(*session, E_FAIL, L"GPU D3D context was not prepared.");
            std::scoped_lock lock(session->gate);
            session->capture_finished = true;
            return;
        }
        HRESULT result = S_OK;

        auto geometry = resolve_capture_geometry(session->source);
        if (!geometry.has_value())
        {
            fail_session(*session, E_INVALIDARG, L"Failed to resolve the capture geometry.");
            std::scoped_lock lock(session->gate);
            session->capture_finished = true;
            return;
        }

        capture_fallback_reason wgc_failure_reason = capture_fallback_reason::none;
        HRESULT wgc_failure_hresult = S_OK;
        HRESULT dxgi_failure_hresult = S_OK;
        constexpr const char* dxgi_backend_name = "dxgi-desktop-duplication";
        auto start_capture_backend = [&](bool allow_wgc) -> std::unique_ptr<capture_backend>
        {
            std::unique_ptr<capture_backend> started_backend;
            result = E_FAIL;
            const auto can_use_dxgi =
                geometry->monitor != nullptr &&
                can_fallback_to_dxgi(session->source.kind);
            const auto prefer_dxgi = can_use_dxgi;

            auto try_start_dxgi = [&]() -> bool
            {
                if (!can_use_dxgi)
                {
                    return false;
                }

                auto dxgi = std::make_unique<dxgi_duplication_capture_backend>(*d3d, *geometry);
                result = dxgi->start();
                if (SUCCEEDED(result))
                {
                    session->capture_backend = dxgi->backend_name();
                    started_backend = std::move(dxgi);
                    return true;
                }

                dxgi_failure_hresult = result;
                return false;
            };

            if (prefer_dxgi && try_start_dxgi())
            {
                return started_backend;
            }

            if (allow_wgc)
            {
                session->wgc_startup_attempted = true;
                auto wgc = std::make_unique<wgc_capture_backend>(*d3d, *geometry, session->source);
                result = wgc->start();
                if (SUCCEEDED(result))
                {
                    if (prefer_dxgi && FAILED(dxgi_failure_hresult))
                    {
                        record_capture_fallback(
                            *session,
                            dxgi_backend_name,
                            "windows-graphics-capture",
                            capture_fallback_reason::startup_hresult,
                            dxgi_failure_hresult);
                    }

                    session->capture_backend = wgc->backend_name();
                    started_backend = std::move(wgc);
                }
                else
                {
                    session->capture_backend = wgc->backend_name();
                    wgc_failure_reason = wgc->last_failure_reason();
                    wgc_failure_hresult = result;
                }
            }

            if (started_backend == nullptr &&
                !prefer_dxgi &&
                can_use_dxgi)
            {
                if (allow_wgc &&
                    (wgc_failure_reason != capture_fallback_reason::none || FAILED(wgc_failure_hresult)))
                {
                    record_capture_fallback(
                        *session,
                        "windows-graphics-capture",
                        dxgi_backend_name,
                        wgc_failure_reason == capture_fallback_reason::none
                            ? capture_fallback_reason::startup_hresult
                            : wgc_failure_reason,
                        wgc_failure_hresult);
                }

                (void)try_start_dxgi();
            }

            return started_backend;
        };

        auto backend = start_capture_backend(true);

        if (backend == nullptr)
        {
            if (session->wgc_startup_attempted &&
                !can_fallback_to_dxgi(session->source.kind) &&
                (wgc_failure_reason != capture_fallback_reason::none || FAILED(wgc_failure_hresult)))
            {
                record_capture_backend_failure(
                    *session,
                    "windows-graphics-capture",
                    wgc_failure_reason == capture_fallback_reason::none
                        ? capture_fallback_reason::startup_hresult
                        : wgc_failure_reason,
                    wgc_failure_hresult);
            }

            std::wstringstream message;
            if (session->wgc_startup_attempted && std::strcmp(session->capture_backend.c_str(), "windows-graphics-capture") == 0)
            {
                message << L"WGC capture startup failed. HRESULT=0x"
                        << std::hex
                        << static_cast<uint32_t>(wgc_failure_hresult);
                if (wgc_failure_reason != capture_fallback_reason::none)
                {
                    const auto reason_name = capture_fallback_reason_name(wgc_failure_reason);
                    message << L" (" << std::wstring(reason_name, reason_name + std::strlen(reason_name)) << L")";
                }
            }
            else
            {
                message << L"GPU capture backends could not be initialized.";
            }

            fail_session(*session, FAILED(result) ? result : E_FAIL, message.str());
            std::scoped_lock lock(session->gate);
            session->capture_finished = true;
            return;
        }

        session->capture_backend = backend->backend_name();
        resolved_capture_geometry active_backend_geometry{};
        if (backend->current_geometry(active_backend_geometry))
        {
            geometry = active_backend_geometry;
            session->capture_item_rect = geometry->capture_item_bounds;
            session->capture_crop_rect = geometry->source_rect;
            session->capture_monitor = geometry->monitor;
            session->capture_window = geometry->window;

            const auto active_source_width = geometry->source_rect.right - geometry->source_rect.left;
            const auto active_source_height = geometry->source_rect.bottom - geometry->source_rect.top;
            if (active_source_width > 0 && active_source_height > 0)
            {
                session->output_height = resolve_target_height(session->options, active_source_height);
                session->output_width = resolve_target_width(active_source_width, active_source_height, session->output_height);
                session->target_frame_rate = resolve_target_frame_rate(
                    session->options,
                    session->output_width,
                    session->output_height,
                    geometry->monitor,
                    session->monitor_refresh_rate,
                    session->fps_cap_reason);
                session->quality_config = resolve_encoder_quality_config(
                    session->options,
                    session->target_frame_rate,
                    session->output_width,
                    session->output_height);
                session->metrics.output_width = session->output_width;
                session->metrics.output_height = session->output_height;
            }
        }

        if (backend->has_received_first_frame())
        {
            session->wgc_first_frame_latency_qpc = std::max<int64_t>(backend->first_frame_arrived_qpc() - session->started_qpc, 0);
        }

        try
        {
            frame_graph graph(
                *d3d,
                session->gpu_slots,
                geometry->capture_item_bounds,
                session->output_width,
                session->output_height,
                session->target_frame_rate,
                session->quality_config.prefer_quality_video_processing,
                session->quality_config.edge_enhancement_requested);
            session->video_processor_usage = graph.video_processor_usage();
            session->edge_enhancement_applied = graph.edge_enhancement_applied();

            bool can_capture = false;
            {
                std::unique_lock lock(session->gate);
                session->gpu_resources_ready = true;
                session->gpu_start_condition.notify_all();
                session->gpu_start_condition.wait(lock, [session]
                {
                    return session->gpu_encoder_ready || session->stop_requested || FAILED(session->failure);
                });

                can_capture = session->gpu_encoder_ready && !session->stop_requested && SUCCEEDED(session->failure);
                if (can_capture)
                {
                    for (size_t index = 0; index < session->gpu_slots.size(); ++index)
                    {
                        session->free_slots.push_back(index);
                    }
                }
            }

            if (!can_capture)
            {
                backend->stop();
                std::scoped_lock lock(session->gate);
                session->capture_finished = true;
                session->ready_condition.notify_all();
                session->gpu_start_condition.notify_all();
                return;
            }

            const auto target_period_qpc = frame_duration_qpc(session->target_frame_rate);
            auto next_capture_deadline = qpc_now();

            while (true)
            {
                bool should_stop = false;
                bool is_paused = false;
                {
                    std::scoped_lock lock(session->gate);
                    should_stop = session->stop_requested;
                    is_paused = session->pause_requested;
                }

                if (should_stop)
                {
                    break;
                }

                if (is_paused)
                {
                    next_capture_deadline = qpc_now();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }

                auto now = qpc_now();
                if (now < next_capture_deadline)
                {
                    const auto sleep_millis = static_cast<int32_t>(qpc_to_millis(next_capture_deadline - now));
                    if (sleep_millis > 1)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_millis - 1));
                    }

                    while ((now = qpc_now()) < next_capture_deadline)
                    {
                        SwitchToThread();
                    }
                }
                else if (now - next_capture_deadline >= target_period_qpc)
                {
                    const auto skipped_periods = static_cast<uint64_t>((now - next_capture_deadline) / target_period_qpc);
                    std::scoped_lock lock(session->gate);
                    session->metrics.pacing_overrun_count += skipped_periods;
                    next_capture_deadline += static_cast<int64_t>(skipped_periods) * target_period_qpc;
                }

                resolved_capture_geometry resized_geometry{};
                if (backend->consume_resize(resized_geometry))
                {
                    {
                        std::scoped_lock lock(session->gate);
                        ++session->wgc_resize_count;
                        session->wgc_resize_status = "recreate-reported";
                    }

                    bool can_rebuild = false;
                    while (true)
                    {
                        {
                            std::scoped_lock lock(session->gate);
                            should_stop = session->stop_requested || FAILED(session->failure);
                            can_rebuild = session->ready_slots.empty();
                        }

                        if (should_stop || can_rebuild)
                        {
                            break;
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }

                    if (!can_rebuild)
                    {
                        break;
                    }

                    const auto rebuild_result = graph.recreate_for_capture_item_rect(resized_geometry.capture_item_bounds);
                    if (FAILED(rebuild_result))
                    {
                        record_capture_backend_failure(
                            *session,
                            backend->backend_name(),
                            capture_fallback_reason::stream_change,
                            rebuild_result);
                        {
                            std::scoped_lock lock(session->gate);
                            session->wgc_resize_status = "frame-graph-rebuild-failed";
                            session->wgc_resize_failure_reason = format_hresult(rebuild_result);
                            ++session->metrics.capture_failure_count;
                        }

                        fail_session(*session, rebuild_result, L"WGC resize was reported but the GPU frame graph could not be rebuilt.");
                        break;
                    }

                    geometry = resized_geometry;
                    session->capture_item_rect = resized_geometry.capture_item_bounds;
                    session->capture_crop_rect = resized_geometry.source_rect;
                    session->capture_monitor = resized_geometry.monitor;
                    session->capture_window = resized_geometry.window;
                    session->video_processor_usage = graph.video_processor_usage();
                    session->edge_enhancement_applied = graph.edge_enhancement_applied();
                    {
                        std::scoped_lock lock(session->gate);
                        session->wgc_resize_status = "continued-after-frame-graph-rebuild";
                        session->crop_resize_mismatch_reason = "wgc-content-size-changed-frame-graph-rebuilt";
                    }

                    next_capture_deadline += target_period_qpc;
                    continue;
                }

                size_t slot_index = 0;
                {
                    std::scoped_lock lock(session->gate);
                    ++session->metrics.capture_attempts;
                    if (session->free_slots.empty())
                    {
                        ++session->metrics.backpressure_drop_count;
                        next_capture_deadline += target_period_qpc;
                        continue;
                    }

                    slot_index = session->free_slots.front();
                    session->free_slots.pop_front();
                }

                auto& slot = session->gpu_slots[slot_index];
                copied_capture_frame copied_frame{};
                const auto capture_started_qpc = qpc_now();
                result = backend->copy_next_frame_to(slot.bgra_texture.Get(), copied_frame);
                const auto capture_finished_qpc = qpc_now();

                {
                    std::scoped_lock lock(session->gate);
                    session->metrics.backpressure_drop_count += backend->consume_backpressure_drops();
                    const auto copy_mismatch_reason = backend->consume_copy_mismatch_reason();
                    if (!copy_mismatch_reason.empty())
                    {
                        ++session->copy_dimension_mismatch_count;
                        session->copy_integrity_status = "texture-mismatch-observed";
                        session->copy_integrity_failure_reason = copy_mismatch_reason;
                        session->crop_resize_mismatch_reason = copy_mismatch_reason;
                    }
                }

                if (is_capture_timeout(result))
                {
                    const auto failure_reason = backend->last_failure_reason();
                    const auto using_wgc = std::strcmp(backend->backend_name(), "windows-graphics-capture") == 0;
                    const auto can_fallback =
                        using_wgc &&
                        geometry->monitor != nullptr &&
                        can_fallback_to_dxgi(session->source.kind);
                    const auto startup_timed_out =
                        using_wgc &&
                        !backend->has_received_first_frame() &&
                        failure_reason == capture_fallback_reason::startup_timeout;

                    if (startup_timed_out)
                    {
                        if (can_fallback)
                        {
                            record_capture_fallback(
                                *session,
                                backend->backend_name(),
                                dxgi_backend_name,
                                capture_fallback_reason::startup_timeout,
                                result);
                            backend->stop();
                            auto fallback_backend = start_capture_backend(false);
                            if (fallback_backend != nullptr)
                            {
                                backend = std::move(fallback_backend);
                                session->capture_backend = backend->backend_name();
                                recycle_slot(*session, slot_index);
                                next_capture_deadline += target_period_qpc;
                                continue;
                            }
                        }
                        else
                        {
                            record_capture_backend_failure(
                                *session,
                                backend->backend_name(),
                                capture_fallback_reason::startup_timeout,
                                result);
                        }

                        {
                            std::scoped_lock lock(session->gate);
                            ++session->metrics.capture_failure_count;
                        }

                        std::wstringstream message;
                        const auto backend_name = backend->backend_name();
                        const std::wstring backend_name_wide(backend_name, backend_name + std::strlen(backend_name));
                        message << L"GPU capture startup timed out on " << backend_name_wide
                                << L". HRESULT=0x"
                                << std::hex
                                << static_cast<uint32_t>(result)
                                << L" (startup-timeout)";
                        fail_session(*session, result, message.str());
                        recycle_slot(*session, slot_index);
                        break;
                    }

                    if (backend->has_received_first_frame() && session->wgc_first_frame_latency_qpc == 0)
                    {
                        session->wgc_first_frame_latency_qpc =
                            std::max<int64_t>(backend->first_frame_arrived_qpc() - session->started_qpc, 0);
                    }

                    recycle_slot(*session, slot_index);
                    next_capture_deadline += target_period_qpc;
                    continue;
                }

                if (FAILED(result))
                {
                    const auto failure_reason = backend->last_failure_reason();
                    const auto using_wgc = std::strcmp(backend->backend_name(), "windows-graphics-capture") == 0;
                    const auto can_fallback =
                        using_wgc &&
                        geometry->monitor != nullptr &&
                        can_fallback_to_dxgi(session->source.kind);

                    if (backend->has_received_first_frame() && session->wgc_first_frame_latency_qpc == 0)
                    {
                        session->wgc_first_frame_latency_qpc =
                            std::max<int64_t>(backend->first_frame_arrived_qpc() - session->started_qpc, 0);
                    }

                    if (using_wgc && failure_reason == capture_fallback_reason::stream_change)
                    {
                        std::scoped_lock lock(session->gate);
                        ++session->wgc_resize_count;
                        session->wgc_resize_status = can_fallback
                            ? "resize-failed-falling-back"
                            : "resize-failed-no-fallback";
                        session->wgc_resize_failure_reason = format_hresult(result);
                    }

                    if (can_fallback)
                    {
                        record_capture_fallback(
                            *session,
                            backend->backend_name(),
                            dxgi_backend_name,
                            failure_reason == capture_fallback_reason::none
                                ? capture_fallback_reason::runtime_hresult
                                : failure_reason,
                            result);
                        backend->stop();
                        auto fallback_backend = start_capture_backend(false);
                        if (fallback_backend != nullptr)
                        {
                            backend = std::move(fallback_backend);
                            session->capture_backend = backend->backend_name();
                            recycle_slot(*session, slot_index);
                            continue;
                        }
                    }
                    else if (using_wgc && failure_reason != capture_fallback_reason::none)
                    {
                        record_capture_backend_failure(
                            *session,
                            backend->backend_name(),
                            failure_reason,
                            result);
                    }

                    {
                        std::scoped_lock lock(session->gate);
                        ++session->metrics.capture_failure_count;
                    }

                    const auto backend_name = backend->backend_name();
                    const std::wstring backend_name_wide(backend_name, backend_name + std::strlen(backend_name));
                    std::wstringstream message;
                    message << L"GPU capture failed on " << backend_name_wide
                            << L". HRESULT=0x"
                            << std::hex
                            << static_cast<uint32_t>(result);
                    if (failure_reason != capture_fallback_reason::none)
                    {
                        const auto reason_name = capture_fallback_reason_name(failure_reason);
                        message << L" (" << std::wstring(reason_name, reason_name + std::strlen(reason_name)) << L")";
                    }
                    fail_session(*session, result, message.str());
                    recycle_slot(*session, slot_index);
                    break;
                }

                if (backend->has_received_first_frame() && session->wgc_first_frame_latency_qpc == 0)
                {
                    session->wgc_first_frame_latency_qpc =
                        std::max<int64_t>(backend->first_frame_arrived_qpc() - session->started_qpc, 0);
                }

                const auto convert_started_qpc = qpc_now();
                try
                {
                    graph.process_slot(slot, copied_frame.source_rect);
                }
                catch (const winrt::hresult_error& error)
                {
                    fail_session(*session, error.code(), L"GPU crop/scale/convert failed.");
                    recycle_slot(*session, slot_index);
                    break;
                }
                const auto convert_finished_qpc = qpc_now();

                {
                    std::scoped_lock lock(session->gate);
                    slot.captured_at_qpc = copied_frame.captured_at_qpc != 0 ? copied_frame.captured_at_qpc : capture_finished_qpc;
                    slot.capture_duration_qpc = capture_finished_qpc - capture_started_qpc;
                    slot.convert_duration_qpc = convert_finished_qpc - convert_started_qpc;
                    slot.source_rect = copied_frame.source_rect;
                    slot.sequence = ++session->next_sequence;
                    session->ready_slots.push_back(slot_index);
                    ++session->metrics.captured_frames;
                    session->metrics.total_capture_latency_qpc += slot.capture_duration_qpc;
                    session->metrics.total_convert_latency_qpc += slot.convert_duration_qpc;
                    session->metrics.first_captured_qpc = session->metrics.first_captured_qpc == 0 ? slot.captured_at_qpc : session->metrics.first_captured_qpc;
                    session->metrics.last_captured_qpc = slot.captured_at_qpc;
                    session->metrics.peak_queue_depth = std::max<uint32_t>(session->metrics.peak_queue_depth, static_cast<uint32_t>(session->ready_slots.size()));
                }

                session->ready_condition.notify_one();
                next_capture_deadline += target_period_qpc;
            }
        }
        catch (const winrt::hresult_error& error)
        {
            fail_session(*session, error.code(), L"GPU frame-graph initialization failed.");
        }

        backend->stop();
        {
            std::scoped_lock lock(session->gate);
            session->capture_finished = true;
        }

        session->ready_condition.notify_all();
        session->gpu_start_condition.notify_all();
    }

    void legacy_capture_loop(recording_session* session)
    {
        if (session == nullptr)
        {
            return;
        }

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        const auto target_period_qpc = frame_duration_qpc(session->target_frame_rate);
        auto next_capture_deadline = qpc_now();

        while (true)
        {
            bool should_stop = false;
            bool is_paused = false;
            {
                std::scoped_lock lock(session->gate);
                should_stop = session->stop_requested;
                is_paused = session->pause_requested;
            }

            if (should_stop)
            {
                break;
            }

            if (is_paused)
            {
                next_capture_deadline = qpc_now();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            auto now = qpc_now();
            if (now < next_capture_deadline)
            {
                const auto sleep_millis = static_cast<int32_t>(qpc_to_millis(next_capture_deadline - now));
                if (sleep_millis > 1)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_millis - 1));
                }

                while ((now = qpc_now()) < next_capture_deadline)
                {
                    SwitchToThread();
                }
            }
            else if (now - next_capture_deadline >= target_period_qpc)
            {
                const auto skipped_periods = static_cast<uint64_t>((now - next_capture_deadline) / target_period_qpc);
                std::scoped_lock lock(session->gate);
                session->metrics.pacing_overrun_count += skipped_periods;
                next_capture_deadline += static_cast<int64_t>(skipped_periods) * target_period_qpc;
            }

            size_t slot_index = 0;
            {
                std::scoped_lock lock(session->gate);
                ++session->metrics.capture_attempts;
                if (session->free_slots.empty())
                {
                    ++session->metrics.backpressure_drop_count;
                    next_capture_deadline += target_period_qpc;
                    continue;
                }

                slot_index = session->free_slots.front();
                session->free_slots.pop_front();
            }

            auto& slot = session->legacy_slots[slot_index];
            const auto capture_started_qpc = qpc_now();
            const auto captured = capture_legacy_frame(*session, slot);
            const auto capture_finished_qpc = qpc_now();

            if (!captured)
            {
                {
                    std::scoped_lock lock(session->gate);
                    ++session->metrics.capture_failure_count;
                    session->free_slots.push_back(slot_index);
                }

                next_capture_deadline += target_period_qpc;
                continue;
            }

            {
                std::scoped_lock lock(session->gate);
                slot.captured_at_qpc = capture_finished_qpc;
                slot.capture_duration_qpc = capture_finished_qpc - capture_started_qpc;
                slot.sequence = ++session->next_sequence;
                session->ready_slots.push_back(slot_index);
                ++session->metrics.captured_frames;
                session->metrics.total_capture_latency_qpc += slot.capture_duration_qpc;
                session->metrics.first_captured_qpc = session->metrics.first_captured_qpc == 0 ? slot.captured_at_qpc : session->metrics.first_captured_qpc;
                session->metrics.last_captured_qpc = slot.captured_at_qpc;
                session->metrics.peak_queue_depth = std::max<uint32_t>(session->metrics.peak_queue_depth, static_cast<uint32_t>(session->ready_slots.size()));
            }

            session->ready_condition.notify_one();
            next_capture_deadline += target_period_qpc;
        }

        {
            std::scoped_lock lock(session->gate);
            session->capture_finished = true;
        }

        session->ready_condition.notify_all();
    }
