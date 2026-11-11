// ui_hit_test_local.h — Fork-only hit testing kept out of ui_hit_test.cpp.
//
// Include this right after "ui_renderer_internal.h" in the translation unit that
// defines MainRenderer::HitTest: the layout struct read here (SettingsLayout) is
// declared there. Everything is inline and reaches no internal-linkage helper, so
// it stays a header rather than a .cpp of its own; upstream's HitTest keeps one
// short call per hook and new local rows are added in this file — not inside its
// if-chains (rules: local/merge-policy.md).
//
// Selftests hit-test a renderer that owns no compositor, so every helper called
// from HitTest must tolerate a null one: HitTest passes null for the DWrite
// factory and format when compositor_ is unset. This note used to sit in
// ui_hit_test.cpp and was the one hunk 1.0.60 conflicted on; it lives here now,
// where it cannot clash with upstream.

#pragma once

namespace pulse::ui::local_hit {

// Same test as the layout helpers' ContainsPt, repeated here because those have
// internal linkage: an inline function reaching into them would not be the same
// definition in every translation unit.
inline bool RectContains(const D2D1_RECT_F& rc, float x, float y) {
    return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}

// Preview switches on the pack page. 启用预览 is the gate: its children (悬停与
// 空格预览, 详情面板预览) and the hover options they own are faded and inert while
// it is off, and the hover options also need 悬停与空格预览 itself. 预览窗口 rows
// belong to the gate only: they fade with it but not with 悬停与空格预览.
inline bool PackActionEnabled(PackAction action, const WindowViewModel& vm) {
    const int value = static_cast<int>(action);
    const bool preview_child = action == PackAction::PreviewHoverSpace ||
        action == PackAction::PreviewDetails;
    const bool hover_option =
        (value >= static_cast<int>(PackAction::TriggerHover) &&
         value <= static_cast<int>(PackAction::HoverSizeXLarge)) ||
        (value >= static_cast<int>(PackAction::HoldOff) &&
         value <= static_cast<int>(PackAction::HoldLong));
    const bool window_option =
        (value >= static_cast<int>(PackAction::PreviewSizeFit) &&
         value <= static_cast<int>(PackAction::PreviewRememberSize)) ||
        (value >= static_cast<int>(PackAction::SpaceClose) &&
         value <= static_cast<int>(PackAction::LoopNone));
    if (!vm.settings_preview_enabled && (preview_child || hover_option || window_option)) return false;
    if (!vm.settings_preview_hover_space && hover_option) return false;
    return true;
}

// Local settings rows upstream does not draw: returns the toggle id, or -1.
// New local rows are added here instead of another branch in HitTest.
inline int SettingsToggleIndex(const SettingsLayout& lay, float x, float y) {
    if (RectContains(lay.folder_size_row, x, y)) return 82;
    return -1;
}

// Local controls in the details panel well: 下载并预览 and 计算大小.
inline bool DetailsControlHit(const DetailsHitRects& hit, float x, float y, HitTestResult* r) {
    if (RectContains(hit.cloud_download, x, y)) {
        r->region = HitTestResult::DetailsCloudDownload;
        return true;
    }
    if (RectContains(hit.size_calc, x, y)) {
        r->region = HitTestResult::DetailsSizeCalc;
        return true;
    }
    return false;
}

}  // namespace pulse::ui::local_hit
