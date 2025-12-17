
#ifndef _RB_CONSUMER_HPP
#define _RB_CONSUMER_HPP

#include "ringbuf.hpp"

namespace rb {

/**
 * @brief The ConsumerHandler class
 *
 * Read only wrapper for `spsc_ringbuf`
 */
template <typename T, size_t MaxSize, bool ThreadSafe>
class ConsumerHandler {
public:
    /**
     * @brief pop_front
     * @return Element from buffer or default T() if empty
     *
     * Read and remove element from buffer
     */
    T pop_front() { return rb_.pop_front(); }

    /**
     * @brief pop_front
     * @param dest: Destination
     * @return true if element is moved
     *
     * Read and remove element from buffer
     */
    bool pop_front(T &dest) { return rb_.pop_front(dest); }

    /**
     * @brief read_ready
     * @param item: Destination array
     * @param size: Number of elements to read
     * @return Number of elements actually read
     *
     * Read and remove multiple elements
     */
    size_t read_ready(T *item, size_t size) { return rb_.read_ready(item, size); }

    /**
     * @brief advance_read_pointer
     * @param advance: Number of elements readed
     * @return Actual number of elements head (read pointer) moved
     *
     * Advance read pointer after manual buffer read
     */
    size_t advance_read_pointer(size_t advance) { return rb_.advance_read_pointer(advance); }

    /**
     * @brief get_first_segment
     * @return LinearBlock representing readable region
     *
     * Get single contiguous read segment
     */
    LinearBlock<T> get_first_segment() { return rb_.get_read_linear_block_single(); }

    /**
     * @brief get_segments
     * @return Buffer segments avaliable for reading
     *
     * Get read segments (handles wrap-around). Allows direct access to buffer memory for direct
     * copy operations
     */
    BufferSegments<T> get_segments() { return rb_.get_read_segments(); }

private:
    friend class spsc_ringbuf<T, MaxSize, ThreadSafe>;

    spsc_ringbuf<T, MaxSize, ThreadSafe> &rb_;

    explicit ConsumerHandler(spsc_ringbuf<T, MaxSize, ThreadSafe> &rb)
        : rb_(rb) {}
};

}  // namespace rb
#endif
