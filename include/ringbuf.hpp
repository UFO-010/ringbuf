
#pragma once

#include <cstddef>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>

#include "blockdata.hpp"

namespace rb {

/// FORWARD DECLARATION
template <typename T, size_t MaxSize, bool ThreadSafe>
class ProducerHandler;

/// FORWARD DECLARATION
template <typename T, size_t MaxSize, bool ThreadSafe>
class ConsumerHandler;

/// Overflow handling strategies (selected at compile-time)
enum class OverflowPolicy {
    DROP,       ///< Discard new data if buffer is full
    OVERWRITE,  ///< Overwrite oldest data (advance head)
    FAIL,       ///< Return 0 elements written (caller handles)
    TOEND       ///< Writes as much data as possible to the end of buffer
};

/// Event types for notifications
enum class EventType {
    DATA_AVAILABLE,  ///< New data written (threshold exceeded or buffer had data)
    BUFFER_FULL,     ///< Buffer capacity exhausted
    BUFFER_EMPTY,    ///< All data consumed
    OVERFLOW,        ///< Overflow policy triggered
    RESET            ///< Buffer reset called
};

/// Event notification structure
struct BufferEvent {
    EventType type;            ///< Type of event that occurred
    size_t current_size;       ///< Current buffer occupancy in elements
    size_t free_space;         ///< Available free space in elements
    uint64_t sequence_number;  ///< Monotonic event sequence counter
};

using EventCallback = std::function<void(const BufferEvent &)>;

struct RingbufStatistics {
    size_t total_pushes = 0;           ///< Total successful push operations
    size_t total_pops = 0;             ///< Total successful pop operations
    size_t overflow_events = 0;        ///< Number of times overflow occurred
    size_t max_occupancy = 0;          ///< Peak element count in buffer
    uint64_t total_bytes_written = 0;  ///< Cumulative bytes written
    uint64_t total_bytes_read = 0;     ///< Cumulative bytes read

    void reset() noexcept {
        total_pushes = total_pops = overflow_events = 0;
        max_occupancy = 0;
        total_bytes_written = total_bytes_read = 0;
    }
};

/**
 * @brief The spsc_ringbuf class
 *
 * @param T: Element type (any type, trivially copyable preferred for performance)
 * @param max_size: Ring buffer capacity (must be power of 2)
 * @param ThreadSafe: If true, uses std::atomic<size_t> for head and tail, else uses size_t
 */
template <typename T,
          size_t MaxSize,
          bool ThreadSafe,
          OverflowPolicy Policy = OverflowPolicy::DROP,
          size_t MaxCallbacks = 4>
class spsc_ringbuf {
    static_assert((MaxSize & (MaxSize - 1)) == 0, "max_size value should be power of 2");

public:
    explicit spsc_ringbuf() = default;

    ProducerHandler<T, MaxSize, ThreadSafe> get_producer() noexcept {
        return ProducerHandler<T, MaxSize, ThreadSafe>(*this);
    }

    ConsumerHandler<T, MaxSize, ThreadSafe> get_consumer() noexcept {
        return ConsumerHandler<T, MaxSize, ThreadSafe>(*this);
    }

    /**
     * @brief reset
     *
     * Reset buffer to empty state (NOT thread-safe, call from single thread)
     */
    void reset() {
        store(head, 0);
        store(tail, 0);

        stats.reset();
        if (!callbacks.empty()) {
            emit_event_internal(EventType::RESET);
        }
    }

    /**
     * @brief size
     * @return Current number of elements stored in buffer
     */
    size_t size() const { return get_data_size(); }

    /**
     * @brief capacity
     * @return Get buffer capacity
     */
    size_t capacity() const { return MaxSize; }

    /**
     * @brief empty
     * @return true if buffer is empty
     */
    bool empty() const { return get_data_size() == 0; }

    bool full() const { return get_free_size() == 0; }

    /**
     * @brief push_back
     * @param item: element to write
     * @return true if write succeeded, false if buffer full
     *
     * Write single element to buffer
     */
    bool push_back(const T &item) {
        size_t local_tail = load(tail, std::memory_order_acquire);
        if (handle_overflow(local_tail, 1) == 0) {
            return false;
        }

        buf[local_tail] = item;

        local_tail = (local_tail + 1) & mask;
        store(tail, local_tail, std::memory_order_release);

        stats.total_pushes++;
        emit_event_internal(EventType::DATA_AVAILABLE);

        return true;
    }

