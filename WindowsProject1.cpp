// WindowsProject1.cpp - Application entry point and main window
//

#include "framework.h"
#include "WindowsProject1.h"
#include "Resource.h"
#include "src/db/Database.h"
#include "src/AppUtils.h"
#include "src/ConfigManager.h"
#include "src/ScanManager.h"
#include "src/DialogProcs.h"

#include <commctrl.h>
#include <shellapi.h>
#include "rapidjson/document.h"
#include <string>
#include <sstream>
#include <algorithm>
#include <vector>

#pragma comment(lib, "comctl32.lib")

#define MAX_LOADSTRING 100
#define IDC_LISTVIEW 1001
#define FILTER_BAR_HEIGHT 24

// Global variables:
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
db::Database g_database;
HWND g_hListView = nullptr;
HWND g_hMainWnd = nullptr;

//=============================================================================
// Database initialization
//=============================================================================

static void CreateMovieTables()
{
    // movie table (mosaic/resolution have no NOT NULL, processed is new)
    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS movie (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            name        VARCHAR(256) NOT NULL,
            sn          VARCHAR(32)  NOT NULL DEFAULT '',
            file_path   VARCHAR(256) NOT NULL,
            file_suffix VARCHAR(16)  NOT NULL,
            file_size   BIGINT       NOT NULL DEFAULT 0,
            mosaic      TINYINT,
            resolution  VARCHAR(20)  NOT NULL DEFAULT '',
            duration    BIGINT,
            ad          INT,
            processed   TINYINT      NOT NULL DEFAULT 0,
            valid       TINYINT      NOT NULL DEFAULT 1
        );
    )";
    g_database.Execute(sql);

    g_database.Execute(
        "CREATE INDEX IF NOT EXISTS idx_movie_file_path ON movie(file_path);");
;

    // actor table
    g_database.Execute(R"(
        CREATE TABLE IF NOT EXISTS actor (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name VARCHAR(256) NOT NULL
        );
    )");

    // movie_actor table
    g_database.Execute(R"(
        CREATE TABLE IF NOT EXISTS movie_actor (
            id       INTEGER PRIMARY KEY AUTOINCREMENT,
            movie_id BIGINT NOT NULL,
            actor_id BIGINT NOT NULL
        );
    )");
    g_database.Execute(
        "CREATE INDEX IF NOT EXISTS idx_movie_actor_movie ON movie_actor(movie_id);");
    g_database.Execute(
        "CREATE INDEX IF NOT EXISTS idx_movie_actor_actor ON movie_actor(actor_id);");

    // tag table
    g_database.Execute(R"(
        CREATE TABLE IF NOT EXISTS tag (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name VARCHAR(256) NOT NULL
        );
    )");

    // movie_tag table
    g_database.Execute(R"(
        CREATE TABLE IF NOT EXISTS movie_tag (
            id       INTEGER PRIMARY KEY AUTOINCREMENT,
            movie_id BIGINT NOT NULL,
            tag_id   BIGINT NOT NULL
        );
    )");
    g_database.Execute(
        "CREATE INDEX IF NOT EXISTS idx_movie_tag_movie ON movie_tag(movie_id);");
    g_database.Execute(
        "CREATE INDEX IF NOT EXISTS idx_movie_tag_tag ON movie_tag(tag_id);");
}

static void InitDatabase()
{
    std::string dbPath = GetExeDirectory() + "\\app.db";

    auto status = g_database.Open(dbPath);
    if (status != db::Status::Ok)
    {
        MessageBoxW(nullptr, L"数据库打开失败！", L"错误", MB_ICONERROR);
        return;
    }

    CreateMovieTables();

    // Load video extensions from database settings
    LoadVideoExtensions(g_database);

    // Check all file paths and update valid column; also normalize backslashes
    db::QueryResult allPaths;
    g_database.ExecuteQuery("SELECT id, file_path FROM movie;", allPaths);
    for (const auto& row : allPaths)
    {
        std::string fp = row.at("file_path");
        int valid = IsFileValid(fp) ? 1 : 0;
        // Normalize double backslashes
        size_t bs = 0;
        while ((bs = fp.find("\\\\", bs)) != std::string::npos)
            fp.replace(bs, 2, "\\");
        auto esc = [](const std::string& s) {
            std::string o; for (char c : s) { if (c == '\'') o += "''"; else o += c; } return o;
        };
        g_database.Execute(
            "UPDATE movie SET valid = " + std::to_string(valid) +
            ", file_path = '" + esc(fp) + "' WHERE id = " + row.at("id") + ";");
    }
}

