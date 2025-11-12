
#include <cstddef>

template <typename T>
struct LinearBlock {
    T* ptr = nullptr;
    size_t size = 0;

    bool empty() const noexcept { return size == 0 || ptr == nullptr; }

    /**
     * @brief end
     * @return pointer to the last element
     */
    T* end() const noexcept { return ptr + size; }

    /**
     * @brief bytes
     * @return size of buffer in bytes
     */
    size_t bytes() const noexcept { return size * sizeof(T); }
};

template <typename T>
struct BufferSegments {
    LinearBlock<T> first;
    LinearBlock<T> second;

    size_t total_size() const noexcept { return first.size + second.size; }

    bool empty() const noexcept { return first.empty() && second.empty(); }

    /**
     * @brief is_linear
     * @return true if buffer is linear
     *
     * Buffer is linear only if second segment is empty
     */
    bool is_linear() const noexcept { return second.empty(); }

    size_t total_bytes() const noexcept { return total_size() * sizeof(T); }
};