    /**
     * @brief push_back
     * @param item: element to write
     * @return true if write succeeded, false if buffer full
     *
     * Write single element to buffer, move version
     */
    bool push_back(T &&item) {
        size_t local_tail = load(tail, std::memory_order_acquire);

        if (handle_overflow(local_tail, 1) == 0) {
            return false;
        }

        buf[local_tail] = std::move(item);

        local_tail = (local_tail + 1) & mask;
        store(tail, local_tail, std::memory_order_release);

        stats.total_pushes++;
        emit_event_internal(EventType::DATA_AVAILABLE);

        return true;
    }

    /**
     * @brief append
     * @param item: Pointer to source array
     * @param size: Number of elements to write
     * @return Number of elements actually written
     *
     * Append multiple elements from array
     */
    size_t append(const T *item, size_t size) {
        if (size == 0 || item == nullptr) {
            return 0;
        }

        size_t local_tail = load(tail, std::memory_order_acquire);

        size_t copy_size = buf_store(local_tail, item, size);

        size_t new_tail = (local_tail + copy_size) & mask;
        store(tail, new_tail, std::memory_order_release);

        stats.total_pushes++;
        if (copy_size > 0) {
            emit_event_internal(EventType::DATA_AVAILABLE);
        }

        return copy_size;
    }

    /**
     * @brief pop_front
     * @return Element from buffer or default T() if empty
     *
     * Read and remove element from buffer
     */
    T pop_front() {
        if (empty()) {
            emit_event_internal(EventType::BUFFER_EMPTY);
            return {};
        }

        size_t local_head = load(head, std::memory_order_acquire);
        T item = std::move(buf[local_head]);

        local_head = (local_head + 1) & mask;
        store(head, local_head, std::memory_order_release);

        stats.total_pops++;

        return item;
    }

    /**
     * @brief pop_front
     * @param dest: Destination
     * @return true if element is moved
     *
     * Read and remove element from buffer
     */
    bool pop_front(T &dest) {
        if (empty()) {
            emit_event_internal(EventType::BUFFER_EMPTY);
            return false;
        }

        size_t local_head = load(head, std::memory_order_acquire);
        dest = std::move(buf[local_head]);

        local_head = (local_head + 1) & mask;
        store(head, local_head, std::memory_order_release);

        stats.total_pops++;

        return true;
    }

    /**
     * @brief read_ready
     * @param item: Destination array
     * @param size: Number of elements to read
     * @return Number of elements actually read
     *
     * Read and remove multiple elements
     */
    size_t read_ready(T *item, size_t size) {
        if (size == 0 || item == nullptr) {
            emit_event_internal(EventType::BUFFER_EMPTY);
            return 0;
        }

        size_t local_head = load(head, std::memory_order_acquire);

        size_t copy_size = buf_read(local_head, item, size);
        size_t new_head = (local_head + copy_size) & mask;

        store(head, new_head, std::memory_order_release);

        stats.total_pops += copy_size;

        if (copy_size == 0) {
            emit_event_internal(EventType::BUFFER_EMPTY);
        }

        return copy_size;
    }

    /**
     * @brief peek
     * @return First element or default T() if empty
     *
     * Peek at first element without removing and moving read pointer
     */
    T peek() const {
        size_t local_head = load(head, std::memory_order_relaxed);
        if (get_data_size(local_head) == 0) {
            return {};
        }
        return buf[local_head];
    }

    /**
     * @brief peek_ready
     * @param item: Destination array
     * @param size: Number of elements to read
     * @return  Number of elements actually read
     *
     * Peek at elements without removing and moving read pointer
     */
    size_t peek_ready(T *item, size_t size) {
        if (size == 0 || item == nullptr) {
            return 0;
        }

        size_t local_head = load(head, std::memory_order_acquire);

        size_t full_data_size = get_data_size(local_head);
        if (full_data_size == 0) {
            return 0;
        }

        size_t copy_size = buf_read(local_head, item, size);

        return copy_size;
    }

