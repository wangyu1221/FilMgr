#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

// Forward declare sqlite3 structures, avoid exposing sqlite3.h
struct sqlite3;
struct sqlite3_stmt;

namespace db
{
	// Query result: Each row is a column name -> value mapping
	using Row = std::unordered_map<std::string, std::string>;
	using QueryResult = std::vector<Row>;

	// Database operation status
	enum class Status
	{
		Ok,          // Success
		Error,       // General error
		Busy,        // Database is locked
		NotFound,    // Database file not found
		SqlError,    // SQL syntax error
		NotOpen      // Database is not open
	};

	// Database wrapper class
	class Database
	{
	public:
		Database() = default;
		~Database();

		// Disable copy
		Database(const Database&) = delete;
		Database& operator=(const Database&) = delete;

		// Allow move
		Database(Database&& other) noexcept;
		Database& operator=(Database&& other) noexcept;

		// Open or create database file (path is UTF-8)
		Status Open(const std::string& dbPath);

		// Close database connection
		void Close();

		// Execute SQL without result set (INSERT, UPDATE, DELETE, CREATE TABLE, etc.)
		// Return operation status, errorMsg optionally receives error message
		Status Execute(const std::string& sql, std::string* errorMsg = nullptr);

		// Execute SELECT query, return result set
		Status ExecuteQuery(const std::string& sql, QueryResult& result, std::string* errorMsg = nullptr);

		// Execute parameterized SQL statement (use ? placeholder)
		// Parameters are bound by position, params values are all text types
		Status ExecuteWithParams(const std::string& sql, const std::vector<std::string>& params,
								  std::string* errorMsg = nullptr);

		// Get the rowid of the last inserted row
		int64_t GetLastInsertRowId() const;

		// Get the number of rows affected (from last Execute call)
		int GetChangesCount() const;

		// Check if database is open
		bool IsOpen() const noexcept;

		// Get the most recent error message
		std::string GetLastError() const;

		// Get database file path
		std::string GetDbPath() const;

	private:
		sqlite3* m_db = nullptr;
		std::string m_dbPath;
		mutable std::string m_lastError;
	};

	// Helper function: Convert Status to readable string
	const wchar_t* StatusToString(Status status);
}
