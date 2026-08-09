#include "AddMonitorDirDlg.h"
#include "../../Resource.h"
#include "../AppUtils.h"
#include "../ConfigManager.h"
#include "../ScanManager.h"
#include "ScanningDlg.h"
#include "../db/Database.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <vector>
#include <string>

extern db::Database g_database;
extern HWND g_hMainWnd;
extern HINSTANCE hInst;
void RefreshListView();

static std::wstring s_selectedDir;

INT_PTR CALLBACK AddMonitorDirDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        {
            s_selectedDir.clear();

            // Set font for child controls
            HFONT hFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
            if (hFont)
            {
                SendMessageW(GetDlgItem(hDlg, IDC_ADD_MONITOR_DIR_PATH), WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessageW(GetDlgItem(hDlg, IDC_ADD_MONITOR_SELECT_BTN), WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessageW(GetDlgItem(hDlg, IDC_ADD_MONITOR_SUBDIR), WM_SETFONT, (WPARAM)hFont, TRUE);
            }

            // Default: include subdirectories checked
            CheckDlgButton(hDlg, IDC_ADD_MONITOR_SUBDIR, BST_CHECKED);

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

    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId)
            {
            case IDC_ADD_MONITOR_SELECT_BTN:
                {
                    std::wstring dirPath;
                    if (ShowOpenDirectoryDialog(hDlg, dirPath, L"请选择要监视的目录"))
                    {
                        s_selectedDir = dirPath;
                        SetDlgItemTextW(hDlg, IDC_ADD_MONITOR_DIR_PATH, dirPath.c_str());
                    }
                }
                break;

            case IDOK:
                {
                    if (s_selectedDir.empty())
                    {
                        MessageBoxW(hDlg, L"请先选择目录。", L"提示", MB_ICONINFORMATION);
                        return (INT_PTR)TRUE;
                    }

                    bool includeSub = (IsDlgButtonChecked(hDlg, IDC_ADD_MONITOR_SUBDIR) == BST_CHECKED);

                    // Load existing monitor_dirs, check duplicate, append new entry
                    std::string json = LoadSetting(g_database, "monitor_dirs");

                    rapidjson::Document doc;
                    if (!json.empty())
                        doc.Parse(json.c_str());

                    // Normalize to array
                    if (!doc.IsArray())
                        doc.SetArray();

                    // Check for duplicate
                    std::string newDirUtf8 = WCharToUtf8(s_selectedDir);
                    for (rapidjson::SizeType i = 0; i < doc.Size(); ++i)
                    {
                        if (doc[i].IsObject() && doc[i].HasMember("dir") && doc[i]["dir"].IsString())
                        {
                            if (newDirUtf8 == doc[i]["dir"].GetString())
                            {
                                MessageBoxW(hDlg, L"该目录已在监视列表中。", L"提示", MB_ICONINFORMATION);
                                return (INT_PTR)TRUE;
                            }
                        }
                    }

                    // Append new entry
                    rapidjson::Value newEntry(rapidjson::kObjectType);
                    rapidjson::Value dirVal(newDirUtf8.c_str(), doc.GetAllocator());
                    newEntry.AddMember("dir", dirVal, doc.GetAllocator());
                    newEntry.AddMember("include_sub", includeSub ? 1 : 0, doc.GetAllocator());
                    doc.PushBack(newEntry, doc.GetAllocator());

                    // Serialize and save
                    rapidjson::StringBuffer sb;
                    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
                    doc.Accept(writer);
                    SaveSetting(g_database, "monitor_dirs", sb.GetString());

                    // Immediately scan the new directory
                    {
                        // Build set of existing file paths for O(1) dedup
                        std::unordered_set<std::string> existingPaths;
                        db::QueryResult films;
                        g_database.ExecuteQuery("SELECT file_path FROM film;", films);
                        for (const auto& row : films)
                            existingPaths.insert(row.at("file_path"));

                        std::vector<ScanDir> dirs;
                        dirs.push_back({ s_selectedDir, includeSub });
                        int found = ShowScanningDialog(hDlg, dirs, &existingPaths);
                        if (found > 0)
                            RefreshListView();
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
