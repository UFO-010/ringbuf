
#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include <cstddef>
#include <algorithm>
#include <atomic>
#include <cstring>

template <typename T, size_t max_size, bool ThreadSafe>
class spsc_ringbuf {
    static_assert((max_size & (max_size - 1)) == 0, "max_size value should be power of 2");

public:
    explicit spsc_ringbuf() = default;

    void reset() {
        store(head, 0);
        store(tail, 0);
    }

    size_t size() const { return get_data_size(); }

    size_t capacity() const { return max_size; }

    bool empty() const { return get_data_size() == 0; }

    bool full() const { return get_free_size() == 0; }

    bool push_back(const T &item) {
        if (full()) {
            return false;
        }

        size_t local_tail = load(tail, std::memory_order_acquire);

        buf[local_tail] = item;

        local_tail = (local_tail + 1) & (max_size - 1);

        tail.store(local_tail, std::memory_order_release);
    }

    size_t append(const T *item, size_t size) {
        if (size == 0 || item == nullptr || size > max_size) {
            return 0;
        }

        size_t local_tail = load(tail, std::memory_order_acquire);

        size_t copy_size = buf_store(local_tail, item, size);
        size_t new_tail = (local_tail + copy_size) & (max_size - 1);

        store(tail, new_tail, std::memory_order_release);
        return copy_size;
    }

    T get() {
        if (empty()) {
            return {};
        }

        size_t local_head = head.load(std::memory_order_acquire);
        T item = buf[local_head];

        local_head = (local_head + 1) & (max_size - 1);

        head.store(local_head, std::memory_order_release);

        return item;
    }

    T peek() {
        size_t head_local = head.load(std::memory_order_relaxed);
        return buf[head_local];
    }

    size_t skip(size_t size) {
        size_t full_data_size = get_data_size();
        if (full_data_size == 0) {
            return 0;
        }

        size = std::min(full_data_size, size);

        size_t local_head = load(head, std::memory_order_acquire);

        local_head += size;
        if (local_head >= max_size) {
            local_head -= max_size;
        }

        store(head, local_head, std::memory_order_release);
        return size;
    }

    size_t read_ready(T *item, size_t size) {
        if (size == 0 || item == nullptr || size > max_size) {
            return 0;
        }

        size_t local_head = load(head, std::memory_order_acquire);

        size_t copy_size = buf_read(local_head, item, size);
        size_t new_head = (local_head + copy_size) & (max_size - 1);

        store(head, new_head, std::memory_order_release);

        return copy_size;
    }

    size_t peek_ready(T *item, size_t size) {
        if (size == 0 || item == nullptr) {
            return 0;
        }

        size_t full_data_size = get_data_size();
        if (full_data_size == 0) {
            return 0;
        }

        size_t local_head = load(head, std::memory_order_relaxed);

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
    size_t get_data_size() {
        size_t local_head = load(head);
        size_t local_tail = load(tail);

        return (local_tail - local_head) & (max_size - 1);
    }

    /**
     * @brief get_free
     * @return Number of free T elements in buffer
     */
    size_t get_free_size() { return max_size - 1 - get_data_size(); }

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
        size_t free_data_size = get_free_size();
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
            std::copy_n(data_ptr, first_part, buf.get() + local_tail);
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
        size_t full_data_size = get_data_size();
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

    std::array<T, max_size> buf;

    using atomic_size = std::conditional_t<ThreadSafe, std::atomic<size_t>, size_t>;
    constexpr static int al = 64;

    alignas(al) atomic_size head = 0;
    alignas(al) atomic_size tail = 0;
};

#endif