    /**
     * @brief get_data_size
     * @return Number of elements available for reading
     *
     *       `head`           `tail`
     * --------|================|---------
     *    free        data         free
     *
     *       `tail`           `head`
     * ========|----------------|========
     *    data        free         data
     *
     * Calculate number of elements available for reading. Uses relaxed memory order - approximate
     * value
     */
    size_t get_data_size() const {
        size_t local_head = load(head, std::memory_order_relaxed);
        size_t local_tail = load(tail, std::memory_order_relaxed);

        return (local_tail - local_head) & mask;
    }

    /**
     * @brief get_free_size
     * @return Free space in buffer
     *
     * Calculate free space in buffer. Reserves 1 element to distinguish empty from full
     */
    size_t get_free_size() const { return MaxSize - 1 - get_data_size(); }

    /**
     * @brief advance_write_pointer
     * @param advance: Number of elements written
     * @return Actual number of elements tail (write pointer) moved
     *
     * Advance write pointer after manual buffer write. Must be called after writing to blocks
     * obtained from get_write_segments()
     */
    size_t advance_write_pointer(size_t advance) {
        if (advance == 0 || full()) {
            return 0;
        }

        size_t local_tail = load(tail, std::memory_order_acquire);
        size_t new_tail = (local_tail + advance) & mask;
        store(tail, new_tail, std::memory_order_release);

        emit_event_internal(EventType::DATA_AVAILABLE);
        stats.total_pushes++;

        return (new_tail - local_tail) & mask;
    }

    /**
     * @brief advance_read_pointer
     * @param advance: Number of elements readed
     * @return Actual number of elements head (read pointer) moved
     *
     * Advance read pointer after manual buffer read
     */
    size_t advance_read_pointer(size_t advance) {
        if (advance == 0 || empty()) {
            return 0;
        }

        size_t local_head = load(head, std::memory_order_acquire);
        size_t new_head = (local_head + advance) & mask;

        store(head, new_head, std::memory_order_release);

        stats.total_pops += advance;

        return (new_head - local_head) & mask;
    }

    /**
     * @brief subscribe
     * @param callback
     * @return true if subsribed, false if callback buffer full
     *
     * Subscribe to buffer events
     * @note Callbacks are called synchronously from producer/consumer threads
     * Keep callbacks fast to avoid blocking operations
     */
    bool subscribe(const EventCallback &callback) noexcept {
        if (callback_count >= MaxCallbacks) {
            return false;
        }

        callbacks[callback_count] = callback;
        callback_count++;
        return true;
    }

    bool subscribe(EventCallback &&callback) noexcept {
        if (callback_count >= MaxCallbacks) {
            return false;
        }

        callbacks[callback_count] = std::move(callback);
        callback_count++;
        return true;
    }

    /**
     * @brief unsubscribe
     * @param index Callback index to remove
     * @return true if unsubsribed, false if callback buffer empty
     *
     * Unsubscribe from buffer events
     */
    bool unsubscribe(size_t index) noexcept {
        if (index >= callback_count) {
            return false;
        }

        callback_count--;
        callbacks[index] = std::move(callbacks[callback_count]);
        return true;
    }

    /**
     * @brief get_write_linear_block_single
     * @return LinearBlock representing first writable region
     *
     * Get contiguous write-available segment. Allows zero-copy write operations
     */
    LinearBlock<T> get_write_linear_block_single() {
        size_t local_tail = load(tail, std::memory_order_acquire);

        size_t free_space = get_free_size(local_tail);

        if (free_space == 0) {
            return {nullptr, 0};
        }

        size_t block_size = std::min(free_space, capacity() - local_tail);
        T *block_ptr = buf.data() + local_tail;

        return {block_ptr, block_size};
    }

    /**
     * @brief get_read_linear_block_single
     * @return LinearBlock representing readable region
     *
     * Get single contiguous read segment
     */
    LinearBlock<T> get_read_linear_block_single() {
        size_t local_head = load(head, std::memory_order_acquire);

        size_t data_size = get_data_size(local_head);

        if (data_size == 0) {
            return {nullptr, 0};
        }

        size_t block_size = std::min(data_size, capacity() - local_head);
        T *block_ptr = buf.data() + local_head;

        return {block_ptr, block_size};
    }

