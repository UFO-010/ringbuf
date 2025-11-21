
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "ringbuf.hpp"

TEST(ringbuf_test, zero_test) {
    constexpr size_t temp_size = 16;
    constexpr size_t temp = 5;

    spsc_ringbuf<char, temp_size, false> rb;
    EXPECT_EQ(rb.append(nullptr, temp), 0);

    std::array<char, temp> t = {};
    EXPECT_EQ(rb.append(t.data(), 0), 0);

    EXPECT_EQ(rb.read_ready(nullptr, 5), 0);

    rb.reset();
    rb.append(t.data(), t.size());
    EXPECT_EQ(rb.read_ready(t.data(), 0), 0);
}

TEST(ringbuf_test, read_test) {
    constexpr size_t temp_size = 16;
    std::array<char, temp_size> out_buf = {};
    out_buf.fill(0);

    size_t read_num = sizeof("00000000000");

    spsc_ringbuf<char, temp_size, false> rb;

    rb.append("00000000000", read_num);
    std::array<char, temp_size> unused = {};
    size_t readed = rb.read_ready(unused.data(), read_num);

    EXPECT_EQ(readed, read_num);

    read_num = sizeof("Hello world");
    rb.append("Hello world", read_num);
    readed = rb.read_ready(out_buf.data(), read_num);
    EXPECT_EQ(readed, read_num);

    std::array<char, temp_size> expected = {};
    std::copy_n("Hello world", read_num, expected.begin());
    ASSERT_THAT(out_buf, testing::ElementsAreArray(expected));
}

// Remember that we keep 1 character to check overflow
TEST(ringbuf_test, size_test) {
    constexpr size_t temp_size = 16;
    spsc_ringbuf<char, temp_size, false> rb;

    std::array<char, temp_size> unused = {};
    constexpr std::string_view st("Hello world", sizeof("Hello world"));

    rb.append(st.data(), st.size());

    EXPECT_EQ(rb.get_data_size(), st.size());
    size_t free_size = rb.capacity() - 1 - rb.get_data_size();
    EXPECT_EQ(free_size, rb.get_free_size());

    size_t readed = rb.read_ready(unused.data(), st.size());
    EXPECT_EQ(readed, st.size());

    free_size = rb.capacity() - 1 - rb.get_data_size();
    EXPECT_EQ(free_size, rb.get_free_size());

    constexpr size_t skip = 5;
    rb.reset();
    rb.advance_write_pointer(skip);
    size_t skipped = rb.get_data_size();
    EXPECT_EQ(skipped, skip);

    rb.reset();
    rb.advance_write_pointer(temp_size - skip);
    skipped = rb.get_free_size();
    EXPECT_EQ(skipped, skip - 1);

    // Do overflow
    rb.reset();
    rb.advance_write_pointer(temp_size);
    skipped = rb.get_data_size();
    EXPECT_EQ(skipped, 0);

    // Skip to the end
    rb.reset();
    rb.advance_write_pointer(temp_size - 1);
    skipped = rb.get_data_size();
    EXPECT_EQ(skipped, temp_size - 1);

    rb.reset();
    rb.advance_read_pointer(temp_size - 1);
    skipped = rb.get_free_size();
    EXPECT_EQ(skipped, temp_size - 1);

    // Skip head to the end, free size should be (capacity - 1)
    rb.reset();
    rb.advance_read_pointer(temp_size);
    skipped = rb.get_free_size();
    EXPECT_EQ(skipped, temp_size - 1);
}

TEST(ringbuf_test, advance_pointers_test) {
    constexpr size_t temp_size = 4;
    spsc_ringbuf<int, temp_size, false> rb;

    rb.advance_write_pointer(0);
    rb.advance_read_pointer(0);
    EXPECT_EQ(rb.get_data_size(), 0);
    EXPECT_EQ(rb.get_free_size(), temp_size - 1);

    // Fill buffer to capacity
    for (size_t i = 0; i < temp_size - 1; ++i) {
        rb.push_back(static_cast<int>(i));
    }
    EXPECT_EQ(rb.get_data_size(), temp_size - 1);
    EXPECT_EQ(rb.get_free_size(), 0);
    EXPECT_TRUE(rb.full());

    // Try advance when full
    rb.advance_write_pointer(1);
    EXPECT_EQ(rb.get_data_size(), temp_size - 1);
    EXPECT_EQ(rb.get_free_size(), 0);

    rb.reset();
    rb.push_back(1);
    rb.push_back(2);
    EXPECT_EQ(rb.get_data_size(), 2);

    rb.advance_read_pointer(1);
    EXPECT_EQ(rb.get_data_size(), 1);

    rb.advance_read_pointer(1);
    EXPECT_EQ(rb.get_data_size(), 0);
    EXPECT_TRUE(rb.empty());

    // Advance read to data, data should be zero
    rb.advance_read_pointer(2);
    EXPECT_EQ(rb.get_data_size(), 0);
    EXPECT_EQ(rb.get_free_size(), temp_size - 1);
}

