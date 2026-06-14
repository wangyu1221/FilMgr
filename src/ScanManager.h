#pragma once

#include <string>

// Forward declaration
struct HWND__;
typedef HWND__* HWND;

// Open directory selection dialog (system folder picker)
// Returns true if user selected a directory
bool ShowOpenDirectoryDialog(HWND hOwner, std::wstring& selectedPath);

// Scan a single directory for video files and insert into database
// hStatusText: optional HWND to display current file path during scan
// Returns the number of newly inserted records
int ScanAndInsertMoviesInDir(const std::wstring& dirPath,
                             HWND hStatusText = nullptr);

// Scan directory for video files and insert into database
// If includeSubdirs is true, recursively scan subdirectories
// hStatusText: optional HWND to display current file path during scan
int ScanAndInsertMovies(const std::wstring& dirPath, bool includeSubdirs,
                        HWND hStatusText = nullptr);
