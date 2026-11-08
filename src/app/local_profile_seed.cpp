#include "../common/windows_compat.h"
#include "local_profile_seed.h"

#include "session.h"

#include <shlobj.h>
#include <cwchar>

namespace pulse::app {
namespace {

// What carries the user's configuration on this PC: the files 设置 > 备份与迁移
// moves, plus the session layout and the cached folder sizes. Everything else
// in the folder is a cache (thumbs, logs, ContentIndex, ...) and can be rebuilt.
constexpr const wchar_t* kConfigFiles[] = {
    L"app.json",
    L"context_menu.json",
    L"ctx_layout.bin",
    L"places.json",
    L"tags.json",
    L"tag_rules.json",
    L"aliases.json",
    L"saved_searches.json",
    L"search_history.json",
    L"jump_history.tsv",
    L"folder_sizes.json",
    L"session.json",
};

bool Exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// The installed release keeps its state under %LOCALAPPDATA%\Pulse.
std::wstring ReleaseDataDir() {
    wchar_t path[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) return {};
    return std::wstring(path) + L"\\Pulse";
}

std::wstring BackupDir(const std::wstring& local) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[64]{};
    swprintf_s(name, L"\\backup-before-sync-%04u%02u%02u-%02u%02u%02u",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    std::wstring dir = local + name;
    return CreateDirectoryW(dir.c_str(), nullptr) ? dir : std::wstring{};
}

} // namespace

bool SeedLocalProfileFromRelease(bool force) {
#ifdef PULSE_LOCAL_PROFILE
    const std::wstring local = GetPulseDataDir();
    const std::wstring release = ReleaseDataDir();
    if (local.empty() || release.empty()) return false;
    if (CompareStringOrdinal(local.c_str(), -1, release.c_str(), -1, TRUE) == CSTR_EQUAL) return false;
    // A profile that already has settings is the user's own: only an explicit
    // --sync-release-config replaces it.
    if (!force && Exists(local + L"\\app.json")) return false;

    std::wstring backup;
    bool copied = false;
    for (const wchar_t* name : kConfigFiles) {
        const std::wstring source = release + L"\\" + name;
        if (!Exists(source)) continue;
        const std::wstring target = local + L"\\" + name;
        if (force && Exists(target)) {
            if (backup.empty()) backup = BackupDir(local);
            if (!backup.empty())
                CopyFileW(target.c_str(), (backup + L"\\" + name).c_str(), FALSE);
        }
        if (CopyFileW(source.c_str(), target.c_str(), force ? FALSE : TRUE)) copied = true;
    }
    return copied;
#else
    (void)force;
    return false;
#endif
}

} // namespace pulse::app
