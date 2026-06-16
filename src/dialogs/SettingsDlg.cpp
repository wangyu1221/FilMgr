#include "SettingsDlg.h"
#include "../../Resource.h"
#include "../ConfigManager.h"
#include "../AppUtils.h"
#include "../db/Database.h"

#include <commctrl.h>
#include <shobjidl.h>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <set>
#include <vector>
#include <string>
#include <algorithm>

extern db::Database g_database;
extern HWND g_hMainWnd;
extern HINSTANCE hInst;

static std::vector<HWND> s_videoCheckboxes;
static std::wstring s_playerPath;

static void Settings_ShowVideoTab(HWND hDlg)
{
    for (auto h : s_videoCheckboxes)
        ShowWindow(h, SW_SHOW);
    ShowWindow(GetDlgItem(hDlg, IDC_SETTING_PLAYER_BTN), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_SETTING_PLAYER_PATH), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_SETTING_TYPE_LABEL), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_SETTING_PLAYER_TYPE), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_SETTING_JUMP_AD), SW_HIDE);
}

static void Settings_ShowPlayerTab(HWND hDlg)
{
    for (auto h : s_videoCheckboxes)
        ShowWindow(h, SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_SETTING_PLAYER_BTN), SW_SHOW);
    ShowWindow(GetDlgItem(hDlg, IDC_SETTING_PLAYER_PATH), SW_SHOW);
    ShowWindow(GetDlgItem(hDlg, IDC_SETTING_TYPE_LABEL), SW_SHOW);
    ShowWindow(GetDlgItem(hDlg, IDC_SETTING_PLAYER_TYPE), SW_SHOW);
    // Show jumpAd only if PotPlayer selected
    HWND hType = GetDlgItem(hDlg, IDC_SETTING_PLAYER_TYPE);
    int sel = static_cast<int>(SendMessageW(hType, CB_GETCURSEL, 0, 0));
    ShowWindow(GetDlgItem(hDlg, IDC_SETTING_JUMP_AD), (sel == 0) ? SW_SHOW : SW_HIDE);
}

static void Settings_CreateVideoCheckboxes(HWND hDlg)
{
    for (auto h : s_videoCheckboxes)
        DestroyWindow(h);
    s_videoCheckboxes.clear();

    std::set<std::wstring> allExtensions = GetDefaultVideoExtensions();
    std::vector<std::wstring> sorted(allExtensions.begin(), allExtensions.end());
    std::sort(sorted.begin(), sorted.end());

    int colCount = 5;
    int rowHeight = 18;
    int colWidth = 65;
    int startX = 22, startY = 37;

    for (size_t i = 0; i < sorted.size(); ++i)
    {
        int col = static_cast<int>(i) % colCount;
        int row = static_cast<int>(i) / colCount;
        int x = startX + col * colWidth;
        int y = startY + row * rowHeight;

        HWND hCheck = CreateWindowW(L"Button", sorted[i].c_str(),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            x, y, colWidth - 5, 16,
            hDlg, (HMENU)(INT_PTR)(IDC_SETTING_VIDEO_FIRST + static_cast<int>(i)),
            hInst, nullptr);

        HFONT hFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
        if (hFont)
            SendMessageW(hCheck, WM_SETFONT, (WPARAM)hFont, TRUE);

        if (g_videoExtensions.find(sorted[i]) != g_videoExtensions.end())
            SendMessageW(hCheck, BM_SETCHECK, BST_CHECKED, 0);

        s_videoCheckboxes.push_back(hCheck);
    }
}

static void Settings_SelectPlayer(HWND hDlg)
{
    bool comInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

    IFileOpenDialog* pDialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                   IID_IFileOpenDialog,
                                   reinterpret_cast<void**>(&pDialog));

    if (FAILED(hr))
    {
        if (comInit) CoUninitialize();
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hDlg;
        ofn.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
        ofn.lpstrFile = new wchar_t[MAX_PATH];
        ofn.lpstrFile[0] = L'\0';
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

        if (GetOpenFileNameW(&ofn))
        {
            s_playerPath = ofn.lpstrFile;
            SetDlgItemTextW(hDlg, IDC_SETTING_PLAYER_PATH, s_playerPath.c_str());
        }
        delete[] ofn.lpstrFile;
        return;
    }

    COMDLG_FILTERSPEC filters[] = {
        { L"可执行文件 (*.exe)", L"*.exe" },
        { L"所有文件 (*.*)", L"*.*" },
    };
    pDialog->SetFileTypes(2, filters);
    pDialog->SetTitle(L"选择播放器程序");

    hr = pDialog->Show(hDlg);
    if (SUCCEEDED(hr))
    {
        IShellItem* pItem = nullptr;
        hr = pDialog->GetResult(&pItem);
        if (SUCCEEDED(hr))
        {
            LPWSTR pPath = nullptr;
            hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pPath);
            if (SUCCEEDED(hr))
            {
                s_playerPath = pPath;
                SetDlgItemTextW(hDlg, IDC_SETTING_PLAYER_PATH, s_playerPath.c_str());
                CoTaskMemFree(pPath);
            }
            pItem->Release();
        }
    }

    pDialog->Release();
    if (comInit) CoUninitialize();
}

