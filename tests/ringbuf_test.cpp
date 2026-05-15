
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ringbuf.hpp"
#include "producer.hpp"
#include "consumer.hpp"

TEST(ringbuf_test, zero_test) {
    constexpr size_t temp_size = 16;
    constexpr size_t temp = 5;

    rb::spsc_ringbuf<char, temp_size, false> rb;
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

    rb::spsc_ringbuf<char, temp_size, false> rb;

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
    rb::spsc_ringbuf<char, temp_size, false> rb;

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
    rb::spsc_ringbuf<int, temp_size, false> rb;

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

    rb::spsc_ringbuf<char, temp_size, false> rb;

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

    // Default overflow policy is `OverflowPolicy::DROP`, so we won't be able to write or read data
    // at all if input size > capacity
    rb.reset();
    constexpr size_t big_size = 128;
    std::array<char, big_size> big_buf = {};
    big_buf.fill('\0');
    size_t readed = rb.append(big_buf.data(), big_buf.size());
    EXPECT_EQ(readed, 0);
    readed = rb.read_ready(big_buf.data(), big_buf.size());
    EXPECT_EQ(readed, 0);
}

TEST(ringbuf_test, peek_test) {
    constexpr size_t temp_size = 4;
    const size_t skip = 2;
    std::array<size_t, temp_size> test = {};

    rb::spsc_ringbuf<size_t, temp_size, false> rb;

    EXPECT_EQ(rb.peek(), {});
    EXPECT_EQ(rb.peek_ready(nullptr, 0), 0);
    EXPECT_EQ(rb.peek_ready(nullptr, skip), 0);
    EXPECT_EQ(rb.peek_ready(test.data(), 0), 0);

    rb.advance_write_pointer(temp_size - 1);
    rb.advance_read_pointer(temp_size - 1);
    EXPECT_EQ(rb.peek_ready(test.data(), test.size()), 0);

    rb.reset();
    const size_t first = 10;
    const size_t second = 20;
    rb.push_back(first);
    rb.push_back(second);
    // Should peek the first item without removing it
    EXPECT_EQ(rb.peek(), first);
    EXPECT_EQ(rb.peek(), first);

    size_t item = 0;
    rb.pop_front(item);
    EXPECT_EQ(item, first);
    EXPECT_EQ(rb.peek(), second);

    rb.reset();

    for (size_t i = 0; i < temp_size; i++) {
        rb.push_back(i);
    }
    // Should peek the items without removing them
    EXPECT_EQ(rb.peek_ready(test.data(), test.size()), temp_size - 1);
    for (size_t i = 0; i < temp_size - 1; i++) {
        EXPECT_EQ(test.at(i), i);
    }

    EXPECT_EQ(rb.peek_ready(test.data(), test.size()), temp_size - 1);
    for (size_t i = 0; i < temp_size - 1; i++) {
        EXPECT_EQ(test.at(i), i);
    }

    rb.reset();
    rb.advance_write_pointer(skip);
    rb.advance_read_pointer(skip);
    for (size_t i = 0; i < test.size(); i++) {
        rb.push_back(i);
    }

    EXPECT_EQ(rb.peek_ready(test.data(), test.size()), test.size() - 1);
    for (size_t i = 0; i < test.size() - 1; i++) {
        EXPECT_EQ(test.at(i), i);
    }

    EXPECT_EQ(rb.peek_ready(test.data(), test.size()), test.size() - 1);
    for (size_t i = 0; i < test.size() - 1; i++) {
        EXPECT_EQ(test.at(i), i);
    }
}

