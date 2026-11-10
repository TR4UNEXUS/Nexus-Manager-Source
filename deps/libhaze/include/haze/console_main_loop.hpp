/*
 * Copyright (c) Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <haze/event_reactor.hpp>
#include <haze/ptp_object_heap.hpp>
#include <haze/ptp_responder.hpp>
#include <haze/thread.hpp>
#include <atomic>

namespace haze {

    class ConsoleMainLoop final : EventConsumer {
        private:
            const Callback m_callback;
            const FsEntries m_entries;
            const u16 m_vid;
            const u16 m_pid;
            Thread m_thread{};
            UEvent m_cancel_event{};
            EventReactor m_event_reactor{};
            bool m_thread_created{}, m_thread_started{};
            std::atomic_bool m_running{false};

        public:
            explicit ConsoleMainLoop(Callback callback, const FsEntries& entries, u16 vid, u16 pid, bool init_log)
            : m_callback{callback}, m_entries{entries}, m_vid{vid}, m_pid{pid} {
                if (init_log) {
                    log_file_init();
                }

                log_write("libhaze log initialized\n");

                /* Create cancel event. */
                ueventCreate(&m_cancel_event, false);
                log_write("Created cancel event\n");

                /* Clear the event reactor. */
                m_event_reactor.SetResult(ResultSuccess());
                m_event_reactor.AddConsumer(this, waiterForUEvent(&m_cancel_event));
                log_write("Added cancel event to reactor\n");

                /* Create and start thread. */
                if (R_FAILED(sphaira::utils::CreateThread(&m_thread, thread_func, this))) return;
                m_thread_created = true;
                m_running = true;
                if (R_FAILED(threadStart(&m_thread))) { m_running = false; return; }
                m_thread_started = true;
                log_write("Started main loop thread\n");
            }

            ~ConsoleMainLoop() {
                ueventSignal(&m_cancel_event);
                if (m_thread_started) threadWaitForExit(&m_thread);
                if (m_thread_created) threadClose(&m_thread);
                log_file_exit();
            }

        public:
            bool IsRunning() const { return m_running.load(); }
            void RunApplication() {
                ON_SCOPE_EXIT { m_running = false; };
                log_write("Entering main loop\n");
                /* Declare the object heap, to hold the database for an active session. */
                PtpObjectHeap ptp_object_heap;

                /* Configure the PTP responder. */
                PtpResponder ptp_responder{m_callback};
                const auto init_result = ptp_responder.Initialize(std::addressof(m_event_reactor), std::addressof(ptp_object_heap), m_entries, m_vid, m_pid);
                log_write("Initialized PTP responder\n");

                /* Ensure we maintain a clean state on exit. */
                ON_SCOPE_EXIT {
                    /* Finalize the PTP responder. */
                    ptp_responder.Finalize();
                    log_write("Finalized PTP responder\n");
                };
                if (R_FAILED(init_result)) return;

                /* Begin processing requests. */
                log_write("Beginning request processing\n");
                R_DISCARD(ptp_responder.LoopProcess());
            }

            static void thread_func(void* user) {
                static_cast<ConsoleMainLoop*>(user)->RunApplication();
            }

        private:
            void ProcessEvent() override {
                m_event_reactor.SetResult(haze::ResultStopRequested());
            };
    };

}
