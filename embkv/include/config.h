#pragma once
#include <mutex>
#include <fstream>
#include <nlohmann/json.hpp>
#include "common/util/nocopyable.h"

namespace embkv {
class Config : public util::Nocopyable {
public:
    // ====== 成员变量 ======
    uint64_t    cluster_id{0};
    uint64_t    node_id{0};
    std::string node_name{"Default_Node"};
    std::string ip{"0.0.0.0"};
    uint16_t    port{8080};

    // 从文件加载配置
    static const Config& load() {
        static Config instance;
        static std::once_flag flag;
        std::call_once(flag, [&] {
            std::ifstream file("config.json");
            if (file.good()) {
                nlohmann::json j;
                file >> j;
                instance.cluster_id = j.value("cluster_id", instance.cluster_id);
                instance.node_id = j.value("node_id", instance.node_id);
                instance.node_name = j.value("node_name", instance.node_name);
                instance.ip = j.value("ip", instance.ip);
                instance.port = j.value("port", instance.port);
            } else {
                save(instance);
            }
        });
        return instance;
    }

private:
    Config() = default;

private:
    // 保存配置到文件
    static void save(const Config& config) {
        nlohmann::json j;
        j["cluster_id"] = config.cluster_id;
        j["node_id"] = config.node_id;
        j["node_name"] = config.node_name;
        j["ip"] = config.ip;
        j["port"] = config.port;

        std::ofstream file("config.json");
        if (!file.good()) {
            throw std::runtime_error("Failed to open config file");
        }
        file << j.dump(4);
    }
};
} // namespace embkv