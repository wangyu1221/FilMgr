#pragma once

#include <windows.h>
#include <string>
#include <set>

struct FilterState
{
    bool active = false;
    std::set<std::wstring> formats;
    int durationSel = 0;
    std::set<std::string> resolutions;
    int mosaicSel = 0;
};

extern FilterState g_filterState;

INT_PTR CALLBACK FilterDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