TEST(ringbuf_test, overflow_test) {
    constexpr size_t temp_size = 16;

    // Remember that string_view doesn't guarantee null-terminated character
    constexpr std::string_view st("Hello world", sizeof("Hello world"));
    std::array<char, st.size()> out_buf = {};
    out_buf.fill('\0');

    spsc_ringbuf<char, temp_size, false> rb;

    rb.append(st.data(), st.size());

    rb.read_ready(out_buf.data(), out_buf.size());
    ASSERT_THAT(out_buf, testing::ElementsAreArray(st));

    rb.append("Hello", sizeof("Hello"));

    std::array<char, sizeof("Hello")> new_buf = {};  // Remember '\0'
    out_buf.fill('\0');

    rb.read_ready(new_buf.data(), sizeof("Hello"));
    ASSERT_THAT(new_buf, testing::ElementsAreArray("Hello"));

    rb.reset();

    out_buf.fill('\0');
    rb.append("Hello world", st.size());
    rb.append("world Hello", st.size());
    rb.read_ready(out_buf.data(), out_buf.size());

    std::array<char, st.size()> expected_overwrite = {};
    std::copy_n("Hello world", st.size(), expected_overwrite.begin());
    ASSERT_THAT(out_buf, testing::ElementsAreArray(expected_overwrite));

    // We should be able to read and write at least ringbuf capacity
    rb.reset();
    constexpr size_t big_size = 128;
    std::array<char, big_size> big_buf = {};
    big_buf.fill('\0');
    size_t readed = rb.append(big_buf.data(), big_buf.size());
    EXPECT_EQ(readed, rb.capacity() - 1);
    readed = rb.read_ready(big_buf.data(), big_buf.size());
    EXPECT_EQ(readed, rb.capacity() - 1);
}

TEST(ringbuf_test, linear_block_test) {
    constexpr size_t temp_size = 16;
    constexpr size_t skip = 5;

    spsc_ringbuf<char, temp_size, false> rb;

    auto bl = rb.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - 1);
    EXPECT_NE(bl.data(), nullptr);
    EXPECT_NE(bl.end(), nullptr);
    EXPECT_FALSE(bl.empty());
    // Test buf is char, num of bytes = size
    EXPECT_EQ(bl.size(), bl.bytes());

    rb.advance_write_pointer(skip);
    bl = rb.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - skip - 1);
    EXPECT_NE(bl.data(), nullptr);
    EXPECT_FALSE(bl.empty());

    rb.reset();
    rb.advance_write_pointer(temp_size - skip);
    bl = rb.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), skip - 1);
    EXPECT_NE(bl.data(), nullptr);

    rb.reset();
    rb.advance_write_pointer(temp_size - 1);
    bl = rb.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), 0);
    EXPECT_TRUE(bl.empty());
    EXPECT_EQ(bl.data(), nullptr);
    EXPECT_EQ(bl.end(), nullptr);

    rb.reset();
    rb.advance_write_pointer(skip);
    bl = rb.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), skip);
    EXPECT_NE(bl.data(), nullptr);

    rb.reset();
    rb.advance_write_pointer(temp_size - skip);
    bl = rb.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - skip);
    EXPECT_NE(bl.data(), nullptr);

    rb.reset();
    rb.advance_write_pointer(temp_size - 1);
    bl = rb.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - 1);
    EXPECT_FALSE(bl.empty());
    EXPECT_NE(bl.data(), nullptr);

    rb.reset();
    bl = rb.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), 0);
    EXPECT_TRUE(bl.empty());
    EXPECT_EQ(bl.data(), nullptr);
    EXPECT_EQ(bl.end(), nullptr);
}

