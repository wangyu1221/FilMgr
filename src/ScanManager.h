#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <unordered_set>

// Forward declaration
struct HWND__;
typedef HWND__* HWND;

// Open directory selection dialog (system folder picker)
// Returns true if user selected a directory
bool ShowOpenDirectoryDialog(HWND hOwner, std::wstring& selectedPath);

// Scan a single directory for video files and insert into database
// hStatusText: optional HWND to display current file path during scan
// extractSn: if true, try to extract SN from filenames
// Returns the number of newly inserted records
int ScanAndInsertFilmsInDir(const std::wstring& dirPath,
                             HWND hStatusText = nullptr,
                             bool extractSn = false);

// Scan directory for video files and insert into database
// If includeSubdirs is true, recursively scan subdirectories
// hStatusText: optional HWND to display current file path during scan
// extractSn: if true, try to extract SN from filenames
int ScanAndInsertFilms(const std::wstring& dirPath, bool includeSubdirs,
                        HWND hStatusText = nullptr,
                        bool extractSn = false);

// Cancel-aware overloads: check cancel flag between files/directories,
// update currentPath (under pathMutex) with the path currently being scanned.
// existingPaths: optional set for O(1) dedup; falls back to SQL if nullptr.
//                Newly inserted paths are added to the set automatically.
int ScanAndInsertFilmsInDir(const std::wstring& dirPath,
                             std::atomic<bool>& cancel,
                             std::wstring& currentPath,
                             std::mutex& pathMutex,
                             std::unordered_set<std::string>* existingPaths = nullptr,
                             bool extractSn = false);

int ScanAndInsertFilms(const std::wstring& dirPath, bool includeSubdirs,
                        std::atomic<bool>& cancel,
                        std::wstring& currentPath,
                        std::mutex& pathMutex,
                        std::unordered_set<std::string>* existingPaths = nullptr,
                        bool extractSn = false);
