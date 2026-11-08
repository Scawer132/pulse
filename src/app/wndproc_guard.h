// wndproc_guard.h - Bounds how deep window messages may nest on one UI thread.
// A handler can send another message synchronously (ShowWindow, DestroyWindow,
// SetForegroundWindow, menus, OLE drag), and every layer keeps a whole
// WndProcImpl frame plus its handler's: crash report 2026-11-08 had 47 of them
// at ~26 KB each on the 1 MB UI stack. This cap turns that overflow into one
// dropped message that the diagnostics log can name.
#pragma once

namespace pulse::app {

// Deepest nesting WndProcImpl accepts. One layer costs ~26 KB (WndProcImpl +
// HandleMouseMove + HitTest in the dump), so 1 MB / 26 KB is ~39 layers; 24
// leaves room for the frames above the deep chain instead of halving the margin.
constexpr int kWndProcDepthLimit = 24;

// Counts one WndProcImpl entry on this thread; `allowed()` is false when the
// message would nest past the limit. Destruction keeps the count exact on every
// return path, so a normal message pays one increment and one decrement.
class WndProcDepthScope {
public:
    explicit WndProcDepthScope(unsigned int msg) noexcept;
    ~WndProcDepthScope();
    WndProcDepthScope(const WndProcDepthScope&) = delete;
    WndProcDepthScope& operator=(const WndProcDepthScope&) = delete;

    bool allowed() const noexcept { return allowed_; }

private:
    bool allowed_ = true;
};

// Depth of the message being handled (1 at the top level).
int CurrentWndProcDepth() noexcept;
// Deepest depth reached since ResetWndProcDepth; a crash breadcrumb reads it.
int PeakWndProcDepth() noexcept;
// A caught SEH exception skips destructors, so the WndProc filter resets the
// count it inherited instead of trusting the unwinding to do it.
void ResetWndProcDepth() noexcept;

}  // namespace pulse::app
