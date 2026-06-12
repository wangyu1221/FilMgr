// Database.cpp: SQLite local database implementation

#include "Database.h"

// SQLite3 header file (used internally, not exposed to external)
#include "../sqlite/sqlite3.h"

#include <cassert>
#include <sstream>

namespace db
{
	// Destructor
	Database::~Database()
	{
		Close();
	}

	// Move constructor
	Database::Database(Database&& other) noexcept
		: m_db(other.m_db)
		, m_dbPath(std::move(other.m_dbPath))
		, m_lastError(std::move(other.m_lastError))
	{
		other.m_db = nullptr;
	}

	// Move assignment
	Database& Database::operator=(Database&& other) noexcept
	{
		if (this != &other)
		{
			Close();
			m_db = other.m_db;
			m_dbPath = std::move(other.m_dbPath);
			m_lastError = std::move(other.m_lastError);
			other.m_db = nullptr;
		}
		return *this;
	}

	// Open: Open or create SQLite database file
	Status Database::Open(const std::string& dbPath)
	{
		// Close if already open
		Close();

		m_dbPath = dbPath;

		// SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE: Create if not exists
		// SQLITE_OPEN_FULLMUTEX: Enable thread-safe mutex
		int rc = sqlite3_open_v2(
			dbPath.c_str(),
			&m_db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
			nullptr  // Don't use VFS
		);

		if (rc != SQLITE_OK)
		{
			m_lastError = sqlite3_errmsg(m_db);
			sqlite3_close(m_db);
			m_db = nullptr;
			return Status::Error;
		}

		// Enable WAL mode (Write-Ahead Logging) to improve concurrent write performance
		Execute("PRAGMA journal_mode=WAL;");

		return Status::Ok;
	}

	// Close: Close database connection
	void Database::Close()
	{
		if (m_db != nullptr)
		{
			sqlite3_close_v2(m_db);
			m_db = nullptr;
		}
		m_dbPath.clear();
		m_lastError.clear();
	}

	// Execute: Execute SQL without result set
	Status Database::Execute(const std::string& sql, std::string* errorMsg)
	{
		if (!IsOpen())
		{
			if (errorMsg) *errorMsg = "Database not open";
			return Status::NotOpen;
		}

		char* err = nullptr;
		int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &err);

		if (rc != SQLITE_OK)
		{
			if (err)
			{
				m_lastError = err;
				if (errorMsg) *errorMsg = err;
				sqlite3_free(err);
			}
			else
			{
				m_lastError = sqlite3_errmsg(m_db);
				if (errorMsg) *errorMsg = m_lastError;
			}

			switch (rc)
			{
			case SQLITE_BUSY: return Status::Busy;
			default:          return Status::SqlError;
			}
		}

		return Status::Ok;
	}

	// ExecuteQuery: Execute SELECT query and return result set
	Status Database::ExecuteQuery(const std::string& sql, QueryResult& result, std::string* errorMsg)
	{
		if (!IsOpen())
		{
			if (errorMsg) *errorMsg = "Database not open";
			return Status::NotOpen;
		}

		result.clear();

		sqlite3_stmt* stmt = nullptr;
		int rc = sqlite3_prepare_v2(m_db, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);

		if (rc != SQLITE_OK)
		{
			m_lastError = sqlite3_errmsg(m_db);
			if (errorMsg) *errorMsg = m_lastError;
			return Status::SqlError;
		}

		// Get column count and names
		int colCount = sqlite3_column_count(stmt);
		std::vector<std::string> colNames;
		colNames.reserve(colCount);
		for (int i = 0; i < colCount; ++i)
		{
			const char* name = sqlite3_column_name(stmt, i);
			colNames.emplace_back(name ? name : "");
		}

		// Iterate result rows
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
		{
			Row row;
			for (int i = 0; i < colCount; ++i)
			{
				const unsigned char* text = sqlite3_column_text(stmt, i);
				row[colNames[i]] = text ? reinterpret_cast<const char*>(text) : "";
			}
			result.push_back(std::move(row));
		}

		sqlite3_finalize(stmt);

		if (rc != SQLITE_DONE)
		{
			m_lastError = sqlite3_errmsg(m_db);
			if (errorMsg) *errorMsg = m_lastError;
			return Status::SqlError;
		}

		return Status::Ok;
	}

	// ExecuteWithParams: Execute SQL with parameters (prevent SQL injection)
	Status Database::ExecuteWithParams(const std::string& sql, const std::vector<std::string>& params,
										std::string* errorMsg)
	{
		if (!IsOpen())
		{
			if (errorMsg) *errorMsg = "Database not open";
			return Status::NotOpen;
		}

		sqlite3_stmt* stmt = nullptr;
		int rc = sqlite3_prepare_v2(m_db, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);

		if (rc != SQLITE_OK)
		{
			m_lastError = sqlite3_errmsg(m_db);
			if (errorMsg) *errorMsg = m_lastError;
			return Status::SqlError;
		}

		// Bind parameters (1-based index)
		for (size_t i = 0; i < params.size(); ++i)
		{
			rc = sqlite3_bind_text(stmt, static_cast<int>(i + 1),
								   params[i].c_str(),
								   static_cast<int>(params[i].size()),
								   SQLITE_TRANSIENT);
			if (rc != SQLITE_OK)
			{
				m_lastError = sqlite3_errmsg(m_db);
				if (errorMsg) *errorMsg = m_lastError;
				sqlite3_finalize(stmt);
				return Status::SqlError;
			}
		}

		// Execute
		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);

		if (rc != SQLITE_DONE && rc != SQLITE_ROW)
		{
			m_lastError = sqlite3_errmsg(m_db);
			if (errorMsg) *errorMsg = m_lastError;
			return Status::SqlError;
		}

		return Status::Ok;
	}

	// GetLastInsertRowId
	int64_t Database::GetLastInsertRowId() const
	{
		if (!IsOpen()) return 0;
		return sqlite3_last_insert_rowid(m_db);
	}

	// GetChangesCount
	int Database::GetChangesCount() const
	{
		if (!IsOpen()) return 0;
		return sqlite3_changes(m_db);
	}

	// IsOpen
	bool Database::IsOpen() const noexcept
	{
		return m_db != nullptr;
	}

	// GetLastError
	std::string Database::GetLastError() const
	{
		if (!IsOpen()) return m_lastError;
		const char* msg = sqlite3_errmsg(m_db);
		return msg ? msg : "";
	}

	// GetDbPath
	std::string Database::GetDbPath() const
	{
		return m_dbPath;
	}

	// StatusToString: Helper function
	const wchar_t* StatusToString(Status status)
	{
		switch (status)
		{
		case Status::Ok:        return L"Success";
		case Status::Error:     return L"Error";
		case Status::Busy:      return L"Database Busy";
		case Status::NotFound:  return L"Not Found";
		case Status::SqlError:  return L"SQL Error";
		case Status::NotOpen:   return L"Database Not Open";
		default:                return L"Unknown Status";
		}
	}
}
