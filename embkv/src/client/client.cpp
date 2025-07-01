#include "client/client.h"
#include "raft/transport/head_manager.h"

void embkv::client::Client::run() {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (is_running() || endpoints_.empty()) {
        return;
    }
    stream_ = connect_to_server(0);
    if (!stream_.is_valid()) {
        log::console().error("Failed to connect to server.");
        return;
    }
    boost::asio::post(pool_, [this]() {
        event_loop();
    });
    is_running_.store(true, std::memory_order_release);
}

void embkv::client::Client::stop() {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (!is_running()) {
        return;
    }
    is_running_.store(false, std::memory_order_release);
    ev_break(loop_, EVBREAK_ALL);
    pool_.wait();
}

void embkv::client::Client::event_loop() {
    auto* rd_data = new ReadData{*this};
    read_watcher_.data = rd_data;
    ev_io_init(&read_watcher_, read_cb, stream_.fd(), EV_READ);
    ev_io_start(loop_, &read_watcher_);
    ev_run(loop_, 0);
    ev_io_stop(loop_, &read_watcher_);
    delete rd_data;
    is_running_.store(false, std::memory_order_release);
}

void embkv::client::Client::read_cb(struct ev_loop* loop, struct ev_io* w, int revents) {
    thread_local char buf[10 * 1024];
    auto* rd_data = static_cast<ReadData*>(w->data);


    if (revents & EV_READ) {
        auto& buffer = raft::detail::HeadManager::buffer();
        auto size = rd_data->client.stream_.read_exact(buffer.data(), buffer.size());
        if (size == 0) {
            log::console().error("Server closed");
            ev_break(loop, EVBREAK_ALL);
            return;
        }
        auto header = raft::detail::HeadManager::deserialize();
        size = rd_data->client.stream_.read_exact(buf, header->length);
        if (size == 0) {
            log::console().error("Failed to read {}", header->length);
            ev_break(loop, EVBREAK_ALL);
            return;
        }
        // 处理回复
        Message msg;
        if (msg.ParseFromArray(buf, header->length)) {
            auto client_response = msg.mutable_client_response();
            if (!client_response->success()) {
                log::console().error("Operation failed : {}", client_response->error());
                // 重定向到leader
                rd_data->client.stream_ = rd_data->client.connect_to_server(client_response->leader_hint());
                if (!rd_data->client.stream_.is_valid()) {
                    log::console().error("Failed to connect to server: {}", client_response->error());
                    ev_break(loop, EVBREAK_ALL);
                }
                return;
            }

            if (!client_response->value().empty()) {
                std::cout << "value : " << client_response->value() << std::endl;
            }
        }
        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("read_cb error : {}", strerror(errno));
    }
}

auto embkv::client::Client::connect_to_server(uint64_t id) const -> socket::net::TcpStream {
    auto endpoint = endpoints_.find(id);
    if (endpoint == endpoints_.end()) {
        log::console().error("Failed to find endpoint: {}", id);
        return socket::net::TcpStream{socket::detail::Socket{-1}};
    }
    auto& address = endpoint->second;
    auto& ip = address->ip;
    auto& port = address->port;
    socket::net::SocketAddr addr{};
    std::error_code ec;
    if (!socket::net::SocketAddr::parse(ip, port, addr, ec)) {
        log::console().error("Failed to parse {}:{} : {}", ip, port, ec.message());
        return socket::net::TcpStream{socket::detail::Socket{-1}};
    }
    auto stream = socket::net::TcpStream::connect(addr);
    if (!stream.is_valid()) {
        log::console().error("Failed to connect {}:{}", ip, port);
        return socket::net::TcpStream{socket::detail::Socket{-1}};
    }
    raft::detail::HeadManager::Header header{
        .length = htobe64(id),
    };
    header.flags = raft::detail::HeadManager::Flags::kClientNode;
    if (raft::detail::HeadManager::serialize(header)) {
        auto& buffer = raft::detail::HeadManager::buffer();
        stream.write_exact(buffer.data(), buffer.size());
    }

    log::console().info("Connect to server");
    return std::move(stream);
}

void embkv::client::Client::put(const std::string& key, const std::string& value) {
    if (!is_running()) {
        log::console().info("Client is not started.");
        return;
    }

    Message msg;
    msg.set_node_id(id());
    auto client_request = msg.mutable_client_request();
    client_request->set_request_id(request_id_.fetch_add(1));
    KVOperation command;
    command.set_op_type(KVOperation_OperationType_PUT);
    command.set_key(key);
    command.set_value(value);
    client_request->set_command(command.SerializeAsString());

    raft::detail::HeadManager::Header header {
        .length = htobe64(msg.ByteSizeLong()),
    };
    raft::detail::HeadManager::serialize(header);
    auto& buffer = raft::detail::HeadManager::buffer();
    stream_.write_exact(buffer.data(), buffer.size());
    stream_.write_exact(msg.SerializeAsString().data(), msg.ByteSizeLong());
}

void embkv::client::Client::get(const std::string& key) {
    if (!is_running()) {
        log::console().info("Client is not started.");
        return;
    }

    Message msg;
    msg.set_node_id(id());
    auto client_request = msg.mutable_client_request();
    client_request->set_request_id(request_id_.fetch_add(1));
    KVOperation command;
    command.set_op_type(KVOperation_OperationType_GET);
    command.set_key(key);
    client_request->set_command(command.SerializeAsString());

    raft::detail::HeadManager::Header header {
        .length = htobe64(msg.ByteSizeLong()),
    };
    raft::detail::HeadManager::serialize(header);
    auto& buffer = raft::detail::HeadManager::buffer();
    stream_.write_exact(buffer.data(), buffer.size());
    stream_.write_exact(msg.SerializeAsString().data(), msg.ByteSizeLong());
}

void embkv::client::Client::del(const std::string& key) {
    if (!is_running()) {
        log::console().info("Client is not started.");
        return;
    }

    Message msg;
    msg.set_node_id(id());
    auto client_request = msg.mutable_client_request();
    client_request->set_request_id(request_id_.fetch_add(1));
    KVOperation command;
    command.set_op_type(KVOperation_OperationType_DELETE);
    command.set_key(key);
    client_request->set_command(command.SerializeAsString());

    raft::detail::HeadManager::Header header {
        .length = htobe64(msg.ByteSizeLong()),
    };
    raft::detail::HeadManager::serialize(header);
    auto& buffer = raft::detail::HeadManager::buffer();
    stream_.write_exact(buffer.data(), buffer.size());
    stream_.write_exact(msg.SerializeAsString().data(), msg.ByteSizeLong());
}
