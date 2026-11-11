// ui_details_panel_local.h — Fork-only details panel helpers (rules: local/merge-policy.md).
//
// The local redesign of the panel (此电脑 card, folder card, 下载并预览 well) needs
// a handful of measurement/layout helpers that used to live in
// ui_renderer_internal.h and ui_details_panel.cpp. They are moved here verbatim
// (comments included) so both upstream files keep a small patch surface.
//
// Included inside ui_renderer_internal.h's anonymous namespace (like
// settings_packs_local.h), after the MeasureTextWidth declaration the fact
// helpers call: no namespace and no includes of its own, everything below is in
// scope. ui_details_panel.cpp includes that header first, so these names are
// visible there as well.

#pragma once

// One line of details text broken at the last space that fits; prefers a
// balanced second line over a lone orphan word ("... Laptop" / "GPU"). Returns
// {text, ""} when it already fits. Used by the 此电脑 card's identity block and
// by fact values that are too long for their column.
inline std::pair<std::wstring, std::wstring> SplitDetailLine(const std::wstring& text, float width,
                                                             IDWriteFactory2* factory,
                                                             IDWriteTextFormat* fmt) {
    std::pair<std::wstring, std::wstring> out{text, std::wstring()};
    if (!factory || !fmt || text.empty() || width <= 1.0f) return out;
    const auto measure = [&](const std::wstring& value) {
        return MeasureTextWidth(factory, fmt, value);
    };
    if (measure(text) <= width) return out;
    size_t cut = text.find_last_of(L' ');
    while (cut != std::wstring::npos && cut > 0 && measure(text.substr(0, cut)) > width)
        cut = cut > 1 ? text.find_last_of(L' ', cut - 1) : std::wstring::npos;
    if (cut == std::wstring::npos || cut == 0) {
        // No usable space (CJK, a hyphenated model name): cut by character.
        size_t lo = 1, hi = text.size();
        while (lo < hi) {
            const size_t mid = (lo + hi + 1) / 2;
            if (measure(text.substr(0, mid)) <= width) lo = mid; else hi = mid - 1;
        }
        cut = lo;
    } else {
        const std::wstring head = text.substr(0, cut);
        const std::wstring tail = cut + 1 < text.size() ? text.substr(cut + 1) : std::wstring();
        const size_t prev = head.find_last_of(L' ');
        if (!tail.empty() && tail.find(L' ') == std::wstring::npos &&
            prev != std::wstring::npos && prev > 0 &&
            measure(tail) * 2.5f < measure(head) && measure(text.substr(prev)) <= width)
            cut = prev;
    }
    std::wstring second = text.substr(cut);
    while (!second.empty() && second.front() == L' ') second.erase(second.begin());
    out.first = text.substr(0, cut);
    while (!out.first.empty() && out.first.back() == L' ') out.first.pop_back();
    out.second = std::move(second);
    return out;
}

// Widest key plus a gap: every value starts at the same x. Clamped so a
// long-worded language cannot eat the values.
inline float DetailsFactKeyWidth(const DetailsPanelView& d, float content_w, float scale,
                                 IDWriteFactory2* factory, IDWriteTextFormat* fmt) {
    float key_w = 0.0f;
    if (factory && fmt) {
        for (const auto& row : d.context_rows)
            key_w = (std::max)(key_w, MeasureTextWidth(factory, fmt, row.first));
    }
    return (std::min)(key_w + 16.0f * scale, content_w * 0.40f);
}

// A CPU or GPU name that does not fit takes a second line instead of being
// cut mid-word. Draw and layout both derive the row height from this.
inline float DetailsFactRowHeight(const std::wstring& value, float value_w, float scale,
                                  IDWriteFactory2* factory, IDWriteTextFormat* fmt) {
    if (!factory || !fmt || value.empty()) return kDetailsFactRowDip * scale;
    const float avail = (std::max)(1.0f, value_w - 2.0f * scale);
    return MeasureTextWidth(factory, fmt, value) <= avail
        ? kDetailsFactRowDip * scale
        : (kDetailsFactRowDip + kDetailsFactLineDip) * scale;
}

inline float DetailsFactRowsHeight(const DetailsPanelView& d, float value_w, float scale,
                                   IDWriteFactory2* factory, IDWriteTextFormat* fmt) {
    float total = 0.0f;
    for (const auto& row : d.context_rows)
        total += DetailsFactRowHeight(row.second, value_w, scale, factory, fmt);
    return total;
}

// A folder card (the folder the tab shows, nothing selected) keeps the fact
// sections and drops the ones whose controls act on a selection: but there
// the target is the folder itself, so the 标签 chips, 属性 toggles and the
// 安全 更改 button would act on what is already open.
inline bool DetailsCardShowsSection(int id) { return id == 0 || id == 4; }

// 包含: a folder states its file/folder count only when a size walk produced
// one; folder sizes are never worked out on their own, so most folders have
// no count to show and the row is left out of both layout and drawing.
inline bool DetailsShowsContains(const DetailsPanelView& d) {
    return d.is_dir && !d.contains_text.empty();
}

// 下载并预览 button: centred near the bottom of the preview well; the
// explanation sits above it. Empty when the well is too short for both.
inline D2D1_RECT_F DetailsCloudButtonRect(const D2D1_RECT_F& preview, float s, float label_w) {
    if (preview.bottom - preview.top < 132.0f * s) return {};
    const float w = std::min(preview.right - preview.left - 24.0f * s, label_w + 16.0f * s + 6.0f * s + 32.0f * s);
    if (w < 60.0f * s) return {};
    const float cx = (preview.left + preview.right) * 0.5f;
    const float bottom = preview.bottom - 16.0f * s;
    return D2D1::RectF(std::round(cx - w * 0.5f), bottom - 30.0f * s, std::round(cx + w * 0.5f), bottom);
}
