
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "ringbuf.hpp"

TEST(ringbuf_test, read_test) {
    constexpr size_t temp_size = 16;
    spcs_ringbuf<char, temp_size, false> a;
    std::array<char, temp_size> out_buf = {};
    out_buf.fill(0);

    size_t read_num = 11;

    a.append("00000000000", read_num);
    std::array<char, temp_size> unused = {};
    size_t readed = a.read_ready(unused.data(), read_num);

    EXPECT_EQ(readed, read_num);

    read_num = sizeof("Hello world");
    a.append("Hello world", read_num);
    readed = a.read_ready(out_buf.data(), read_num);
    EXPECT_EQ(readed, read_num);
    //    std::cout << out_buf << "\n";
    ASSERT_THAT(out_buf, testing::ElementsAreArray("Hello world\0\0\0\0"));
}

TEST(ringbuf_test, overflow_test) {
    constexpr size_t temp_size = 16;
    // Remember that string_view doesn't guarantee null-terminated character
    constexpr std::string_view st("Hello world", sizeof("Hello world"));
    spcs_ringbuf<char, temp_size, false> a;
    std::array<char, st.size()> out_buf = {};  // Remember '\0'
    out_buf.fill('\0');

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
}
