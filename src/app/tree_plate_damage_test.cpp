#if defined(PULSE_WITH_SELFTEST) || defined(PULSE_TREE_PLATE_STANDALONE)
#include "../ui/ui_renderer_internal.h"
#include "../ui/ui_motion.h"
#include "../common/windows_compat.h"
#include <filesystem>
#include <fstream>
#include <memory>

namespace pulse::ui {

// The details tree needs every visible row sampled each frame so
// ListShiftMotion can diff one whole frame against the next. A plate-only
// frame samples just the hovered row, so the next small list change (an
// inline folder expand) reads every row as new and flashes the pane; panes
// drawing tree guides therefore take whole frames while plates glide
// (HoverMotionDamage). These checks pin that rule and keep plate-only
// frames working everywhere else.
struct TreePlateDamageTest {
    static bool Run() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) return false;
        compat::EnableDpiAwareness();
        l10n::Initialize(GetModuleHandleW(nullptr), L"zh-CN");
        const auto root = std::filesystem::current_path() / L"bench_data" / L"tree-plate-damage";
        std::filesystem::create_directories(root);
        std::ofstream log(root / L"results.log");
        bool ok = true;
        auto check = [&](bool passed, const char* name) {
            log << (passed ? "[PASS] " : "[FAIL] ") << name << std::endl;
            ok &= passed;
        };
        if (!motion::SystemAnimationsEnabled()) {
            log << "[SKIP] window animations are off; the plate snaps instead of gliding" << std::endl;
            CoUninitialize();
            return true;
        }
        HWND hwnd = CreateWindowExW(0, L"STATIC", L"Tree plate damage", WS_POPUP,
            0, 0, 800, 500, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        {
            Compositor compositor;
            if (!hwnd || !compositor.Init(hwnd)) {
                if (hwnd) DestroyWindow(hwnd);
                log << "[FAIL] compositor init" << std::endl;
                CoUninitialize();
                return false;
            }
            MainRenderer renderer;
            renderer.SetCompositor(&compositor);
            compositor.Resize(800, 500);
            compositor.RecreateTextFormats(1.0f);
            renderer.SetScale(1.0f);
            const auto theme = MakeTheme(false, HexColor(0x0078D4));
            renderer.UpdateBrushes(theme);
            renderer.text_background_ = theme.bg;
            renderer.icon_cache_.SetDeviceContext(compositor.Dc());
            renderer.painter_.BeginFrame(theme, false);

            auto make_pane = [](const wchar_t* tag, std::shared_ptr<const DetailsTreeRows> tree) {
                PaneViewModel p;
                p.path = std::wstring(L"C:\\") + tag;
                p.header_text = tag;
                p.view_mode = ViewMode::Details;
                for (int i = 0; i < 3; ++i) {
                    ListEntryView e;
                    e.name = std::wstring(tag) + L" " + std::to_wstring(i);
                    e.is_dir = true;
                    e.path = p.path + L"\\" + e.name;
                    p.entries.push_back(std::move(e));
                }
                p.tree = std::move(tree);
                return p;
            };
            auto tree_rows = [] {
                auto rows = std::make_shared<DetailsTreeRows>();
                DetailsTreeRow folder; folder.depth = 0; folder.expandable = true;
                DetailsTreeRow child;  child.depth = 1; child.expandable = true;
                DetailsTreeRow file;   file.depth = 0;
                rows->rows = {folder, child, file};
                rows->view_of_source = {0, 1, 2};
                return rows;
            };
            auto draw = [&](PaneViewModel& p, int pane_index, int hover_source) {
                p.hover_index = hover_source;
                WindowViewModel vm;
                vm.dark = false;
                renderer.motion_now_ = motion::NowMs();
                ++renderer.motion_frame_;
                auto* dc = compositor.Dc();
                dc->BeginDraw();
                dc->Clear(theme.bg);
                renderer.DrawSinglePane(vm, p, D2D1::RectF(8, 8, 792, 492), pane_index, true, false, theme);
                return SUCCEEDED(dc->EndDraw());
            };
            // The plate spawns on the first frame and glides to the next row
            // on the second; right after, HoverMotionDamage is read the way
            // WM_FRAME_PUMP reads it before marking a plate-only frame.
            auto plate_only_damage = [&](PaneViewModel& p, int pane_index) {
                D2D1_RECT_F span{};
                return draw(p, pane_index, 0) && draw(p, pane_index, 1) &&
                       renderer.HoverMotionDamage(&span);
            };

            PaneViewModel plain = make_pane(L"plain", nullptr);
            check(plate_only_damage(plain, 0),
                  "a plain pane still reports plate-only damage while its plate glides");

            PaneViewModel tree = make_pane(L"tree", tree_rows());
            check(!plate_only_damage(tree, 0),
                  "a pane drawing tree guides takes whole frames while its own plate glides");

            // Nothing expanded: every row is depth 0 and no guide line is
            // drawn, so the pane must keep the plate-only path. This is the
            // details view of an ordinary folder, the common case.
            auto flat_rows = [] {
                auto rows = std::make_shared<DetailsTreeRows>();
                for (int i = 0; i < 3; ++i) {
                    DetailsTreeRow row;
                    row.depth = 0;
                    row.expandable = true;
                    rows->rows.push_back(row);
                }
                rows->view_of_source = {0, 1, 2};
                return rows;
            };
            PaneViewModel flat = make_pane(L"flat", flat_rows());
            check(plate_only_damage(flat, 0),
                  "a details pane with nothing expanded draws no guide, so its plate keeps plate-only frames");

            // Split layout: the plate glides in the plain pane while nobody
            // hovers the tree pane, whose rows the damage test skips all the same.
            PaneViewModel split_tree = make_pane(L"split tree", tree_rows());
            PaneViewModel split_plain = make_pane(L"split plain", nullptr);
            const bool frames_drawn = draw(split_tree, 0, -1) && draw(split_plain, 1, 0) &&
                                      draw(split_plain, 1, 1);
            D2D1_RECT_F span{};
            check(frames_drawn && !renderer.HoverMotionDamage(&span),
                  "a tree pane takes whole frames while another pane's plate glides");
        }
        if (hwnd) DestroyWindow(hwnd);
        CoUninitialize();
        return ok;
    }
};

} // namespace pulse::ui

namespace pulse::app {
bool RunTreePlateDamageTest() {
    return pulse::ui::TreePlateDamageTest::Run();
}
}

#ifdef PULSE_TREE_PLATE_STANDALONE
int wmain() {
    return pulse::app::RunTreePlateDamageTest() ? 0 : 1;
}
#endif
#endif // PULSE_WITH_SELFTEST || PULSE_TREE_PLATE_STANDALONE
