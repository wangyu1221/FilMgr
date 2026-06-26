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
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <string>
#include <sstream>
#include <algorithm>
#include <vector>

#pragma comment(lib, "comctl32.lib")

#define MAX_LOADSTRING 100
#define IDC_LISTVIEW 1001
#define FILTER_BAR_HEIGHT 24
#define LISTVIEW_ROW_HEIGHT 24

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

static void CreateFilmTables()
{
    // film table
    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS film (
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
            valid       TINYINT      NOT NULL DEFAULT 1,
            producer    VARCHAR(32),
            star        TINYINT
        );
    )";
    g_database.Execute(sql);

    g_database.Execute(
        "CREATE INDEX IF NOT EXISTS idx_film_file_path ON film(file_path);");

    // Migration: add star column for existing databases (ignored if already present)
    g_database.Execute("ALTER TABLE film ADD COLUMN star TINYINT;");

    // actor table
    g_database.Execute(R"(
        CREATE TABLE IF NOT EXISTS actor (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name VARCHAR(256) NOT NULL
        );
    )");

    // film_actor table
    g_database.Execute(R"(
        CREATE TABLE IF NOT EXISTS film_actor (
            id       INTEGER PRIMARY KEY AUTOINCREMENT,
            film_id BIGINT NOT NULL,
            actor_id BIGINT NOT NULL
        );
    )");
    g_database.Execute(
        "CREATE INDEX IF NOT EXISTS idx_film_actor_film ON film_actor(film_id);");
    g_database.Execute(
        "CREATE INDEX IF NOT EXISTS idx_film_actor_actor ON film_actor(actor_id);");

    // tag table
    g_database.Execute(R"(
        CREATE TABLE IF NOT EXISTS tag (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name VARCHAR(256) NOT NULL
        );
    )");

    // film_tag table
    g_database.Execute(R"(
        CREATE TABLE IF NOT EXISTS film_tag (
            id       INTEGER PRIMARY KEY AUTOINCREMENT,
            film_id BIGINT NOT NULL,
            tag_id   BIGINT NOT NULL
        );
    )");
    g_database.Execute(
        "CREATE INDEX IF NOT EXISTS idx_film_tag_film ON film_tag(film_id);");
    g_database.Execute(
        "CREATE INDEX IF NOT EXISTS idx_film_tag_tag ON film_tag(tag_id);");
}

static void InitDatabase()
{
    // Ensure C:\ProgramData\Oldman\FilMgr directory exists
    // CreateDirectoryW only creates the last component, so ensure parent exists first
    CreateDirectoryW(L"C:\\ProgramData\\Oldman", nullptr);
    const wchar_t* dataDir = L"C:\\ProgramData\\Oldman\\FilMgr";
    if (GetFileAttributesW(dataDir) == INVALID_FILE_ATTRIBUTES)
        CreateDirectoryW(dataDir, nullptr);

    std::string dbPath = WCharToUtf8(dataDir) + std::string("\\default.db");

    auto status = g_database.Open(dbPath);
    if (status != db::Status::Ok)
    {
        MessageBoxW(nullptr, L"数据库打开失败！", L"错误", MB_ICONERROR);
        return;
    }

    // Create setting table if not exists (key-value store)
    g_database.Execute(
        "CREATE TABLE IF NOT EXISTS setting ("
        "    setting_key   VARCHAR(64) NOT NULL PRIMARY KEY,"
        "    setting_value TEXT"
        ");");

    CreateFilmTables();

    // Load video extensions from database settings
    LoadVideoExtensions(g_database);

    // Check all file paths and update valid column; also normalize backslashes
    db::QueryResult allPaths;
    g_database.ExecuteQuery("SELECT id, file_path FROM film;", allPaths);
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
            "UPDATE film SET valid = " + std::to_string(valid) +
            ", file_path = '" + esc(fp) + "' WHERE id = " + row.at("id") + ";");
    }
}

//=============================================================================
// ListView helpers
//=============================================================================

// Parallel arrays for list view item metadata (index = list item index)
static std::vector<int64_t> s_listFilmIds;
static std::vector<int>    s_listValid;

// Sort state
static int  s_sortCol = -1;
static bool s_sortAsc = true;

// Column visibility
static std::vector<bool> s_colVisible;        // which of 12 original columns are visible
static std::vector<int>  s_colDisplayToOrig;  // display index → original column index
static constexpr int     IDM_COL_BASE = 3000; // WM_COMMAND base for column toggle (3000-3011)

