#include "AboutDlg.h"
#include "../../Resource.h"
#include "../AppUtils.h"
#include "../ConfigManager.h"
#include "../db/Database.h"

extern db::Database g_database;

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        {
            // Read version from database
            std::string version = LoadSetting(g_database, "exe_version", "0.0.0");
            std::wstring text = L"FilMgr, 版本 " + Utf8ToWChar(version);
            SetDlgItemTextW(hDlg, IDC_ABOUT_VERSION, text.c_str());

            // Read ffmpeg version in real-time
            //std::string ffmpegVer = GetFfmpegVersion();
            //if (!ffmpegVer.empty())
            //{
            //    std::wstring ffmpegText = Utf8ToWChar(ffmpegVer);
            //    SetDlgItemTextW(hDlg, IDC_ABOUT_FFMPEG, ffmpegText.c_str());
            //}
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
