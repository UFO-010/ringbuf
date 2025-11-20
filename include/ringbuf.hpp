
#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include <cstddef>
#include <algorithm>
#include <atomic>
#include <cstring>

#include "blockdata.hpp"

template <typename T, size_t max_size, bool ThreadSafe>
class spsc_ringbuf {
    static_assert((max_size & (max_size - 1)) == 0, "max_size value should be power of 2");

public:
    explicit spsc_ringbuf() = default;

    void reset() {
        store(head, 0);
        store(tail, 0);
    }

    /**
     * @brief size
     * @return current number of data stored in buffer
     */
    size_t size() const { return get_data_size(); }

    /**
     * @brief capacity
     * @return size of data buffer can store
     */
    size_t capacity() const { return max_size; }

    bool empty() const { return get_data_size() == 0; }

    bool full() const { return get_free_size() == 0; }

    bool push_back(const T &item) {
        if (full()) {
            return false;
        }

        size_t local_tail = load(tail, std::memory_order_acquire);

        buf[local_tail] = item;

        local_tail = (local_tail + 1) & mask;

        store(tail, local_tail, std::memory_order_release);

        return true;
    }

    bool push_back(T &&item) {
        if (full()) {
            return false;
        }

        size_t local_tail = load(tail, std::memory_order_acquire);

        buf[local_tail] = std::move(item);

        local_tail = (local_tail + 1) & mask;

        store(tail, local_tail, std::memory_order_release);

        return true;
    }

    size_t append(const T *item, size_t size) {
        if (size == 0 || item == nullptr) {
            return 0;
        }

        size_t local_tail = load(tail, std::memory_order_acquire);

        size_t copy_size = buf_store(local_tail, item, size);
        size_t new_tail = (local_tail + copy_size) & mask;

        store(tail, new_tail, std::memory_order_release);
        return copy_size;
    }

    T pop_front() {
        if (empty()) {
            return {};
        }

        size_t local_head = load(head, std::memory_order_acquire);
        T item = std::move(buf[local_head]);

        local_head = (local_head + 1) & mask;
        store(head, local_head, std::memory_order_release);

        return item;
    }

    bool pop_front(T &dest) {
        if (empty()) {
            return false;
        }

        size_t local_head = load(head, std::memory_order_acquire);
        dest = std::move(buf[local_head]);

        local_head = (local_head + 1) & mask;
        store(head, local_head, std::memory_order_release);

        return true;
    }

    size_t read_ready(T *item, size_t size) {
        if (size == 0 || item == nullptr) {
            return 0;
        }

        size_t local_head = load(head, std::memory_order_acquire);

        size_t copy_size = buf_read(local_head, item, size);
        size_t new_head = (local_head + copy_size) & mask;

        store(head, new_head, std::memory_order_release);

        return copy_size;
    }

    T peek() const {
        size_t head_local = load(head, std::memory_order_relaxed);
        return buf[head_local];
    }

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
     * @brief get_full
     * @return Number of T elements in buffer
     *
     *       `head`           `tail`
     * --------|================|---------
     *    free        data         free
     *
     *       `tail`           `head`
     * ========|----------------|========
     *    data        free         data
     */
    size_t get_data_size() const {
        size_t local_head = load(head, std::memory_order_relaxed);
        size_t local_tail = load(tail, std::memory_order_relaxed);

        return (local_tail - local_head) & mask;
    }

    size_t get_data_size(size_t local_head) const {
        size_t local_tail = load(tail, std::memory_order_relaxed);
        return (local_tail - local_head) & mask;
    }

    /**
     * @brief get_free
     * @return Number of free T elements in buffer
     */
    size_t get_free_size() const { return max_size - 1 - get_data_size(); }

    size_t get_free_size(size_t local_tail) const {
        size_t local_head = load(head, std::memory_order_relaxed);

        return max_size - 1 - ((local_tail - local_head) & mask);
    }

    void advance_write_pointer(size_t advance) {
        if (advance == 0 || full()) {
            return;
        }

        size_t local_tail = load(tail, std::memory_order_acquire);
        size_t new_tail = (local_tail + advance) & mask;

        store(tail, new_tail, std::memory_order_release);
    }

    void advance_read_pointer(size_t advance) {
        if (advance == 0 || empty()) {
            return;
        }

        size_t local_head = load(head, std::memory_order_acquire);
        size_t new_head = (local_head + advance) & mask;

        store(head, new_head, std::memory_order_release);
    }

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

    BufferSegments<T> get_write_segments() {
        size_t local_tail = load(tail, std::memory_order_acquire);

        size_t free_space = get_free_size(local_tail);

        if (free_space == 0) {
            return {{nullptr, 0}, {nullptr, 0}};
        }

        size_t first_size = std::min(free_space, max_size - local_tail);
        T *first_ptr = buf.data() + local_tail;
        LinearBlock<T> first_block = {first_ptr, first_size};

        LinearBlock<T> second_block = {nullptr, 0};
        size_t second_size = free_space - first_size;

        if (second_size > 0) {
            second_block = {buf.data(), second_size};
        }

        return {first_block, second_block};
    }

    BufferSegments<T> get_read_segments() {
        size_t local_head = load(head, std::memory_order_acquire);
        size_t data_size = get_data_size(local_head);

        if (data_size == 0) {
            return {{nullptr, 0}, {nullptr, 0}};
        }

        size_t first_size = std::min(data_size, max_size - local_head);

        T *first_ptr = buf.data() + local_head;
        LinearBlock<T> first_block = {first_ptr, first_size};

        LinearBlock<T> second_block = {nullptr, 0};
        size_t second_size = data_size - first_size;

        if (second_size > 0) {
            second_block = {buf.data(), second_size};
        }

        return {first_block, second_block};
    }

private:
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
     * @param var
     * @param value new value of `var`
     * @param order
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

    size_t buf_store(const size_t local_tail, const T *item, size_t size) {
        size_t free_data_size = get_free_size(local_tail);
        if (free_data_size == 0) {
            return 0;
        }

        size_t copy_size = std::min(size, free_data_size);

        // Copy linear part
        const T *data_ptr = item;
        size_t first_part = std::min(max_size - local_tail, copy_size);
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

    size_t buf_read(const size_t local_head, T *item, const size_t size) {
        size_t full_data_size = get_data_size(local_head);
        if (full_data_size == 0) {
            return 0;
        }

        size_t copy_size = std::min(full_data_size, size);

        // Copy linear part
        T *data_ptr = item;
        size_t first_part = std::min(max_size - local_head, copy_size);
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

    std::array<T, max_size> buf = {};

    /// Conditional type of head and tail. Atomic if ThreadSafe is true.
    using atomic_size = std::conditional_t<ThreadSafe, std::atomic<size_t>, size_t>;
    /// Data alignment of head and tail
    constexpr static int al = 64;
    /// Bitmask we use to check buffer overflow
    constexpr static size_t mask = (max_size - 1);

    alignas(al) atomic_size head = 0;
    alignas(al) atomic_size tail = 0;
};

#endif
