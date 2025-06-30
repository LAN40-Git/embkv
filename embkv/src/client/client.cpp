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
}

void embkv::client::Client::read_cb(struct ev_loop* loop, struct ev_io* w, int revents) {
    // TODO: 接收服务端的消息
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

    return std::move(stream);
}

auto embkv::client::Client::put(const std::string& key, const std::string& value) -> bool {
    if (!is_running()) {
        log::console().info("Client is not started.");
        return false;
    }

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
