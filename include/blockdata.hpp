
#include <cstddef>

template <typename T>
struct LinearBlock {
    T* ptr = nullptr;
    size_t b_size = 0;

    bool empty() const noexcept { return b_size == 0 || ptr == nullptr; }

    T* data() const noexcept { return ptr; }

    size_t size() const noexcept { return b_size; }

    /**
     * @brief end
     * @return pointer to the last element
     */
    T* end() const noexcept { return ptr + b_size; }

    /**
     * @brief bytes
     * @return size of buffer in bytes
     */
    size_t bytes() const noexcept { return b_size * sizeof(T); }
};

template <typename T>
struct BufferSegments {
    LinearBlock<T> first;
    LinearBlock<T> second;

    size_t total_size() const noexcept { return first.size() + second.size(); }

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
