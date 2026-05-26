#include "repository/network_repo.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

NetworkRepo::NetworkRepo(Database& db) : db_(db) {}

bool NetworkRepo::create(const std::string& name, int64_t owner_id) {
    const char* sql = "INSERT INTO networks (name, owner_id) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("Prepare create network failed: {}", sqlite3_errmsg(db_.handle()));
        return false;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, owner_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("Create network failed: {}", sqlite3_errmsg(db_.handle()));
        return false;
    }
    return true;
}

std::optional<Network> NetworkRepo::find_by_id(int64_t id) {
    const char* sql = "SELECT id, name, owner_id, created_at, updated_at FROM networks WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, id);

    std::optional<Network> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Network n;
        n.id = sqlite3_column_int64(stmt, 0);
        n.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        n.owner_id = sqlite3_column_int64(stmt, 2);
        n.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        n.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        result = std::move(n);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<Network> NetworkRepo::find_by_owner(int64_t owner_id) {
    const char* sql = "SELECT id, name, owner_id, created_at, updated_at FROM networks WHERE owner_id = ?;";
    sqlite3_stmt* stmt = nullptr;

    std::vector<Network> results;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_int64(stmt, 1, owner_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Network n;
        n.id = sqlite3_column_int64(stmt, 0);
        n.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        n.owner_id = sqlite3_column_int64(stmt, 2);
        n.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        n.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        results.push_back(std::move(n));
    }

    sqlite3_finalize(stmt);
    return results;
}

bool NetworkRepo::add_device(int64_t network_id, int64_t device_id) {
    const char* sql = "INSERT OR IGNORE INTO network_devices (network_id, device_id) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("Prepare add device to network failed: {}", sqlite3_errmsg(db_.handle()));
        return false;
    }

    sqlite3_bind_int64(stmt, 1, network_id);
    sqlite3_bind_int64(stmt, 2, device_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("Add device to network failed: {}", sqlite3_errmsg(db_.handle()));
        return false;
    }
    return true;
}

bool NetworkRepo::remove_device(int64_t network_id, int64_t device_id) {
    const char* sql = "DELETE FROM network_devices WHERE network_id=? AND device_id=?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, network_id);
    sqlite3_bind_int64(stmt, 2, device_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<Device> NetworkRepo::get_network_devices(int64_t network_id) {
    const char* sql = R"(
        SELECT d.id, d.node_id, d.user_id, d.device_name, d.public_key,
               d.public_ip, d.public_port, d.lan_ips, d.online, d.last_heartbeat,
               d.created_at, d.updated_at
        FROM devices d
        JOIN network_devices nd ON d.id = nd.device_id
        WHERE nd.network_id = ?;
    )";
    sqlite3_stmt* stmt = nullptr;

    std::vector<Device> results;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_int64(stmt, 1, network_id);

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

bool NetworkRepo::is_device_in_network(int64_t network_id, int64_t device_id) {
    const char* sql = "SELECT 1 FROM network_devices WHERE network_id=? AND device_id=?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, network_id);
    sqlite3_bind_int64(stmt, 2, device_id);

    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

bool NetworkRepo::remove(int64_t network_id) {
    const char* sql = "DELETE FROM networks WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, network_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}
