#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class PacketQueue {
public:
    PacketQueue() = default;
    ~PacketQueue() = default;

    // 推入元素
    void push(T item) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push(item);
        }
        cond_.notify_one();
    }

    // 弹出元素，如果队列为空则阻塞
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        // 等待队列非空或者 stop_ 被触发
        cond_.wait(lock, [this]{ return !queue_.empty() || stop_; });
        // 队列为空且 stop_ 已经被调用，返回空
        if (queue_.empty()) return nullptr;  

        T item = queue_.front();
        queue_.pop();
        return item;
    }

    // 停止队列
    void stop() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cond_.notify_all();
    }

    // 判断队列是否为空
    bool empty() {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // 清空队列（释放资源）
    void clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            queue_.pop();
        }
    }

private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool stop_ = false;
};
