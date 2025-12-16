
#ifndef _RB_CONSUMER_HPP
#define _RB_CONSUMER_HPP

#include "ringbuf.hpp"

/**
 * @brief The ConsumerHandler class
 *
 * Read only wrapper for `spsc_ringbuf`
 */
template <typename T, size_t max_size, bool ThreadSafe>
class ConsumerHandler {
public:
    T pop_front() { return rb_.pop_front(); }

    bool pop_front(T &dest) { return rb_.pop_front(dest); }

    size_t read_ready(T *item, size_t size) { return rb_.read_ready(item, size); }

    size_t advance_read_pointer(size_t advance) { return rb_.advance_read_pointer(advance); }

    LinearBlock<T> get_first_segment() { return rb_.get_read_linear_block_single(); }

    BufferSegments<T> get_segments() { return rb_.get_read_segments(); }

private:
    friend class spsc_ringbuf<T, max_size, ThreadSafe>;

    spsc_ringbuf<T, max_size, ThreadSafe> &rb_;

    explicit ConsumerHandler(spsc_ringbuf<T, max_size, ThreadSafe> &rb)
        : rb_(rb) {}
};

#endif
