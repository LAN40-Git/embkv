#pragma once
#include "buffer.h"
#include "file.h"
#include "level.h"
#include "common/util/nocopyable.h"
#include <fmt/core.h>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <list>
#include <mutex>
#include <thread>
#include <memory>

namespace embkv::log
{
namespace detail
{
struct LogRecord {
    const char* datatime;
    std::string log;
};

template<typename LoggerType>
class BaseLogger : public util::Nocopyable {
public:
    BaseLogger() = default;
    ~BaseLogger() noexcept = default;

public:
    void set_level(LogLevel level) noexcept {
        level_ = level;
    }

    auto level() const noexcept -> LogLevel {
        return level_;
    }

    template<typename... Args>
    void debug(std::string fmt, Args&& ...args) {
        format<LogLevel::Debug>(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void info(std::string fmt, Args&& ...args) {
        format<LogLevel::Info>(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warn(std::string fmt, Args&& ...args) {
        format<LogLevel::Warn>(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(std::string fmt, Args&& ...args) {
        format<LogLevel::Error>(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void fatal(std::string fmt, Args&& ...args) {
        format<LogLevel::Fatal>(fmt, std::forward<Args>(args)...);
    }

private:
    template <LogLevel LEVEL, typename ...Args>
    void format(const std::string& fmt, const Args& ...args) {
        thread_local std::array<char, 64> buffer{};
        thread_local time_t               last_second{0};

        if (LEVEL < level_) {
            return;
        }

        time_t current_second = ::time(nullptr);
        if (current_second != last_second) {
            tm tm_time{};
            ::localtime_r(&current_second, &tm_time);
            ::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", &tm_time);
            last_second = current_second;
        }

        static_cast<LoggerType*>(this) -> template log<LEVEL>(
            LogRecord{
                buffer.data(),
                fmt::vformat(fmt, fmt::make_format_args(args...))
            }
        );
    }

private:
    LogLevel level_{LogLevel::Debug};
};
} // namespace detail

class ConsoleLogger : public detail::BaseLogger<ConsoleLogger> {
public:
    template <detail::LogLevel LEVEL>
    void log(const detail::LogRecord& record) {
        std::cout << fmt::format("{} {} {}\n",
                                    record.datatime,
                                    detail::level_to_string(LEVEL),
                                    record.log);
    }
};

class FileLogger : public detail::BaseLogger<FileLogger> {
public:
    explicit FileLogger(const std::string& file_base_name)
        : file_(file_base_name)
        , current_buffer_(std::make_unique<Buffer>())
        , thread_{&FileLogger::work, this} {
        empty_buffers_.emplace_back(std::make_unique<Buffer>());
    }

    ~FileLogger() {
        is_running_ = false;
        cond_.notify_one();
        if (thread_.joinable()) thread_.join();
    }

private:
    template <detail::LogLevel LEVEL>
    void log(const detail::LogRecord& record) {
        if (!is_running_) {
            return;
        }

        {
            std::string msg{fmt::format("{} {} {}\n",
                                        record.datatime,
                                        detail::level_to_string(LEVEL),
                                        record.log)};
            std::lock_guard<std::mutex> lock(mutex_);
            if (current_buffer_->writable_bytes() > msg.size()) {
                current_buffer_->write(msg);
                return;
            }
            full_buffers_.emplace_back(std::move(current_buffer_));
            if (!empty_buffers_.empty()) {
                current_buffer_ = std::move(empty_buffers_.front());
                empty_buffers_.pop_front();
            } else {
                current_buffer_ = std::make_unique<Buffer>();
            }
            current_buffer_->write(msg);
        }
        cond_.notify_one();
    }

    void set_max_file_size(off_t roll_size) noexcept {
        file_.set_max_file_size(roll_size);
    }

private:
    void work() {
        while (is_running_) {
            constexpr std::size_t max_buffer_list_size = 15;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cond_.wait_for(lock, std::chrono::milliseconds(3), [this]() -> bool {
                    return !this->full_buffers_.empty();
                });
                // 交换缓冲区
                full_buffers_.push_back(std::move(current_buffer_));
                if (!empty_buffers_.empty()) {
                    current_buffer_ = std::move(empty_buffers_.front());
                    empty_buffers_.pop_front();
                } else {
                    current_buffer_ = std::make_unique<Buffer>();
                }
            }

            if (full_buffers_.size() > max_buffer_list_size) {
                std::cerr << fmt::format("Dropped log messages {} larger buffers\n", full_buffers_.size() - 2);
                full_buffers_.resize(2);
            }

            for (auto& buffer : full_buffers_) {
                file_.write(buffer->data(), buffer->size());
                buffer->reset();
            }

            if (full_buffers_.size() > 2) {
                full_buffers_.resize(2);
            }
            file_.flush();
            empty_buffers_.splice(empty_buffers_.end(), full_buffers_);
        }

        if (!current_buffer_->empty()) {
            full_buffers_.emplace_back(std::move(current_buffer_));
        }
        for (auto& buffer : full_buffers_) {
            file_.write(buffer->data(), buffer->size());
        }
        file_.flush();
    }

private:
    using Buffer = detail::LogBuffer<4000 * 1024>;
    using BufferPtr = std::unique_ptr<Buffer>;

    detail::LogFile         file_;
    BufferPtr               current_buffer_;
    std::list<BufferPtr>    empty_buffers_{};
    std::list<BufferPtr>    full_buffers_{};
    std::mutex              mutex_{};
    std::condition_variable cond_{};
    std::thread             thread_{};
    std::atomic<bool>       is_running_{true};
};

} // namespace embkv::log