    /**
     * @brief get_write_segments
     * @return Buffer segments avaliable for writing
     *
     * Get write segments (handles wrap-around). Allows direct access to buffer memory for zero-copy
     * operations
     */
    BufferSegments<T> get_write_segments() {
        size_t local_tail = load(tail, std::memory_order_acquire);

        size_t free_space = get_free_size(local_tail);

        if (free_space == 0) {
            return {{nullptr, 0}, {nullptr, 0}};
        }

        size_t first_size = std::min(free_space, MaxSize - local_tail);
        T *first_ptr = buf.data() + local_tail;
        LinearBlock<T> first_block = {first_ptr, first_size};

        LinearBlock<T> second_block = {nullptr, 0};
        size_t second_size = free_space - first_size;

        if (second_size > 0) {
            second_block = {buf.data(), second_size};
        }

        return {first_block, second_block};
    }

    /**
     * @brief get_read_segments
     * @return Buffer segments avaliable for reading
     *
     * Get read segments (handles wrap-around). Allows direct access to buffer memory for direct
     * copy operations
     */
    BufferSegments<T> get_read_segments() {
        size_t local_head = load(head, std::memory_order_acquire);
        size_t data_size = get_data_size(local_head);

        if (data_size == 0) {
            return {{nullptr, 0}, {nullptr, 0}};
        }

        size_t first_size = std::min(data_size, MaxSize - local_head);

        T *first_ptr = buf.data() + local_head;
        LinearBlock<T> first_block = {first_ptr, first_size};

        LinearBlock<T> second_block = {nullptr, 0};
        size_t second_size = data_size - first_size;

        if (second_size > 0) {
            second_block = {buf.data(), second_size};
        }

        return {first_block, second_block};
    }

    /// Get current statistics
    RingbufStatistics get_statistics() const noexcept { return stats; }

    /// Reset statistics counters
    void reset_statistics() noexcept { stats.reset(); }

private:
    /**
     * @brief load
     * @param var: variable to load it's value from
     * @param order: memory order for atomic operations.
     * @return value: stored in `var`
     *
     * Wrapper function to load values stored in `head` and `tail` with desired memory order. If
     * `ThreadSafe` == false, values are loaded directly and `order` is ignored
     */
    template <typename varType>
    constexpr size_t load(const varType &var,
                          std::memory_order order = std::memory_order_relaxed) const {
        if constexpr (ThreadSafe) {
            return var.load(order);
        } else {
            (void)order;
            return var;
        }
    }

    /**
     * @brief store
     * @param var: variable to store `value` in
     * @param value: new value of `var`
     * @param order: memory order for atomic operations.
     *
     * Wrapper function to update values stored in `head` and `tail` with desired memory order. If
     * `ThreadSafe` == false, values are updated directly and `order` is ignored
     */
    template <typename varType>
    constexpr void store(varType &var,
                         size_t value,
                         std::memory_order order = std::memory_order_relaxed) const {
        if constexpr (ThreadSafe) {
            var.store(value, order);
        } else {
            (void)order;
            var = value;
        }
    }

    /**
     * @brief buf_store
     * @param local_tail: Current write position
     * @param item: Source data array
     * @param size: Number of elements to write
     * @return Number of elements actually written
     *
     * Write data to buffer with overflow handling
     */
    size_t buf_store(const size_t local_tail, const T *item, size_t size) {
        size_t copy_size = handle_overflow(local_tail, size);
        if (copy_size == 0) {
            return 0;
        }

        // Copy linear part
        const T *data_ptr = item;
        size_t first_part = std::min(MaxSize - local_tail, copy_size);
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(buf.data() + local_tail, data_ptr, first_part * sizeof(T));
        } else {
            std::copy_n(data_ptr, first_part, buf.data() + local_tail);
        }
        data_ptr += first_part;

        size_t second_part = copy_size - first_part;

        // Copy overflow part
        if (second_part > 0) {
            if constexpr (std::is_trivially_copyable_v<T>) {
                std::memcpy(buf.data(), data_ptr, second_part * sizeof(T));
            } else {
                std::copy_n(data_ptr, second_part, buf.data());
            }
        }

        return copy_size;
    }

