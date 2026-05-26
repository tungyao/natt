#include "repository/database.h"
#include <spdlog/spdlog.h>
#include <filesystem>

Database::Database(const std::string& path) {
    // Ensure parent directory exists
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        spdlog::error("Failed to open database: {}", sqlite3_errmsg(db_));
        db_ = nullptr;
        return;
    }

    spdlog::info("Database opened: {}", path);

    // Enable WAL mode for better concurrency
    execute("PRAGMA journal_mode=WAL;");
    execute("PRAGMA foreign_keys=ON;");
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
        spdlog::info("Database closed");
    }
}

bool Database::execute(const std::string& sql) {
    if (!db_) return false;
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        spdlog::error("SQL error: {} (rc={})", err ? err : "unknown", rc);
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool Database::initialize() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS users (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            username        TEXT NOT NULL UNIQUE,
            password_hash   TEXT NOT NULL,
            created_at      TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at      TEXT NOT NULL DEFAULT (datetime('now'))
        );

        CREATE TABLE IF NOT EXISTS devices (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            node_id         TEXT NOT NULL UNIQUE,
            user_id         INTEGER NOT NULL,
            device_name     TEXT NOT NULL,
            public_key      TEXT NOT NULL DEFAULT '',
            public_ip       TEXT NOT NULL DEFAULT '',
            public_port     INTEGER NOT NULL DEFAULT 0,
            lan_ips         TEXT NOT NULL DEFAULT '[]',
            online          INTEGER NOT NULL DEFAULT 0,
            last_heartbeat  TEXT,
            created_at      TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at      TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS networks (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT NOT NULL,
            owner_id    INTEGER NOT NULL,
            created_at  TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at  TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS network_devices (
            network_id  INTEGER NOT NULL,
            device_id   INTEGER NOT NULL,
            joined_at   TEXT NOT NULL DEFAULT (datetime('now')),
            PRIMARY KEY (network_id, device_id),
            FOREIGN KEY (network_id) REFERENCES networks(id) ON DELETE CASCADE,
            FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_devices_user_id ON devices(user_id);
        CREATE INDEX IF NOT EXISTS idx_devices_node_id ON devices(node_id);
        CREATE INDEX IF NOT EXISTS idx_network_devices_network ON network_devices(network_id);
        CREATE INDEX IF NOT EXISTS idx_network_devices_device ON network_devices(device_id);
    )";

    return execute(schema);
}
