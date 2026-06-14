#include "FilterDlg.h"
#include "../../Resource.h"
#include "../AppUtils.h"
#include "../db/Database.h"

#include <commctrl.h>
#include <vector>
#include <string>

extern db::Database g_database;
extern HWND g_hMainWnd;
extern HINSTANCE hInst;
void RefreshListView();

FilterState g_filterState;

struct FilterDlgData
{
    std::vector<HWND> formatChecks;
    std::vector<std::string> formatValues;
    std::vector<HWND> resChecks;
    std::vector<std::string> resValues;
};

INT_PTR CALLBACK FilterDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            FilterDlgData* data = new FilterDlgData();
            SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

            HWND hDuration = GetDlgItem(hDlg, IDC_FILTER_DURATION);
            const wchar_t* durItems[] = { L"不关心", L"30分钟以内", L"30-60分钟", L"1-2小时", L"2小时以上" };
            for (int i = 0; i < _countof(durItems); ++i)
                SendMessageW(hDuration, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(durItems[i]));
            SendMessageW(hDuration, CB_SETCURSEL, g_filterState.durationSel, 0);

            HWND hMosaic = GetDlgItem(hDlg, IDC_FILTER_MOSAIC);
            const wchar_t* mosItems[] = { L"不关心", L"无", L"有", L"破解或AI" };
            for (int i = 0; i < _countof(mosItems); ++i)
                SendMessageW(hMosaic, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mosItems[i]));
            SendMessageW(hMosaic, CB_SETCURSEL, g_filterState.mosaicSel, 0);

            // Format checkboxes — position from GROUPBOX rect
            RECT rcFmt;
            GetWindowRect(GetDlgItem(hDlg, IDC_FILTER_GROUPBOX_FORMAT), &rcFmt);
            MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&rcFmt, 2);
            int cols = 4, colW = 70, rowH = 16, padX = 8, padY = 14;
            int fx = rcFmt.left + padX, fy = rcFmt.top + padY;
            int fmtAreaW = rcFmt.right - rcFmt.left - padX * 2;

            db::QueryResult formats;
            g_database.ExecuteQuery("SELECT DISTINCT file_suffix FROM movie ORDER BY file_suffix;", formats);
            for (size_t i = 0; i < formats.size(); ++i)
            {
                std::string fmt = formats[i]["file_suffix"];
                data->formatValues.push_back(fmt);
                std::wstring wfmt = Utf8ToWChar(fmt);
                int cw = (fmtAreaW - (cols - 1) * 4) / cols;
                HWND hCB = CreateWindowW(L"Button", wfmt.c_str(),
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                    fx + static_cast<int>(i % cols) * (cw + 4),
                    fy + static_cast<int>(i / cols) * rowH + 5,
                    cw, rowH - 2,
                    hDlg, (HMENU)(INT_PTR)(IDC_FILTER_FORMAT_START + static_cast<int>(i)),
                    hInst, nullptr);
                HFONT hFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
                if (hFont) SendMessageW(hCB, WM_SETFONT, (WPARAM)hFont, TRUE);
                if (!g_filterState.active || g_filterState.formats.find(wfmt) != g_filterState.formats.end())
                    SendMessageW(hCB, BM_SETCHECK, BST_CHECKED, 0);
                data->formatChecks.push_back(hCB);
            }

            // Resolution checkboxes — position from GROUPBOX rect
            RECT rcRes;
            GetWindowRect(GetDlgItem(hDlg, IDC_FILTER_GROUPBOX_RESOLUTION), &rcRes);
            MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&rcRes, 2);
            int rx = rcRes.left + padX, ry = rcRes.top + padY;
            int resAreaW = rcRes.right - rcRes.left - padX * 2;

            db::QueryResult resolutions;
            g_database.ExecuteQuery("SELECT DISTINCT resolution FROM movie WHERE resolution != '' ORDER BY resolution;", resolutions);
            for (size_t i = 0; i < resolutions.size(); ++i)
            {
                std::string res = resolutions[i]["resolution"];
                data->resValues.push_back(res);
                std::wstring wres = Utf8ToWChar(res);
                int cw = (resAreaW - (cols - 1) * 4) / cols;
                HWND hCB = CreateWindowW(L"Button", wres.c_str(),
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                    rx + static_cast<int>(i % cols) * (cw + 4), 
                    ry + static_cast<int>(i / cols) * rowH + 5,
                    cw, 
                    rowH - 2,
                    hDlg, (HMENU)(INT_PTR)(IDC_FILTER_RESOLUTION_START + static_cast<int>(i)),
                    hInst, nullptr);
                HFONT hFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
                if (hFont) SendMessageW(hCB, WM_SETFONT, (WPARAM)hFont, TRUE);
                if (!g_filterState.active || g_filterState.resolutions.find(res) != g_filterState.resolutions.end())
                    SendMessageW(hCB, BM_SETCHECK, BST_CHECKED, 0);
                data->resChecks.push_back(hCB);
            }

            if (g_hMainWnd)
            {
                RECT rcParent, rcDlg;
                GetWindowRect(g_hMainWnd, &rcParent);
                GetWindowRect(hDlg, &rcDlg);
                int dlgW = rcDlg.right - rcDlg.left, dlgH = rcDlg.bottom - rcDlg.top;
                int parentW = rcParent.right - rcParent.left, parentH = rcParent.bottom - rcParent.top;
                int cx = rcParent.left + (parentW - dlgW) / 2;
                int cy = rcParent.top + (parentH - dlgH) / 2;
                SetWindowPos(hDlg, nullptr, cx, cy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            if (wmId == IDC_FILTER_BTN_FILTER)
            {
                FilterDlgData* data = reinterpret_cast<FilterDlgData*>(
                    GetWindowLongPtr(hDlg, GWLP_USERDATA));
                g_filterState.active = true;
                g_filterState.formats.clear();
                for (size_t i = 0; i < data->formatChecks.size(); ++i)
                    if (SendMessageW(data->formatChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
                        g_filterState.formats.insert(Utf8ToWChar(data->formatValues[i]));
                g_filterState.durationSel = static_cast<int>(
                    SendMessageW(GetDlgItem(hDlg, IDC_FILTER_DURATION), CB_GETCURSEL, 0, 0));
                g_filterState.resolutions.clear();
                for (size_t i = 0; i < data->resChecks.size(); ++i)
                    if (SendMessageW(data->resChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
                        g_filterState.resolutions.insert(data->resValues[i]);
                g_filterState.mosaicSel = static_cast<int>(
                    SendMessageW(GetDlgItem(hDlg, IDC_FILTER_MOSAIC), CB_GETCURSEL, 0, 0));
                delete data;
                SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
                RefreshListView();
                EndDialog(hDlg, IDOK);
            }
            else if (wmId == IDC_FILTER_BTN_CANCEL || wmId == IDCANCEL)
            {
                FilterDlgData* data = reinterpret_cast<FilterDlgData*>(
                    GetWindowLongPtr(hDlg, GWLP_USERDATA));
                delete data;
                SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
                EndDialog(hDlg, IDCANCEL);
            }
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        break;
    }
    return (INT_PTR)FALSE;
}
