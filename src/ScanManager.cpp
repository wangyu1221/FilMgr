#include "ScanManager.h"
#include "AppUtils.h"
#include "ConfigManager.h"
#include "db/Database.h"

#include <shlobj.h>
#include <set>
#include <string>

// External reference to global database
extern db::Database g_database;

// Show system directory picker dialog
bool ShowOpenDirectoryDialog(HWND hOwner, std::wstring& selectedPath)
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
        bi.lpszTitle = L"请选择包含视频文件的目录";
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
    pDialog->SetTitle(L"请选择包含视频文件的目录");

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

// Scan a single directory for video files and insert into database
int ScanAndInsertMoviesInDir(const std::wstring& dirPath,
                             HWND hStatusText)
{
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
            for (auto& c : suffix) c = towlower(c);
        }

        // Check if it's a video file
        if (!IsVideoFile(suffix))
            continue;

        // Construct full path
        std::wstring fullPath = dirPath + L"\\" + fileName;
        std::string filePath = WCharToUtf8(fullPath);
        std::string fileNameUtf8 = WCharToUtf8(fileName);
        std::string suffixUtf8 = WCharToUtf8(suffix);
        int64_t fileSize = (static_cast<int64_t>(fd.nFileSizeHigh) << 32) |
                           static_cast<int64_t>(fd.nFileSizeLow);

        // Update status text with current file
        if (hStatusText)
        {
            std::wstring status = L"正在扫描: " + fullPath;
            SetWindowTextW(hStatusText, status.c_str());
            UpdateWindow(hStatusText);
        }

        // Check if already exists (by file_path)
        std::string checkSql = "SELECT COUNT(*) AS cnt FROM movie WHERE file_path = '"
                               + filePath + "';";
        db::QueryResult result;
        g_database.ExecuteQuery(checkSql, result);

        if (!result.empty() && result[0]["cnt"] != "0")
            continue; // Already exists, skip

        // Probe video file for resolution and duration
        ProbeResult probe = ProbeVideoFile(filePath);

        // Insert movie record
        std::string insertSql = "INSERT INTO movie "
            "(name, sn, file_path, file_name, file_suffix, file_size, resolution, duration) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
        g_database.ExecuteWithParams(insertSql, {
            fileNameUtf8,
            "",
            filePath,
            fileNameUtf8,
            suffixUtf8,
            std::to_string(fileSize),
            probe.resolution,
            std::to_string(probe.durationSec)
        });

        ++inserted;
    }
    while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return inserted;
}

// Scan directory recursively
int ScanAndInsertMovies(const std::wstring& dirPath, bool includeSubdirs,
                        HWND hStatusText)
{
    int totalInserted = 0;

    // Scan current directory
    totalInserted += ScanAndInsertMoviesInDir(dirPath, hStatusText);

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
                    totalInserted += ScanAndInsertMovies(subDir, true, hStatusText);
                }
            }
            while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    return totalInserted;
}
