// system_info.cpp — see system_info.h. Everything here runs once per process.
#include "system_info.h"
#include "../common/localization.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace pulse::app {
namespace {

std::wstring RegistryString(HKEY root, const wchar_t* subkey, const wchar_t* name,
                            DWORD bytes = 0) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};
    wchar_t buffer[512]{};
    if (bytes == 0 || bytes > sizeof(buffer)) bytes = sizeof(buffer);
    const LSTATUS status = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer, &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return {};
    std::wstring value(buffer);
    // ProcessorNameString arrives with trailing blanks on some builds, and so
    // do some firmware strings.
    while (!value.empty() && (value.back() == L' ' || value.back() == L'\t')) value.pop_back();
    return value;
}

DWORD RegistryDword(HKEY root, const wchar_t* subkey, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return 0;
    DWORD value = 0, bytes = sizeof(value);
    const LSTATUS status = RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS ? value : 0;
}

bool EqualsIgnoreCase(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

// Firmware often ships "System Product Name" / "Default string" instead of a
// model; those rows would say nothing.
bool IsPlaceholderModel(const std::wstring& value) {
    static const wchar_t* const kPlaceholders[] = {
        L"system product name", L"system version", L"default string", L"to be filled by o.e.m.",
        L"oem", L"not applicable", L"none", L"unknown",
    };
    for (const wchar_t* placeholder : kPlaceholders)
        if (EqualsIgnoreCase(value, placeholder)) return true;
    return false;
}

// Primary display adapters only, as their drivers name them: fastfetch's GPU
// line, without a WMI query.
std::wstring DisplayAdapters() {
    std::wstring out;
    int shown = 0;
    for (DWORD i = 0; i < 8 && shown < 2; ++i) {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, i, &device, 0)) break;
        if ((device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) continue;
        const std::wstring name = device.DeviceString;
        if (name.empty() || EqualsIgnoreCase(name, L"Microsoft Basic Render Driver")) continue;
        if (!out.empty() && out.find(name) != std::wstring::npos) continue;
        if (!out.empty()) out += L" · ";
        out += name;
        ++shown;
    }
    return out;
}

int PhysicalCoreCount() {
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (bytes == 0) return 0;
    std::vector<BYTE> buffer(bytes);
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes))
        return 0;
    int cores = 0;
    for (DWORD offset = 0; offset < bytes;) {
        const auto* info =
            reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (info->Size == 0) break;
        ++cores;
        offset += info->Size;
    }
    return cores;
}

std::wstring FirmwareText() {
    using Fn = BOOL(WINAPI*)(PFIRMWARE_TYPE);
    static const auto get_firmware_type = reinterpret_cast<Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetFirmwareType"));
    if (!get_firmware_type) return {};
    FIRMWARE_TYPE type{};
    if (!get_firmware_type(&type) || type == FirmwareTypeUnknown) return {};
    return type == FirmwareTypeUefi ? L"UEFI" : L"BIOS";
}

std::wstring OsArchitecture() {
#if defined(_M_ARM64)
    return L"ARM64";
#elif defined(_M_X64)
    return L"x64";
#else
    return L"x86";
#endif
}

