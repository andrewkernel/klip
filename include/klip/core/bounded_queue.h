#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

namespace klip {

template <typename T>
class SpscQueue {
 public:
  explicit SpscQueue(std::size_t capacity) : slots_(capacity + 1) {}

  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;

  bool TryPush(T value) {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto next = Increment(head);
    if (next == tail_.load(std::memory_order_acquire)) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    slots_[head].emplace(std::move(value));
    head_.store(next, std::memory_order_release);
    not_empty_.notify_one();
    return true;
  }

  bool TryPop(T& value) {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;
    }
    value = std::move(*slots_[tail]);
    slots_[tail].reset();
    tail_.store(Increment(tail), std::memory_order_release);
    return true;
  }

  bool WaitPop(T& value, std::stop_token stop_token) {
    if (TryPop(value)) {
      return true;
    }
    std::unique_lock lock(wait_mutex_);
    std::stop_callback wake_on_stop(stop_token, [this] { not_empty_.notify_all(); });
    not_empty_.wait(lock, [&] {
      return stop_token.stop_requested() ||
             tail_.load(std::memory_order_acquire) != head_.load(std::memory_order_acquire);
    });
    return !stop_token.stop_requested() && TryPop(value);
  }

  [[nodiscard]] std::size_t Size() const {
    const auto head = head_.load(std::memory_order_acquire);
    const auto tail = tail_.load(std::memory_order_acquire);
    return head >= tail ? head - tail : slots_.size() - tail + head;
  }

  [[nodiscard]] std::size_t Capacity() const { return slots_.size() - 1; }
  [[nodiscard]] std::uint64_t Dropped() const { return dropped_.load(std::memory_order_relaxed); }

  void Clear() {
    T item;
    while (TryPop(item)) {
    }
  }

 private:
  std::size_t Increment(std::size_t index) const noexcept { return (index + 1) % slots_.size(); }

  std::vector<std::optional<T>> slots_;
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
  std::atomic<std::uint64_t> dropped_{0};
  std::mutex wait_mutex_;
  std::condition_variable not_empty_;
};

template <typename T>
class BlockingBoundedQueue {
 public:
  explicit BlockingBoundedQueue(std::size_t capacity) : capacity_(capacity) {}

  BlockingBoundedQueue(const BlockingBoundedQueue&) = delete;
  BlockingBoundedQueue& operator=(const BlockingBoundedQueue&) = delete;

  bool TryPush(T value) {
    std::scoped_lock lock(mutex_);
    if (closed_ || items_.size() >= capacity_) {
      return false;
    }
    items_.push_back(std::move(value));
    not_empty_.notify_one();
    return true;
  }

  bool WaitPop(T& value, std::stop_token stop_token) {
    std::unique_lock lock(mutex_);
    std::stop_callback wake_on_stop(stop_token, [this] { not_empty_.notify_all(); });
    not_empty_.wait(lock,
                    [&] { return closed_ || !items_.empty() || stop_token.stop_requested(); });
    if (items_.empty()) {
      return false;
    }
    value = std::move(items_.front());
    items_.pop_front();
    return true;
  }

  void Close() {
    std::scoped_lock lock(mutex_);
    closed_ = true;
    not_empty_.notify_all();
  }

  [[nodiscard]] bool Closed() const {
    std::scoped_lock lock(mutex_);
    return closed_;
  }

  [[nodiscard]] std::size_t Size() const {
    std::scoped_lock lock(mutex_);
    return items_.size();
  }

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::deque<T> items_;
  bool closed_ = false;
};

}  // namespace klip
