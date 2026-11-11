// settings_packs_local.h — Fork-only parts of the 快速预览 page (5).
//
// The 启用预览 master switch with its two indented children is a local redesign
// of upstream's flat 预览方式 section: the gate owns 悬停与空格预览, 详情面板预览
// and the 预览窗口 rows. Its layout and the tinted band live here so
// settings_layout_sections.h and ui_settings_packs.cpp keep a one-call patch
// surface (rules: local/merge-policy.md).
//
// Included inside ui_renderer_internal.h's anonymous namespace (like
// settings_layout_sections.h), so no namespace and no includes of its own: it
// uses the layout struct and the drawing helpers already in scope.

namespace local_pack_text {

inline const wchar_t* MasterTitle() { return l10n::Get(l10n::StringId::PreviewEnableTitle).c_str(); }
inline const wchar_t* MasterDesc() { return l10n::Get(l10n::StringId::PreviewEnableDesc).c_str(); }
inline const wchar_t* HoverChildTitle() { return l10n::Get(l10n::StringId::PreviewHoverSpaceTitle).c_str(); }
inline const wchar_t* HoverChildDesc() { return l10n::Get(l10n::StringId::PreviewHoverSpaceDesc).c_str(); }
inline const wchar_t* DetailsChildTitle() { return l10n::Get(l10n::StringId::PreviewDetailsTitle).c_str(); }
inline const wchar_t* DetailsChildDesc() { return l10n::Get(l10n::StringId::PreviewDetailsDesc).c_str(); }

}  // namespace local_pack_text

namespace local_pack_preview {

// Lays out the gate switch and, on one tinted band, every row it owns: the two
// children, 预览方式 with its hover delay and card size, 按住预览, and 预览窗口.
// `row` is upstream's full-width row helper (it advances the caller's y through
// the reference passed in `y`); `caption_h` measures a wrapped caption.
template <class RowFn, class CaptionFn>
void Layout(SettingsLayout& l, const WindowViewModel& vm, float left, float right, float scale,
            float font, float& y, RowFn&& row, CaptionFn&& caption_h) {
    const bool narrow = right - left < 560*scale;
    auto segments = [&](D2D1_RECT_F r, D2D1_RECT_F* output, int count, float width, bool stack = false) {
        const auto c = D2D1::RectF(narrow || stack ? r.left + 16*scale : r.right - (width + 16)*scale,
            r.bottom - 44*scale, r.right - 16*scale, r.bottom - 12*scale);
        const float w = (c.right - c.left) / static_cast<float>(count);
        for (int i = 0; i < count; ++i)
            output[i] = D2D1::RectF(c.left + w*static_cast<float>(i), c.top, c.left + w*static_cast<float>(i + 1), c.bottom);
    };
    const float h = narrow ? 112.0f : 64.0f + 8.0f*(font - 1.0f)*4.0f;
    // A switch row only holds a title and a caption; narrow pages at a large
    // font wrap that caption to two lines.
    const float switch_h = narrow ? 88.0f : 64.0f;
    const float child_inset = 28.0f*scale;
    float band_top = -1.0f;
    auto child_row = [&](float height) {
        if (band_top < 0.0f) {
            y += 8*scale;
            band_top = y;
        }
        D2D1_RECT_F r = row(height);
        r.left = left + child_inset;
        l.dividers.push_back(D2D1::RectF(r.left + 16*scale, r.top, r.right - 16*scale, r.top));
        return r;
    };
    l.preview_master_row = row(switch_h);
    l.preview_hover_row = child_row(switch_h);
    l.trigger_row = child_row(h);
    segments(l.trigger_row, l.trigger_choice, 2, 220*font);
    if (vm.settings_preview_trigger == 0) {
        l.hover_delay_row = child_row(h);
        segments(l.hover_delay_row, l.hover_delay_choice, 3, 240*font);
        l.hover_size_row = child_row(h);
        segments(l.hover_size_row, l.hover_size_choice, 4, 300*font);
    }
    l.hold_row = child_row(h);
    segments(l.hold_row, l.hold_choice, 4, 300*font);
    l.preview_details_row = child_row(switch_h);
    // 预览窗口: five sizes need more room before they sit beside the text.
    const bool stack_sizes = right - left < 820*scale;
    l.window_section = child_row(28*font);
    l.preview_size_row = child_row(narrow || stack_sizes ? 112.0f : h);
    segments(l.preview_size_row, l.preview_size_choice, 5, 420*font, stack_sizes);
    l.preview_remember_row = child_row((caption_h(pack_text::RememberDesc(), right - left - 126*scale) + 47*scale) / scale);
    l.space_row = child_row(h);
    segments(l.space_row, l.space_choice, 2, 260*font);
    l.loop_row = child_row(h);
    segments(l.loop_row, l.loop_choice, 3, 320*font);
    l.child_bands.push_back(D2D1::RectF(left, band_top, right, y));
    y += 24*scale;
}

// The tinted band and its guide line, then the divider above every row.
inline void DrawBands(ID2D1DeviceContext* dc, const SettingsLayout& l, float scale,
                      ID2D1SolidColorBrush* band, ID2D1SolidColorBrush* guide,
                      ID2D1SolidColorBrush* divider) {
    for (const auto& rc : l.child_bands) {
        dc->FillRoundedRectangle(D2D1::RoundedRect(rc, 8*scale, 8*scale), band);
        FillRoundedRect(dc, guide, rc.left + 29*scale, rc.top + 12*scale, 1.5f*scale,
                        (std::max)(0.0f, rc.bottom - rc.top - 24*scale), 0.75f*scale);
    }
    for (const auto& rc : l.dividers)
        FillRect(dc, divider, rc.left, rc.top, rc.right - rc.left, 1);
}

}  // namespace local_pack_preview
