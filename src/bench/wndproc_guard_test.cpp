// wndproc_guard_test.cpp - app::WndProcDepthScope caps message nesting.
#include "../app/wndproc_guard.h"

#include <windows.h>
#include <cstdio>
#include <cwchar>

namespace {
int passed = 0, failed = 0;
void Check(bool value, const wchar_t* name) {
    std::wprintf(L"[%s] %s\n", value ? L"PASS" : L"FAIL", name);
    value ? ++passed : ++failed;
}

int g_deepest = 0;
bool g_refused = false;

// One scope per level, the way WndProcImpl nests: a refused level stops the
// recursion but still unwinds through the destructors of the levels above.
bool Nest(int remaining) {
    pulse::app::WndProcDepthScope scope(WM_APP + 1);
    if (pulse::app::CurrentWndProcDepth() > g_deepest)
        g_deepest = pulse::app::CurrentWndProcDepth();
    if (!scope.allowed()) {
        g_refused = true;
        return false;
    }
    return remaining <= 1 ? true : Nest(remaining - 1);
}
}  // namespace

int main() {
    using pulse::app::CurrentWndProcDepth;
    using pulse::app::kWndProcDepthLimit;
    using pulse::app::PeakWndProcDepth;
    using pulse::app::ResetWndProcDepth;

    ResetWndProcDepth();
    Check(CurrentWndProcDepth() == 0 && PeakWndProcDepth() == 0, L"a fresh thread starts at depth 0");
    Check(kWndProcDepthLimit == 24, L"the cap is the documented 24 layers");

    g_deepest = 0;
    g_refused = false;
    Check(Nest(kWndProcDepthLimit), L"messages up to the cap are accepted");
    Check(!g_refused && g_deepest == kWndProcDepthLimit, L"the deepest accepted message sits at the cap");
    Check(PeakWndProcDepth() == kWndProcDepthLimit, L"the peak records the deepest message");
    Check(CurrentWndProcDepth() == 0, L"unwinding restores the depth");

    g_deepest = 0;
    g_refused = false;
    Check(!Nest(kWndProcDepthLimit + 1), L"one message past the cap is refused");
    Check(g_refused && g_deepest == kWndProcDepthLimit + 1, L"the refused message is still counted while it is on the stack");
    Check(CurrentWndProcDepth() == 0, L"a refused message leaves no count behind");
    g_refused = false;
    Check(Nest(1) && !g_refused, L"the next top-level message is accepted again");

    ResetWndProcDepth();
    (void)Nest(2);
    Check(PeakWndProcDepth() == 2, L"reset forgets the previous peak");
    ResetWndProcDepth();
    Check(CurrentWndProcDepth() == 0 && PeakWndProcDepth() == 0, L"reset clears depth and peak");

    std::wprintf(L"== wndproc guard test: %d passed, %d failed ==\n", passed, failed);
    return failed ? 1 : 0;
}
