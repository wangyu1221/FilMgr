#pragma once

#include <windows.h>
#include <string>

INT_PTR CALLBACK DirSelectDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

extern std::wstring g_dlgSelectedPath;
