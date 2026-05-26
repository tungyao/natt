#pragma once

#include <sqlite3.h>
#include <string>
#include <memory>
#include <functional>

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    sqlite3* handle() const { return db_; }

    bool execute(const std::string& sql);
    bool initialize();

private:
    sqlite3* db_ = nullptr;
};
