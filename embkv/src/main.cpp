#include "raft/node.h"
#include "client/client.h"
#include "log.h"
#include <atomic>

using namespace embkv::raft;
using namespace embkv::client;
using namespace embkv::log;

std::atomic<bool> running{true};

std::unordered_map<uint64_t, std::unique_ptr<Peer>> create_raft_peers() {
    std::unordered_map<uint64_t, std::unique_ptr<Peer>> peers;
    peers.emplace(0, std::make_unique<Peer>(0, 0, "Node0", "127.0.0.1", 8081));
    peers.emplace(1, std::make_unique<Peer>(0, 1, "Node1", "127.0.0.1", 8082));
    peers.emplace(2, std::make_unique<Peer>(0, 2, "Node2", "127.0.0.1", 8083));
    return peers;
}

std::unordered_map<uint64_t, std::unique_ptr<Client::RaftNodeInfo>> create_client_endpoints() {
    std::unordered_map<uint64_t, std::unique_ptr<Client::RaftNodeInfo>> endpoints;
    endpoints.emplace(0, std::make_unique<Client::RaftNodeInfo>("127.0.0.1", 8081));
    endpoints.emplace(1, std::make_unique<Client::RaftNodeInfo>("127.0.0.1", 8082));
    endpoints.emplace(2, std::make_unique<Client::RaftNodeInfo>("127.0.0.1", 8083));
    return endpoints;
}

void run_raft_node() {
    auto peers = create_raft_peers();
    auto transport = std::make_shared<Transport>(std::move(peers));
    transport->run();

    RaftNode node(transport);
    node.run();

    std::string option;
    console().info("Raft node started successfully");
    std::cout << "1. q to quit" << std::endl;
    std::cout << "2. r to run" << std::endl;
    std::cout << "3. s to stop" << std::endl;
    while (running) {
        std::cin >> option;
        if (option == "q") {
            running = false;
        } else if (option == "r") {
            node.run();
        } else {
            node.stop();
        }
    }
}

void run_client_node() {
    auto endpoints = create_client_endpoints();
    Client client{0, std::move(endpoints)};
    client.run();

    std::string option;
    console().info("Client started successfully ('q' to quit)");
    std::cout << "1. quit" << std::endl;
    std::cout << "2. run" << std::endl;
    std::cout << "3. stop" << std::endl;
    std::cout << "4. put" << std::endl;
    std::cout << "5. get" << std::endl;
    std::cout << "6. del" << std::endl;
    while (running) {
        std::cin >> option;
        if (option == "1") {
            running = false;
        } else if (option == "2") {
            client.run();
        } else if (option == "3") {
            client.stop();
        } else if (option == "4") {
            std::string key;
            std::string value;
            std::cin >> key >> value;
            client.put(std::move(key), std::move(value));
        } else if (option == "5") {
            std::string key;
            std::cin >> key;
            client.get(std::move(key));
        } else if (option == "6") {
            std::string key;
            std::cin >> key;
            client.del(std::move(key));
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " -raftnode|-clientnode" << std::endl;
        return 1;
    }

    std::string mode{argv[1]};
    if (mode == "-raftnode") {
        run_raft_node();
    } else if (mode == "-clientnode") {
        run_client_node();
    } else {
        std::cerr << "Invalid argument. Use -raftnode or -clientnode" << std::endl;
        return 1;
    }

    std::cout << "Shutting down..." << std::endl;
    return 0;
}