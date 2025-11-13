
#include <cstring>
#include <thread>
#include <gtest/gtest.h>
#include <bit>
#include <format>
#include "ringbuf.hpp"

/**
 * Test for thread-safety. Run with ThreadSanitizer (optional). One thread would write numbers in
 * text buffer with delimeters. The other one would read data until writer is running. After all
 * threads is done, we would split data by delimeters and check that numbers in not lost and placed
 * in the rigth order. I put numbers into text just to make test more complicated and avoid using
 * complex data types
 */

// Number of messages to be written
constexpr size_t test_size = 1000;
// Max length of message we would store
constexpr size_t max_len = 16;
// Remember that we need to store text itself
constexpr size_t buf_size = std::bit_ceil(test_size * max_len);

constexpr size_t spsc_size = std::bit_ceil(test_size);

void thread_producer(spsc_ringbuf<char, spsc_size, true> &a, bool *ended) {
    std::array<std::array<char, max_len>, test_size> test = {};
    for (int i = 0; i < test.size(); i++) {
        test.at(i).fill('\0');
        auto result = std::format_to_n(test.at(i).data(), static_cast<long>(test.at(i).size()),
                                       "test {}/", i);
        a.append(test.at(i).data(), result.size);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    *ended = true;
}

void thread_consumer(spsc_ringbuf<char, spsc_size, true> &a,
                     std::array<char, buf_size> &out_buf,
                     const bool *ended) {
    size_t s = 0;
    while (!(*ended)) {
        s += a.read_ready(out_buf.data() + s, max_len);
    }
}

std::vector<std::string> splitStringStream(const std::string &str, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    while (std::getline(iss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

TEST(ringbuf_thread_test, thread_safe) {
    spsc_ringbuf<char, spsc_size, true> a;
    std::array<char, buf_size> out_buf = {};
    bool ended = false;
    out_buf.fill('\0');
    {
        auto thread1 = std::thread(&thread_producer, std::ref(a), &ended);
        auto thread2 = std::thread(&thread_consumer, std::ref(a), std::ref(out_buf), &ended);
        thread1.join();
        thread2.join();
    }

    std::string temp_str;
    temp_str.append(out_buf.data());

    std::vector<std::string> test = splitStringStream(temp_str, '/');
    EXPECT_EQ(test.size(), test_size);
    for (size_t i = 0; i < test.size(); i++) {
        std::vector<std::string> internal_test = splitStringStream(test.at(i), ' ');
        EXPECT_EQ(internal_test.at(0), std::string("test"));
        size_t value = std::stoi(internal_test.at(1));
        EXPECT_EQ(i, value);
    }
}
