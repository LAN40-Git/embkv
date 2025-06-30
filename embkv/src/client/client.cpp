#include "client/client.h"

void embkv::client::Client::run() {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (is_running() || endpoints_.empty()) {
        return;
    }
    is_running_.store(true, std::memory_order_release);
    // 连接到第一个节点并在发送消息时进行重定向
    auto& address = endpoints_.begin()->second;
    auto& ip = address->ip;
    auto& port = address->port;
    socket::net::SocketAddr addr;
    std::error_code ec;
    if (!socket::net::SocketAddr::parse(ip, port, addr, ec)) {
        log::console().error("Failed to parse {}:{} : {}", ip, port, ec.message());
        is_running_.store(false, std::memory_order_release);
        return;
    }
    auto stream = socket::net::TcpStream::connect(addr);
    if (!stream.is_valid()) {
        log::console().error("Failed to connect {}:{}", ip, port);
        return;
    }

    stream_ = std::move(stream);
}

void embkv::client::Client::stop() {

}

auto embkv::client::Client::put(const std::string& key, const std::string& value) -> bool {

}

auto embkv::client::Client::get(const std::string& key) -> boost::optional<std::string> {

}

auto embkv::client::Client::del(const std::string& key) -> bool {

}