    /**
     * @brief buf_read
     * @param local_head: Current read position
     * @param item: Destination array
     * @param size: Number of elements to read
     * @return Number of elements actually read
     *
     * Read data from buffer
     */
    size_t buf_read(const size_t local_head, T *item, const size_t size) {
        size_t full_data_size = get_data_size(local_head);
        if (full_data_size == 0) {
            return 0;
        }

        size_t copy_size = std::min(full_data_size, size);

        // Copy linear part
        T *data_ptr = item;
        size_t first_part = std::min(MaxSize - local_head, copy_size);
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(data_ptr, buf.data() + local_head, first_part * sizeof(T));
        } else {
            std::copy_n(buf.data() + local_head, first_part, data_ptr);
        }

        data_ptr += first_part;
        size_t second_part = copy_size - first_part;

        // Copy overflow part
        if (second_part > 0) {
            if constexpr (std::is_trivially_copyable_v<T>) {
                std::memcpy(data_ptr, buf.data(), second_part * sizeof(T));
            } else {
                std::copy_n(buf.data(), second_part, data_ptr);
            }
        }

        return copy_size;
    }

    /**
     * @brief get_data_size
     * @param local_head: Current read position
     * @return Number of elements available for reading
     *
     * Calculate number of elements available for reading given a specific head position
     */
    size_t get_data_size(size_t local_head) const {
        size_t local_tail = load(tail, std::memory_order_relaxed);
        return (local_tail - local_head) & mask;
    }

    /**
     * @brief get_free_size
     * @param local_tail Current write position
     * @return Free space in buffer
     *
     * Calculate free space given a specific tail position. Reserves 1 element to distinguish empty
     * from full
     */
    size_t get_free_size(size_t local_tail) const {
        size_t local_head = load(head, std::memory_order_relaxed);
        return MaxSize - 1 - ((local_tail - local_head) & mask);
    }

    size_t handle_overflow(size_t local_tail, size_t requested_size) noexcept {
        size_t free_space = get_free_size(local_tail);

        if (free_space >= requested_size) {
            return requested_size;
        }

        stats.overflow_events++;
        emit_event_internal(EventType::OVERFLOW);

        if constexpr (Policy == OverflowPolicy::FAIL) {
            return 0;
        } else if constexpr (Policy == OverflowPolicy::DROP) {
            return 0;
        } else if constexpr (Policy == OverflowPolicy::TOEND) {
            size_t space_to_make = std::min(requested_size, free_space);

            return space_to_make;
        } else if constexpr (Policy == OverflowPolicy::OVERWRITE) {
            size_t space_to_make = std::min(requested_size, MaxSize - 1);
            size_t local_head = load(head, std::memory_order_acquire);
            size_t new_head = (local_head + space_to_make) & mask;

            store(head, new_head, std::memory_order_release);

            return space_to_make;
        }
    }

    /**
     * @brief emit_event_internal
     * @param type: Event type
     *
     * Emit event to all subscribers
     */
    void emit_event_internal(EventType type) noexcept {
        if (callback_count == 0) return;

        BufferEvent evt{.type = type,
                        .current_size = get_data_size(),
                        .free_space = get_free_size(),
                        .sequence_number = event_sequence++};

        for (size_t i = 0; i < callback_count; ++i) {
            if (callbacks[i]) {
                callbacks[i](evt);
            }
        }
    }

    /// Ring buffer storage (fixed-size, allocated on stack).
    /// Layout: [0] [1] [2] ... [MaxSize-1] -> wraps to [0].
    /// Consider using an external data storage
    std::array<T, MaxSize> buf = {};

    /// Event notification callbacks
    std::array<EventCallback, MaxCallbacks> callbacks = {};
    size_t callback_count = 0;

    /// Conditional type of head and tail. Atomic if ThreadSafe is true.
    using atomic_size = std::conditional_t<ThreadSafe, std::atomic<size_t>, size_t>;
    /// Data alignment of head and tail
    constexpr static int al = 64;
    /// Bitmask we use to check buffer overflow
    constexpr static size_t mask = (MaxSize - 1);
    /// Event sequence counter for monotonic event ordering
    size_t event_sequence = 0;

    /// Statistics counters
    RingbufStatistics stats;

    /// Read pointer (where consumer reads from)
    alignas(al) atomic_size head = 0;
    /// Write pointer (where producer writes to)
    alignas(al) atomic_size tail = 0;
};

}  // namespace rb