static void Settings_OnOK(HWND hDlg)
{
    std::set<std::wstring> extensions;
    std::set<std::wstring> allExtensions = GetDefaultVideoExtensions();
    std::vector<std::wstring> sorted(allExtensions.begin(), allExtensions.end());
    std::sort(sorted.begin(), sorted.end());

    for (size_t i = 0; i < sorted.size() && i < s_videoCheckboxes.size(); ++i)
    {
        if (SendMessageW(s_videoCheckboxes[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
            extensions.insert(sorted[i]);
    }

    SaveVideoExtensions(g_database, extensions);

    wchar_t pathBuf[MAX_PATH] = {};
    GetDlgItemTextW(hDlg, IDC_SETTING_PLAYER_PATH, pathBuf, MAX_PATH);
    if (pathBuf[0] != L'\0')
        s_playerPath = pathBuf;

    // Save player config JSON via RapidJSON
    HWND hType = GetDlgItem(hDlg, IDC_SETTING_PLAYER_TYPE);
    int playerTypeIdx = static_cast<int>(SendMessageW(hType, CB_GETCURSEL, 0, 0));
    const char* playerType = (playerTypeIdx == 0) ? "potplayer" : "other";
    bool jumpAd = (IsDlgButtonChecked(hDlg, IDC_SETTING_JUMP_AD) == BST_CHECKED);

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.Key("player_type"); writer.String(playerType);
    writer.Key("player_exe_path"); writer.String(WCharToUtf8(s_playerPath).c_str());
    writer.Key("player_config"); writer.StartObject();
    writer.Key(playerType); writer.StartObject();
    writer.Key("jumpAd"); writer.Bool(jumpAd);
    writer.EndObject();
    writer.EndObject();
    writer.EndObject();
    SaveSetting(g_database, "player_config", sb.GetString());

    EndDialog(hDlg, IDOK);
}

INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            // Read from player_config JSON via RapidJSON
            std::string playerCfg = LoadSetting(g_database, "player_config");
            s_playerPath.clear();
            if (!playerCfg.empty())
            {
                rapidjson::Document doc;
                doc.Parse(playerCfg.c_str());
                if (!doc.HasParseError() && doc.IsObject())
                {
                    if (doc.HasMember("player_exe_path") && doc["player_exe_path"].IsString())
                        s_playerPath = Utf8ToWChar(doc["player_exe_path"].GetString());
                }
            }

            HWND hTab = GetDlgItem(hDlg, IDC_SETTING_TAB);
            HFONT hFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
            if (hFont)
                SendMessageW(hTab, WM_SETFONT, (WPARAM)hFont, TRUE);

            TCITEMW tci = {};
            tci.mask = TCIF_TEXT;
            tci.pszText = const_cast<LPWSTR>(L"视频类型");
            SendMessageW(hTab, TCM_INSERTITEM, 0, (LPARAM)&tci);
            tci.pszText = const_cast<LPWSTR>(L"播放器");
            SendMessageW(hTab, TCM_INSERTITEM, 1, (LPARAM)&tci);

            HWND hPlayerBtn = GetDlgItem(hDlg, IDC_SETTING_PLAYER_BTN);
            if (hFont)
                SendMessageW(hPlayerBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hPlayerPath = GetDlgItem(hDlg, IDC_SETTING_PLAYER_PATH);
            SetWindowTextW(hPlayerPath, s_playerPath.c_str());
            if (hFont)
                SendMessageW(hPlayerPath, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Player type combo + jumpAd checkbox
            HWND hTypeCombo = CreateWindowW(L"Combobox", nullptr,
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                25, 94, 120, 80,
                hDlg, (HMENU)(INT_PTR)IDC_SETTING_PLAYER_TYPE, hInst, nullptr);
            const wchar_t* types[] = { L"PotPlayer", L"其他" };
            for (int i = 0; i < 2; ++i)
                SendMessageW(hTypeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(types[i]));
            if (hFont) SendMessageW(hTypeCombo, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Static label
            HWND hTypeLabel = CreateWindowW(L"Static", L"特殊选项",
                WS_CHILD | WS_VISIBLE,
                25, 72, 60, 12,
                hDlg, (HMENU)(INT_PTR)IDC_SETTING_TYPE_LABEL, hInst, nullptr);
            if (hFont) SendMessageW(hTypeLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

            // JumpAd checkbox (initially hidden)
            HWND hJumpAd = CreateWindowW(L"Button", L"跳过广告开始播放",
                WS_CHILD | BS_AUTOCHECKBOX | WS_TABSTOP,
                25, 138, 130, 14,
                hDlg, (HMENU)(INT_PTR)IDC_SETTING_JUMP_AD, hInst, nullptr);
            if (hFont) SendMessageW(hJumpAd, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Load player config via RapidJSON and populate controls
            if (!playerCfg.empty())
            {
                rapidjson::Document doc;
                doc.Parse(playerCfg.c_str());
                if (!doc.HasParseError() && doc.IsObject())
                {
                    if (doc.HasMember("player_type") && doc["player_type"].IsString())
                        SendMessageW(hTypeCombo, CB_SETCURSEL,
                            (strcmp(doc["player_type"].GetString(), "potplayer") == 0) ? 0 : 1, 0);
                    else
                        SendMessageW(hTypeCombo, CB_SETCURSEL, 0, 0);

                    if (doc.HasMember("player_config") && doc["player_config"].IsObject())
                    {
                        int sel = static_cast<int>(SendMessageW(hTypeCombo, CB_GETCURSEL, 0, 0));
                        const char* ptKey = (sel == 0) ? "potplayer" : "other";
                        const auto& cfg = doc["player_config"];
                        if (cfg.HasMember(ptKey) && cfg[ptKey].IsObject()
                            && cfg[ptKey].HasMember("jumpAd") && cfg[ptKey]["jumpAd"].IsBool())
                            CheckDlgButton(hDlg, IDC_SETTING_JUMP_AD,
                                cfg[ptKey]["jumpAd"].GetBool() ? BST_CHECKED : BST_UNCHECKED);
                    }
                    int sel = static_cast<int>(SendMessageW(hTypeCombo, CB_GETCURSEL, 0, 0));
                    ShowWindow(hJumpAd, (sel == 0) ? SW_SHOW : SW_HIDE);
                }
            }
            else
            {
                SendMessageW(hTypeCombo, CB_SETCURSEL, 0, 0);
                ShowWindow(hJumpAd, SW_SHOW);
            }

            Settings_CreateVideoCheckboxes(hDlg);
            Settings_ShowVideoTab(hDlg);

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

    case WM_NOTIFY:
        {
            NMHDR* pnmh = (NMHDR*)lParam;
            if (pnmh->idFrom == IDC_SETTING_TAB && pnmh->code == TCN_SELCHANGE)
            {
                HWND hTab = GetDlgItem(hDlg, IDC_SETTING_TAB);
                int sel = TabCtrl_GetCurSel(hTab);
                if (sel == 0) Settings_ShowVideoTab(hDlg);
                else          Settings_ShowPlayerTab(hDlg);
            }
        }
        break;

    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId)
            {
            case IDC_SETTING_PLAYER_BTN:
                Settings_SelectPlayer(hDlg);
                break;
            case IDOK:
                Settings_OnOK(hDlg);
                break;
            case IDCANCEL:
                EndDialog(hDlg, IDCANCEL);
                break;
            default:
                if (HIWORD(wParam) == CBN_SELCHANGE && wmId == IDC_SETTING_PLAYER_TYPE)
                {
                    HWND hType = GetDlgItem(hDlg, IDC_SETTING_PLAYER_TYPE);
                    int sel = static_cast<int>(SendMessageW(hType, CB_GETCURSEL, 0, 0));
                    HWND hJumpAd = GetDlgItem(hDlg, IDC_SETTING_JUMP_AD);
                    ShowWindow(hJumpAd, (sel == 0) ? SW_SHOW : SW_HIDE);
                }
                break;
            }
        }
        break;

    case WM_DESTROY:
        s_videoCheckboxes.clear();
        break;
    }
    return (INT_PTR)FALSE;
}