//=============================================================================
// ListView helpers
//=============================================================================

// Parallel arrays for list view item metadata (index = list item index)
static std::vector<int64_t> s_listMovieIds;
static std::vector<int>    s_listValid;

// Sort state
static int  s_sortCol = -1;
static bool s_sortAsc = true;

//=============================================================================

static std::wstring GetMovieTags(int64_t movieId)
{
    std::string sql = "SELECT t.name FROM tag t "
                      "INNER JOIN movie_tag mt ON t.id = mt.tag_id "
                      "WHERE mt.movie_id = " + std::to_string(movieId) + ";";
    db::QueryResult result;
    g_database.ExecuteQuery(sql, result);

    std::wstring tags;
    for (size_t i = 0; i < result.size(); ++i)
    {
        if (i > 0) tags += L", ";
        tags += Utf8ToWChar(result[i]["name"]);
    }
    return tags;
}

static std::wstring GetMovieActors(int64_t movieId)
{
    std::string sql = "SELECT a.name FROM actor a "
                      "INNER JOIN movie_actor ma ON a.id = ma.actor_id "
                      "WHERE ma.movie_id = " + std::to_string(movieId) + ";";
    db::QueryResult result;
    g_database.ExecuteQuery(sql, result);

    std::wstring actors;
    for (size_t i = 0; i < result.size(); ++i)
    {
        if (i > 0) actors += L", ";
        actors += Utf8ToWChar(result[i]["name"]);
    }
    return actors;
}

