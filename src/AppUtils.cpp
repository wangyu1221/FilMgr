#include "AppUtils.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>

std::string GetExeDirectory()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring wpath(path);
    size_t pos = wpath.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        wpath = wpath.substr(0, pos);

    int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

std::string WCharToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWChar(const std::string& str)
{
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
    return result;
}

std::wstring FormatFileSize(int64_t size)
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

bool IsFileValid(const std::string& filePath)
{
    std::wstring wpath = Utf8ToWChar(filePath);
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES &&
            !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

// Extract SN from filename (without extension)
// Recognizes patterns: 000000-00, XXX-000, FC2PPV-0000000, etc.
// X = any English letter, 0 = any digit
// Post-processing: replace _ and space with -; insert - between letters and digits if missing
std::string ExtractSN(const std::wstring& fileName)
{
    // Regex matching all SN formats, ordered from most specific to most general.
    // The | alternation tries each branch in order at each starting position;
    // {3,5} is greedy so it prefers the longest letter sequence.
    static const std::wregex snRegex(
        L"(FC2-PPV-\\d{5,7})"           // FC2-PPV-0000000
        L"|(FC2 PPV \\d{5,7})"           // FC2 PPV 0000000
        L"|(FC2PPV-\\d{5,7})"           // FC2PPV-0000000
        L"|(\\d{6}(-|_)\\d{3})"           // 000000-000
        L"|(\\d{6}(-|_)\\d{2})"           // 000000-00
        L"|([A-Za-z]{5}( |-|_)\\d{4})"   // XXXXX 0000
        L"|([A-Za-z]{3,5}( |-|_)\\d{3})"    // XXX-000, XXXX-000, XXXXX-000
        L"|([A-Za-z]{3,5}\\d{3})",   // XXX000, XXXX000, 
        std::regex_constants::icase
    );

    std::wsmatch match;
    if (!std::regex_search(fileName, match, snRegex))
        return "";

    std::wstring result = match.str();

    // Convert all lowercase English letters to uppercase
    for (auto& c : result)
    {
        if (c >= L'a' && c <= L'z')
            c = c - L'a' + L'A';
    }

    // Step 1: Replace underscores and spaces with hyphens
    for (auto& c : result)
    {
        if (c == L'_' || c == L' ')
            c = L'-';
    }

    // Step 2: Normalize FC2PPV to FC2-PPV (case-insensitive)
    {
        auto toLower = [](wchar_t c) -> wchar_t {
            return (c >= L'A' && c <= L'Z') ? (c - L'A' + L'a') : c;
        };
        std::wstring lower = result;
        for (auto& c : lower) c = toLower(c);
        size_t pos = lower.find(L"fc2ppv");
        if (pos != std::wstring::npos)
            result.replace(pos, 6, L"FC2-PPV");
    }

    // Step 3: If no hyphen separator exists, insert '-' between letters and digits
    if (result.find(L'-') == std::wstring::npos)
    {
        auto isAlpha = [](wchar_t c) -> bool {
            return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
        };
        auto isDigit = [](wchar_t c) -> bool {
            return c >= L'0' && c <= L'9';
        };
        for (size_t i = 0; i + 1 < result.length(); ++i)
        {
            if (isAlpha(result[i]) && isDigit(result[i + 1]))
            {
                result.insert(i + 1, L"-");
                break;
            }
        }
    }

    return WCharToUtf8(result);
}

ProbeResult ProbeVideoFile(const std::string& filePath)
{
    ProbeResult result;
    result.resolution.clear();
    result.durationSec = 0;

    std::wstring exeDir = Utf8ToWChar(GetExeDirectory());
    std::wstring ffprobePath = exeDir + L"\\ffmpeg-master-latest-win64-gpl-shared\\bin\\ffprobe.exe";
    if (!IsFileValid(WCharToUtf8(ffprobePath)))
        return result;

    // CWD must be ffprobe's directory (DLL lookup)
    std::wstring ffprobeDir = ffprobePath.substr(0, ffprobePath.find_last_of(L"\\/"));
    std::wstring wFilePath = Utf8ToWChar(filePath);

    // Temp file for ffprobe stderr output (use W variants for path)
    wchar_t tmpPathW[MAX_PATH], tmpFileW[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPathW);
    GetTempFileNameW(tmpPathW, L"ffp", 0, tmpFileW);

    // Build wide-char command — use CreateProcessW to support emoji/Unicode paths
    std::wstring cmd = L"\"" + ffprobePath + L"\""
        L" -v error -select_streams v:0 -show_entries stream=width,height"
        L" -show_entries format=duration -of default=noprint_wrappers=1"
        L" -i \"" + wFilePath + L"\" -o \"" + tmpFileW + L"\"";

    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW,
                       nullptr, ffprobeDir.c_str(), &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 15000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // Read temp file
    std::string output;
    FILE* fp = nullptr;
    _wfopen_s(&fp, tmpFileW, L"r");
    if (fp) {
        char buf[1024];
        while (fgets(buf, sizeof(buf), fp))
            output += buf;
        fclose(fp);
    }
    DeleteFileW(tmpFileW);

    // Parse: width=NNN, height=NNN, duration=N.NNNNNN
    int width = 0, height = 0;
    double duration = 0.0;
    bool hasWidth = false, hasHeight = false, hasDuration = false;

    size_t pos = 0;
    while (pos < output.size())
    {
        size_t eol = output.find('\n', pos);
        if (eol == std::string::npos) eol = output.size();
        std::string line = output.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = eol + 1;
        if (line.find("width=") == 0)
            { width = std::atoi(line.c_str() + 6); hasWidth = true; }
        else if (line.find("height=") == 0)
            { height = std::atoi(line.c_str() + 7); hasHeight = true; }
        else if (line.find("duration=") == 0)
            { duration = std::atof(line.c_str() + 9); hasDuration = true; }
    }

    if (hasWidth && hasHeight)
        result.resolution = std::to_string(width) + "*" + std::to_string(height);
    if (hasDuration)
        result.durationSec = static_cast<int>(duration);

    return result;
}
