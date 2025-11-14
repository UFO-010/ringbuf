
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

    ASSERT_THAT(out_buf, testing::ElementsAreArray("Hello world\0\0\0\0"));
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
    rb.advance_read_pointer(skip);
    skipped = rb.get_free_size();
    EXPECT_EQ(skipped, skip - 1);

    // do overflow
    rb.reset();
    rb.advance_write_pointer(temp_size);
    skipped = rb.get_data_size();
    EXPECT_EQ(skipped, 0);

    // skip to the end
    rb.reset();
    rb.advance_write_pointer(temp_size - 1);
    skipped = rb.get_data_size();
    EXPECT_EQ(skipped, temp_size - 1);

    rb.reset();
    rb.advance_read_pointer(temp_size - 1);
    skipped = rb.get_free_size();
    EXPECT_EQ(skipped, temp_size - 2);

    rb.reset();
    rb.advance_read_pointer(temp_size);
    skipped = rb.get_free_size();
    EXPECT_EQ(skipped, temp_size - 1);
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
    ASSERT_THAT(out_buf, testing::ElementsAreArray("Hello world"));

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

TEST(ringbuf_test, block_test) {
    constexpr size_t temp_size = 16;
    constexpr size_t skip = 5;

    spsc_ringbuf<char, temp_size, false> rb;

    auto bl = rb.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - 1);
    EXPECT_NE(bl.data(), nullptr);

    rb.advance_write_pointer(skip);
    bl = rb.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - skip - 1);
    EXPECT_NE(bl.data(), nullptr);

    rb.reset();
    rb.advance_read_pointer(skip);
    bl = rb.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), skip - 1);
    EXPECT_NE(bl.data(), nullptr);

    rb.reset();
    rb.advance_write_pointer(temp_size - 1);
    bl = rb.get_write_linear_block_single();
    EXPECT_EQ(bl.size(), 0);
    EXPECT_EQ(bl.data(), nullptr);

    rb.reset();
    rb.advance_write_pointer(skip);
    bl = rb.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), skip);
    EXPECT_NE(bl.data(), nullptr);

    rb.reset();
    rb.advance_read_pointer(skip);
    bl = rb.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - skip);
    EXPECT_NE(bl.data(), nullptr);

    rb.reset();
    rb.advance_write_pointer(temp_size - 1);
    bl = rb.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), temp_size - 1);
    EXPECT_NE(bl.data(), nullptr);

    rb.reset();
    bl = rb.get_read_linear_block_single();
    EXPECT_EQ(bl.size(), 0);
    EXPECT_EQ(bl.data(), nullptr);
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
}
