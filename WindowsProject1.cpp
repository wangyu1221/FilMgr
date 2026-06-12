// WindowsProject1.cpp - Main application implementation

#include "framework.h"
#include "WindowsProject1.h"
#include "src/db/Database.h"

#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "comctl32.lib")

#define MAX_LOADSTRING 100
#define IDC_LISTVIEW 1001

// Global variables:
HINSTANCE hInst;                                // Current instance
WCHAR szTitle[MAX_LOADSTRING];                  // Title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // Main window class name
db::Database g_database;                        // Global database instance
HWND g_hListView = nullptr;                     // ListView control handle
HWND g_hMainWnd = nullptr;                      // Main window handle

// Helper function: Get the directory where the executable is located
static std::string GetExeDirectory()
{
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::wstring wpath(path);
	size_t pos = wpath.find_last_of(L"\\/");
	if (pos != std::wstring::npos)
		wpath = wpath.substr(0, pos);

	// Wide character to UTF-8
	int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string result(len - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &result[0], len, nullptr, nullptr);
	return result;
}

// Convert wide character string to UTF-8 string
static std::string WCharToUtf8(const std::wstring& wstr)
{
	if (wstr.empty()) return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string result(len - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
	return result;
}

// Convert UTF-8 string to wide character string
static std::wstring Utf8ToWChar(const std::string& str)
{
	if (str.empty()) return {};
	int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	std::wstring result(len - 1, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
	return result;
}

// Format file size as readable string
static std::wstring FormatFileSize(int64_t size)
{
	if (size < 1024)
		return std::to_wstring(size) + L" B";
	else if (size < 1024 * 1024)
	{
		std::wstringstream ss;
		ss << std::fixed << std::setprecision(1) << (size / 1024.0) << L" KB";
		return ss.str();
	}
	else if (size < 1024LL * 1024 * 1024)
	{
		std::wstringstream ss;
		ss << std::fixed << std::setprecision(1) << (size / (1024.0 * 1024.0)) << L" MB";
		return ss.str();
	}
	else
	{
		std::wstringstream ss;
		ss << std::fixed << std::setprecision(2) << (size / (1024.0 * 1024.0 * 1024.0)) << L" GB";
		return ss.str();
	}
}

// Create database tables if they don't exist
static void CreateMovieTables()
{
	// movie table (mosaic/resolution have no NOT NULL, processed is new)
	std::string sql = R"(
		CREATE TABLE IF NOT EXISTS movie (
			id          INTEGER PRIMARY KEY AUTOINCREMENT,
			name        VARCHAR(256) NOT NULL,
			sn          VARCHAR(32)  NOT NULL DEFAULT '',
			file_path   VARCHAR(256) NOT NULL,
			file_name   VARCHAR(256) NOT NULL,
			file_suffix VARCHAR(16)  NOT NULL,
			file_size   BIGINT       NOT NULL DEFAULT 0,
			mosaic      TINYINT,
			resolution  INT,
			processed   TINYINT      NOT NULL DEFAULT 0
		);
	)";
	g_database.Execute(sql);

	// file_path index
	g_database.Execute(
		"CREATE INDEX IF NOT EXISTS idx_movie_file_path ON movie(file_path);");

	// file_name index
	g_database.Execute(
		"CREATE INDEX IF NOT EXISTS idx_movie_file_name ON movie(file_name);");

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

// Migrate database for existing installations (v0.1 -> v0.2)
static void MigrateDatabase()
{
	// Try adding the processed column (ignore error if it already exists)
	g_database.Execute(
		"ALTER TABLE movie ADD COLUMN processed TINYINT NOT NULL DEFAULT 0;");
}

// Initialize database: open connection and create tables
static void InitDatabase()
{
	std::string dbPath = GetExeDirectory() + "\\app.db";
	std::string errMsg;

	auto status = g_database.Open(dbPath);
	if (status != db::Status::Ok)
	{
		MessageBoxW(nullptr, L"Database open failed!", L"Error", MB_ICONERROR);
		return;
	}

	// Create table structure
	CreateMovieTables();

	// Run migration for existing databases
	MigrateDatabase();
}

// Get movie tags as comma-separated string
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

// Get movie actors as comma-separated string
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

// Check if file path is valid (file exists)
static bool IsFileValid(const std::string& filePath)
{
	std::wstring wpath = Utf8ToWChar(filePath);
	DWORD attrs = GetFileAttributesW(wpath.c_str());
	return (attrs != INVALID_FILE_ATTRIBUTES &&
			!(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

// Refresh ListView display
static void RefreshListView()
{
	if (!g_hListView) return;

	// Clear current items
	ListView_DeleteAllItems(g_hListView);

	// Query all movies from database
	db::QueryResult movies;
	std::string sql = "SELECT id, sn, file_name, file_suffix, file_size, "
					  "file_path, mosaic, resolution FROM movie ORDER BY id;";
	g_database.ExecuteQuery(sql, movies);

	for (size_t i = 0; i < movies.size(); ++i)
	{
		const auto& row = movies[i];

		// SN
		std::wstring sn = Utf8ToWChar(row.at("sn"));

		// File name
		std::wstring fileName = Utf8ToWChar(row.at("file_name"));

		// Format
		std::wstring suffix = Utf8ToWChar(row.at("file_suffix"));

		// Size
		int64_t fileSize = std::stoll(row.at("file_size"));
		std::wstring sizeStr = FormatFileSize(fileSize);

		// Is valid
		bool valid = IsFileValid(row.at("file_path"));
		std::wstring validStr = valid ? L"Yes" : L"No";

		// Tags
		int64_t movieId = std::stoll(row.at("id"));
		std::wstring tags = GetMovieTags(movieId);

		// Actors
		std::wstring actors = GetMovieActors(movieId);

		// Mosaic (may be NULL)
		std::wstring mosaicStr = L"-";
		const std::string& mosaicVal = row.at("mosaic");
		if (!mosaicVal.empty())
		{
			int mosaic = std::stoi(mosaicVal);
			mosaicStr = mosaic == 1 ? L"Yes" : L"No";
		}

		// Resolution (may be NULL)
		std::wstring resolutionStr = L"-";
		const std::string& resVal = row.at("resolution");
		if (!resVal.empty())
		{
			int resolution = std::stoi(resVal);
			switch (resolution)
			{
			case 1: resolutionStr = L"SD"; break;
			case 2: resolutionStr = L"720p"; break;
			case 3: resolutionStr = L"1080p"; break;
			case 4: resolutionStr = L"4K"; break;
			default: resolutionStr = std::to_wstring(resolution); break;
			}
		}

		LVITEMW lvi = {};
		lvi.mask = LVIF_TEXT;
		lvi.iItem = static_cast<int>(i);
		lvi.iSubItem = 0;
		lvi.pszText = const_cast<LPWSTR>(sn.c_str());
		int idx = ListView_InsertItem(g_hListView, &lvi);

		// Set subitems
		ListView_SetItemText(g_hListView, idx, 1, const_cast<LPWSTR>(fileName.c_str()));
		ListView_SetItemText(g_hListView, idx, 2, const_cast<LPWSTR>(suffix.c_str()));
		ListView_SetItemText(g_hListView, idx, 3, const_cast<LPWSTR>(sizeStr.c_str()));
		ListView_SetItemText(g_hListView, idx, 4, const_cast<LPWSTR>(validStr.c_str()));
		ListView_SetItemText(g_hListView, idx, 5, const_cast<LPWSTR>(tags.c_str()));
		ListView_SetItemText(g_hListView, idx, 6, const_cast<LPWSTR>(actors.c_str()));
		ListView_SetItemText(g_hListView, idx, 7, const_cast<LPWSTR>(mosaicStr.c_str()));
		ListView_SetItemText(g_hListView, idx, 8, const_cast<LPWSTR>(resolutionStr.c_str()));
	}
}

// Create ListView control
static HWND CreateListView(HWND hParent)
{
	HWND hListView = CreateWindowW(WC_LISTVIEWW, nullptr,
		WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT |
		LVS_SHOWSELALWAYS | LVS_SINGLESEL,
		0, 0, 0, 0,
		hParent, (HMENU)(INT_PTR)IDC_LISTVIEW, hInst, nullptr);

	if (!hListView) return nullptr;

	// Enable full row selection and grid lines
	ListView_SetExtendedListViewStyle(hListView,
		LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

	// Add columns
	struct { const wchar_t* title; int width; } columns[] = {
		{ L"SN",           100 },
		{ L"File Name",    250 },
		{ L"Format",       60 },
		{ L"Size",         100 },
		{ L"Valid",        80 },
		{ L"Tags",         120 },
		{ L"Actors",       120 },
		{ L"Mosaic",       60 },
		{ L"Resolution",   80 },
	};

	LVCOLUMNW lvc = {};
	lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

	for (int i = 0; i < _countof(columns); ++i)
	{
		lvc.iSubItem = i;
		lvc.pszText = const_cast<LPWSTR>(columns[i].title);
		lvc.cx = columns[i].width;
		ListView_InsertColumn(hListView, i, &lvc);
	}

	return hListView;
}

// Supported video file extensions
static const std::set<std::wstring> g_videoExtensions = {
	L".mp4",  L".avi",  L".mkv",  L".wmv",  L".mov",
	L".flv",  L".webm", L".m4v",  L".mpg",  L".mpeg",
	L".ts",   L".rmvb", L".rm",   L".3gp",  L".vob",
	L".divx", L".xvid", L".asf",  L".f4v",  L".mts",
	L".m2ts", L".ogv",  L".m1v",  L".dat",  L".swf",
};

static bool IsVideoFile(const std::wstring& ext)
{
	return g_videoExtensions.find(ext) != g_videoExtensions.end();
}

// Scan a single directory for video files and insert into database
// Returns the number of newly inserted records
static int ScanAndInsertMoviesInDir(const std::wstring& dirPath)
{
	// Construct search path
	std::wstring searchPath = dirPath + L"\\*";

	WIN32_FIND_DATAW fd;
	HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
	if (hFind == INVALID_HANDLE_VALUE)
		return 0;

	int inserted = 0;

	do
	{
		// Skip . and ..
		if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
			continue;

		// Skip directories (they are handled separately in recursive mode)
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		// Extract file name and extension
		std::wstring fileName(fd.cFileName);
		size_t dotPos = fileName.find_last_of(L'.');
		std::wstring suffix;
		if (dotPos != std::wstring::npos)
		{
			suffix = fileName.substr(dotPos);
			// Convert to lowercase
			for (auto& c : suffix) c = towlower(c);
		}

		// Check if it's a video file
		if (!IsVideoFile(suffix))
			continue;

		// Construct full path
		std::string filePath = WCharToUtf8(dirPath + L"\\" + fileName);
		std::string fileNameUtf8 = WCharToUtf8(fileName);
		std::string suffixUtf8 = WCharToUtf8(suffix);
		int64_t fileSize = (static_cast<int64_t>(fd.nFileSizeHigh) << 32) |
						   static_cast<int64_t>(fd.nFileSizeLow);

		// Check if already exists (by file_path)
		// Path comes from filesystem scan, not user input, safe to embed in SQL
		std::string checkSql = "SELECT COUNT(*) AS cnt FROM movie WHERE file_path = '"
							   + filePath + "';";
		db::QueryResult result;
		g_database.ExecuteQuery(checkSql, result);

		if (!result.empty() && result[0]["cnt"] != "0")
			continue; // Already exists, skip

		// Insert movie record (mosaic/resolution/processed use defaults: NULL/NULL/0)
		std::string insertSql = "INSERT INTO movie "
			"(name, sn, file_path, file_name, file_suffix, file_size) "
			"VALUES (?, ?, ?, ?, ?, ?);";
		g_database.ExecuteWithParams(insertSql, {
			fileNameUtf8,   // name: use file name by default
			"",             // sn: leave empty for manual editing later
			filePath,
			fileNameUtf8,
			suffixUtf8,
			std::to_string(fileSize)
		});

		++inserted;
	}
	while (FindNextFileW(hFind, &fd));

	FindClose(hFind);
	return inserted;
}

// Scan directory for video files and insert into database
// If includeSubdirs is true, recursively scan subdirectories
static int ScanAndInsertMovies(const std::wstring& dirPath, bool includeSubdirs)
{
	int totalInserted = 0;

	// Scan current directory
	totalInserted += ScanAndInsertMoviesInDir(dirPath);

	// Recursively scan subdirectories if requested
	if (includeSubdirs)
	{
		std::wstring searchPath = dirPath + L"\\*";
		WIN32_FIND_DATAW fd;
		HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
		if (hFind != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (wcscmp(fd.cFileName, L".") == 0 ||
					wcscmp(fd.cFileName, L"..") == 0)
					continue;

				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					std::wstring subDir = dirPath + L"\\" + fd.cFileName;
					totalInserted += ScanAndInsertMovies(subDir, true);
				}
			}
			while (FindNextFileW(hFind, &fd));
			FindClose(hFind);
		}
	}

	return totalInserted;
}

// Open directory selection dialog
static bool ShowOpenDirectoryDialog(HWND hOwner, std::wstring& selectedPath)
{
	// Initialize COM
	bool comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

	IFileOpenDialog* pDialog = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
								   IID_IFileOpenDialog,
								   reinterpret_cast<void**>(&pDialog));

	if (FAILED(hr))
	{
		// Fall back to old API
		if (comInitialized) CoUninitialize();

		BROWSEINFOW bi = {};
		bi.hwndOwner = hOwner;
		bi.lpszTitle = L"Please select a directory containing video files";
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

		LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
		if (!pidl) return false;

		wchar_t path[MAX_PATH];
		if (SHGetPathFromIDListW(pidl, path))
			selectedPath = path;
		else
		{
			CoTaskMemFree(pidl);
			return false;
		}
		CoTaskMemFree(pidl);
		return true;
	}

	// Set folder selection mode
	DWORD dwFlags;
	pDialog->GetOptions(&dwFlags);
	pDialog->SetOptions(dwFlags | FOS_PICKFOLDERS);
	pDialog->SetTitle(L"Please select a directory containing video files");

	hr = pDialog->Show(hOwner);
	if (FAILED(hr))
	{
		pDialog->Release();
		if (comInitialized) CoUninitialize();
		return false;
	}

	IShellItem* pItem = nullptr;
	hr = pDialog->GetResult(&pItem);
	if (FAILED(hr))
	{
		pDialog->Release();
		if (comInitialized) CoUninitialize();
		return false;
	}

	LPWSTR pPath = nullptr;
	hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pPath);
	if (SUCCEEDED(hr))
	{
		selectedPath = pPath;
		CoTaskMemFree(pPath);
	}

	pItem->Release();
	pDialog->Release();
	if (comInitialized) CoUninitialize();

	return SUCCEEDED(hr);
}

// Directory selection dialog procedure
static std::wstring g_dlgSelectedPath; // Stores the selected directory path in the dialog

INT_PTR CALLBACK DirSelectDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);

	switch (message)
	{
	case WM_INITDIALOG:
		g_dlgSelectedPath.clear();
		// Set checkbox default: unchecked
		CheckDlgButton(hDlg, IDC_CHK_SUBDIR, BST_UNCHECKED);
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		{
			int wmId = LOWORD(wParam);
			switch (wmId)
			{
			case IDC_BTN_SELECT_DIR:
				{
					std::wstring dirPath;
					if (ShowOpenDirectoryDialog(hDlg, dirPath))
					{
						g_dlgSelectedPath = dirPath;
						SetDlgItemTextW(hDlg, IDC_DIR_PATH, dirPath.c_str());
					}
				}
				break;

			case IDC_BTN_START_SCAN:
				{
					if (g_dlgSelectedPath.empty())
					{
						MessageBoxW(hDlg,
							L"Please select a directory first.",
							L"Info", MB_ICONINFORMATION);
						return (INT_PTR)TRUE;
					}

					// Get checkbox state
					bool includeSubdirs =
						(IsDlgButtonChecked(hDlg, IDC_CHK_SUBDIR) == BST_CHECKED);

					// Scan and insert
					int count = ScanAndInsertMovies(g_dlgSelectedPath, includeSubdirs);

					// Notify user
					std::wstring msg = L"Scan complete. Added " +
									   std::to_wstring(count) + L" video files.";
					MessageBoxW(hDlg, msg.c_str(), L"Info", MB_ICONINFORMATION);

					// Close dialog with IDOK to signal that scanning was done
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

// Forward declarations of functions:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    DirSelectDlgProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
					 _In_opt_ HINSTANCE hPrevInstance,
					 _In_ LPWSTR    lpCmdLine,
					 _In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// Initialize common controls (needed for ListView)
	INITCOMMONCONTROLSEX icex = {};
	icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icex.dwICC = ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&icex);

	// Initialize global strings
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_WINDOWSPROJECT1, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow))
	{
		return FALSE;
	}

	// Initialize local database
	InitDatabase();

	// Refresh ListView display
	RefreshListView();

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINDOWSPROJECT1));

	MSG msg;

	// Main message loop:
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