TEST(ringbuf_test, block_test) {
    constexpr size_t temp_size = 8;
    spsc_ringbuf<int, temp_size, false> rb;
    const int skip = 3;

    for (int i = 0; i < skip; ++i) {
        rb.push_back(i);
    }

    auto read_blocks = rb.get_read_segments();
    EXPECT_TRUE(read_blocks.is_linear());
    EXPECT_EQ(read_blocks.first.size(), skip);
    EXPECT_EQ(read_blocks.second.size(), 0);
    EXPECT_EQ(read_blocks.total_size(), skip);
    for (int i = 0; i < skip; i++) {
        // clang-format off
        // NOLINTNEXTLINE
        EXPECT_EQ(*(read_blocks.first.data() + i), i); //NOSONAR
        // clang-format on
    }

    auto write_blocks = rb.get_write_segments();
    EXPECT_TRUE(write_blocks.is_linear());
    EXPECT_EQ(write_blocks.first.size(), temp_size - 1 - skip);
    EXPECT_EQ(write_blocks.second.size(), 0);

    // Do overflow, so data wrap around
    rb.reset();
    rb.advance_write_pointer(temp_size - skip);
    rb.advance_read_pointer(temp_size - skip);

    const int over_skip = skip + 4;

    for (int i = 0; i < over_skip; i++) {
        rb.push_back(i);
    }

    read_blocks = rb.get_read_segments();
    EXPECT_FALSE(read_blocks.is_linear());
    EXPECT_EQ(read_blocks.total_size(), over_skip);
    EXPECT_EQ(read_blocks.total_bytes(), over_skip * sizeof(int));
    EXPECT_FALSE(read_blocks.empty());

    const int first_expected = temp_size - skip - 2;
    const int second_expected = over_skip - first_expected;
    EXPECT_EQ(read_blocks.first.size(), first_expected);
    EXPECT_NE(read_blocks.first.data(), nullptr);
    EXPECT_EQ(read_blocks.second.size(), second_expected);
    EXPECT_NE(read_blocks.second.data(), nullptr);

    for (int i = 0; i < read_blocks.first.size(); i++) {
        // clang-format off
        // NOLINTNEXTLINE
        EXPECT_EQ(*(read_blocks.first.data() + i), i); //NOSONAR
        // clang-format on
    }

    for (int i = 0; i < read_blocks.second.size(); i++) {
        // clang-format off
        // NOLINTNEXTLINE
        EXPECT_EQ(*(read_blocks.second.data() + i), i + read_blocks.first.size()); //NOSONAR
        // clang-format on
    }

    write_blocks = rb.get_write_segments();
    EXPECT_TRUE(write_blocks.is_linear());
}

TEST(ringbuf_test, push_pop_test) {
    constexpr size_t temp_size = 8;
    const char test_ch = 'H';

    spsc_ringbuf<char, temp_size, false> rb;

    rb.push_back(test_ch);
    char test = 0;
    rb.pop_front(test);
    EXPECT_EQ(test, test_ch);

    rb.push_back(test_ch);
    test = rb.pop_front();
    EXPECT_EQ(test, test_ch);

    rb.reset();
    rb.advance_write_pointer(rb.capacity() - 1);
    EXPECT_EQ(rb.push_back(test_ch), false);

    rb.reset();
    rb.advance_read_pointer(rb.capacity());
    EXPECT_EQ(rb.pop_front(test), false);
    rb.push_back('A');
    EXPECT_EQ(rb.pop_front(), 'A');
    EXPECT_EQ(rb.pop_front(), {});

    spsc_ringbuf<std::string, temp_size, false> st_rb;
    std::string st_test = "Hello world";
    st_rb.push_back(st_test);
    EXPECT_EQ(st_rb.pop_front(), st_test);

    st_rb.reset();
    st_test = "Hello world";
    st_rb.push_back(st_test);
    std::string str;
    st_rb.pop_front(str);
    EXPECT_EQ(str, st_test);

    st_rb.reset();
    std::array<std::string, temp_size> st_ar;
    st_test = "Hello";
    st_rb.push_back(st_test);
    st_test = " world";
    st_rb.push_back(st_test);
    // Read only 2 items
    st_rb.read_ready(st_ar.data(), st_ar.size());
    st_test = st_ar.at(0) + st_ar.at(1);
    EXPECT_EQ(st_test, "Hello world");
}

TEST(ringbuf_test, move_semantics) {
    constexpr size_t temp_size = 4;
    spsc_ringbuf<std::string, temp_size, false> rb;

    std::string original = "This is a long string that might trigger move semantics";
    // Keep a copy to check original is moved from
    std::string original_copy = original;

    rb.push_back(std::move(original));
    // Original should be moved from (empty string is common result)
    // clang-format off
    // NOLINTNEXTLINE
    EXPECT_EQ(original, ""); //NOSONAR
    // clang-format on
    EXPECT_FALSE(rb.empty());

    std::string retrieved = rb.pop_front();
    // Retrieved should be the original content
    EXPECT_EQ(retrieved, original_copy);
    EXPECT_TRUE(rb.empty());
}
