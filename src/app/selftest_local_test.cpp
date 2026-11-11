// selftest_local_test.cpp — Fork-only --selftest cases (see selftest_1b2_local.h).
//
// These used to sit inside selftest_1b2.cpp. Keeping them here leaves upstream's
// file with one Attach()/RunCase() pair and one call per extra assertion
// (rules: local/merge-policy.md). The local build is the only one that compiles
// this: selftest_1b2.cpp itself is left out of release builds.
#include "selftest_1b2_local.h"

#ifdef PULSE_WITH_SELFTEST

#include "app_internal.h"
#include "details_info.h"
#include "details_meta.h"
#include "system_info.h"
#include "../common/localization.h"
#include "../common/windows_compat.h"
#include "../fs/fs_enum.h"
#include "../ui/ui_renderer.h"
#include <cmath>
#include <cstdarg>
#include <memory>
#include <string>
#include <vector>

namespace pulse::app {

// Defined by tree_plate_damage_test.cpp, which is compiled only for the
// selftest build (same guard as this file).
bool RunTreePlateDamageTest();

} // namespace pulse::app

namespace pulse::app::selftest_local {

namespace {

int* g_pass = nullptr;
int* g_fail = nullptr;
std::FILE* g_log = nullptr;

int Failures() { return g_fail ? *g_fail : 0; }

void LogLine(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    vswprintf_s(buf, fmt, ap);
    va_end(ap);
    wprintf(L"%s", buf);
    if (g_log) {
        fwprintf(g_log, L"%s", buf);
        fflush(g_log);
    }
}

void Check(bool cond, const wchar_t* name) {
    if (!g_pass || !g_fail) {
        wprintf(L"[FAIL] %s (selftest counters not attached)\n", name);
        std::abort();
    }
    ++(cond ? *g_pass : *g_fail);
    LogLine(cond ? L"[PASS] %s\n" : L"[FAIL] %s\n", name);
}

// Details panel: with nothing selected the panel describes the current folder,
// or the machine while the tab sits at 此电脑. Virtual views keep the standard
// empty-state illustration.
bool RunDetailsInfoCardTest() {
    const int before = Failures();
    {
        ui::DetailsPanelView view;
        DetailsInfoInput input;
        input.this_pc = true;
        FillDetailsInfo(input, view);
        Check(view.has_selection && view.context && view.pc_card,
            L"this pc card: uses the selection layout path");
        Check(view.name == l10n::Get(l10n::StringId::ThisPc), L"this pc card: title is 此电脑");
        Check(view.context_rows.size() >= 5, L"this pc card: the system facts are listed");
        bool filled = true;
        for (const auto& row : view.context_rows)
            filled = filled && !row.first.empty() && !row.second.empty();
        Check(filled, L"this pc card: every fact has a label and a value");
        Check(view.subtitle_text == Facts().os_name,
            L"this pc card: OS name heads the card, as the About page shows it");
        Check(!view.pc_meta.empty(), L"this pc card: host and architecture under the OS name");
    }
    {
        ui::DetailsPanelView view;
        DetailsInfoInput input;
        input.folder_path = L"C:\\Users";
        FillDetailsInfo(input, view);
        Check(view.context && !view.pc_card && view.is_dir,
            L"folder card: describes the folder, not the machine");
        Check(view.name == L"Users", L"folder card: leaf name");
        Check(view.has_selection && view.path == L"C:\\Users" && view.multi_count == 1,
            L"folder card: shares the selected-folder layout path");
        Check(view.context_rows.empty(), L"folder card: the fact rows come from the sections");
    }
    {
        ui::DetailsPanelView view;
        DetailsInfoInput input;
        input.folder_path = L"D:\\";
        FillDetailsInfo(input, view);
        Check(view.name == L"D:", L"folder card: a drive root reads as its letter");
    }
    Check(DetailsInfoAppliesTo(L"C:\\Windows"), L"card: a real folder takes the card");
    Check(!DetailsInfoAppliesTo(L""), L"card: an empty path is 此电脑");
    Check(!DetailsInfoAppliesTo(L"pulse:recycle"), L"card: a virtual view keeps the empty state");
    return Failures() == before;
}

// The panel's scroll offset belongs to the subject it describes. Scrolling a
// selected drive and then clearing the selection must show the 此电脑 card from
// the top, not shifted up by the drive's offset (which used to draw the sheet
// over its pinned header until a wheel event clamped the offset).
bool RunDetailsPanelScrollTest() {
    const int before = Failures();
    auto state = std::make_unique<AppState>();
    state->places.persist = false;
    state->appPrefs.persist = false;
    state->isolatedTest = true;
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
        0, 0, 1200, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(hwnd != nullptr, L"details scroll: isolated owner created");
    if (!hwnd) return false;
    state->hwnd = hwnd;
    if (!state->compositor.Init(hwnd)) {
        Check(false, L"details scroll: graphics initialized");
        DestroyWindow(hwnd);
        return false;
    }
    state->compositor.RecreateTextFormats(1.0f);
    state->compositor.Resize(1200, 800);
    state->renderer.SetCompositor(&state->compositor);
    state->showDetailsPanel = true;
    state->renderer.SetDetailsPanelVisible(true);
    // The card, not the background size walk, is under test.
    state->appPrefs.folder_size_auto = false;
    state->window_tabs.NewTab(L"C:\\");
    state->pane = state->window_tabs.Active()->panes.front().get();
    app::Tab* tab = state->pane ? state->pane->ActiveTab() : nullptr;
    Check(tab != nullptr, L"details scroll: tab created");
    if (tab) {
        tab->current_path.clear();   // 此电脑
        auto entries = std::make_shared<std::vector<fs::DirEntry>>(1);
        auto& drive = (*entries)[0];
        drive.name = L"Windows-SSD (C:)";
        drive.full_path = L"pulse:test-drive";   // virtual: no metadata probe
        drive.is_dir = true;
        drive.attrs = FILE_ATTRIBUTE_DIRECTORY;
        tab->SetSnapshot(entries);
        tab->loading = false;

        auto vm = BuildVm(*state);
        Check(vm.details.pc_card && vm.details.context,
            L"details scroll: 此电脑 shows the system card");
        tab->SelectOnly(0);
        vm = BuildVm(*state);
        Check(vm.details.has_selection && vm.details.path == L"pulse:test-drive",
            L"details scroll: the selected drive is the panel's subject");
        // 文件夹大小 off: the blue 计算大小 text is what the size row shows; a dash
        // drawn under it reads as two values at once.
        Check(vm.details.size_manual && vm.details.size_text.empty() &&
                  !vm.details.size_pending,
              L"details panel: 计算大小 replaces the size row, without a dash");
        state->detailsScroll = 240.0f;
        vm = BuildVm(*state);
        Check(std::fabs(vm.details.scroll_y - 240.0f) < 0.01f,
            L"details scroll: the same subject keeps its scroll offset");
        tab->ClearSelection();
        vm = BuildVm(*state);
        Check(vm.details.pc_card && std::fabs(vm.details.scroll_y) < 0.01f &&
              state->detailsScroll == 0.0f,
            L"details scroll: clearing the selection starts the card at the top");
    }
    state->renderer.SetCompositor(nullptr);
    state->compositor.Shutdown();
    DestroyWindow(hwnd);
    state->hwnd = nullptr;
    return Failures() == before;
}

// A crash-loop safe-mode start is a property of the run, not of the saved
// configuration: it draws without the wallpaper and heavy effect but must
// leave app.json alone, or the next normal start finds the appearance gone.
bool RunSafeModeAppearanceTest() {
    const int before = Failures();
    auto state = std::make_unique<AppState>();
    state->places.persist = false;
    state->appPrefs.persist = false;
    state->isolatedTest = true;
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
        0, 0, 1200, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(hwnd != nullptr, L"safe mode: isolated owner created");
    if (!hwnd) return false;
    state->hwnd = hwnd;
    if (!state->compositor.Init(hwnd)) {
        Check(false, L"safe mode: graphics initialized");
        DestroyWindow(hwnd);
        return false;
    }
    state->compositor.RecreateTextFormats(1.0f);
    state->compositor.Resize(1200, 800);
    state->renderer.SetCompositor(&state->compositor);
    state->window_tabs.NewTab(L"C:\\");
    state->pane = state->window_tabs.Active()->panes.front().get();
    state->appPrefs.background_image = L"C:\\wall.png";
    state->appPrefs.window_effect = L"mica-alt";

    state->safeMode = true;
    auto vm = BuildVm(*state, false);
    Check(vm.background_image.empty() && vm.window_effect == ui::WindowEffect::None,
        L"safe mode: the run draws without the wallpaper and window effect");
    Check(state->appPrefs.background_image == L"C:\\wall.png" &&
          state->appPrefs.window_effect == L"mica-alt",
        L"safe mode: the saved appearance is left untouched");
    state->safeMode = false;
    vm = BuildVm(*state, false);
    Check(vm.background_image == L"C:\\wall.png" &&
          vm.window_effect == ui::WindowEffect::MicaAlt,
        L"safe mode: the next normal start still has the user's appearance");

    state->renderer.SetCompositor(nullptr);
    state->compositor.Shutdown();
    DestroyWindow(hwnd);
    state->hwnd = nullptr;
    return Failures() == before;
}

} // namespace

void Attach(int* pass, int* fail, std::FILE* log) {
    g_pass = pass;
    g_fail = fail;
    g_log = log;
}

int RunCase(const wchar_t* name) {
    if (wcscmp(name, L"details-info-card") == 0) return RunDetailsInfoCardTest() ? 0 : 1;
    if (wcscmp(name, L"details-panel-scroll") == 0) return RunDetailsPanelScrollTest() ? 0 : 1;
    if (wcscmp(name, L"safe-mode-appearance") == 0) return RunSafeModeAppearanceTest() ? 0 : 1;
    if (wcscmp(name, L"tree-plate-damage") == 0) return RunTreePlateDamageTest() ? 0 : 1;
    return -1;
}

void CheckVolumeMeta() {
    // A volume root names its volume and carries 已用; a folder inside it names
    // the same volume, which is what the details panel compares to decide
    // between the volume's used space and a plain folder.
    const auto root_meta = FetchDetailsMeta(L"C:\\");
    Check(root_meta.volume_root == L"C:", L"meta: a drive root reports its volume root");
    Check(root_meta.used.starts_with(l10n::Get(l10n::StringId::Used_2)),
          L"meta: a volume root carries 已用");
    const auto inside_meta = FetchDetailsMeta(L"C:\\Windows");
    Check(inside_meta.volume_root == L"C:",
          L"meta: a folder reports the volume root it lives on");
    Check(DetailsPathIsVolumeRoot(L"C:\\", L"C:"),
          L"meta: a drive root is its volume's root");
    Check(DetailsPathIsVolumeRoot(L"\\\\?\\C:\\", L"C:"),
          L"meta: the \\\\?\\ spelling is the same volume root");
    Check(!DetailsPathIsVolumeRoot(L"C:\\Windows", L"C:"),
          L"meta: a folder inside the volume is not its root");
}

void CheckIconOnlyWell() {
    // 启用预览 › 详情面板预览 off: the well keeps its band (and the file-type icon
    // drawn there) but offers nothing that would read the file, including the
    // download-and-preview button a cloud-only file shows by default.
    ui::MainRenderer renderer;
    renderer.SetScale(1.0f);
    renderer.SetDetailsPanelVisible(true);
    renderer.SetDetailsPanelWidth(360.0f);
    const auto window = D2D1::RectF(0, 0, 1400.0f, 720.0f);
    const auto panel = renderer.DetailsPanelRect(window.right, window.bottom);
    const float panel_cx = (panel.left + panel.right) * 0.5f;
    ui::WindowViewModel vm;
    vm.details_visible = true;
    vm.details.has_selection = true;
    vm.details.multi_count = 1;
    vm.details.cloud_online = true;
    auto cloud_reachable = [&] {
        for (float yy = panel.top; yy < panel.bottom; yy += 4.0f)
            for (float xx = panel.left + 20.0f; xx < panel.right - 10.0f; xx += 4.0f)
                if (renderer.HitTest(vm, window, xx, yy).region ==
                    ui::HitTestResult::DetailsCloudDownload) return true;
        return false;
    };
    const bool cloud_button = cloud_reachable();
    vm.details.preview_icon_only = true;
    const bool well_kept = renderer.HitTest(vm, window, panel_cx, panel.top + 20.0f).region ==
        ui::HitTestResult::DetailsPreview;
    Check(cloud_button && well_kept && !cloud_reachable(),
        L"preview layout: icon-only keeps the well and drops every content action in it");
}

} // namespace pulse::app::selftest_local

#endif  // PULSE_WITH_SELFTEST
