#pragma once

#include <set>
#include <string>
#include <map>

// Forward declaration
namespace db { class Database; }

// The set of currently active video file extensions (lowercase with dot)
extern std::set<std::wstring> g_videoExtensions;

// Get the default set of video extensions
std::set<std::wstring> GetDefaultVideoExtensions();

// Check if a file extension is a known video type
bool IsVideoFile(const std::wstring& ext);

// Load video extensions from the database setting table
// Falls back to defaults if no setting found
void LoadVideoExtensions(db::Database& db);

// Save video extensions to the database setting table (as JSON)
void SaveVideoExtensions(db::Database& db, const std::set<std::wstring>& extensions);

// Load a single setting value by key
std::string LoadSetting(db::Database& db, const std::string& key,
                        const std::string& defaultValue = "");

// Save a single setting key/value pair (INSERT or REPLACE)
void SaveSetting(db::Database& db, const std::string& key,
                 const std::string& value);

// Parse extension JSON like {".avi":1,".mp4":0} into a set
// Only extensions with value=1 are included
std::set<std::wstring> ParseExtensionJson(const std::string& json);

// Generate extension JSON from the full set of known extensions and the
// enabled subset.  Enabled extensions get value 1, disabled get value 0.
// Format: {".avi":1,".mp4":0,".mkv":1,...}
std::string GenerateExtensionJson(const std::set<std::wstring>& allExtensions,
                                   const std::set<std::wstring>& enabledExtensions);
