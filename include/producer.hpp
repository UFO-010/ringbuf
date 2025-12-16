
#ifndef _RB_PRODUCER_HPP
#define _RB_PRODUCER_HPP

#include "ringbuf.hpp"

/**
 * @brief The ProducerHandler class
 *
 * Write only wrapper for `spsc_ringbuf`
 */
template <typename T, size_t max_size, bool ThreadSafe>
class ProducerHandler {
public:
    bool push_back(const T &item) { return rb_.push_back(item); }

    bool push_back(T &&item) { return rb_.push_back(std::move(item)); }

    size_t append(const T *item, size_t size) { return rb_.append(item, size); }

    size_t advance_write_pointer(size_t advance) { return rb_.advance_write_pointer(advance); }

    LinearBlock<T> get_first_segment() { return rb_.get_write_linear_block_single(); }

    BufferSegments<T> get_segments() { return rb_.get_write_segments(); }

private:
    friend class spsc_ringbuf<T, max_size, ThreadSafe>;

    spsc_ringbuf<T, max_size, ThreadSafe> &rb_;

    explicit ProducerHandler(spsc_ringbuf<T, max_size, ThreadSafe> &rb)
        : rb_(rb) {}
};

#endif
