#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_set>

struct ScanDir
{
    std::wstring path;
    bool includeSub = true;
};

// Show scanning dialog. Runs scan in background thread, keeps UI responsive.
// existingPaths: optional set of already-known file paths for O(1) dedup.
//                Newly inserted paths are added to the set.
// Returns total new files found, or -1 if cancelled.
int ShowScanningDialog(HWND hParent, const std::vector<ScanDir>& dirs,
                       std::unordered_set<std::string>* existingPaths = nullptr);