//=============================================================================

static std::wstring GetFilmTags(int64_t filmId)
{
    std::string sql = "SELECT t.name FROM tag t "
                      "INNER JOIN film_tag mt ON t.id = mt.tag_id "
                      "WHERE mt.film_id = " + std::to_string(filmId) + ";";
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

static std::wstring GetFilmActors(int64_t filmId)
{
    std::string sql = "SELECT a.name FROM actor a "
                      "INNER JOIN film_actor ma ON a.id = ma.actor_id "
                      "WHERE ma.film_id = " + std::to_string(filmId) + ";";
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

// Build display strings for all 12 original columns from a DB row.
// Also returns the valid flag for the film.
static int BuildFilmColumnValues(int64_t filmId,
                                  const std::unordered_map<std::string,std::string>& row,
                                  std::wstring colValues[12])
{
    colValues[0] = Utf8ToWChar(row.at("name"));       // 片名
    colValues[1] = Utf8ToWChar(row.at("sn"));         // 番号
    colValues[2] = Utf8ToWChar(row.at("file_path"));
    colValues[3] = Utf8ToWChar(row.at("file_suffix"));

    int64_t fileSize = std::stoll(row.at("file_size"));
    colValues[4] = FormatFileSize(fileSize);

    // Resolution
    std::wstring resolutionStr = L"-";
    const std::string& resVal = row.at("resolution");
    if (!resVal.empty())
    {
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
    colValues[5] = resolutionStr;

    // Duration
    std::wstring durationStr = L"-";
    const std::string& durVal = row.at("duration");
    int durSec = durVal.empty() ? 0 : std::stoi(durVal);
    if (durSec > 0)
    {
        int h = durSec / 3600, m = (durSec % 3600) / 60, s = durSec % 60;
        wchar_t buf[32];
        if (h > 0) swprintf(buf, 32, L"%d:%02d:%02d", h, m, s);
        else       swprintf(buf, 32, L"%d:%02d", m, s);
        durationStr = buf;
    }
    colValues[6] = durationStr;

    // Mosaic
    std::wstring mosaicStr = L"-";
    const std::string& mosaicVal = row.at("mosaic");
    if (!mosaicVal.empty())
    {
        int mosaic = std::stoi(mosaicVal);
        if (mosaic == 1) mosaicStr = L"无";
        else if (mosaic == 2) mosaicStr = L"有";
        else if (mosaic == 3) mosaicStr = L"破解或AI";
    }
    colValues[7] = mosaicStr;

    colValues[8] = GetFilmActors(filmId);
    colValues[9] = GetFilmTags(filmId);
    colValues[10] = Utf8ToWChar(row.count("producer") ? row.at("producer") : "");

    // Star rating
    std::wstring starStr = L"-";
    const std::string& starVal = row.count("star") ? row.at("star") : "";
    if (!starVal.empty())
    {
        int star = std::stoi(starVal);
        if (star > 0) starStr = std::wstring(star, L'☆');  // ☆
    }
    colValues[11] = starStr;

    int valid = 1;
    const std::string& validVal = row.at("valid");
    if (!validVal.empty()) valid = std::stoi(validVal);
    return valid;
}

void RefreshListView()
{
    if (!g_hListView) return;

    ListView_DeleteAllItems(g_hListView);
    s_listFilmIds.clear();
    s_listValid.clear();

    db::QueryResult films;
    std::string sql = "SELECT id, sn, name, file_path, file_suffix, file_size, "
                      "mosaic, resolution, duration, valid, producer, star FROM film;";
    g_database.ExecuteQuery(sql, films);

    // In-memory sort
    std::sort(films.begin(), films.end(),
        [](const std::unordered_map<std::string,std::string>& a,
           const std::unordered_map<std::string,std::string>& b) {
            if (s_sortCol < 0 || s_sortCol >= (int)s_colDisplayToOrig.size())
                return false;
            int origCol = s_colDisplayToOrig[s_sortCol];
            const char* fields[] = {"name","sn","file_path","file_suffix","file_size",
                                     "resolution","duration","mosaic","","","producer","star"};
            const char* f = fields[origCol];
            if (!f || !*f) return false;
            auto ia = a.find(f), ib = b.find(f);
            if (ia == a.end() || ib == b.end()) return false;
            const std::string& va = ia->second;
            const std::string& vb = ib->second;
            bool asc = s_sortAsc;
            if (origCol == 4 || origCol == 6) {
                int64_t na = va.empty() ? 0 : std::stoll(va);
                int64_t nb = vb.empty() ? 0 : std::stoll(vb);
                return asc ? na < nb : na > nb;
            }
            if (origCol == 5) {
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

    for (size_t i = 0; i < films.size(); ++i)
    {
        const auto& row = films[i];

        // --- In-memory filtering ---
        if (g_filterState.active)
        {
            {
                std::wstring suffixW = Utf8ToWChar(row.at("file_suffix"));
                if (g_filterState.formats.find(suffixW) == g_filterState.formats.end())
                    continue;
            }
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
            {
                const std::string& resVal = row.at("resolution");
                if (g_filterState.resolutions.find(resVal) == g_filterState.resolutions.end())
                    continue;
            }
            if (g_filterState.mosaicSel != 0)
            {
                const std::string& mosVal = row.at("mosaic");
                int mos = mosVal.empty() ? 0 : std::stoi(mosVal);
                if (mos != g_filterState.mosaicSel) continue;
            }
        }

        int64_t filmId = std::stoll(row.at("id"));

        std::wstring colValues[12];
        int valid = BuildFilmColumnValues(filmId, row, colValues);

        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = static_cast<int>(i);
        lvi.iSubItem = 0;
        lvi.pszText = const_cast<LPWSTR>(
            (s_colDisplayToOrig.empty() ? L"" : colValues[s_colDisplayToOrig[0]].c_str()));
        lvi.lParam = static_cast<LPARAM>(filmId);
        int idx = ListView_InsertItem(g_hListView, &lvi);

        for (size_t di = 1; di < s_colDisplayToOrig.size(); ++di)
        {
            int origCol = s_colDisplayToOrig[di];
            ListView_SetItemText(g_hListView, idx, (int)di,
                const_cast<LPWSTR>(colValues[origCol].c_str()));
        }

        s_listFilmIds.push_back(filmId);
        s_listValid.push_back(valid);
    }
}

// Update a single row in the ListView after edit (no full reload)
void UpdateListViewItem(int64_t filmId)
{
    if (!g_hListView) return;

    // Find the list item index for this filmId
    int listIdx = -1;
    for (size_t i = 0; i < s_listFilmIds.size(); ++i)
    {
        if (s_listFilmIds[i] == filmId) { listIdx = static_cast<int>(i); break; }
    }
    if (listIdx < 0) return;

    // Re-fetch updated data from database
    std::string sql = "SELECT id, sn, name, file_path, file_suffix, file_size, "
                      "mosaic, resolution, duration, valid, producer, star "
                      "FROM film WHERE id = " + std::to_string(filmId) + ";";
    db::QueryResult result;
    g_database.ExecuteQuery(sql, result);
    if (result.empty()) return;

    std::wstring colValues[12];
    int valid = BuildFilmColumnValues(filmId, result[0], colValues);

    // Update text for every visible column in-place
    for (size_t di = 0; di < s_colDisplayToOrig.size(); ++di)
    {
        int origCol = s_colDisplayToOrig[di];
        ListView_SetItemText(g_hListView, listIdx, static_cast<int>(di),
            const_cast<LPWSTR>(colValues[origCol].c_str()));
    }

    // Update validity tracking (affects custom-draw text color)
    if (listIdx < static_cast<int>(s_listValid.size()))
        s_listValid[listIdx] = valid;
}

//=============================================================================
//=============================================================================
// Column visibility helpers
//=============================================================================

static void LoadColumnVisibility()
{
    s_colVisible.assign(12, true);  // all visible by default
    std::string json = LoadSetting(g_database, "listview_col");
    if (!json.empty())
    {
        rapidjson::Document doc;
        doc.Parse(json.c_str());
        if (doc.IsArray())
        {
            s_colVisible.assign(12, false);
            for (rapidjson::SizeType i = 0; i < doc.Size(); ++i)
            {
                if (doc[i].IsInt())
                {
                    int col = doc[i].GetInt();
                    if (col >= 0 && col < 12)
                        s_colVisible[col] = true;
                }
            }
        }
    }
}

static void SaveColumnVisibility()
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartArray();
    for (int i = 0; i < 12; ++i)
    {
        if (s_colVisible[i])
            writer.Int(i);
    }
    writer.EndArray();
    SaveSetting(g_database, "listview_col", sb.GetString());
}

static void RebuildColumnMap()
{
    s_colDisplayToOrig.clear();
    for (int i = 0; i < 12; ++i)
    {
        if (s_colVisible[i])
            s_colDisplayToOrig.push_back(i);
    }
    // Reset sort state since column indices changed
    s_sortCol = -1;
    s_sortAsc = true;
}

// Mapping: original column index → film field name
static const char* s_colFieldNames[] = {
    "name", "sn", "file_path", "file_suffix", "file_size",
    "resolution", "duration", "mosaic", "actors", "tags", "producer", "star"
};

// Saved column widths (0 = use default)
static int s_colWidths[12] = {0};

static void LoadColumnWidths()
{
    for (int i = 0; i < 12; ++i) s_colWidths[i] = 0;
    std::string json = LoadSetting(g_database, "listview_col_width");
    if (json.empty()) return;
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (!doc.IsObject()) return;
    for (int i = 0; i < 12; ++i)
    {
        if (doc.HasMember(s_colFieldNames[i]) && doc[s_colFieldNames[i]].IsInt())
            s_colWidths[i] = doc[s_colFieldNames[i]].GetInt();
    }
}

static void SaveColumnWidths()
{
    // Use s_colWidths[] as the source of truth — it is already updated
    // from NMHEADERW.pitem->cxy before this function is called (in the
    // HDN_ENDTRACKW handler).  Do NOT query ListView_GetColumnWidth here
    // because it may still return the pre-resize (stale) width.
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    for (int i = 0; i < 12; ++i)
    {
        if (!s_colVisible[i]) continue;
        if (s_colWidths[i] > 0)
        {
            writer.Key(s_colFieldNames[i]);
            writer.Int(s_colWidths[i]);
        }
    }
    writer.EndObject();
    SaveSetting(g_database, "listview_col_width", sb.GetString());
}

//=============================================================================
// ListView column setup
//=============================================================================

static void SetupListViewColumns(HWND hListView)
{
    ListView_SetExtendedListViewStyle(hListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // Set row height via a 1-pixel-wide image list
    HIMAGELIST hImgList = ImageList_Create(1, LISTVIEW_ROW_HEIGHT, ILC_COLOR32, 0, 1);
    ListView_SetImageList(hListView, hImgList, LVSIL_SMALL);

    struct { const wchar_t* title; int width; int fmt; } columns[] = {
        { L"片名",         250, LVCFMT_LEFT },
        { L"番号",         150, LVCFMT_LEFT },
        { L"文件路径",     250, LVCFMT_LEFT },
        { L"格式",         60,  LVCFMT_LEFT },
        { L"大小",         100, LVCFMT_RIGHT },
        { L"分辨率",       80,  LVCFMT_LEFT },
        { L"时长",         65,  LVCFMT_RIGHT },
        { L"码",           60,  LVCFMT_LEFT },
        { L"演员",         120, LVCFMT_LEFT },
        { L"标签",         120, LVCFMT_LEFT },
        { L"制片商",       100, LVCFMT_LEFT },
        { L"评价",         80,  LVCFMT_LEFT }
    };

    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;

    int displayIdx = 0;
    int numCols = _countof(columns);
    for (int i = 0; i < numCols; ++i)
    {
        if (!s_colVisible[i])
            continue;
        lvc.iSubItem = displayIdx;
        lvc.pszText = const_cast<LPWSTR>(columns[i].title);
        lvc.cx = (s_colWidths[i] > 0) ? s_colWidths[i] : columns[i].width;
        lvc.fmt = columns[i].fmt;
        ListView_InsertColumn(hListView, displayIdx, &lvc);
        ++displayIdx;
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
            LoadColumnVisibility();
            LoadColumnWidths();
            RebuildColumnMap();
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

            case IDM_MONITOR_DIR:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_MONITOR_DIR), hDlg, MonitorDirDlgProc);
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
                {
                    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
                    if (sel < 0 || sel >= static_cast<int>(s_listFilmIds.size()))
                        break;

                    int64_t filmId = s_listFilmIds[sel];

                    std::string pathSql = "SELECT file_path FROM film WHERE id = "
                                          + std::to_string(filmId) + ";";
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
                            g_database.Execute("UPDATE film SET valid = 0 WHERE id = "
                                + std::to_string(filmId) + ";");
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
                                std::string adSql = "SELECT ad FROM film WHERE id = "
                                    + std::to_string(filmId) + ";";
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
                        DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_EDIT_FILM),
                                       hDlg, EditFilmDlgProc, static_cast<LPARAM>(filmId));
                }
                break;

            case IDM_CTX_RECHECK:
                {
                    // Collect all selected film IDs
                    std::vector<int64_t> selectedIds;
                    int sel = -1;
                    while ((sel = ListView_GetNextItem(g_hListView, sel, LVNI_SELECTED)) != -1)
                    {
                        if (sel >= 0 && sel < static_cast<int>(s_listFilmIds.size()))
                            selectedIds.push_back(s_listFilmIds[sel]);
                    }
                    if (selectedIds.empty()) break;

                    for (int64_t filmId : selectedIds)
                    {
                        std::string pathSql = "SELECT file_path FROM film WHERE id = "
                                              + std::to_string(filmId) + ";";
                        db::QueryResult pathResult;
                        g_database.ExecuteQuery(pathSql, pathResult);
                        std::string filePath;
                        if (!pathResult.empty()) filePath = pathResult[0]["file_path"];
                        bool exists = IsFileValid(filePath);
                        g_database.Execute("UPDATE film SET valid = " + std::to_string(exists ? 1 : 0)
                            + " WHERE id = " + std::to_string(filmId) + ";");
                    }
                    RefreshListView();
                }
                break;

            case IDM_CTX_DELETE:
                {
                    // Collect all selected film IDs
                    std::vector<int64_t> selectedIds;
                    int sel = -1;
                    while ((sel = ListView_GetNextItem(g_hListView, sel, LVNI_SELECTED)) != -1)
                    {
                        if (sel >= 0 && sel < static_cast<int>(s_listFilmIds.size()))
                            selectedIds.push_back(s_listFilmIds[sel]);
                    }
                    if (selectedIds.empty()) break;

                    std::wstring msg;
                    if (selectedIds.size() > 1)
                        msg = L"从列表中删除选中的多个文件记录？";
                    else
                    {
                        std::string pathSql = "SELECT file_path FROM film WHERE id = "
                                              + std::to_string(selectedIds[0]) + ";";
                        db::QueryResult pathResult;
                        g_database.ExecuteQuery(pathSql, pathResult);
                        std::string filePath;
                        if (!pathResult.empty()) filePath = pathResult[0]["file_path"];
                        msg = L"从列表中删除 \"" + Utf8ToWChar(filePath) + L"\"？";
                    }

                    if (MessageBoxW(hDlg, msg.c_str(), L"确认从列表中删除", MB_YESNO | MB_ICONWARNING) == IDYES)
                    {
                        for (int64_t filmId : selectedIds)
                            g_database.Execute("DELETE FROM film WHERE id = " + std::to_string(filmId) + ";");
                        RefreshListView();
                    }
                }
                break;

            case IDM_CTX_DELETE_FILE:
                {
                    // Collect all selected film IDs
                    std::vector<int64_t> selectedIds;
                    int sel = -1;
                    while ((sel = ListView_GetNextItem(g_hListView, sel, LVNI_SELECTED)) != -1)
                    {
                        if (sel >= 0 && sel < static_cast<int>(s_listFilmIds.size()))
                            selectedIds.push_back(s_listFilmIds[sel]);
                    }
                    if (selectedIds.empty()) break;

                    // Build double-null-terminated file path list for SHFileOperationW
                    std::wstring fileList;
                    for (int64_t filmId : selectedIds)
                    {
                        std::string pathSql = "SELECT file_path FROM film WHERE id = "
                                              + std::to_string(filmId) + ";";
                        db::QueryResult pathResult;
                        g_database.ExecuteQuery(pathSql, pathResult);
                        if (!pathResult.empty())
                        {
                            std::wstring wPath = Utf8ToWChar(pathResult[0]["file_path"]);
                            if (IsFileValid(pathResult[0]["file_path"]))
                                fileList += wPath + L'\0';
                        }
                    }
                    if (fileList.empty())
                    {
                        MessageBoxW(hDlg, L"没有可删除的磁盘文件。", L"提示", MB_OK | MB_ICONINFORMATION);
                        break;
                    }
                    fileList += L'\0'; // final null terminator

                    std::wstring msg;
                    if (selectedIds.size() > 1)
                        msg = L"将选中的 " + std::to_wstring(selectedIds.size()) + L" 个文件移入回收站？";
                    else
                        msg = L"将选中的文件移入回收站？";

                    if (MessageBoxW(hDlg, msg.c_str(), L"确认删除磁盘文件", MB_YESNO | MB_ICONWARNING) == IDYES)
                    {
                        SHFILEOPSTRUCTW fos = {};
                        fos.hwnd = hDlg;
                        fos.wFunc = FO_DELETE;
                        fos.pFrom = fileList.c_str();
                        fos.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;

                        int result = SHFileOperationW(&fos);
                        if (result == 0 || fos.fAnyOperationsAborted)
                        {
                            // Remove DB records for deleted files
                            for (int64_t filmId : selectedIds)
                                g_database.Execute("DELETE FROM film WHERE id = " + std::to_string(filmId) + ";");
                            RefreshListView();
                        }
                        else
                        {
                            MessageBoxW(hDlg, L"删除文件失败。", L"错误", MB_OK | MB_ICONERROR);
                        }
                    }
                }
                break;

            case IDM_CTX_DELETE_INVALID:
                {
                    // Count invalid files
                    db::QueryResult invalidFiles;
                    g_database.ExecuteQuery(
                        "SELECT id, file_path FROM film WHERE valid = 0;", invalidFiles);

                    if (invalidFiles.empty())
                    {
                        MessageBoxW(hDlg, L"没有无效文件记录需要删除。", L"提示", MB_OK | MB_ICONINFORMATION);
                        break;
                    }

                    std::wstring msg = L"共有 " + std::to_wstring(invalidFiles.size())
                        + L" 个无效文件（文件路径不存在）。\n确认从数据库中删除这些记录吗？";
                    if (MessageBoxW(hDlg, msg.c_str(), L"确认删除无效文件记录",
                        MB_YESNO | MB_ICONWARNING) == IDYES)
                    {
                        g_database.Execute("DELETE FROM film WHERE valid = 0;");
                        // Also clean up orphaned film_actor and film_tag entries
                        g_database.Execute(
                            "DELETE FROM film_actor WHERE film_id NOT IN "
                            "(SELECT id FROM film);");
                        g_database.Execute(
                            "DELETE FROM film_tag WHERE film_id NOT IN "
                            "(SELECT id FROM film);");
                        RefreshListView();
                    }
                }
                break;

            default:
                // Column visibility toggle from header context menu
                if (wmId >= IDM_COL_BASE && wmId < IDM_COL_BASE + 12)
                {
                    int col = wmId - IDM_COL_BASE;
                    s_colVisible[col] = !s_colVisible[col];
                    SaveColumnVisibility();
                    RebuildColumnMap();

                    // Remove all existing columns
                    while (ListView_DeleteColumn(g_hListView, 0)) {}
                    // Rebuild with new visibility
                    SetupListViewColumns(g_hListView);
                    RefreshListView();
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
                        // If right-clicked item is not in current selection,
                        // select only that item (single-select behavior)
                        if (!(ListView_GetItemState(g_hListView, nmia->iItem, LVIS_SELECTED) & LVIS_SELECTED))
                        {
                            ListView_SetItemState(g_hListView, -1, 0, LVIS_SELECTED);
                            ListView_SetItemState(g_hListView, nmia->iItem,
                                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                        }

                        int selCount = ListView_GetSelectedCount(g_hListView);

                        HMENU hMenu = CreatePopupMenu();
                        UINT openFlags = (selCount > 1) ? (MF_STRING | MF_GRAYED) : MF_STRING;
                        AppendMenuW(hMenu, openFlags, IDM_CTX_OPEN, L"打开");
                        AppendMenuW(hMenu, openFlags, IDM_CTX_OPEN_PATH, L"打开路径");
                        AppendMenuW(hMenu, openFlags, IDM_CTX_EDIT, L"编辑");
                        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_DELETE, L"删除选中记录");
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_DELETE_FILE, L"删除磁盘文件");
                        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_RECHECK, L"重新检查");
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_DELETE_INVALID, L"删除无效");

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
                else if (pnmh->code == HDN_ENDTRACKW)
                {
                    // ListView forwards header column-resize notifications with
                    // idFrom==IDC_LISTVIEW. Read the new width directly from the
                    // NMHEADERW notification data — more reliable than querying
                    // ListView_GetColumnWidth, which may not be updated yet.
                    NMHEADERW* pnmhdr = reinterpret_cast<NMHEADERW*>(lParam);
                    int displayIdx = pnmhdr->iItem;
                    if (displayIdx >= 0 && displayIdx < static_cast<int>(s_colDisplayToOrig.size()))
                    {
                        int origIdx = s_colDisplayToOrig[displayIdx];
                        if (pnmhdr->pitem && (pnmhdr->pitem->mask & HDI_WIDTH))
                        {
                            int newWidth = pnmhdr->pitem->cxy;
                            if (newWidth > 0)
                                s_colWidths[origIdx] = newWidth;
                        }
                    }
                    SaveColumnWidths();
                }
            }
            else
            {
                // Header control right-click → column visibility menu
                HWND hHeader = ListView_GetHeader(g_hListView);
                if (hHeader && pnmh->hwndFrom == hHeader)
                {
                    if (pnmh->code == NM_RCLICK)
                    {
                        const wchar_t* colNames[] = {
                            L"片名", L"番号", L"文件路径", L"格式", L"大小",
                            L"分辨率", L"时长", L"码", L"演员", L"标签", L"制片商", L"评价"
                        };

                        HMENU hMenu = CreatePopupMenu();
                        for (int i = 0; i < 12; ++i)
                        {
                            UINT flags = MF_STRING | (s_colVisible[i] ? MF_CHECKED : MF_UNCHECKED);
                            AppendMenuW(hMenu, flags, IDM_COL_BASE + i, colNames[i]);
                        }

                        // Get cursor position for popup
                        POINT pt;
                        GetCursorPos(&pt);
                        TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                       pt.x, pt.y, 0, hDlg, nullptr);
                        DestroyMenu(hMenu);
                    }
                    else if (pnmh->code == HDN_ENDTRACKW)
                    {
                        // Read the new width from NMHEADERW before saving
                        NMHEADERW* pnmhdr = reinterpret_cast<NMHEADERW*>(lParam);
                        int displayIdx = pnmhdr->iItem;
                        if (displayIdx >= 0 && displayIdx < static_cast<int>(s_colDisplayToOrig.size()))
                        {
                            int origIdx = s_colDisplayToOrig[displayIdx];
                            if (pnmhdr->pitem && (pnmhdr->pitem->mask & HDI_WIDTH))
                            {
                                int newWidth = pnmhdr->pitem->cxy;
                                if (newWidth > 0)
                                    s_colWidths[origIdx] = newWidth;
                            }
                        }
                        SaveColumnWidths();
                    }
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
// Auto-scan monitored directories
//=============================================================================

static int AutoScanMonitorDirs()
{
    std::string json = LoadSetting(g_database, "monitor_dirs");
    if (json.empty()) return 0;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsArray()) return 0;

    std::vector<ScanDir> dirs;

    for (rapidjson::SizeType i = 0; i < doc.Size(); ++i)
    {
        if (!doc[i].IsObject()) continue;

        std::wstring dir;
        bool includeSub = true;

        if (doc[i].HasMember("dir") && doc[i]["dir"].IsString())
            dir = Utf8ToWChar(doc[i]["dir"].GetString());
        if (doc[i].HasMember("include_sub") && doc[i]["include_sub"].IsInt())
            includeSub = (doc[i]["include_sub"].GetInt() != 0);

        if (dir.empty()) continue;

        dirs.push_back({ dir, includeSub });
    }

    if (dirs.empty()) return 0;

    // Build set of existing file paths for O(1) dedup
    std::unordered_set<std::string> existingPaths;
    {
        db::QueryResult films;
        g_database.ExecuteQuery("SELECT file_path FROM film;", films);
        for (const auto& row : films)
            existingPaths.insert(row.at("file_path"));
    }

    int totalNew = ShowScanningDialog(g_hMainWnd, dirs, &existingPaths);
    if (totalNew < 0) totalNew = 0;
    return totalNew;
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

    InitDatabase();

    HWND hDlg = CreateDialogW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainDlgProc);
    if (!hDlg) return FALSE;

    g_hMainWnd = hDlg;
    ShowWindow(hDlg, nCmdShow);

    RefreshListView();

    int newFiles = AutoScanMonitorDirs();
    if (newFiles > 0)
    {
        RefreshListView();
        std::wstring msg = L"已自动扫描并添加 " + std::to_wstring(newFiles) + L" 个新文件记录。";
        MessageBoxW(hDlg, msg.c_str(), L"提示", MB_ICONINFORMATION);
    }

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
