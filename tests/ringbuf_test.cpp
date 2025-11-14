
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "ringbuf.hpp"

TEST(ringbuf_test, zero_test) {
    constexpr size_t temp_size = 16;
    constexpr size_t temp = 5;

    spsc_ringbuf<char, temp_size, false> a;
    EXPECT_EQ(a.append(nullptr, temp), 0);

    std::array<char, temp> t = {};
    EXPECT_EQ(a.append(t.data(), 0), 0);

    EXPECT_EQ(a.read_ready(nullptr, 5), 0);

    a.reset();
    a.append(t.data(), t.size());
    EXPECT_EQ(a.read_ready(t.data(), 0), 0);
}

TEST(ringbuf_test, read_test) {
    constexpr size_t temp_size = 16;
    std::array<char, temp_size> out_buf = {};
    out_buf.fill(0);

    size_t read_num = sizeof("00000000000");

    spsc_ringbuf<char, temp_size, false> a;

    a.append("00000000000", read_num);
    std::array<char, temp_size> unused = {};
    size_t readed = a.read_ready(unused.data(), read_num);

    EXPECT_EQ(readed, read_num);

    read_num = sizeof("Hello world");
    a.append("Hello world", read_num);
    readed = a.read_ready(out_buf.data(), read_num);
    EXPECT_EQ(readed, read_num);

    ASSERT_THAT(out_buf, testing::ElementsAreArray("Hello world\0\0\0\0"));
}

// Remember that we keep 1 character to check overflow
TEST(ringbuf_test, size_test) {
    constexpr size_t temp_size = 16;
    spsc_ringbuf<char, temp_size, false> a;

    std::array<char, temp_size> unused = {};
    constexpr std::string_view st("Hello world", sizeof("Hello world"));

    a.append(st.data(), st.size());

    EXPECT_EQ(a.get_data_size(), st.size());
    size_t free_size = a.capacity() - 1 - a.get_data_size();
    EXPECT_EQ(free_size, a.get_free_size());

    size_t readed = a.read_ready(unused.data(), st.size());
    EXPECT_EQ(readed, st.size());

    free_size = a.capacity() - 1 - a.get_data_size();
    EXPECT_EQ(free_size, a.get_free_size());

    constexpr size_t skip = 5;
    a.reset();
    a.advance_write_pointer(skip);
    size_t skipped = a.get_data_size();
    EXPECT_EQ(skipped, skip);

    a.reset();
    a.advance_read_pointer(skip);
    skipped = a.get_free_size();
    EXPECT_EQ(skipped, skip - 1);

    // do overflow
    a.reset();
    a.advance_write_pointer(temp_size);
    skipped = a.get_data_size();
    EXPECT_EQ(skipped, 0);

    // skip to the end
    a.reset();
    a.advance_write_pointer(temp_size - 1);
    skipped = a.get_data_size();
    EXPECT_EQ(skipped, temp_size - 1);

    a.reset();
    a.advance_read_pointer(temp_size - 1);
    skipped = a.get_free_size();
    EXPECT_EQ(skipped, temp_size - 2);

    a.reset();
    a.advance_read_pointer(temp_size);
    skipped = a.get_free_size();
    EXPECT_EQ(skipped, temp_size - 1);
}

TEST(ringbuf_test, overflow_test) {
    constexpr size_t temp_size = 16;

    // Remember that string_view doesn't guarantee null-terminated character
    constexpr std::string_view st("Hello world", sizeof("Hello world"));
    std::array<char, st.size()> out_buf = {};
    out_buf.fill('\0');

    spsc_ringbuf<char, temp_size, false> a;

    a.append(st.data(), st.size());

    a.read_ready(out_buf.data(), out_buf.size());
    ASSERT_THAT(out_buf, testing::ElementsAreArray(st));

    a.append("Hello", sizeof("Hello"));

    std::array<char, sizeof("Hello")> new_buf = {};  // Remember '\0'
    out_buf.fill('\0');

    a.read_ready(new_buf.data(), sizeof("Hello"));
    ASSERT_THAT(new_buf, testing::ElementsAreArray("Hello"));

    a.reset();

    out_buf.fill('\0');
    a.append("Hello world", st.size());
    a.append("world Hello", st.size());
    a.read_ready(out_buf.data(), out_buf.size());
    ASSERT_THAT(out_buf, testing::ElementsAreArray("Hello world"));

    // We should be able to read and write at least ringbuf capacity
    a.reset();
    constexpr size_t big_size = 128;
    std::array<char, big_size> big_buf = {};
    big_buf.fill('\0');
    size_t readed = a.append(big_buf.data(), big_buf.size());
    EXPECT_EQ(readed, a.capacity() - 1);
    readed = a.read_ready(big_buf.data(), big_buf.size());
    EXPECT_EQ(readed, a.capacity() - 1);
}

TEST(ringbuf_test, block_test) {
    constexpr size_t temp_size = 16;
    constexpr size_t skip = 5;

    spsc_ringbuf<char, temp_size, false> a;

    auto bl = a.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - 1);
    EXPECT_NE(bl.data(), nullptr);

    a.advance_write_pointer(skip);
    bl = a.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - skip - 1);
    EXPECT_NE(bl.data(), nullptr);

    a.reset();
    a.advance_read_pointer(skip);
    bl = a.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), skip - 1);
    EXPECT_NE(bl.data(), nullptr);

    a.reset();
    a.advance_write_pointer(temp_size - 1);
    bl = a.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), 0);
    EXPECT_EQ(bl.data(), nullptr);

    a.reset();
    a.advance_write_pointer(skip);
    bl = a.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), skip);
    EXPECT_NE(bl.data(), nullptr);

    a.reset();
    a.advance_read_pointer(skip);
    bl = a.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - skip);
    EXPECT_NE(bl.data(), nullptr);

    a.reset();
    a.advance_write_pointer(temp_size - 1);
    bl = a.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - 1);
    EXPECT_NE(bl.data(), nullptr);

    a.reset();
    bl = a.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), 0);
    EXPECT_EQ(bl.data(), nullptr);
}

TEST(ringbuf_test, push_pop_test) {
    constexpr size_t temp_size = 8;
    const char test_ch = 'H';

    spsc_ringbuf<char, temp_size, false> a;

    a.push_back(test_ch);
    char test = 0;
    a.pop_front(test);
    EXPECT_EQ(test, test_ch);

    a.push_back(test_ch);
    test = a.pop_front();
    EXPECT_EQ(test, test_ch);

    a.reset();
    a.advance_write_pointer(a.capacity() - 1);
    EXPECT_EQ(a.push_back(test_ch), false);

    a.reset();
    a.advance_read_pointer(a.capacity());
    EXPECT_EQ(a.pop_front(test), false);
    EXPECT_EQ(a.pop_front(), {});
}
