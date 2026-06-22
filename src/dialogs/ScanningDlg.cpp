#include "ScanningDlg.h"
#include "../../Resource.h"
#include "../ScanManager.h"
#include "../db/Database.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_set>
#include <commctrl.h>

extern db::Database g_database;
extern HWND g_hMainWnd;
extern HINSTANCE hInst;

#define WM_SCAN_DONE  (WM_USER + 200)
#define TIMER_SCAN_ID 1

struct ScanContext
{
    std::vector<ScanDir> dirs;
    std::atomic<bool> cancel{false};
    std::atomic<bool> running{false};
    std::atomic<int> totalFound{0};
    std::mutex mtx;
    std::wstring currentPath;
    std::unordered_set<std::string>* existingPaths = nullptr;
};

static DWORD WINAPI ScanThreadProc(LPVOID param)
{
    ScanContext* ctx = static_cast<ScanContext*>(param);
    ctx->running = true;

    for (const auto& d : ctx->dirs)
    {
        if (ctx->cancel.load())
            break;

        int n = ScanAndInsertFilms(d.path, d.includeSub, ctx->cancel,
                                    ctx->currentPath, ctx->mtx,
                                    ctx->existingPaths, true);
        ctx->totalFound.fetch_add(n);
    }

    ctx->running = false;
    return 0;
}

INT_PTR CALLBACK ScanningDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    ScanContext* ctx = reinterpret_cast<ScanContext*>(
        GetWindowLongPtrW(hDlg, GWLP_USERDATA));

    switch (message)
    {
    case WM_INITDIALOG:
        {
            ctx = static_cast<ScanContext*>(reinterpret_cast<void*>(lParam));
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

            // Set font
            HFONT hFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
            if (hFont)
            {
                SendMessageW(GetDlgItem(hDlg, IDC_SCAN_CUR_PATH), WM_SETFONT, (WPARAM)hFont, TRUE);
            }

            // Start polling timer (100ms)
            SetTimer(hDlg, TIMER_SCAN_ID, 100, nullptr);

            // Start worker thread
            HANDLE hThread = CreateThread(nullptr, 0, ScanThreadProc, ctx, 0, nullptr);
            if (hThread)
                CloseHandle(hThread);

            // Center on parent
            if (g_hMainWnd)
            {
                RECT rcParent, rcDlg;
                GetWindowRect(g_hMainWnd, &rcParent);
                GetWindowRect(hDlg, &rcDlg);
                int dlgW = rcDlg.right - rcDlg.left;
                int dlgH = rcDlg.bottom - rcDlg.top;
                int parentW = rcParent.right - rcParent.left;
                int parentH = rcParent.bottom - rcParent.top;
                int x = rcParent.left + (parentW - dlgW) / 2;
                int y = rcParent.top + (parentH - dlgH) / 2;
                SetWindowPos(hDlg, nullptr, x, y, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        return (INT_PTR)TRUE;

    case WM_TIMER:
        if (wParam == TIMER_SCAN_ID && ctx)
        {
            // Update current path
            {
                std::lock_guard<std::mutex> lk(ctx->mtx);
                SetDlgItemTextW(hDlg, IDC_SCAN_CUR_PATH, ctx->currentPath.c_str());
            }

            // Check if done
            if (!ctx->running.load())
            {
                KillTimer(hDlg, TIMER_SCAN_ID);
                EnableWindow(GetDlgItem(hDlg, IDCANCEL), FALSE);
                EndDialog(hDlg, IDOK);
            }
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL && ctx)
        {
            ctx->cancel.store(true);
            SetDlgItemTextW(hDlg, IDCANCEL, L"正在取消...");
            EnableWindow(GetDlgItem(hDlg, IDCANCEL), FALSE);
        }
        return (INT_PTR)TRUE;

    case WM_DESTROY:
        // ctx is owned by ShowScanningDialog; do not delete here
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
        break;
    }
    return (INT_PTR)FALSE;
}

int ShowScanningDialog(HWND hParent, const std::vector<ScanDir>& dirs,
                       std::unordered_set<std::string>* existingPaths)
{
    if (dirs.empty()) return 0;

    auto* ctx = new ScanContext();
    ctx->dirs = dirs;
    ctx->existingPaths = existingPaths;

    INT_PTR result = DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_SCANNING),
                                      hParent, ScanningDlgProc,
                                      reinterpret_cast<LPARAM>(ctx));

    int total = ctx->totalFound.load();
    if (ctx->cancel.load()) total = -1;
    delete ctx;
    return total;
}
