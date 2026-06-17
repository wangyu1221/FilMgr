#include "EditFilmDlg.h"
#include "../../Resource.h"
#include "../AppUtils.h"
#include "../db/Database.h"

#include <shobjidl.h>
#include <string>

extern db::Database g_database;
extern HWND g_hMainWnd;
void RefreshListView();
void UpdateListViewItem(int64_t filmId);

struct EditFilmState
{
    int64_t filmId = 0;
    std::string originalFilePath;
};

INT_PTR CALLBACK EditFilmDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            int64_t filmId = static_cast<int64_t>(lParam);
            if (filmId <= 0) { EndDialog(hDlg, IDCANCEL); return (INT_PTR)TRUE; }

            EditFilmState* state = new EditFilmState();
            state->filmId = filmId;
            SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            std::string sql = "SELECT id, name, sn, file_path, "
                              "file_suffix, file_size, mosaic, resolution, duration, producer "
                              "FROM film WHERE id = " + std::to_string(filmId) + ";";
            db::QueryResult result;
            g_database.ExecuteQuery(sql, result);
            if (result.empty()) { delete state; EndDialog(hDlg, IDCANCEL); return (INT_PTR)TRUE; }

            const auto& row = result[0];
            state->originalFilePath = row.at("file_path");

            SetDlgItemTextW(hDlg, IDC_EDIT_ID, Utf8ToWChar(row.at("id")).c_str());
            SetDlgItemTextW(hDlg, IDC_EDIT_NAME, Utf8ToWChar(row.at("name")).c_str());
            SetDlgItemTextW(hDlg, IDC_EDIT_SN, Utf8ToWChar(row.at("sn")).c_str());
            SetDlgItemTextW(hDlg, IDC_EDIT_PRODUCER, Utf8ToWChar(row.at("producer")).c_str());
            SetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH, Utf8ToWChar(row.at("file_path")).c_str());
            SetDlgItemTextW(hDlg, IDC_EDIT_FILE_SUFFIX, Utf8ToWChar(row.at("file_suffix")).c_str());

            int64_t fileSize = std::stoll(row.at("file_size"));
            SetDlgItemTextW(hDlg, IDC_EDIT_FILE_SIZE, FormatFileSize(fileSize).c_str());

            // Mosaic combobox
            HWND hMosaic = GetDlgItem(hDlg, IDC_EDIT_MOSAIC);
            const wchar_t* mosaicItems[] = { L"-", L"无", L"有", L"破解或AI" };
            for (int i = 0; i < _countof(mosaicItems); ++i)
                SendMessageW(hMosaic, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mosaicItems[i]));
            const std::string& mosaicVal = row.at("mosaic");
            int mosaicIdx = (!mosaicVal.empty()) ? std::stoi(mosaicVal) : 0;
            SendMessageW(hMosaic, CB_SETCURSEL, mosaicIdx, 0);

            // Resolution
            SetDlgItemTextW(hDlg, IDC_EDIT_RESOLUTION, Utf8ToWChar(row.at("resolution")).c_str());

            // Duration
            const std::string& durVal = row.at("duration");
            int durSec = durVal.empty() ? 0 : std::stoi(durVal);
            if (durSec > 0)
            {
                int h = durSec / 3600, m = (durSec % 3600) / 60, s = durSec % 60;
                wchar_t durBuf[32];
                if (h > 0) swprintf(durBuf, 32, L"%d:%02d:%02d", h, m, s);
                else       swprintf(durBuf, 32, L"%d:%02d", m, s);
                SetDlgItemTextW(hDlg, IDC_EDIT_DURATION, durBuf);
            }

            if (g_hMainWnd)
            {
                RECT rcParent, rcDlg;
                GetWindowRect(g_hMainWnd, &rcParent);
                GetWindowRect(hDlg, &rcDlg);
                int dlgW = rcDlg.right - rcDlg.left, dlgH = rcDlg.bottom - rcDlg.top;
                int parentW = rcParent.right - rcParent.left, parentH = rcParent.bottom - rcParent.top;
                int x = rcParent.left + (parentW - dlgW) / 2;
                int y = rcParent.top + (parentH - dlgH) / 2;
                SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            if (wmId == IDOK)
            {
                EditFilmState* state = reinterpret_cast<EditFilmState*>(
                    GetWindowLongPtr(hDlg, GWLP_USERDATA));
                if (!state) break;

                wchar_t buf[512];
                GetDlgItemTextW(hDlg, IDC_EDIT_NAME, buf, 512);
                std::string name = WCharToUtf8(buf);
                GetDlgItemTextW(hDlg, IDC_EDIT_SN, buf, 512);
                std::string sn = WCharToUtf8(buf);
                GetDlgItemTextW(hDlg, IDC_EDIT_PRODUCER, buf, 512);
                std::string producer = WCharToUtf8(buf);
                GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH, buf, 512);
                std::string filePath = WCharToUtf8(buf);
                // Normalize backslashes
                size_t bs = 0;
                while ((bs = filePath.find("\\\\", bs)) != std::string::npos)
                    filePath.replace(bs, 2, "\\");
                GetDlgItemTextW(hDlg, IDC_EDIT_FILE_SUFFIX, buf, 512);
                std::string fileSuffix = WCharToUtf8(buf);

                std::string fileSize;
                db::QueryResult fsRes;
                g_database.ExecuteQuery("SELECT file_size FROM film WHERE id = " + std::to_string(state->filmId) + ";", fsRes);
                fileSize = (!fsRes.empty()) ? fsRes[0]["file_size"] : "0";
                if (filePath != state->originalFilePath)
                {
                    std::wstring wPath = Utf8ToWChar(filePath);
                    WIN32_FILE_ATTRIBUTE_DATA attr = {};
                    if (GetFileAttributesExW(wPath.c_str(), GetFileExInfoStandard, &attr))
                    {
                        LARGE_INTEGER li;
                        li.LowPart = attr.nFileSizeLow; li.HighPart = attr.nFileSizeHigh;
                        fileSize = std::to_string(li.QuadPart);
                    }
                }

                int mosaic = static_cast<int>(
                    SendMessageW(GetDlgItem(hDlg, IDC_EDIT_MOSAIC), CB_GETCURSEL, 0, 0));
                GetDlgItemTextW(hDlg, IDC_EDIT_RESOLUTION, buf, 512);
                std::string resolution = WCharToUtf8(buf);

                db::QueryResult durRes;
                g_database.ExecuteQuery("SELECT duration FROM film WHERE id = " + std::to_string(state->filmId) + ";", durRes);
                std::string duration = (!durRes.empty()) ? durRes[0]["duration"] : "0";

                auto esc = [](const std::string& s) -> std::string {
                    std::string out;
                    for (char c : s) { if (c == '\'') out += "''"; else out += c; }
                    return out;
                };

                std::string updateSql =
                    "UPDATE film SET name = '" + esc(name) +
                    "', sn = '" + esc(sn) +
                    "', producer = '" + esc(producer) +
                    "', file_path = '" + esc(filePath) +
                    "', file_suffix = '" + esc(fileSuffix) +
                    "', file_size = " + fileSize +
                    ", mosaic = " + std::to_string(mosaic) +
                    ", resolution = '" + esc(resolution) +
                    "', duration = " + duration +
                    " WHERE id = " + std::to_string(state->filmId) + ";";
                g_database.Execute(updateSql);

                int64_t filmId = state->filmId;
                delete state;
                SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
                UpdateListViewItem(filmId);
                EndDialog(hDlg, IDOK);
            }
            else if (wmId == IDCANCEL)
            {
                EditFilmState* state = reinterpret_cast<EditFilmState*>(
                    GetWindowLongPtr(hDlg, GWLP_USERDATA));
                delete state;
                SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
                EndDialog(hDlg, IDCANCEL);
            }
        }
        break;
    }
    return (INT_PTR)FALSE;
}
