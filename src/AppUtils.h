#pragma once

#include <string>
#include <sstream>
#include <iomanip>

// Convert wide character string to UTF-8 string
std::string WCharToUtf8(const std::wstring& wstr);

// Convert UTF-8 string to wide character string
std::wstring Utf8ToWChar(const std::string& str);

// Format file size as readable string
std::wstring FormatFileSize(int64_t size);

// Get the directory where the executable is located
std::string GetExeDirectory();

// Check if file path is valid (file exists on disk)
bool IsFileValid(const std::string& filePath);

// Result of probing a video file with ffprobe
struct ProbeResult
{
    std::string resolution;   // e.g. "1920x1080", empty if unknown
    int durationSec;          // duration in seconds, 0 if unknown
};

// Probe a video file using ffprobe.exe (expected in the same directory as the exe)
// Returns resolution string and duration in seconds
ProbeResult ProbeVideoFile(const std::string& filePath);

// Extract SN (serial number) from a filename (without extension)
// Recognizes patterns like: 000000-00, XXX-000, FC2PPV-0000000, etc.
// Returns the matched SN string, or empty string if no pattern matches
std::string ExtractSN(const std::wstring& fileName);
