// preview_switch_test.cpp - 启用预览 turned off must stop the preview hosts.
//
// Runs inside a real, isolated Pulse window: it navigates to a fixture folder,
// selects a text file whose details preview needs the decode host, opens Quick
// Look for a second host, then flips the master switch through the settings
// controller (the path a click takes) and counts the Pulse.Preview.exe children
// of this process. It reproduces the promises of the switch: no host survives
// it, and turning it back on previews the selection again.
#ifdef PULSE_WITH_SELFTEST
#include "app_internal.h"
#include "app_commands.h"
#include "app_navigation.h"
#include "app_preview_policy.h"
#include "settings_controller.h"
#include "../ui/ui_renderer.h"
#include <tlhelp32.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

namespace pulse {
namespace {

// Preview hosts this process owns: Pulse.Preview.exe started with us as parent.
int PreviewHosts() {
    int count = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL next = Process32FirstW(snapshot, &entry); next; next = Process32NextW(snapshot, &entry)) {
        if (entry.th32ParentProcessID == GetCurrentProcessId() &&
            _wcsicmp(entry.szExeFile, L"Pulse.Preview.exe") == 0)
            ++count;
    }
    CloseHandle(snapshot);
    return count;
}

// Pumps the window queue: the folder listing and the repaint both arrive as
// messages, and the details preview is only asked for from a painted frame.
bool Pump(int timeout_ms, const std::function<bool()>& done) {
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
    for (;;) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (done()) return true;
        if (GetTickCount64() >= deadline) return done();
        Sleep(15);
    }
}

void Paint(AppState& s) {
    auto* dc = s.compositor.Dc();
    if (!dc) return;
    ui::WindowViewModel vm = BuildVm(s, false);
    const auto window = D2D1::RectF(0, 0, static_cast<float>(s.compositor.Width()),
                                    static_cast<float>(s.compositor.Height()));
    dc->BeginDraw();
    s.renderer.Render(vm, window, ui::MakeTheme(s.darkMode, s.accentColor));
    dc->EndDraw();
}

// Text has no system preview handler here, so the details well and Quick Look
// both decode it in the preview host - the process the switch must stop.
bool WriteFixtureText(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "Pulse preview switch fixture.\r\nSecond line for the details well.\r\n";
    return out.good();
}

}  // namespace
}  // namespace pulse

int RunPreviewSwitchProbe(pulse::AppState& s, const wchar_t* output) {
    using namespace pulse;
    const auto folder = std::filesystem::path(output).parent_path();
    std::filesystem::create_directories(folder);
    std::ofstream log{folder / "preview-switch.log"};
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        log << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
        if (!ok) ++failures;
    };
    const auto fixtures = folder / "preview-switch-fixtures";
    std::filesystem::create_directories(fixtures / "000-sub");
    WriteFixtureText(fixtures / "notes.txt");
    WriteFixtureText(fixtures / "other.txt");

    s.showDetailsPanel = true;
    s.renderer.SetDetailsPanelVisible(true);
    s.renderer.SetDetailsPanelWidth(360.0f);
    NavigateTo(s, fixtures.wstring());
    AppState* window = &s;
    const bool listed = Pump(8000, [window] {
        const app::Tab* tab = ActiveTab(*window);
        return tab && tab->snapshot && !tab->snapshot->empty();
    });
    check(listed, "fixture folder listed");
    auto select_name = [&](const std::wstring& name) {
        app::Tab* current = ActiveTab(s);
        if (!current || !current->snapshot) return false;
        for (size_t i = 0; i < current->snapshot->size(); ++i) {
            if (_wcsicmp((*current->snapshot)[i].name.c_str(), name.c_str()) != 0) continue;
            current->SelectOnly(static_cast<int>(i));
            return true;
        }
        return false;
    };
    const bool selected = listed && select_name(L"notes.txt");
    check(selected, "fixture file selected");

    const bool spawn = Pump(8000, [&] {
        Paint(s);
        return PreviewHosts() > 0;
    });
    log << "hosts after selection=" << PreviewHosts() << '\n';
    check(spawn, "the details preview starts its host for a selected file");

    // A folder's well draws only its icon; the icon-only well of a file has to
    // look the same, so both frames are saved for the pixel check.
    const bool folder_selected = select_name(L"000-sub");
    check(folder_selected, "fixture folder selected");
    for (int i = 0; i < 4; ++i) { Paint(s); Sleep(40); }
    s.compositor.SaveSnapshot((folder / "well-folder.png").c_str());
    select_name(L"notes.txt");

    // Space opens a second preview surface with its own host; the switch has to
    // stop that one too, which is where a closed Quick Look used to linger.
    ToggleQuickPreview(s);
    const bool quick_open = Pump(8000, [&] {
        Paint(s);
        return s.quickPreview.visible() && PreviewHosts() >= 2;
    });
    log << "hosts with Quick Look open=" << PreviewHosts() << '\n';
    check(quick_open, "Quick Look starts its own preview host");

    s.settings.PreviewEnabled(false);
    const bool stopped = Pump(6000, [&] {
        Paint(s);
        return PreviewHosts() == 0;
    });
    log << "hosts after 启用预览 off=" << PreviewHosts()
        << " details=" << DetailsPreviewContentOn(s.appPrefs)
        << " hover=" << HoverSpacePreviewOn(s.appPrefs) << '\n';
    check(!s.appPrefs.preview_enabled, "the master switch is off");
    check(stopped, "turning 启用预览 off stops every preview host");

    check(Pump(1500, [&] { Paint(s); return PreviewHosts() == 0; }),
          "no host comes back while previews stay off");
    for (int i = 0; i < 4; ++i) { Paint(s); Sleep(40); }
    s.compositor.SaveSnapshot((folder / "well-file.png").c_str());

    s.settings.PreviewEnabled(true);
    // The old selection may still be cached (nothing to decode again); a new
    // file proves the hosts restart on demand.
    select_name(L"other.txt");
    const bool restarted = Pump(8000, [&] {
        Paint(s);
        return PreviewHosts() > 0;
    });
    log << "hosts after 启用预览 on=" << PreviewHosts() << '\n';
    check(restarted, "turning 启用预览 back on previews the selection again");
    return failures ? 1 : 0;
}
#endif
