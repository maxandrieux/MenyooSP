#pragma once
//
// Lightweight diagnostic logger for the Bodyguard (squad) module.
// Writes one line per squad EVENT (not per frame) to:
//     %LOCALAPPDATA%\MenyooBodyguard\bodyguard.log
// which is OUTSIDE the GTA installation folder. The external capture script
// (tools/Capture-BodyguardSession.ps1) tails this file live.
//
// Flip BODYGUARD_DEBUG_LOG to 0 to produce a clean (non-logging) build.
//
#define BODYGUARD_DEBUG_LOG 1

#include <string>
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace sub::BodyguardMenu::dbg
{
    inline std::string LogFilePath()
    {
        std::string dir;
        char* val = nullptr;
        size_t len = 0;
        if (_dupenv_s(&val, &len, "LOCALAPPDATA") == 0 && val)
            dir = val;
        if (val)
            free(val);
        if (dir.empty())
            dir = ".";
        dir += "\\MenyooBodyguard";

        std::error_code ec;
        std::filesystem::create_directories(dir, ec); // ignore failure; Log() will no-op if unopened
        return dir + "\\bodyguard.log";
    }

    // Single shared append stream (one instance across all translation units,
    // because this is an inline function with a function-local static).
    inline std::ofstream& Stream()
    {
        static std::ofstream ofs(LogFilePath(), std::ios::out | std::ios::app);
        return ofs;
    }

    inline std::string TimeStamp()
    {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const std::time_t t = system_clock::to_time_t(now);
        std::tm tmv{};
        localtime_s(&tmv, &t);
        const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03lld",
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec, static_cast<long long>(ms.count()));
        return std::string(buf);
    }

    inline void Log(const std::string& msg)
    {
#if BODYGUARD_DEBUG_LOG
        std::ofstream& ofs = Stream();
        if (!ofs.is_open())
            return;

        static bool first = true;
        if (first)
        {
            first = false;
            ofs << "\n===== Bodyguard log session (process load) =====\n";
        }

        ofs << "[" << TimeStamp() << "] " << msg << "\n";
        ofs.flush();
#else
        (void)msg;
#endif
    }
}
