#include "repository/user_repo.h"
#include <spdlog/spdlog.h>

UserRepo::UserRepo(Database& db) : db_(db) {}

bool UserRepo::create(const std::string& username, const std::string& password_hash) {
    const char* sql = "INSERT INTO users (username, password_hash) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("Failed to prepare insert user: {}", sqlite3_errmsg(db_.handle()));
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password_hash.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("Failed to insert user: {}", sqlite3_errmsg(db_.handle()));
        return false;
    }
    return true;
}

std::optional<User> UserRepo::find_by_id(int64_t id) {
    const char* sql = "SELECT id, username, password_hash, created_at, updated_at FROM users WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, id);

    std::optional<User> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id = sqlite3_column_int64(stmt, 0);
        u.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        u.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        result = std::move(u);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::optional<User> UserRepo::find_by_username(const std::string& username) {
    const char* sql = "SELECT id, username, password_hash, created_at, updated_at FROM users WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<User> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id = sqlite3_column_int64(stmt, 0);
        u.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        u.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        result = std::move(u);
    }

    sqlite3_finalize(stmt);
    return result;
}