void RefreshListView()
{
    if (!g_hListView) return;

    ListView_DeleteAllItems(g_hListView);
    s_listMovieIds.clear();
    s_listValid.clear();

    db::QueryResult movies;
    std::string sql = "SELECT id, sn, name, file_path, file_suffix, file_size, "
                      "mosaic, resolution, duration, valid FROM movie;";
    g_database.ExecuteQuery(sql, movies);

    // In-memory sort
    std::sort(movies.begin(), movies.end(),
        [](const std::unordered_map<std::string,std::string>& a,
           const std::unordered_map<std::string,std::string>& b) {
            if (s_sortCol < 0 || s_sortCol > 11) return false;
            // Map column index to field name
            const char* fields[] = {"sn","name","file_path","file_suffix","file_size",
                                     "resolution","duration","mosaic","",""};
            const char* f = fields[s_sortCol];
            if (!f || !*f) return false;
            auto ia = a.find(f), ib = b.find(f);
            if (ia == a.end() || ib == b.end()) return false;
            const std::string& va = ia->second;
            const std::string& vb = ib->second;
            bool asc = s_sortAsc;
            if (s_sortCol == 4 || s_sortCol == 6) { // Size, Duration: numeric
                int64_t na = va.empty() ? 0 : std::stoll(va);
                int64_t nb = vb.empty() ? 0 : std::stoll(vb);
                return asc ? na < nb : na > nb;
            }
            if (s_sortCol == 5) { // Resolution by vertical (*1080)
                auto getV = [](const std::string& s) -> int {
                    size_t star = s.find('*');
                    if (star == std::string::npos) star = s.find('x');
                    return (star != std::string::npos) ? std::atoi(s.c_str() + star + 1) : 0;
                };
                int ra = getV(va), rb = getV(vb);
                return asc ? ra < rb : ra > rb;
            }
            return asc ? va < vb : va > vb;
        });

    for (size_t i = 0; i < movies.size(); ++i)
    {
        const auto& row = movies[i];

        // --- In-memory filtering ---
        if (g_filterState.active)
        {
            // Format filter (no "不关心" — empty = nothing matches)
            {
                std::wstring suffixW = Utf8ToWChar(row.at("file_suffix"));
                if (g_filterState.formats.find(suffixW) == g_filterState.formats.end())
                    continue;
            }
            // Duration filter
            if (g_filterState.durationSel != 0)
            {
                const std::string& durVal = row.at("duration");
                int dur = durVal.empty() ? 0 : std::stoi(durVal);
                bool match = false;
                if (g_filterState.durationSel == 1 && dur < 1800) match = true;
                else if (g_filterState.durationSel == 2 && dur >= 1800 && dur < 3600) match = true;
                else if (g_filterState.durationSel == 3 && dur >= 3600 && dur < 7200) match = true;
                else if (g_filterState.durationSel == 4 && dur >= 7200) match = true;
                if (!match) continue;
            }
            // Resolution filter (no "不关心" — empty = nothing matches)
            {
                const std::string& resVal = row.at("resolution");
                if (g_filterState.resolutions.find(resVal) == g_filterState.resolutions.end())
                    continue;
            }
            // Mosaic filter
            if (g_filterState.mosaicSel != 0)
            {
                const std::string& mosVal = row.at("mosaic");
                int mos = mosVal.empty() ? 0 : std::stoi(mosVal);
                if (mos != g_filterState.mosaicSel) continue;
            }
        }

        std::wstring sn = Utf8ToWChar(row.at("sn"));
        std::wstring name = Utf8ToWChar(row.at("name"));
        std::wstring filePath = Utf8ToWChar(row.at("file_path"));
        std::wstring suffix = Utf8ToWChar(row.at("file_suffix"));

        int64_t fileSize = std::stoll(row.at("file_size"));
        std::wstring sizeStr = FormatFileSize(fileSize);

        // Use stored valid column (updated at startup and on re-check)
        int valid = 1;
        const std::string& validVal = row.at("valid");
        if (!validVal.empty())
            valid = std::stoi(validVal);

        int64_t movieId = std::stoll(row.at("id"));
        std::wstring tags = GetMovieTags(movieId);
        std::wstring actors = GetMovieActors(movieId);

        std::wstring mosaicStr = L"-";
        const std::string& mosaicVal = row.at("mosaic");
        if (!mosaicVal.empty())
        {
            int mosaic = std::stoi(mosaicVal);
            if (mosaic == 1) mosaicStr = L"无";
            else if (mosaic == 2) mosaicStr = L"有";
            else if (mosaic == 3) mosaicStr = L"破解或AI";
        }

        // Resolution is now a string from ffprobe (e.g. "1920x1080"), or empty
        std::wstring resolutionStr = L"-";
        const std::string& resVal = row.at("resolution");
        if (!resVal.empty())
        {
            // If it's a legacy integer (1-4), map to display string
            if (resVal.find('x') != std::string::npos)
                resolutionStr = Utf8ToWChar(resVal);
            else
            {
                int res = std::stoi(resVal);
                switch (res)
                {
                case 1: resolutionStr = L"SD"; break;
                case 2: resolutionStr = L"720p"; break;
                case 3: resolutionStr = L"1080p"; break;
                case 4: resolutionStr = L"4K"; break;
                default: resolutionStr = Utf8ToWChar(resVal); break;
                }
            }
        }

        // Duration: seconds -> h:mm:ss or m:ss
        std::wstring durationStr = L"-";
        const std::string& durVal = row.at("duration");
        int durSec = durVal.empty() ? 0 : std::stoi(durVal);
        if (durSec > 0)
        {
            int h = durSec / 3600;
            int m = (durSec % 3600) / 60;
            int s = durSec % 60;
            wchar_t buf[32];
            if (h > 0)
                swprintf(buf, 32, L"%d:%02d:%02d", h, m, s);
            else
                swprintf(buf, 32, L"%d:%02d", m, s);
            durationStr = buf;
        }

        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = static_cast<int>(i);
        lvi.iSubItem = 0;
        lvi.pszText = const_cast<LPWSTR>(sn.c_str());
        lvi.lParam = static_cast<LPARAM>(movieId);
        int idx = ListView_InsertItem(g_hListView, &lvi);

        ListView_SetItemText(g_hListView, idx, 1, const_cast<LPWSTR>(name.c_str()));
        ListView_SetItemText(g_hListView, idx, 2, const_cast<LPWSTR>(filePath.c_str()));
        ListView_SetItemText(g_hListView, idx, 3, const_cast<LPWSTR>(suffix.c_str()));
        ListView_SetItemText(g_hListView, idx, 4, const_cast<LPWSTR>(sizeStr.c_str()));
        ListView_SetItemText(g_hListView, idx, 5, const_cast<LPWSTR>(resolutionStr.c_str()));
        ListView_SetItemText(g_hListView, idx, 6, const_cast<LPWSTR>(durationStr.c_str()));
        ListView_SetItemText(g_hListView, idx, 7, const_cast<LPWSTR>(mosaicStr.c_str()));
        ListView_SetItemText(g_hListView, idx, 8, const_cast<LPWSTR>(actors.c_str()));
        ListView_SetItemText(g_hListView, idx, 9, const_cast<LPWSTR>(tags.c_str()));

        // Track metadata for custom draw and context menu
        s_listMovieIds.push_back(movieId);
        s_listValid.push_back(valid);
    }
}

