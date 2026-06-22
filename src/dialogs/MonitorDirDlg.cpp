#include "MonitorDirDlg.h"
#include "AddMonitorDirDlg.h"
#include "../../Resource.h"
#include "../AppUtils.h"
#include "../ConfigManager.h"
#include "../db/Database.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <commctrl.h>
#include <vector>
#include <string>

extern db::Database g_database;
extern HWND g_hMainWnd;
extern HINSTANCE hInst;

struct MonitorDirItem
{
    std::wstring dir;
    bool includeSub = true;
};

static std::vector<MonitorDirItem> s_monitorDirs;
static HWND s_hListView = nullptr;

// Save all directories to setting table as JSON array
static void SaveMonitorDirs()
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartArray();
    for (const auto& item : s_monitorDirs)
    {
        writer.StartObject();
        writer.Key("dir");
        writer.String(WCharToUtf8(item.dir).c_str());
        writer.Key("include_sub");
        writer.Int(item.includeSub ? 1 : 0);
        writer.EndObject();
    }
    writer.EndArray();
    SaveSetting(g_database, "monitor_dirs", sb.GetString());
}

// Load directories from setting table JSON array
static void LoadMonitorDirs()
{
    s_monitorDirs.clear();
    std::string json = LoadSetting(g_database, "monitor_dirs");
    if (json.empty()) return;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError()) return;

    // Support both old format (string array) and new format (object array)
    if (doc.IsArray())
    {
        for (rapidjson::SizeType i = 0; i < doc.Size(); ++i)
        {
            MonitorDirItem item;
            if (doc[i].IsString())
            {
                // Old format: ["c:\\dir1", "c:\\dir2"]
                item.dir = Utf8ToWChar(doc[i].GetString());
                item.includeSub = true;
            }
            else if (doc[i].IsObject())
            {
                // New format: {"dir":"...", "include_sub":1}
                if (doc[i].HasMember("dir") && doc[i]["dir"].IsString())
                    item.dir = Utf8ToWChar(doc[i]["dir"].GetString());
                if (doc[i].HasMember("include_sub") && doc[i]["include_sub"].IsInt())
                    item.includeSub = (doc[i]["include_sub"].GetInt() != 0);
                else
                    item.includeSub = true;
            }
            if (!item.dir.empty())
                s_monitorDirs.push_back(std::move(item));
        }
    }
}

// Refresh the ListView from s_monitorDirs
static void RefreshMonitorListView(HWND hListView)
{
    ListView_DeleteAllItems(hListView);

    for (size_t i = 0; i < s_monitorDirs.size(); ++i)
    {
        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = static_cast<int>(i);
        lvi.iSubItem = 0;
        lvi.pszText = const_cast<LPWSTR>(s_monitorDirs[i].dir.c_str());
        int idx = ListView_InsertItem(hListView, &lvi);

        // Second column: subdirectory status
        const wchar_t* status = s_monitorDirs[i].includeSub ? L"含子目录" : L"不含子目录";
        ListView_SetItemText(hListView, idx, 1, const_cast<LPWSTR>(status));
    }
}

// Delete the selected directory from the list
static void MonitorDir_DelDir(HWND hDlg, HWND hListView)
{
    int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(s_monitorDirs.size()))
    {
        MessageBoxW(hDlg, L"请先选中要删除的目录。", L"提示", MB_ICONINFORMATION);
        return;
    }

    s_monitorDirs.erase(s_monitorDirs.begin() + sel);
    SaveMonitorDirs();
    RefreshMonitorListView(hListView);
}

INT_PTR CALLBACK MonitorDirDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        {
            s_hListView = GetDlgItem(hDlg, IDC_MONITOR_LISTVIEW);

            // Set ListView style: two columns without visible header, full row select
            ListView_SetExtendedListViewStyle(s_hListView,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

            // Insert two columns (no header text)
            LVCOLUMNW lvc = {};
            lvc.mask = LVCF_WIDTH | LVCF_SUBITEM;
            lvc.iSubItem = 0;
            lvc.cx = 300;
            ListView_InsertColumn(s_hListView, 0, &lvc);

            lvc.iSubItem = 1;
            lvc.cx = 80;
            ListView_InsertColumn(s_hListView, 1, &lvc);

            // Set font for child controls
            HFONT hFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
            if (hFont)
            {
                SendMessageW(GetDlgItem(hDlg, IDC_MONITOR_ADD_BTN), WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessageW(GetDlgItem(hDlg, IDC_MONITOR_DEL_BTN), WM_SETFONT, (WPARAM)hFont, TRUE);
            }

            // Load saved directories
            LoadMonitorDirs();
            RefreshMonitorListView(s_hListView);

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

    case WM_SIZE:
        {
            if (s_hListView)
            {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);
                // ListView fills the dialog, leaving room for controls at bottom
                MoveWindow(s_hListView, 7, 7, width - 14, height - 52, TRUE);

                HWND hAdd = GetDlgItem(hDlg, IDC_MONITOR_ADD_BTN);
                HWND hDel = GetDlgItem(hDlg, IDC_MONITOR_DEL_BTN);
                if (hAdd) MoveWindow(hAdd, 12, height - 38, 72, 22, TRUE);
                if (hDel) MoveWindow(hDel, 94, height - 38, 72, 22, TRUE);

                // Adjust ListView column widths: col0 takes remaining space, col1 fixed 80
                int lvWidth = width - 14;
                ListView_SetColumnWidth(s_hListView, 0, lvWidth - 84);
                ListView_SetColumnWidth(s_hListView, 1, 80);
            }
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId)
            {
            case IDC_MONITOR_ADD_BTN:
                {
                    INT_PTR result = DialogBox(hInst,
                        MAKEINTRESOURCE(IDD_ADD_MONITOR_DIR), hDlg, AddMonitorDirDlgProc);
                    if (result == IDOK)
                    {
                        LoadMonitorDirs();
                        RefreshMonitorListView(s_hListView);
                    }
                }
                break;

            case IDC_MONITOR_DEL_BTN:
                MonitorDir_DelDir(hDlg, s_hListView);
                break;

            case IDOK:
            case IDCANCEL:
                EndDialog(hDlg, wmId);
                break;
            }
        }
        break;
    }
    return (INT_PTR)FALSE;
}