#include "network_trial.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <mutex>

namespace
{
    struct Log
    {
        const wchar_t* filename;
        const char* environment;
        const char* header;
        FILE* trial = nullptr;
        FILE* legacy = nullptr;
        std::once_flag legacyOpen{};
    };

    std::array<Log, 3> g_logs{{
        { L"cuadros.csv", "PES_FRAME_TRACE",
            "guest_frame_field_before,guest_frame_field_after,sequence,exchanged,home_buttons,away_buttons,"
            "match_start,match_remaining,game_random,gpu_frame,home_team,away_team\n" },
        { L"mandos.csv", "PES_PAD_TRACE", nullptr },
        { L"partido.csv", "PES_MATCH_TRACE",
            "callback,guest_thread,caller,gpu_frame,manager_before,step_before,clock_before,period_start_before,rng_before,period_before,"
            "manager_after,step_after,clock_after,period_start_after,rng_after,period_after,result,"
            "call,parent_call,gameplay_before,gameplay_after,fingerprint_before,fingerprint_after\n" }
    }};

    std::atomic_bool g_ready{false};
    std::mutex g_prepare;

    void WriteHeader(FILE* stream, const char* header)
    {
        if (header)
        {
            std::fputs(header, stream);
            std::fflush(stream);
        }
    }

    struct CloseLog
    {
        void operator()(FILE* file) const { std::fclose(file); }
    };
}

namespace Online
{
    bool PrepareNetworkTrial()
    {
        std::lock_guard lock(g_prepare);
        if (g_ready.load(std::memory_order_acquire)) return true;

        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t id[48]{};
        std::swprintf(id, std::size(id), L"%04u%02u%02u-%02u%02u%02u-%03u-%lu",
            unsigned(now.wYear), unsigned(now.wMonth), unsigned(now.wDay), unsigned(now.wHour), unsigned(now.wMinute),
            unsigned(now.wSecond), unsigned(now.wMilliseconds), GetCurrentProcessId());
        const auto folder = std::filesystem::path(L"registros-red") / id;
        std::error_code error;
        if (!std::filesystem::create_directories(folder, error) || error) return false;

        std::array<std::unique_ptr<FILE, CloseLog>, g_logs.size()> opened;
        for (size_t i = 0; i < g_logs.size(); ++i)
        {
            const auto& log = g_logs[i];
            opened[i].reset(_wfopen((folder / log.filename).c_str(), L"w"));
            if (!opened[i]) return false;
            WriteHeader(opened[i].get(), log.header);
        }
        for (size_t i = 0; i < g_logs.size(); ++i)
            g_logs[i].trial = opened[i].release();
        g_ready.store(true, std::memory_order_release);
        return true;
    }

    bool NetworkTrialInputEnabled()
    {
        static const bool legacy = std::getenv("PES_EXPERIMENTAL_SYNC") != nullptr;
        return legacy || g_ready.load(std::memory_order_acquire);
    }

    FILE* NetworkTrialLog(TrialLog kind)
    {
        auto& log = g_logs[static_cast<size_t>(kind)];
        if (g_ready.load(std::memory_order_acquire)) return log.trial;
        std::call_once(log.legacyOpen, [&log] {
            const char* path = std::getenv(log.environment);
            if (!path || !*path) return;
            log.legacy = std::fopen(path, "w");
            if (log.legacy) WriteHeader(log.legacy, log.header);
            else std::perror(log.environment);
        });
        return log.legacy;
    }
}