//=============================================================================
// ListView column setup
//=============================================================================

static void SetupListViewColumns(HWND hListView)
{
    ListView_SetExtendedListViewStyle(hListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    struct { const wchar_t* title; int width; int fmt; } columns[] = {
        { L"番号",         100, LVCFMT_LEFT },
        { L"片名",         250, LVCFMT_LEFT },
        { L"文件路径",     250, LVCFMT_LEFT },
        { L"格式",         60,  LVCFMT_LEFT },
        { L"大小",         100, LVCFMT_RIGHT },
        { L"分辨率",       80,  LVCFMT_LEFT },
        { L"时长",         65,  LVCFMT_RIGHT },
        { L"码",           60,  LVCFMT_LEFT },
        { L"演员",         120, LVCFMT_LEFT },
        { L"标签",         120, LVCFMT_LEFT }
    };

    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;

    for (int i = 0; i < _countof(columns); ++i)
    {
        lvc.iSubItem = i;
        lvc.pszText = const_cast<LPWSTR>(columns[i].title);
        lvc.cx = columns[i].width;
        lvc.fmt = columns[i].fmt;
        ListView_InsertColumn(hListView, i, &lvc);
    }
}

//=============================================================================
// Main dialog procedure
//=============================================================================

INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            // Set icons (normally from RegisterClass)
            HICON hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_WINDOWSPROJECT1));
            SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            HICON hIconSm = LoadIcon(hInst, MAKEINTRESOURCE(IDI_SMALL));
            SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);

            g_hListView = GetDlgItem(hDlg, IDC_LISTVIEW);
            SetupListViewColumns(g_hListView);
        }
        return (INT_PTR)TRUE;

    case WM_SIZE:
        if (g_hListView)
        {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            HWND hBtn = GetDlgItem(hDlg, IDC_BTN_FILTER);
            if (hBtn)
                MoveWindow(hBtn, width - 65, 0, 65, FILTER_BAR_HEIGHT, TRUE);
            MoveWindow(g_hListView, 0, FILTER_BAR_HEIGHT + 2,
                       width, height - FILTER_BAR_HEIGHT - 2, TRUE);
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId)
            {
            case IDM_OPEN_DIR:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_DIR_SELECT), hDlg, DirSelectDlgProc);
                break;

            case IDM_SETTING:
                {
                    INT_PTR result = DialogBox(hInst,
                        MAKEINTRESOURCE(IDD_SETTING), hDlg, SettingsDlgProc);
                    if (result == IDOK)
                    {
                        LoadVideoExtensions(g_database);
                        RefreshListView();
                    }
                }
                break;

            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, About);
                break;

            case IDM_EXIT:
                DestroyWindow(hDlg);
                break;

            case IDC_BTN_FILTER:
                {
                    DialogBox(hInst, MAKEINTRESOURCE(IDD_FILTER), hDlg, FilterDlgProc);
                    HWND hFB = GetDlgItem(hDlg, IDC_BTN_FILTER);
                    if (hFB)
                        SetWindowTextW(hFB, g_filterState.active ? L"筛选(开)" : L"筛选");
                }
                break;

            case IDM_CTX_OPEN:
            case IDM_CTX_OPEN_PATH:
            case IDM_CTX_EDIT:
            case IDM_CTX_RECHECK:
            case IDM_CTX_DELETE:
                {
                    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
                    if (sel < 0 || sel >= static_cast<int>(s_listMovieIds.size()))
                        break;

                    int64_t movieId = s_listMovieIds[sel];

                    std::string pathSql = "SELECT file_path FROM movie WHERE id = "
                                          + std::to_string(movieId) + ";";
                    db::QueryResult pathResult;
                    g_database.ExecuteQuery(pathSql, pathResult);
                    std::string filePath;
                    if (!pathResult.empty()) filePath = pathResult[0]["file_path"];

                    if (wmId == IDM_CTX_OPEN)
                    {
                        // Re-check file existence before playing
                        if (!IsFileValid(filePath))
                        {
                            MessageBoxW(hDlg, L"文件不存在，请检查文件路径。", L"提示", MB_OK | MB_ICONWARNING);
                            g_database.Execute("UPDATE movie SET valid = 0 WHERE id = "
                                + std::to_string(movieId) + ";");
                            if (sel < static_cast<int>(s_listValid.size()))
                                s_listValid[sel] = 0;
                            InvalidateRect(g_hListView, nullptr, TRUE);
                            break;
                        }

                        // Read player config JSON via RapidJSON
                        std::string playerCfg = LoadSetting(g_database, "player_config");
                        std::string playerExe;
                        std::wstring extraArgs;
                        if (!playerCfg.empty())
                        {
                            rapidjson::Document doc;
                            doc.Parse(playerCfg.c_str());
                            if (!doc.HasParseError() && doc.IsObject())
                            {
                                if (doc.HasMember("player_exe_path") && doc["player_exe_path"].IsString())
                                    playerExe = doc["player_exe_path"].GetString();
                                bool isPot = false, jumpAd = false;
                                if (doc.HasMember("player_type") && doc["player_type"].IsString())
                                    isPot = (strcmp(doc["player_type"].GetString(), "potplayer") == 0);
                                if (isPot && doc.HasMember("player_config") && doc["player_config"].IsObject())
                                {
                                    const auto& cfg = doc["player_config"];
                                    if (cfg.HasMember("potplayer") && cfg["potplayer"].IsObject()
                                        && cfg["potplayer"].HasMember("jumpAd") && cfg["potplayer"]["jumpAd"].IsBool())
                                        jumpAd = cfg["potplayer"]["jumpAd"].GetBool();
                                }
                                if (isPot && jumpAd)
                            {
                                std::string adSql = "SELECT ad FROM movie WHERE id = "
                                    + std::to_string(movieId) + ";";
                                db::QueryResult adRes;
                                g_database.ExecuteQuery(adSql, adRes);
                                int adVal = 0;
                                if (!adRes.empty() && !adRes[0]["ad"].empty())
                                    adVal = std::stoi(adRes[0]["ad"]);
                                extraArgs = L" /seek=" + std::to_wstring(adVal);
                            }
                        }
                        }

                        std::wstring exePath = Utf8ToWChar(filePath);
                        std::wstring params = L"\"" + exePath + L"\"" + extraArgs;
                        if (!playerExe.empty())
                            ShellExecuteW(nullptr, L"open", Utf8ToWChar(playerExe).c_str(),
                                          params.c_str(), nullptr, SW_SHOWNORMAL);
                        else
                            ShellExecuteW(nullptr, L"open", exePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    else if (wmId == IDM_CTX_OPEN_PATH)
                    {
                        std::wstring wPath = Utf8ToWChar(filePath);
                        // Normalize backslashes
                        size_t bs = 0;
                        while ((bs = wPath.find(L"\\\\", bs)) != std::wstring::npos)
                            wPath.replace(bs, 2, L"\\");
                        std::wstring params = L"/select,\"" + wPath + L"\"";
                        ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
                    }
                    else if (wmId == IDM_CTX_EDIT)
                        DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_EDIT_MOVIE),
                                       hDlg, EditMovieDlgProc, static_cast<LPARAM>(movieId));
                    else if (wmId == IDM_CTX_RECHECK)
                    {
                        bool exists = IsFileValid(filePath);
                        g_database.Execute("UPDATE movie SET valid = " + std::to_string(exists ? 1 : 0)
                            + " WHERE id = " + std::to_string(movieId) + ";");
                        RefreshListView();
                    }
                    else if (wmId == IDM_CTX_DELETE)
                    {
                        std::wstring msg = L"删除 \"" + Utf8ToWChar(filePath) + L"\"？";
                        if (MessageBoxW(hDlg, msg.c_str(), L"确认删除", MB_YESNO | MB_ICONWARNING) == IDYES)
                        {
                            g_database.Execute("DELETE FROM movie WHERE id = " + std::to_string(movieId) + ";");
                            RefreshListView();
                        }
                    }
                }
                break;
            }
        }
        return (INT_PTR)TRUE;

    case WM_NOTIFY:
        {
            NMHDR* pnmh = reinterpret_cast<NMHDR*>(lParam);
            if (pnmh->idFrom == IDC_LISTVIEW)
            {
                if (pnmh->code == NM_CUSTOMDRAW)
                {
                    NMLVCUSTOMDRAW* lvcd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                    switch (lvcd->nmcd.dwDrawStage)
                    {
                    case CDDS_PREPAINT:
                        SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                        return (INT_PTR)TRUE;
                    case CDDS_ITEMPREPAINT:
                        {
                            int idx = static_cast<int>(lvcd->nmcd.dwItemSpec);
                            if (idx >= 0 && idx < static_cast<int>(s_listValid.size())
                                && s_listValid[idx] == 0)
                                lvcd->clrText = RGB(255, 0, 0);
                            SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_DODEFAULT);
                            return (INT_PTR)TRUE;
                        }
                    }
                }
                else if (pnmh->code == NM_RCLICK)
                {
                    NMITEMACTIVATE* nmia = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                    if (nmia->iItem != -1)
                    {
                        ListView_SetItemState(g_hListView, nmia->iItem,
                            LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

                        HMENU hMenu = CreatePopupMenu();
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_OPEN, L"打开");
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_OPEN_PATH, L"打开路径");
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_EDIT, L"编辑");
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_RECHECK, L"重新检查");
                        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_DELETE, L"删除");

                        POINT pt = nmia->ptAction;
                        ClientToScreen(g_hListView, &pt);
                        TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                       pt.x, pt.y, 0, hDlg, nullptr);
                        DestroyMenu(hMenu);
                    }
                }
                else if (pnmh->code == NM_DBLCLK)
                {
                    NMITEMACTIVATE* nmia = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                    if (nmia->iItem != -1)
                        SendMessage(hDlg, WM_COMMAND, IDM_CTX_OPEN, 0);
                }
                else if (pnmh->code == LVN_COLUMNCLICK)
                {
                    NMLISTVIEW* pnlv = reinterpret_cast<NMLISTVIEW*>(lParam);
                    if (pnlv->iSubItem == s_sortCol)
                        s_sortAsc = !s_sortAsc;
                    else
                        { s_sortCol = pnlv->iSubItem; s_sortAsc = true; }
                    RefreshListView();
                }
            }
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hDlg);
        return (INT_PTR)TRUE;

    case WM_DESTROY:
        g_database.Close();
        PostQuitMessage(0);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

//=============================================================================
// Application entry point
//=============================================================================

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icex);

    hInst = hInstance;

    HWND hDlg = CreateDialogW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainDlgProc);
    if (!hDlg) return FALSE;

    g_hMainWnd = hDlg;
    ShowWindow(hDlg, nCmdShow);

    InitDatabase();
    RefreshListView();

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINDOWSPROJECT1));

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}
