// Internal implementation partition for SimpleRecorder.Engine.Native.
// GPU and GDI encode worker loops.
// Included by engine.cpp inside the native engine anonymous namespace.

    void gpu_encode_loop(recording_session* session)
    {
        if (session == nullptr)
        {
            return;
        }

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        media_foundation_scope media_foundation;
        if (!media_foundation.is_ready())
        {
            fail_session(*session, E_FAIL, L"Media Foundation could not be initialized.");
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
            return;
        }
        auto* d3d = session->gpu_d3d.get();
        if (d3d == nullptr)
        {
            fail_session(*session, E_FAIL, L"GPU D3D context was not prepared.");
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
            return;
        }
        auto result = S_OK;

        try
        {
            {
                std::unique_lock lock(session->gate);
                session->gpu_start_condition.wait(lock, [session]
                {
                    return session->gpu_resources_ready || session->capture_finished || FAILED(session->failure);
                });

                if (!session->gpu_resources_ready || FAILED(session->failure))
                {
                    session->encode_finished = true;
                    session->gpu_start_condition.notify_all();
                    return;
                }
            }

            encoder_backend encoder(*session, *d3d);
            encoder.prepare_slot_samples();
            {
                std::scoped_lock lock(session->gate);
                session->encode_backend = encoder.backend_name();
                session->hardware_encode = encoder.is_hardware_encode();
                session->hardware_encode_status = encoder.hardware_encode_status();
                session->gpu_encoder_ready = true;
            }
            session->gpu_start_condition.notify_all();

            const auto default_duration_qpc = frame_duration_qpc(session->target_frame_rate);
            size_t pending_slot_index = static_cast<size_t>(-1);
            uint64_t pending_slot_write_count = 0;
            int64_t next_sample_time_qpc = 0;
            uint64_t next_sample_index = 0;
            auto write_pending_once = [&](gpu_capture_slot& pending_slot, int64_t duration_qpc, bool duplicated_frame)
            {
                const auto sample_duration_qpc = std::max<int64_t>(duration_qpc, default_duration_qpc);
                const auto sample_time_hns = frame_timestamp_hns(next_sample_index, session->target_frame_rate);
                const auto sample_duration_hns = frame_duration_hns(next_sample_index, session->target_frame_rate);
                const auto encode_started_qpc = qpc_now();
                encoder.write_slot(pending_slot, sample_time_hns, sample_duration_hns);
                const auto encode_finished_qpc = qpc_now();

                {
                    std::scoped_lock lock(session->gate);
                    ++session->metrics.encoded_frames;
                    if (duplicated_frame)
                    {
                        ++session->metrics.duplicated_frame_count;
                    }
                    session->metrics.total_queue_latency_qpc += std::max<int64_t>(0, encode_started_qpc - pending_slot.captured_at_qpc);
                    session->metrics.total_encode_latency_qpc += encode_finished_qpc - encode_started_qpc;
                    session->metrics.last_sample_duration_qpc = sample_duration_qpc;
                    session->metrics.represented_duration_hns += sample_duration_hns;
                    session->metrics.first_sample_timestamp_hns = session->metrics.first_sample_timestamp_hns < 0
                        ? sample_time_hns
                        : session->metrics.first_sample_timestamp_hns;
                    session->metrics.last_sample_timestamp_hns = sample_time_hns;
                    session->metrics.last_sample_duration_hns = sample_duration_hns;
                    session->metrics.represented_duration_qpc += sample_duration_qpc;
                }

                ++pending_slot_write_count;
                ++next_sample_index;
                next_sample_time_qpc += sample_duration_qpc;
            };
            auto write_pending_until = [&](int64_t target_time_qpc)
            {
                if (pending_slot_index == static_cast<size_t>(-1))
                {
                    return;
                }

                auto& pending_slot = session->gpu_slots[pending_slot_index];
                while (next_sample_time_qpc < target_time_qpc && SUCCEEDED(session->failure))
                {
                    write_pending_once(pending_slot, default_duration_qpc, pending_slot_write_count > 0);
                }
            };

            while (true)
            {
                size_t ready_slot_index = static_cast<size_t>(-1);
                bool has_ready_backlog = false;
                bool should_duplicate_pending_slot = false;
                {
                    std::unique_lock lock(session->gate);
                    if (pending_slot_index == static_cast<size_t>(-1))
                    {
                        session->ready_condition.wait(lock, [&session]
                        {
                            return !session->ready_slots.empty() || session->capture_finished || FAILED(session->failure);
                        });
                    }
                    else
                    {
                        session->ready_condition.wait_for(lock, std::chrono::milliseconds(std::max<int32_t>(
                            1,
                            static_cast<int32_t>(qpc_to_millis(default_duration_qpc)))), [&session]
                        {
                            return !session->ready_slots.empty() || session->capture_finished || FAILED(session->failure);
                        });
                    }

                    if (!session->ready_slots.empty())
                    {
                        ready_slot_index = session->ready_slots.front();
                        session->ready_slots.pop_front();
                        has_ready_backlog = !session->ready_slots.empty();
                    }
                    else if (pending_slot_index != static_cast<size_t>(-1) &&
                        !session->capture_finished &&
                        SUCCEEDED(session->failure))
                    {
                        should_duplicate_pending_slot = true;
                    }
                    else
                    {
                        if (session->capture_finished || FAILED(session->failure))
                        {
                            break;
                        }

                        continue;
                    }
                }

                if (pending_slot_index == static_cast<size_t>(-1))
                {
                    pending_slot_index = ready_slot_index;
                    pending_slot_write_count = 0;
                    continue;
                }

                if (should_duplicate_pending_slot)
                {
                    write_pending_until(next_sample_time_qpc + default_duration_qpc);
                    continue;
                }

                const auto ready_capture_offset_qpc = std::max<int64_t>(
                    session->gpu_slots[ready_slot_index].captured_at_qpc - session->started_qpc,
                    0);
                const auto pending_target_qpc = has_ready_backlog
                    ? std::min<int64_t>(ready_capture_offset_qpc, next_sample_time_qpc + default_duration_qpc)
                    : ready_capture_offset_qpc;
                write_pending_until(pending_target_qpc);
                recycle_slot(*session, pending_slot_index);
                pending_slot_index = ready_slot_index;
                pending_slot_write_count = 0;
            }

            if (pending_slot_index != static_cast<size_t>(-1) && SUCCEEDED(session->failure))
            {
                int64_t completed_qpc = 0;
                {
                    std::scoped_lock lock(session->gate);
                    completed_qpc = session->completed_qpc != 0 ? session->completed_qpc : qpc_now();
                }

                write_pending_until(std::max<int64_t>(completed_qpc - session->started_qpc, 0));
            }

            if (pending_slot_index != static_cast<size_t>(-1))
            {
                recycle_slot(*session, pending_slot_index);
            }

            if (SUCCEEDED(session->failure))
            {
                result = encoder.finalize();
                if (FAILED(result))
                {
                    fail_session(*session, result, L"Failed to finalize the MP4 file.");
                }
            }
        }
        catch (const winrt::hresult_error& error)
        {
            std::wstringstream message;
            message << L"GPU encode path failed. HRESULT=0x" << std::hex << static_cast<uint32_t>(error.code().value);
            const auto detail = std::wstring(error.message().c_str());
            if (!detail.empty())
            {
                message << L". " << detail;
            }
            fail_session(*session, error.code(), message.str());
        }

        {
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
        }
    }

    void legacy_encode_loop(recording_session* session)
    {
        if (session == nullptr)
        {
            return;
        }

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        media_foundation_scope media_foundation;
        if (!media_foundation.is_ready())
        {
            fail_session(*session, E_FAIL, L"Media Foundation could not be initialized.");
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
            return;
        }

        ComPtr<IMFSinkWriter> sink_writer;
        DWORD stream_index = 0;
        auto writer_result = create_legacy_sink_writer(*session, sink_writer, stream_index, true);
        if (SUCCEEDED(writer_result))
        {
            session->encoder_config_status = "configured";
        }
        else
        {
            writer_result = create_legacy_sink_writer(*session, sink_writer, stream_index, false);
            if (SUCCEEDED(writer_result))
            {
                session->encoder_config_status = "fallback-without-encoder-config";
            }
        }

        if (FAILED(writer_result))
        {
            fail_session(*session, writer_result, L"Failed to create the legacy MP4 sink writer.");
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
            return;
        }

        const auto default_duration_qpc = frame_duration_qpc(session->target_frame_rate);
        size_t pending_slot_index = static_cast<size_t>(-1);
        uint64_t pending_slot_write_count = 0;
        int64_t next_sample_time_qpc = 0;
        uint64_t next_sample_index = 0;
        auto write_pending_until = [&](int64_t target_time_qpc) -> HRESULT
        {
            if (pending_slot_index == static_cast<size_t>(-1))
            {
                return S_OK;
            }

            auto& pending_slot = session->legacy_slots[pending_slot_index];
            while (next_sample_time_qpc < target_time_qpc && SUCCEEDED(session->failure))
            {
                const auto sample_time_hns = frame_timestamp_hns(next_sample_index, session->target_frame_rate);
                const auto sample_duration_hns = frame_duration_hns(next_sample_index, session->target_frame_rate);
                const auto encode_started_qpc = qpc_now();
                const auto result = write_legacy_sample(sink_writer.Get(), stream_index, pending_slot, sample_time_hns, sample_duration_hns);
                const auto encode_finished_qpc = qpc_now();
                if (FAILED(result))
                {
                    return result;
                }

                {
                    std::scoped_lock lock(session->gate);
                    ++session->metrics.encoded_frames;
                    if (pending_slot_write_count > 0)
                    {
                        ++session->metrics.duplicated_frame_count;
                    }
                    session->metrics.total_queue_latency_qpc += std::max<int64_t>(0, encode_started_qpc - pending_slot.captured_at_qpc);
                    session->metrics.total_encode_latency_qpc += encode_finished_qpc - encode_started_qpc;
                    session->metrics.last_sample_duration_qpc = default_duration_qpc;
                    session->metrics.represented_duration_hns += sample_duration_hns;
                    session->metrics.first_sample_timestamp_hns = session->metrics.first_sample_timestamp_hns < 0
                        ? sample_time_hns
                        : session->metrics.first_sample_timestamp_hns;
                    session->metrics.last_sample_timestamp_hns = sample_time_hns;
                    session->metrics.last_sample_duration_hns = sample_duration_hns;
                    session->metrics.represented_duration_qpc += default_duration_qpc;
                }

                ++pending_slot_write_count;
                ++next_sample_index;
                next_sample_time_qpc += default_duration_qpc;
            }

            return S_OK;
        };

        while (true)
        {
            size_t ready_slot_index = static_cast<size_t>(-1);
            bool should_duplicate_pending_slot = false;
            {
                std::unique_lock lock(session->gate);
                if (pending_slot_index == static_cast<size_t>(-1))
                {
                    session->ready_condition.wait(lock, [&session]
                    {
                        return !session->ready_slots.empty() || session->capture_finished || FAILED(session->failure);
                    });
                }
                else
                {
                    session->ready_condition.wait_for(lock, std::chrono::milliseconds(std::max<int32_t>(
                        1,
                        static_cast<int32_t>(qpc_to_millis(default_duration_qpc)))), [&session]
                    {
                        return !session->ready_slots.empty() || session->capture_finished || FAILED(session->failure);
                    });
                }

                if (!session->ready_slots.empty())
                {
                    ready_slot_index = session->ready_slots.front();
                    session->ready_slots.pop_front();
                }
                else if (pending_slot_index != static_cast<size_t>(-1) && !session->capture_finished && SUCCEEDED(session->failure))
                {
                    should_duplicate_pending_slot = true;
                }
                else
                {
                    if (session->capture_finished || FAILED(session->failure))
                    {
                        break;
                    }

                    continue;
                }
            }

            if (pending_slot_index == static_cast<size_t>(-1))
            {
                pending_slot_index = ready_slot_index;
                pending_slot_write_count = 0;
                continue;
            }

            if (should_duplicate_pending_slot)
            {
                const auto result = write_pending_until(next_sample_time_qpc + default_duration_qpc);
                if (FAILED(result))
                {
                    fail_session(*session, result, L"Failed to write a repeated legacy video sample.");
                    recycle_slot(*session, pending_slot_index);
                    pending_slot_index = static_cast<size_t>(-1);
                    break;
                }

                continue;
            }

            const auto ready_capture_offset_qpc = std::max<int64_t>(
                session->legacy_slots[ready_slot_index].captured_at_qpc - session->started_qpc,
                0);
            const auto result = write_pending_until(ready_capture_offset_qpc);
            if (FAILED(result))
            {
                fail_session(*session, result, L"Failed to write a legacy video sample.");
                recycle_slot(*session, pending_slot_index);
                recycle_slot(*session, ready_slot_index);
                pending_slot_index = static_cast<size_t>(-1);
                break;
            }

            recycle_slot(*session, pending_slot_index);
            pending_slot_index = ready_slot_index;
            pending_slot_write_count = 0;
        }

        if (pending_slot_index != static_cast<size_t>(-1) && SUCCEEDED(session->failure))
        {
            int64_t completed_qpc = 0;
            {
                std::scoped_lock lock(session->gate);
                completed_qpc = session->completed_qpc != 0 ? session->completed_qpc : qpc_now();
            }

            const auto result = write_pending_until(std::max<int64_t>(completed_qpc - session->started_qpc, 0));
            if (FAILED(result))
            {
                fail_session(*session, result, L"Failed to write the final legacy video sample.");
            }
        }

        if (pending_slot_index != static_cast<size_t>(-1))
        {
            recycle_slot(*session, pending_slot_index);
        }

        if (SUCCEEDED(session->failure))
        {
            const auto finalize_result = sink_writer->Finalize();
            if (FAILED(finalize_result))
            {
                fail_session(*session, finalize_result, L"Failed to finalize the legacy MP4 file.");
            }
        }

        {
            std::scoped_lock lock(session->gate);
            session->encode_finished = true;
        }
    }
