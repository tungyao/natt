#include "repository/device_repo.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

DeviceRepo::DeviceRepo(Database& db) : db_(db) {}

bool DeviceRepo::create(const Device& device) {
    const char* sql = R"(
        INSERT INTO devices (node_id, user_id, device_name, public_key, public_ip, public_port, lan_ips)
        VALUES (?, ?, ?, ?, ?, ?, ?);
    )";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("Prepare create device failed: {}", sqlite3_errmsg(db_.handle()));
        return false;
    }

    auto lan_json = nlohmann::json(device.lan_ips).dump();

    sqlite3_bind_text(stmt, 1, device.node_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, device.user_id);
    sqlite3_bind_text(stmt, 3, device.device_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, device.public_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, device.public_ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, device.public_port);
    sqlite3_bind_text(stmt, 7, lan_json.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("Create device failed: {}", sqlite3_errmsg(db_.handle()));
        return false;
    }
    return true;
}

std::optional<Device> DeviceRepo::find_by_node_id(const std::string& node_id) {
    const char* sql = "SELECT id, node_id, user_id, device_name, public_key, public_ip, public_port, lan_ips, online, last_heartbeat, created_at, updated_at FROM devices WHERE node_id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, node_id.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<Device> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Device d;
        d.id = sqlite3_column_int64(stmt, 0);
        d.node_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        d.user_id = sqlite3_column_int64(stmt, 2);
        d.device_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        d.public_key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        d.public_ip = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        d.public_port = sqlite3_column_int(stmt, 6);

        auto lan_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (lan_str) {
            try {
                d.lan_ips = nlohmann::json::parse(lan_str).get<std::vector<std::string>>();
            } catch (...) {}
        }

        d.online = sqlite3_column_int(stmt, 8) != 0;
        auto hb = sqlite3_column_text(stmt, 9);
        if (hb) d.last_heartbeat = reinterpret_cast<const char*>(hb);
        d.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        d.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));

        result = std::move(d);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<Device> DeviceRepo::find_by_user_id(int64_t user_id) {
    const char* sql = "SELECT id, node_id, user_id, device_name, public_key, public_ip, public_port, lan_ips, online, last_heartbeat, created_at, updated_at FROM devices WHERE user_id = ?;";
    sqlite3_stmt* stmt = nullptr;

    std::vector<Device> results;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_int64(stmt, 1, user_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Device d;
        d.id = sqlite3_column_int64(stmt, 0);
        d.node_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        d.user_id = sqlite3_column_int64(stmt, 2);
        d.device_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        d.public_key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        d.public_ip = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        d.public_port = sqlite3_column_int(stmt, 6);

        auto lan_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (lan_str) {
            try { d.lan_ips = nlohmann::json::parse(lan_str).get<std::vector<std::string>>(); } catch (...) {}
        }

        d.online = sqlite3_column_int(stmt, 8) != 0;
        auto hb = sqlite3_column_text(stmt, 9);
        if (hb) d.last_heartbeat = reinterpret_cast<const char*>(hb);
        d.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        d.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));

        results.push_back(std::move(d));
    }

    sqlite3_finalize(stmt);
    return results;
}

bool DeviceRepo::update_connection_info(const std::string& node_id,
                                         const std::string& public_ip, int public_port,
                                         const std::string& lan_ips_json) {
    const char* sql = "UPDATE devices SET public_ip=?, public_port=?, lan_ips=?, updated_at=datetime('now') WHERE node_id=?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, public_ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, public_port);
    sqlite3_bind_text(stmt, 3, lan_ips_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, node_id.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool DeviceRepo::update_online_status(const std::string& node_id, bool online) {
    const char* sql = "UPDATE devices SET online=?, updated_at=datetime('now') WHERE node_id=?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, online ? 1 : 0);
    sqlite3_bind_text(stmt, 2, node_id.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool DeviceRepo::update_heartbeat(const std::string& node_id) {
    const char* sql = "UPDATE devices SET online=1, last_heartbeat=datetime('now'), updated_at=datetime('now') WHERE node_id=?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, node_id.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool DeviceRepo::remove(const std::string& node_id) {
    const char* sql = "DELETE FROM devices WHERE node_id=?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, node_id.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<Device> DeviceRepo::find_offline_timeout(int timeout_sec) {
    const char* sql = R"(
        SELECT id, node_id, user_id, device_name, public_key, public_ip, public_port, lan_ips, online, last_heartbeat, created_at, updated_at
        FROM devices WHERE online=1 AND last_heartbeat IS NOT NULL
        AND (julianday('now') - julianday(last_heartbeat)) * 86400 > ?;
    )";
    sqlite3_stmt* stmt = nullptr;

    std::vector<Device> results;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_int(stmt, 1, timeout_sec);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Device d;
        d.id = sqlite3_column_int64(stmt, 0);
        d.node_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        d.user_id = sqlite3_column_int64(stmt, 2);
        d.device_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        d.public_key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        d.public_ip = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        d.public_port = sqlite3_column_int(stmt, 6);
        auto lan_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (lan_str) {
            try { d.lan_ips = nlohmann::json::parse(lan_str).get<std::vector<std::string>>(); } catch (...) {}
        }
        d.online = sqlite3_column_int(stmt, 8) != 0;
        auto hb = sqlite3_column_text(stmt, 9);
        if (hb) d.last_heartbeat = reinterpret_cast<const char*>(hb);
        d.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        d.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        results.push_back(std::move(d));
    }

    sqlite3_finalize(stmt);
    return results;
}