TEST(ringbuf_test, LinearBlockObtain) {
    constexpr size_t temp_size = 16;
    constexpr size_t skip = 5;

    rb::spsc_ringbuf<char, temp_size, false> rb;

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

TEST(ringbuf_test, BlockDataObtain) {
    constexpr size_t temp_size = 8;
    rb::spsc_ringbuf<size_t, temp_size, false> rb;
    const size_t skip = 3;

    auto read_blocks = rb.get_read_segments();
    EXPECT_TRUE(read_blocks.empty());

    auto write_blocks = rb.get_write_segments();
    EXPECT_FALSE(write_blocks.empty());

    for (size_t i = 0; i < skip; ++i) {
        rb.push_back(i);
    }

    read_blocks = rb.get_read_segments();
    EXPECT_TRUE(read_blocks.is_linear());
    EXPECT_EQ(read_blocks.first.size(), skip);
    EXPECT_EQ(read_blocks.second.size(), 0);
    EXPECT_EQ(read_blocks.total_size(), skip);
    EXPECT_FALSE(read_blocks.empty());
    for (size_t i = 0; i < skip; i++) {
        // clang-format off
        // NOLINTNEXTLINE
        EXPECT_EQ(*(read_blocks.first.data() + i), i); //NOSONAR
        // clang-format on
    }

    write_blocks = rb.get_write_segments();
    EXPECT_TRUE(write_blocks.is_linear());
    EXPECT_EQ(write_blocks.first.size(), temp_size - 1 - skip);
    EXPECT_EQ(write_blocks.second.size(), 0);

    rb.reset();
    rb.advance_write_pointer(skip);
    rb.advance_read_pointer(skip);
    write_blocks = rb.get_write_segments();
    EXPECT_FALSE(write_blocks.is_linear());
    EXPECT_EQ(write_blocks.first.size(), temp_size - skip);
    EXPECT_EQ(write_blocks.second.size(), temp_size - (temp_size - skip) - 1);

    // Do overflow, so data wrap around
    rb.reset();
    rb.advance_write_pointer(temp_size - skip);
    rb.advance_read_pointer(temp_size - skip);

    const size_t over_skip = skip + 4;

    for (size_t i = 0; i < over_skip; i++) {
        rb.push_back(i);
    }

    read_blocks = rb.get_read_segments();
    EXPECT_FALSE(read_blocks.is_linear());
    EXPECT_EQ(read_blocks.total_size(), over_skip);
    EXPECT_EQ(read_blocks.total_bytes(), over_skip * sizeof(size_t));
    EXPECT_FALSE(read_blocks.empty());

    const size_t first_expected = temp_size - skip - 2;
    const size_t second_expected = over_skip - first_expected;
    EXPECT_EQ(read_blocks.first.size(), first_expected);
    EXPECT_NE(read_blocks.first.data(), nullptr);
    EXPECT_EQ(read_blocks.second.size(), second_expected);
    EXPECT_NE(read_blocks.second.data(), nullptr);

    for (size_t i = 0; i < read_blocks.first.size(); i++) {
        // clang-format off
        // NOLINTNEXTLINE
        EXPECT_EQ(*(read_blocks.first.data() + i), i); //NOSONAR
        // clang-format on
    }

    for (size_t i = 0; i < read_blocks.second.size(); i++) {
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

    rb::spsc_ringbuf<char, temp_size, false> rb;

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

    rb::spsc_ringbuf<std::string, temp_size, false> st_rb;
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

    st_rb.reset();
    st_rb.advance_write_pointer(temp_size - 1);
    EXPECT_FALSE(st_rb.push_back(std::move(st_test)));
    st_rb.advance_read_pointer(temp_size - 1);
    EXPECT_EQ(st_rb.pop_front(), std::string());
}

TEST(ringbuf_test, move_semantics) {
    constexpr size_t temp_size = 4;
    rb::spsc_ringbuf<std::string, temp_size, false> rb;

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

TEST(ringbuf_test, ProducerConsumer) {
    constexpr size_t temp_size = 8;
    const size_t test_ch = 40;

    rb::spsc_ringbuf<size_t, temp_size, false> rb;
    auto producer = rb.get_producer();
    auto consumer = rb.get_consumer();

    producer.push_back(test_ch);
    size_t test = 0;
    consumer.pop_front(test);
    EXPECT_EQ(test, test_ch);

    producer.push_back(test_ch);
    test = consumer.pop_front();
    EXPECT_EQ(test, test_ch);

    rb.reset();
    EXPECT_EQ(producer.advance_write_pointer(rb.capacity() - 1), rb.capacity() - 1);
    EXPECT_EQ(producer.push_back(test_ch), false);
    EXPECT_EQ(consumer.advance_read_pointer(rb.capacity() - 1), rb.capacity() - 1);
    EXPECT_EQ(consumer.pop_front(), {});
}

TEST(ringbuf_test, OverflowPolicy_DROP) {
    constexpr size_t tempsize = 4;
    rb::spsc_ringbuf<size_t, tempsize, false, rb::OverflowPolicy::DROP> rb;
    for (size_t i = 0; i < tempsize - 1; i++) {
        rb.push_back(i);
    }

    EXPECT_FALSE(rb.push_back(99));  // Should fail silently
    EXPECT_EQ(rb.get_data_size(), tempsize - 1);
    EXPECT_EQ(rb.get_statistics().overflow_events, 1);  // Overflow event counted
}

TEST(ringbuf_test, OverflowPolicy_FAIL) {
    constexpr size_t tempsize = 4;
    rb::spsc_ringbuf<size_t, tempsize, false, rb::OverflowPolicy::FAIL> rb;
    for (size_t i = 0; i < tempsize - 1; i++) {
        rb.push_back(i);
    }
    EXPECT_FALSE(rb.push_back(99));  // Returns false on overflow
    EXPECT_EQ(rb.get_data_size(), tempsize - 1);
    EXPECT_EQ(rb.get_statistics().overflow_events, 1);
}

TEST(ringbuf_test, OverflowPolicy_OVERWRITE) {
    constexpr size_t tempsize = 4;
    const size_t over_start = 5;
    const size_t over_end = over_start + 5;
    rb::spsc_ringbuf<size_t, tempsize, false, rb::OverflowPolicy::OVERWRITE> rb;
    for (size_t i = 0; i < tempsize - 1; i++) {
        rb.push_back(i);
    }

    EXPECT_TRUE(rb.push_back(99));  // Overwrites oldest (0)
    EXPECT_EQ(rb.get_data_size(), tempsize - 1);

    size_t oldest = 0;
    rb.pop_front(oldest);
    EXPECT_EQ(oldest, 1);  // 0 overwritten
    EXPECT_EQ(rb.get_statistics().overflow_events, 1);

    for (size_t i = over_start; i < over_end; ++i) {
        EXPECT_TRUE(rb.push_back(i));
    }

    // Should have 5 overflow events (one per overwrite)
    EXPECT_EQ(rb.get_statistics().overflow_events, over_end - over_start);

    // Should have 8 total pushes (3 initial + 5 overwrites)
    EXPECT_EQ(rb.get_statistics().total_pushes, tempsize + (over_end - over_start));

    // Buffer should contain last 3 values: 12, 13, 14
    std::array<size_t, 3> vals = {};
    rb.read_ready(vals.data(), 3);
    EXPECT_EQ(vals[0], over_end - 3);
    EXPECT_EQ(vals[1], over_end - 2);
    EXPECT_EQ(vals[2], over_end - 1);
}

TEST(ringbuf_test, OverflowPolicy_TOEND) {
    constexpr size_t tempsize = 4;
    size_t first_write = tempsize - 2;
    rb::spsc_ringbuf<size_t, tempsize, false, rb::OverflowPolicy::TOEND> rb;
    for (size_t i = 0; i < first_write; i++) {
        rb.push_back(i);
    }

    std::array<size_t, 3> temp = {1, 2, 3};
    size_t written = rb.append(temp.data(), temp.size());  // Partial write to end
    EXPECT_LT(written, 3u);
    EXPECT_EQ(written, tempsize - first_write - 1);  // No space, writes 0
    EXPECT_EQ(rb.get_data_size(), tempsize - 1);
}

TEST(ringbuf_test, CallbackSubscription) {
    constexpr size_t tempsize = 8;
    rb::spsc_ringbuf<int, tempsize, false, rb::OverflowPolicy::DROP> rb;

    static std::vector<rb::BufferEvent> events1;
    static std::vector<rb::BufferEvent> events2;

    EXPECT_TRUE(
        rb.subscribe(rb::EventType::DATA_AVAILABLE, static_cast<void *>(nullptr),
                     [](const rb::BufferEvent &evt, void *) noexcept { events1.push_back(evt); }));
    EXPECT_TRUE(
        rb.subscribe(rb::EventType::RESET, static_cast<void *>(nullptr),
                     [](const rb::BufferEvent &evt, void *) noexcept { events2.push_back(evt); }));
    // overwrite event callback
    EXPECT_TRUE(
        rb.subscribe(rb::EventType::DATA_AVAILABLE, static_cast<void *>(nullptr),
                     [](const rb::BufferEvent &evt, void *) noexcept { events1.push_back(evt); }));

    rb.push_back(1);  // Triggers DATA_AVAILABLE
    EXPECT_EQ(events1.at(0).type, rb::EventType::DATA_AVAILABLE);
    EXPECT_EQ(events1.at(0).current_size, 1);
    EXPECT_EQ(events1.at(0).free_space, tempsize - 2);

    rb.reset();  // Triggers RESET
    EXPECT_EQ(events2.size(), 1);
    EXPECT_EQ(events2.at(0).type, rb::EventType::RESET);
    EXPECT_EQ(events2.size(), 1);  // capture2 shouldn't get RESET
}

TEST(ringbuf_test, OverflowCallback) {
    constexpr size_t tempsize = 4;
    rb::spsc_ringbuf<int, tempsize, false, rb::OverflowPolicy::OVERWRITE> rb;

    static std::vector<rb::BufferEvent> events;
    EXPECT_TRUE(
        rb.subscribe(rb::EventType::DATA_AVAILABLE, static_cast<void *>(nullptr),
                     [](const rb::BufferEvent &evt, void *) noexcept { events.push_back(evt); }));
    EXPECT_TRUE(
        rb.subscribe(rb::EventType::BUFFER_OVERFLOW, static_cast<void *>(nullptr),
                     [](const rb::BufferEvent &evt, void *) noexcept { events.push_back(evt); }));

    for (int i = 0; i < tempsize; ++i) {
        rb.push_back(i);  // Fill + overflow
    }

    EXPECT_GE(events.size(), 1);
    // Since Policy is OVERWRITE, callback should throw OVERFLOW event and then DATA_AVAILABLE event
    EXPECT_EQ(events.at(tempsize - 1).type, rb::EventType::BUFFER_OVERFLOW);
    EXPECT_EQ(events.back().type, rb::EventType::DATA_AVAILABLE);
    EXPECT_EQ(rb.get_statistics().overflow_events, 1);
}

TEST(ringbuf_test, CallbackSubscription_MaxCallbacks) {
    constexpr size_t tempsize = 8;
    rb::spsc_ringbuf<int, tempsize, false> rb;

    std::vector<rb::BufferEvent> events;

    EXPECT_TRUE(rb.subscribe(rb::EventType::DATA_AVAILABLE, &events,
                             [](const rb::BufferEvent &, void *) noexcept {}));

    // Check getting over _COUNT
    auto invalid_type = static_cast<rb::EventType>(rb::EventType::_COUNT);
    EXPECT_FALSE(
        rb.subscribe(invalid_type, &events, [](const rb::BufferEvent &, void *) noexcept {}));

    auto out_of_bounds_type = static_cast<rb::EventType>(100);
    EXPECT_FALSE(
        rb.subscribe(out_of_bounds_type, &events, [](const rb::BufferEvent &, void *) noexcept {}));
}

TEST(ringbuf_test, Statistics_TotalPushes) {
    constexpr size_t tempsize = 8;
    rb::spsc_ringbuf<int, tempsize, false> rb;

    EXPECT_EQ(rb.get_statistics().total_pushes, 0);

    rb.push_back(1);
    EXPECT_EQ(rb.get_statistics().total_pushes, 1);

    rb.push_back(2);
    EXPECT_EQ(rb.get_statistics().total_pushes, 2);

    std::array<int, 3> data = {3, 4, 5};
    rb.append(data.data(), 3);
    EXPECT_EQ(rb.get_statistics().total_pushes, 3);  // append is one operation
}

TEST(ringbuf_test, Statistics_TotalPops) {
    constexpr size_t tempsize = 8;
    rb::spsc_ringbuf<int, tempsize, false> rb;

    std::array<int, 3> data = {1, 2, 3};
    rb.append(data.data(), data.size());

    EXPECT_EQ(rb.get_statistics().total_pops, 0);

    int val = rb.pop_front();
    EXPECT_EQ(rb.get_statistics().total_pops, 1);

    rb.pop_front(val);
    EXPECT_EQ(rb.get_statistics().total_pops, 2);

    std::array<int, 2> vals = {};
    rb.read_ready(vals.data(), 2);
    EXPECT_EQ(rb.get_statistics().total_pops, 3);
}

TEST(ringbuf_test, Statistics_OverflowEvents) {
    constexpr size_t tempsize = 4;
    rb::spsc_ringbuf<int, tempsize, false, rb::OverflowPolicy::DROP> rb;

    EXPECT_EQ(rb.get_statistics().overflow_events, 0);

    // Fill buffer
    for (int i = 0; i < tempsize - 1; ++i) {
        rb.push_back(i);
    }
    EXPECT_EQ(rb.get_statistics().overflow_events, 0);

    // Trigger overflow
    rb.push_back(99);
    EXPECT_EQ(rb.get_statistics().overflow_events, 1);

    // Try again
    rb.push_back(100);
    EXPECT_EQ(rb.get_statistics().overflow_events, 2);
}

TEST(ringbuf_test, Statistics_Reset) {
    constexpr size_t tempsize = 8;
    rb::spsc_ringbuf<int, tempsize, false> rb;

    rb.push_back(1);
    int val = rb.pop_front();
    (void)val;

    EXPECT_EQ(rb.get_statistics().total_pushes, 1);
    EXPECT_EQ(rb.get_statistics().total_pops, 1);

    rb.reset_statistics();

    EXPECT_EQ(rb.get_statistics().total_pushes, 0);
    EXPECT_EQ(rb.get_statistics().total_pops, 0);
    EXPECT_EQ(rb.get_statistics().overflow_events, 0);
}
