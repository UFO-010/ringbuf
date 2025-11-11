
#include <cstring>
#include <thread>
#include <gtest/gtest.h>
#include "ringbuf.hpp"

/**
 * Test for thread-safety. Run with ThreadSanitizer - I don't know how to check thread-safety with
 * googletest automatically :(
 */

spcs_ringbuf<char, 512, false> a;

void thread_producer() {
    for (int i = 0; i < 1000; i++) {
        std::array<char, 64> buf = {};
        memset(buf.data(), '0', 64);
        int n = snprintf(buf.data(), 64, "Consumer thread run Hello world %d\n", i);
        a.append(buf.data(), n);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::array<char, 512> out_buf = {};

void thread_consumer() {
    for (int i = 0; i < 1000; i++) {
        size_t s = a.read_ready(out_buf.data(), 512);
        if (s > 0) {
            //            std::cout << out_buf;
            memset(out_buf.data(), '\0', s);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

TEST(ringbuf_thread_test, thread_safe) {
    std::thread thread1 = std::thread(&thread_producer);
    std::thread thread2 = std::thread(&thread_consumer);

    thread1.join();
    thread2.join();
}
