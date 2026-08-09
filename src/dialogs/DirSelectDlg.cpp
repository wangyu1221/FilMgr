#include "DirSelectDlg.h"
#include "ScanningDlg.h"
#include "../../Resource.h"
#include "../ScanManager.h"
#include "../db/Database.h"

#include <string>
#include <unordered_set>

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
                    if (ShowOpenDirectoryDialog(hDlg, dirPath, L"请选择包含视频文件的目录"))
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

                    bool includeSubdirs =
                        (IsDlgButtonChecked(hDlg, IDC_CHK_SUBDIR) == BST_CHECKED);

                    std::vector<ScanDir> dirs;
                    dirs.push_back({ g_dlgSelectedPath, includeSubdirs });

                    // Build set of existing file paths for O(1) dedup
                    std::unordered_set<std::string> existingPaths;
                    {
                        db::QueryResult films;
                        g_database.ExecuteQuery("SELECT file_path FROM film;", films);
                        for (const auto& row : films)
                            existingPaths.insert(row.at("file_path"));
                    }

                    int count = ShowScanningDialog(hDlg, dirs, &existingPaths);

                    RefreshListView();

                    if (count < 0)
                    {
                        MessageBoxW(hDlg, L"扫描已取消。", L"提示", MB_ICONINFORMATION);
                    }
                    else
                    {
                        std::wstring msg = L"扫描完成。新增 " +
                                           std::to_wstring(count) + L" 个视频文件。";
                        MessageBoxW(hDlg, msg.c_str(), L"提示", MB_ICONINFORMATION);
                    }
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
