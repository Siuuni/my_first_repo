#pragma once
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>

template<typename T>
class RingBuffer {
public:
    RingBuffer(size_t capacity)
        : capacity_(capacity), buffer_(capacity) {}

    // push: 生产者
    void push(const T& item) {
        std::unique_lock<std::mutex> lock(mtx_);
        not_full_.wait(lock, [&] { return size_ < capacity_ || stop_; });
        if (stop_) return;

        buffer_[writeIndex_] = item;
        writeIndex_ = (writeIndex_ + 1) % capacity_;
        size_++;

        not_empty_.notify_one();
    }

    // pop: 消费者
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mtx_);
        not_empty_.wait(lock, [&] { return size_ > 0 || stop_; });
        if (stop_ && size_ == 0) return false;

        item = buffer_[readIndex_];
        readIndex_ = (readIndex_ + 1) % capacity_;
        size_--;

        not_full_.notify_one();
        return true;
    }

    // 停止
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    size_t capacity_;
    std::vector<T> buffer_;

    size_t writeIndex_ = 0;
    size_t readIndex_  = 0;
    size_t size_ = 0;

    std::mutex mtx_;
    std::condition_variable not_empty_, not_full_;
    bool stop_ = false;
};
