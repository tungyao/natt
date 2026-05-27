#include "config/config.h"
#include <spdlog/spdlog.h>
#include <filesystem>

Config Config::load(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        spdlog::warn("Config file {} not found, using defaults", path);
        return load_default();
    }

    Config cfg;
    YAML::Node root = YAML::LoadFile(path);

    if (auto s = root["server"]) {
        if (s["host"]) cfg.server.host = s["host"].as<std::string>();
        if (s["port"]) cfg.server.port = s["port"].as<int>();
        if (s["workers"]) cfg.server.workers = s["workers"].as<int>();
    }

    if (auto d = root["database"]) {
        if (d["path"]) cfg.database.path = d["path"].as<std::string>();
    }

    if (auto j = root["jwt"]) {
        if (j["secret"]) cfg.jwt.secret = j["secret"].as<std::string>();
        if (j["expiration_hours"]) cfg.jwt.expiration_hours = j["expiration_hours"].as<int>();
    }

    if (auto w = root["websocket"]) {
        if (w["heartbeat_interval_sec"]) cfg.websocket.heartbeat_interval_sec = w["heartbeat_interval_sec"].as<int>();
        if (w["heartbeat_timeout_sec"]) cfg.websocket.heartbeat_timeout_sec = w["heartbeat_timeout_sec"].as<int>();
    }

    if (auto l = root["logging"]) {
        if (l["level"]) cfg.logging.level = l["level"].as<std::string>();
        if (l["pattern"]) cfg.logging.pattern = l["pattern"].as<std::string>();
    }

    if (auto ipam = root["ipam"]) {
        if (auto pools = ipam["pools"]) {
            cfg.ipam.pools.clear();
            for (auto it = pools.begin(); it != pools.end(); ++it) {
                cfg.ipam.pools[it->first.as<std::string>()] = it->second.as<std::string>();
            }
        }
    }

    return cfg;
}

Config Config::load_default() {
    return Config{};
}
