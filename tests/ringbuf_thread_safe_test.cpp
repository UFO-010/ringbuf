
#include <cstring>
#include <thread>
#include <gtest/gtest.h>
#include "ringbuf.hpp"

/**
 * Test for thread-safety. Run with ThreadSanitizer - I don't know how to check thread-safety with
 * googletest automatically :(
 */

void thread_producer(spcs_ringbuf<char, 512, true> &a) {
    for (int i = 0; i < 1000; i++) {
        std::array<char, 64> buf = {};
        memset(buf.data(), '0', 64);
        int n = snprintf(buf.data(), 64, "Consumer thread run Hello world %d\n", i);
        a.append(buf.data(), n);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void thread_consumer(spcs_ringbuf<char, 512, true> &a) {
    std::array<char, 512> out_buf = {};
    for (int i = 0; i < 1000; i++) {
        size_t s = a.read_ready(out_buf.data(), 512);
        if (s > 0) {
            memset(out_buf.data(), '\0', s);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

TEST(ringbuf_thread_test, thread_safe) {
    spcs_ringbuf<char, 512, true> a;
    auto thread1 = std::thread(&thread_producer, std::ref(a));
    auto thread2 = std::thread(&thread_consumer, std::ref(a));

    thread1.join();
    thread2.join();
}