// Function: MyRegisterClass()
// Purpose: Register the window class.
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style          = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc    = WndProc;
	wcex.cbClsExtra     = 0;
	wcex.cbWndExtra     = 0;
	wcex.hInstance      = hInstance;
	wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINDOWSPROJECT1));
	wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_WINDOWSPROJECT1);
	wcex.lpszClassName  = szWindowClass;
	wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

// Function: InitInstance(HINSTANCE, int)
// Purpose: Save instance handle and create main window
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
	  CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
	  return FALSE;
   }

   g_hMainWnd = hWnd;

   // Create ListView control
   g_hListView = CreateListView(hWnd);

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

// Function: WndProc(HWND, UINT, WPARAM, LPARAM)
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
		// ListView already created in InitInstance
		break;

	case WM_SIZE:
		// Adjust ListView size to fill client area
		if (g_hListView)
		{
			int width = LOWORD(lParam);
			int height = HIWORD(lParam);
			MoveWindow(g_hListView, 0, 0, width, height, TRUE);
		}
		break;

	case WM_COMMAND:
		{
			int wmId = LOWORD(wParam);
			// Parse menu selections:
			switch (wmId)
			{
			case IDM_OPEN_DIR:
				{
					// Open the directory selection dialog
					// Returns IDOK if scan was performed, IDCANCEL otherwise
					INT_PTR result = DialogBox(hInst,
						MAKEINTRESOURCE(IDD_DIR_SELECT), hWnd, DirSelectDlgProc);

					// Refresh ListView if scanning was done
					if (result == IDOK)
					{
						RefreshListView();
					}
				}
				break;

			case IDM_ABOUT:
				DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
				break;

			case IDM_EXIT:
				DestroyWindow(hWnd);
				break;

			default:
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
		}
		break;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			BeginPaint(hWnd, &ps);
			// Background drawing left to ListView control
			EndPaint(hWnd, &ps);
		}
		break;

	case WM_DESTROY:
		// Close database connection
		g_database.Close();
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

// Message handler for About dialog.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
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
