#include "DirSelectDlg.h"
#include "../../Resource.h"
#include "../ScanManager.h"
#include "../db/Database.h"

#include <string>

extern db::Database g_database;
extern HWND g_hMainWnd;
extern HINSTANCE hInst;
void RefreshListView();

std::wstring g_dlgSelectedPath;

INT_PTR CALLBACK DirSelectDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        {
            g_dlgSelectedPath.clear();
            CheckDlgButton(hDlg, IDC_CHK_SUBDIR, BST_UNCHECKED);

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

    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId)
            {
            case IDC_BTN_SELECT_DIR:
                {
                    std::wstring dirPath;
                    if (ShowOpenDirectoryDialog(hDlg, dirPath))
                    {
                        g_dlgSelectedPath = dirPath;
                        SetDlgItemTextW(hDlg, IDC_DIR_PATH, dirPath.c_str());
                    }
                }
                break;

            case IDC_BTN_START_SCAN:
                {
                    if (g_dlgSelectedPath.empty())
                    {
                        MessageBoxW(hDlg,
                            L"请先选择目录。",
                            L"提示", MB_ICONINFORMATION);
                        return (INT_PTR)TRUE;
                    }

                    HWND hBtnStart = GetDlgItem(hDlg, IDC_BTN_START_SCAN);
                    HWND hStatusText = GetDlgItem(hDlg, IDC_SCAN_STATUS);
                    EnableWindow(hBtnStart, FALSE);

                    bool includeSubdirs =
                        (IsDlgButtonChecked(hDlg, IDC_CHK_SUBDIR) == BST_CHECKED);
                    bool extractSn =
                        (IsDlgButtonChecked(hDlg, IDC_CHK_EXTRACT_SN) == BST_CHECKED);

                    int count = ScanAndInsertFilms(g_dlgSelectedPath, includeSubdirs,
                        hStatusText, extractSn);

                    SetWindowTextW(hStatusText, L"");
                    EnableWindow(hBtnStart, TRUE);
                    RefreshListView();

                    std::wstring msg = L"扫描完成。新增 " +
                                       std::to_wstring(count) + L" 个视频文件。";
                    MessageBoxW(hDlg, msg.c_str(), L"提示", MB_ICONINFORMATION);
                    EndDialog(hDlg, IDOK);
                }
                break;

            case IDCANCEL:
                EndDialog(hDlg, IDCANCEL);
                break;
            }
        }
        break;
    }
    return (INT_PTR)FALSE;
}
