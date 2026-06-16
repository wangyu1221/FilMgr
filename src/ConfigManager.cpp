#include "ConfigManager.h"
#include "AppUtils.h"
#include "db/Database.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <algorithm>
#include <sstream>

// Default video extensions
static const std::set<std::wstring> s_defaultExtensions = {
    L".mp4",  L".avi",  L".mkv",  L".wmv",  L".mov",
    L".flv",  L".webm", L".m4v",  L".mpg",  L".mpeg",
    L".ts",   L".rmvb", L".rm",   L".3gp",  L".vob",
    L".divx", L".xvid", L".asf",  L".f4v",  L".mts",
    L".m2ts", L".ogv",  L".m1v",  L".dat",  L".swf",
};

// The active set of video extensions (loaded from DB)
std::set<std::wstring> g_videoExtensions;

std::set<std::wstring> GetDefaultVideoExtensions()
{
    return s_defaultExtensions;
}

bool IsVideoFile(const std::wstring& ext)
{
    return g_videoExtensions.find(ext) != g_videoExtensions.end();
}

// Simple JSON parser for format: {"key1":val1,"key2":val2,...}
// Returns set of keys where value != 0
std::set<std::wstring> ParseExtensionJson(const std::string& json)
{
    std::set<std::wstring> result;
    if (json.empty()) return result;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return result;

    for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it)
    {
        if (it->value.IsInt() && it->value.GetInt() != 0)
            result.insert(Utf8ToWChar(it->name.GetString()));
    }
    return result;
}

std::string GenerateExtensionJson(const std::set<std::wstring>& allExtensions,
                                   const std::set<std::wstring>& enabledExtensions)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    for (const auto& ext : allExtensions)
    {
        writer.Key(WCharToUtf8(ext).c_str());
        writer.Int(enabledExtensions.count(ext) ? 1 : 0);
    }
    writer.EndObject();
    return sb.GetString();
}

void LoadVideoExtensions(db::Database& db)
{
    std::string json = LoadSetting(db, "extentions");
    if (json.empty())
    {
        // First run: seed DB with all defaults enabled
        g_videoExtensions = GetDefaultVideoExtensions();
        SaveVideoExtensions(db, g_videoExtensions);
        return;
    }

    auto loaded = ParseExtensionJson(json);
    g_videoExtensions = loaded;
}

void SaveVideoExtensions(db::Database& db, const std::set<std::wstring>& enabledExtensions)
{
    std::set<std::wstring> allExtensions = GetDefaultVideoExtensions();
    std::string json = GenerateExtensionJson(allExtensions, enabledExtensions);
    SaveSetting(db, "extentions", json);
    g_videoExtensions = enabledExtensions;
}

std::string LoadSetting(db::Database& db, const std::string& key,
                        const std::string& defaultValue)
{
    std::string sql = "SELECT setting_value FROM setting WHERE setting_key = '"
                      + key + "';";
    db::QueryResult result;
    db.ExecuteQuery(sql, result);

    if (!result.empty())
        return result[0]["setting_value"];

    return defaultValue;
}

void SaveSetting(db::Database& db, const std::string& key,
                 const std::string& value)
{
    // Use INSERT OR REPLACE (UPSERT pattern)
    std::string sql = "DELETE FROM setting WHERE setting_key = '" + key + "';";
    db.Execute(sql);

    sql = "INSERT INTO setting (setting_key, setting_value) VALUES ('"
          + key + "', '" + value + "');";
    db.Execute(sql);
}
