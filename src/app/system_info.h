// system_info.h — Machine facts the UI shows in more than one place (the About
// page and the details panel's 此电脑 card). Read once, never on a hot path.
#pragma once
#include <string>

namespace pulse::app {

struct SystemFacts {
    std::wstring os_name;   // Windows 11 专业版 24H2
    std::wstring os_build;  // 26100.2894
    std::wstring computer;  // DESKTOP-1A2B3C
    std::wstring cpu;       // 12th Gen Intel(R) Core(TM) i7-12700H
    std::wstring memory;    // 32.0 GB
};

// Registry and Win32 reads happen on first use; the values are stable for the
// process lifetime, so callers may use them from a per-frame path.
const SystemFacts& Facts();

// "Windows 11 专业版 24H2 (26100.2894)", as the About page lists it.
const std::wstring& OsText();

} // namespace pulse::app