SystemFacts ReadFacts() {
    constexpr const wchar_t* kCurrentVersion = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    SystemFacts facts;
    facts.os_arch = OsArchitecture();
    const std::wstring product = RegistryString(HKEY_LOCAL_MACHINE, kCurrentVersion, L"ProductName");
    std::wstring display = RegistryString(HKEY_LOCAL_MACHINE, kCurrentVersion, L"DisplayVersion");
    if (display.empty()) display = RegistryString(HKEY_LOCAL_MACHINE, kCurrentVersion, L"ReleaseId");
    const std::wstring build = RegistryString(HKEY_LOCAL_MACHINE, kCurrentVersion, L"CurrentBuildNumber");
    const DWORD ubr = RegistryDword(HKEY_LOCAL_MACHINE, kCurrentVersion, L"UBR");

    std::wstring name = product.empty() ? std::wstring(L"Windows") : product;
    // ProductName still says "Windows 10" on Windows 11.
    if (wcstoul(build.c_str(), nullptr, 10) >= 22000) {
        const size_t at = name.find(L"Windows 10");
        if (at != std::wstring::npos) name.replace(at, 10, L"Windows 11");
    }
    if (!display.empty()) name += L" " + display;
    facts.os_name = name;
    if (!build.empty()) {
        facts.os_build = build;
        if (ubr) facts.os_build += L"." + std::to_wstring(ubr);
    }

    facts.cpu = RegistryString(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    const DWORD mhz = RegistryDword(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz");
    if (mhz) {
        wchar_t clock[32]{};
        swprintf_s(clock, L"%.2f GHz", static_cast<double>(mhz) / 1000.0);
        facts.cpu_clock = clock;
    }
    facts.cpu_physical = PhysicalCoreCount();
    facts.cpu_logical = static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));

    // Firmware identifies the machine better than the monitor does.
    constexpr const wchar_t* kBios = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    const std::wstring vendor = RegistryString(HKEY_LOCAL_MACHINE, kBios, L"SystemManufacturer");
    std::wstring model = RegistryString(HKEY_LOCAL_MACHINE, kBios, L"SystemProductName");
    if (IsPlaceholderModel(model)) model.clear();
    if (!model.empty()) {
        if (!vendor.empty() && _wcsnicmp(model.c_str(), vendor.c_str(), vendor.size()) != 0)
            facts.model = vendor + L" " + model;
        else
            facts.model = model;
    }
    facts.firmware = FirmwareText();
    facts.gpu = DisplayAdapters();

    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD computer_size = ARRAYSIZE(computer);
    if (GetComputerNameW(computer, &computer_size) && computer_size)
        facts.computer.assign(computer, computer_size);

    // The primary display: physical pixels and the refresh rate it runs at.
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
        facts.screen_width = static_cast<int>(mode.dmPelsWidth);
        facts.screen_height = static_cast<int>(mode.dmPelsHeight);
        // 1 and 0 mean "hardware default", not a real rate.
        if (mode.dmDisplayFrequency >= 20 && mode.dmDisplayFrequency <= 500)
            facts.refresh_hz = static_cast<int>(mode.dmDisplayFrequency);
    }
    if (facts.screen_width <= 0 || facts.screen_height <= 0) {
        facts.screen_width = GetSystemMetrics(SM_CXSCREEN);
        facts.screen_height = GetSystemMetrics(SM_CYSCREEN);
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys) {
        const double gib = static_cast<double>(memory.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
        wchar_t text[32]{};
        // Firmware and the iGPU carve out a little, so 24 GB reads as 23.8:
        // show the installed size the way the system settings do.
        if (std::abs(gib - std::round(gib)) < 0.25)
            swprintf_s(text, L"%.0f GB", std::round(gib));
        else
            swprintf_s(text, L"%.1f GB", gib);
        facts.memory = text;
        facts.memory_total = memory.ullTotalPhys;
        facts.memory_used = memory.ullTotalPhys - memory.ullAvailPhys;
        facts.memory_percent = static_cast<int>(memory.dwMemoryLoad);
    }

    // The system drive is where Windows lives, not necessarily C:.
    wchar_t windows[MAX_PATH]{};
    const UINT windows_len = GetWindowsDirectoryW(windows, ARRAYSIZE(windows));
    if (windows_len >= 2 && windows_len < ARRAYSIZE(windows) && windows[1] == L':') {
        facts.system_drive.assign(windows, 2);
        const std::wstring root = facts.system_drive + L"\\";
        ULARGE_INTEGER free_bytes{}, total_bytes{};
        if (GetDiskFreeSpaceExW(root.c_str(), &free_bytes, &total_bytes, nullptr) &&
            total_bytes.QuadPart) {
            facts.system_total = total_bytes.QuadPart;
            facts.system_free = free_bytes.QuadPart;
        }
    }
    return facts;
}

} // namespace

const SystemFacts& Facts() {
    static const SystemFacts facts = ReadFacts();
    return facts;
}

const std::wstring& OsText() {
    static const std::wstring text = [] {
        const SystemFacts& facts = Facts();
        std::wstring value = facts.os_name;
        if (!facts.os_build.empty()) value += L" (" + facts.os_build + L")";
        return value;
    }();
    return text;
}

std::wstring UptimeText() {
    using I = l10n::StringId;
    const uint64_t minutes = GetTickCount64() / 60000ull;
    const uint64_t days = minutes / (24ull * 60ull);
    const uint64_t hours = (minutes / 60ull) % 24ull;
    wchar_t text[96]{};
    if (days > 0)
        swprintf_s(text, l10n::Get(I::DetailsUptimeDays).c_str(),
                   static_cast<unsigned long long>(days),
                   static_cast<unsigned long long>(hours));
    else if (hours > 0)
        swprintf_s(text, l10n::Get(I::DetailsUptimeHours).c_str(),
                   static_cast<unsigned long long>(hours),
                   static_cast<unsigned long long>(minutes % 60ull));
    else
        swprintf_s(text, l10n::Get(I::DetailsUptimeMinutes).c_str(),
                   static_cast<unsigned long long>(minutes));
    return text;
}

} // namespace pulse::app
