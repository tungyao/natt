#pragma once

#include <string>
#include <map>
#include <yaml-cpp/yaml.h>

struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    int workers = 4;
};

struct DatabaseConfig {
    std::string path = "data/natmesh.db";
};

struct JwtConfig {
    std::string secret = "change-me";
    int expiration_hours = 72;
};

struct WebSocketConfig {
    int heartbeat_interval_sec = 15;
    int heartbeat_timeout_sec = 45;
};

struct LogConfig {
    std::string level = "info";
    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v";
};

struct AdminConfig {
    bool enabled = true;
    std::string username = "admin";
    std::string password = "admin123456";
};

struct TlsConfig {
    bool enabled = false;
    std::string cert_file = "server.crt";
    std::string key_file = "server.key";
};

struct IpamConfig {
    std::map<std::string, std::string> pools = {
        {"home", "10.0.0.0/16"},
        {"default", "10.0.0.0/24"}
    };
};

struct Config {
    ServerConfig server;
    DatabaseConfig database;
    JwtConfig jwt;
    WebSocketConfig websocket;
    LogConfig logging;
    AdminConfig admin;
    TlsConfig tls;
    IpamConfig ipam;

    static Config load(const std::string& path);
    static Config load_default();
};
