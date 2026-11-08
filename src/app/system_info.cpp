// system_info.cpp — see system_info.h. Everything here runs once per process.
#include "system_info.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pulse::app {
namespace {

std::wstring RegistryString(HKEY root, const wchar_t* subkey, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};
    wchar_t buffer[256]{};
    DWORD bytes = sizeof(buffer);
    const LSTATUS status = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer, &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return {};
    std::wstring value(buffer);
    // ProcessorNameString arrives with trailing blanks on some builds.
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

SystemFacts ReadFacts() {
    constexpr const wchar_t* kCurrentVersion = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    SystemFacts facts;
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

    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD computer_size = ARRAYSIZE(computer);
    if (GetComputerNameW(computer, &computer_size) && computer_size)
        facts.computer.assign(computer, computer_size);

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys) {
        const double gib = static_cast<double>(memory.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
        wchar_t text[32]{};
        if (std::abs(gib - std::round(gib)) < 0.05)
            swprintf_s(text, L"%.0f GB", std::round(gib));
        else
            swprintf_s(text, L"%.1f GB", gib);
        facts.memory = text;
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

} // namespace pulse::app
