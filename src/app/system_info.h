// system_info.h — Machine facts the UI shows in more than one place (the About
// page and the details panel's 此电脑 card). Read once, never on a hot path.
#pragma once
#include <cstdint>
#include <string>

namespace pulse::app {

struct SystemFacts {
    std::wstring os_name;      // Windows 11 专业版 24H2
    std::wstring os_build;     // 26100.2894
    std::wstring os_arch;      // x64
    std::wstring computer;     // DESKTOP-1A2B3C
    std::wstring model;        // LENOVO 82WK (firmware vendor + product)
    std::wstring cpu;          // 12th Gen Intel(R) Core(TM) i7-12700H
    std::wstring cpu_clock;    // 2.70 GHz (nominal, as the firmware reports it)
    std::wstring memory;       // 32 GB (installed)
    std::wstring gpu;          // Intel(R) UHD Graphics (display adapters)
    std::wstring firmware;     // UEFI / BIOS
    std::wstring system_drive; // C:
    int cpu_physical = 0;      // physical cores
    int cpu_logical = 0;       // logical processors
    int screen_width = 0;      // primary display, physical pixels
    int screen_height = 0;
    int refresh_hz = 0;
    uint64_t memory_total = 0;  // bytes
    uint64_t memory_used = 0;   // bytes in use at first read
    int memory_percent = 0;     // 0..100 as the OS reports it
    uint64_t system_total = 0;  // system drive, bytes
    uint64_t system_free = 0;
};

// Registry and Win32 reads happen on first use; the values are stable for the
// process lifetime, so callers may use them from a per-frame path.
const SystemFacts& Facts();

// "Windows 11 专业版 24H2 (26100.2894)", as the About page lists it.
const std::wstring& OsText();

// "2 天 5 小时" / "2 d 5 h": time since boot, in the UI language. Unlike
// Facts() this is read live, so a long-running window does not lie.
std::wstring UptimeText();

} // namespace pulse::app
