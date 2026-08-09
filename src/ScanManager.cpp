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
bool ShowOpenDirectoryDialog(HWND hOwner, std::wstring& selectedPath, const wchar_t* title)
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
        bi.lpszTitle = title;
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
    pDialog->SetTitle(title);

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
int ScanAndInsertFilmsInDir(const std::wstring& dirPath,
                             HWND hStatusText,
                             bool extractSn)
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
            std::wstring status = fullPath;
            SetWindowTextW(hStatusText, status.c_str());
            UpdateWindow(hStatusText);
        }

        // Check if already exists (by file_path)
        // Escape single quotes to prevent SQL syntax error on paths like "F:\User's\file.mp4"
        std::string escPath = filePath;
        size_t pos = 0;
        while ((pos = escPath.find('\'', pos)) != std::string::npos)
        {
            escPath.insert(pos, "'");
            pos += 2;
        }
        std::string checkSql = "SELECT COUNT(*) AS cnt FROM film WHERE file_path = '"
                               + escPath + "';";
        db::QueryResult result;
        g_database.ExecuteQuery(checkSql, result);

        if (!result.empty() && result[0]["cnt"] != "0")
            continue; // Already exists, skip

        // Probe video file for resolution and duration
        ProbeResult probe = ProbeVideoFile(filePath);

        // Extract SN from filename (without extension) if enabled
        std::string sn;
        if (extractSn)
        {
            std::wstring nameWithoutExt = fileName;
            if (dotPos != std::wstring::npos)
                nameWithoutExt = fileName.substr(0, dotPos);
            sn = ExtractSN(nameWithoutExt);
        }

        // Insert film record
        std::string insertSql = "INSERT INTO film "
            "(name, sn, file_path, file_suffix, file_size, resolution, duration, producer) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
        g_database.ExecuteWithParams(insertSql, {
            fileNameUtf8,
            sn,
            filePath,
            suffixUtf8,
            std::to_string(fileSize),
            probe.resolution,
            std::to_string(probe.durationSec),
            ""
        });

        ++inserted;
    }
    while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return inserted;
}

// Scan directory recursively
int ScanAndInsertFilms(const std::wstring& dirPath, bool includeSubdirs,
                        HWND hStatusText,
                        bool extractSn)
{
    int totalInserted = 0;

    // Scan current directory
    totalInserted += ScanAndInsertFilmsInDir(dirPath, hStatusText, extractSn);

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
                    totalInserted += ScanAndInsertFilms(subDir, true, hStatusText, extractSn);
                }
            }
            while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    return totalInserted;
}

// Cancel-aware: Scan a single directory
int ScanAndInsertFilmsInDir(const std::wstring& dirPath,
                             std::atomic<bool>& cancel,
                             std::wstring& currentPath,
                             std::mutex& pathMutex,
                             std::unordered_set<std::string>* existingPaths,
                             bool extractSn)
{
    {
        std::lock_guard<std::mutex> lk(pathMutex);
        currentPath = dirPath;
    }

    std::wstring searchPath = dirPath + L"\\*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;

    int inserted = 0;

    do
    {
        // Check cancel flag
        if (cancel.load()) break;

        // Skip . and ..
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        // Skip directories
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

        // Update current path for UI (protected by mutex)
        {
            std::lock_guard<std::mutex> lk(pathMutex);
            currentPath = fullPath;
        }

        // Check if already exists — use set for O(1) lookup if available
        bool exists = false;
        if (existingPaths)
        {
            exists = (existingPaths->find(filePath) != existingPaths->end());
        }
        else
        {
            // Fall back to SQL query
            std::string escPath = filePath;
            size_t pos = 0;
            while ((pos = escPath.find('\'', pos)) != std::string::npos)
            {
                escPath.insert(pos, "'");
                pos += 2;
            }
            std::string checkSql = "SELECT COUNT(*) AS cnt FROM film WHERE file_path = '"
                                   + escPath + "';";
            db::QueryResult result;
            g_database.ExecuteQuery(checkSql, result);
            exists = (!result.empty() && result[0]["cnt"] != "0");
        }

        if (exists) continue;

        // Probe video file for resolution and duration
        ProbeResult probe = ProbeVideoFile(filePath);

        // Extract SN from filename if enabled
        std::string sn;
        if (extractSn)
        {
            std::wstring nameWithoutExt = fileName;
            if (dotPos != std::wstring::npos)
                nameWithoutExt = fileName.substr(0, dotPos);
            sn = ExtractSN(nameWithoutExt);
        }

        // Insert film record
        std::string insertSql = "INSERT INTO film "
            "(name, sn, file_path, file_suffix, file_size, resolution, duration, producer) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
        g_database.ExecuteWithParams(insertSql, {
            fileNameUtf8,
            sn,
            filePath,
            suffixUtf8,
            std::to_string(fileSize),
            probe.resolution,
            std::to_string(probe.durationSec),
            ""
        });

        // Add to set for future O(1) lookups within this scan session
        if (existingPaths)
            existingPaths->insert(filePath);

        ++inserted;
    }
    while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return inserted;
}

// Cancel-aware: Scan directory recursively
int ScanAndInsertFilms(const std::wstring& dirPath, bool includeSubdirs,
                        std::atomic<bool>& cancel,
                        std::wstring& currentPath,
                        std::mutex& pathMutex,
                        std::unordered_set<std::string>* existingPaths,
                        bool extractSn)
{
    int totalInserted = 0;

    // Scan current directory
    totalInserted += ScanAndInsertFilmsInDir(dirPath, cancel, currentPath, pathMutex, existingPaths, extractSn);

    if (cancel.load()) return totalInserted;

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
                if (cancel.load()) break;

                if (wcscmp(fd.cFileName, L".") == 0 ||
                    wcscmp(fd.cFileName, L"..") == 0)
                    continue;

                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    std::wstring subDir = dirPath + L"\\" + fd.cFileName;
                    totalInserted += ScanAndInsertFilms(subDir, true, cancel, currentPath, pathMutex, existingPaths, extractSn);
                }
            }
            while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    return totalInserted;
}
