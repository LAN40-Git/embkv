#include "client/client.h"

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
    is_running_.store(true, std::memory_order_release);
}

void embkv::client::Client::stop() {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (!is_running()) {
        return;
    }
    is_running_.store(false, std::memory_order_release);
}

auto embkv::client::Client::connect_to_server(uint64_t id) const -> socket::net::TcpStream {
    auto& address = endpoints_.begin()->second;
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

    return std::move(stream);
}

void embkv::client::Client::redirect_to_leader(uint64_t leader_hint) {
    stream_ = connect_to_server(leader_hint);
}

auto embkv::client::Client::put(const std::string& key, const std::string& value) -> bool {
    if (!is_running()) {
        log::console().info("Client is not started.");
        return false;
    }

    Message request;

}

auto embkv::client::Client::get(const std::string& key) -> boost::optional<std::string> {
    if (!is_running()) {
        log::console().info("Client is not started.");
        return boost::none;
    }


}

auto embkv::client::Client::del(const std::string& key) -> bool {
    if (!is_running()) {
        log::console().info("Client is not started.");
        return false;
    }


}
