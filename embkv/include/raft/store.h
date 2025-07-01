#pragma once
#include <string>
#include <boost/optional.hpp>
#include <sqlite3.h>

class RaftStore {
public:
    // 打开或创建数据库
    explicit RaftStore(const std::string& db_path = "raftstore.db") {
        sqlite3_open(db_path.c_str(), &db_);
        sqlite3_exec(db_, "CREATE TABLE IF NOT EXISTS kv (key TEXT PRIMARY KEY, value TEXT)", nullptr, nullptr, nullptr);
    }

    ~RaftStore() {
        sqlite3_close(db_);
    }

    // 插入或更新键值对
    void put(const std::string& key, const std::string& value) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO kv (key, value) VALUES (?, ?)", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // 获取键对应的值
    boost::optional<std::string> get(const std::string& key) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, "SELECT value FROM kv WHERE key = ?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

        boost::optional<std::string> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        }

        sqlite3_finalize(stmt);
        return result;
    }

    // 删除键值对
    void del(const std::string& key) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, "DELETE FROM kv WHERE key = ?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

private:
    sqlite3* db_;
};
