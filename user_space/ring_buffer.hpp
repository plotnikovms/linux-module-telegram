#include <cstddef>
#include <iostream>
#include <vector>

template <typename T>
class RingBuffer {
 public:
  RingBuffer() : kCapacity(1) {
    ring_queue_.resize(kCapacity);
  }

  explicit RingBuffer(size_t capacity) : kCapacity(capacity) {
    ring_queue_.resize(capacity);
  }

  size_t Size() const { return end_ - begin_; }

  bool Empty() const { return begin_ == end_; }

  void Push(T element) {
    if (Size() == kCapacity) {
      ++begin_;
    }
    ring_queue_[end_ % kCapacity] = element;
    ++end_;
  }

  void Pop(T* element) {
    if (Size() == 0) {
      return;
    }
    *element = ring_queue_[begin_ % kCapacity];
    ++begin_;
  }

  const T& operator[](size_t index) const {
    if (index >= Size()) {
      throw std::out_of_range("Index out of range");
    }
    return ring_queue_[(begin_ + index) % kCapacity];
  }

 private:
  std::vector<T> ring_queue_;
  size_t begin_ = 0;
  size_t end_ = 0;
  const size_t kCapacity;
};
