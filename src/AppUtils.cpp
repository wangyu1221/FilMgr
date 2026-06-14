#include "AppUtils.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>

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
    // Convert paths to GBK (936) for ANSI command line
    std::wstring wFilePath = Utf8ToWChar(filePath);
    int plen = WideCharToMultiByte(936, 0, wFilePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string gbkPath(plen, '\0');
    WideCharToMultiByte(936, 0, wFilePath.c_str(), -1, &gbkPath[0], plen, nullptr, nullptr);
    while (!gbkPath.empty() && gbkPath.back() == '\0') gbkPath.pop_back();

    int flen = WideCharToMultiByte(936, 0, ffprobePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string gbkFfprobe(flen, '\0');
    WideCharToMultiByte(936, 0, ffprobePath.c_str(), -1, &gbkFfprobe[0], flen, nullptr, nullptr);
    while (!gbkFfprobe.empty() && gbkFfprobe.back() == '\0') gbkFfprobe.pop_back();

    int dlen = WideCharToMultiByte(936, 0, ffprobeDir.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string gbkDir(dlen, '\0');
    WideCharToMultiByte(936, 0, ffprobeDir.c_str(), -1, &gbkDir[0], dlen, nullptr, nullptr);
    while (!gbkDir.empty() && gbkDir.back() == '\0') gbkDir.pop_back();

    // Temp file for ffprobe stderr output
    char tmpPath[MAX_PATH], tmpFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    GetTempFileNameA(tmpPath, "ffp", 0, tmpFile);

    // Build GBK command — run ffprobe directly, no cmd.exe needed
    std::string cmd = "\"" + gbkFfprobe + "\""
        " -v error -select_streams v:0 -show_entries stream=width,height"
        " -show_entries format=duration -of default=noprint_wrappers=1"
        " -i \"" + gbkPath + "\" -o \"" + std::string(tmpFile) + "\"";

    // Use CreateProcessA with CREATE_NO_WINDOW to hide cmd popup
    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessA(nullptr, &cmd[0], nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW,
                       nullptr, gbkDir.c_str(), &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 15000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // Read temp file
    std::string output;
    FILE* fp = nullptr;
    fopen_s(&fp, tmpFile, "r");
    if (fp) {
        char buf[1024];
        while (fgets(buf, sizeof(buf), fp))
            output += buf;
        fclose(fp);
    }
    DeleteFileA(tmpFile);

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
