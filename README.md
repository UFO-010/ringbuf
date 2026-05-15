# ringbuf

A minimal, high-performance, lock-free Single Producer Single Consumer (SPSC) ring buffer implementation in modern C++17. Designed for embedded systems, real-time applications, and high-frequency data transfer scenarios.

## Overview

This library provides a zero-copy, cache-aware circular buffer that enables efficient data transfer between a single producer thread and a single consumer thread without locks or busy-waiting.

## Architecture

Core Design Principles

```
┌─────────────────────────────────────────────┐
│     SPSC Ring Buffer (MaxSize elements)     │
├─────────────────────────────────────────────┤
│     ... [max_size-1]                        │
│   ↑                ↑                        │
│ head            tail                        │
│ (Consumer)      (Producer)                  │
└─────────────────────────────────────────────┘
  Cache-line aligned pointers (64 bytes)
```

Memory Model:

- Producer writes to tail position and increments tail
- Consumer reads from head position and increments head
- No access to each other's pointer = minimal synchronization
- Atomic operations use memory_order_acquire/release semantics
- Local caching of pointer values reduces atomic loads
    
### Producer/Consumer Handles

Instead of a monolithic class with mixed APIs, ringbuf provides typed handles:

```cpp
auto producer = buffer.get_producer();  // Write-only interface
auto consumer = buffer.get_consumer();  // Read-only interface

producer.push_back(data);
producer.append(array, size);
consumer.pop_front();
consumer.read_ready(dest, size);
```

Benefits:

- Prevents accidental API misuse at compile time
- Explicit intent: code clarity
- Better code organization
- Easier to reason about thread safety


## Usage

### Basic Usage

```cpp
#include "ringbuf.hpp"

int main() {
    rb::spsc_ringbuf<char, 16, false> test;
    std::string_view st("Hello world", sizeof("Hello world"));
    
    test.append(st.data(), st.size());
    
    return 0;
}
```

### Zero-Copy Operations

For maximum performance with large data:

```cpp
// Get direct access to buffer memory
auto segments = producer.get_write_segments();

// Write to first segment
for (size_t i = 0; i < segments.first.size(); ++i) {
    segments.first.data()[i] = compute_data(i);
}

// Write to second segment (if wrap-around)
for (size_t i = 0; i < segments.second.size(); ++i) {
    segments.second.data()[i] = compute_data(segments.first.size() + i);
}

// Commit the write
size_t written = segments.total_size();
producer.advance_write_pointer(written);
```

### Overflow Handling

Choose appropriate overflow policy for your use case:

```cpp
// DROP (default): Reject writes when full
ringbuf::spsc_ringbuf<Data, 1024, true, rb::OverflowPolicy::DROP> buf1;

// OVERWRITE: Overwrite oldest data
ringbuf::spsc_ringbuf<Data, 1024, true, rb::OverflowPolicy::OVERWRITE> buf2;

// FAIL: Return error indication
ringbuf::spsc_ringbuf<Data, 1024, true, rb::OverflowPolicy::FAIL> buf3;

// TOEND: Writes as much data as possible to the end of buffer
ringbuf::spsc_ringbuf<Data, 1024, true, rb::OverflowPolicy::TOEND> buf3;
```

| Policy | Behavior | Use Case |
|--------|----------|----------|
| **DROP** | Discard new data if full | Non-critical telemetry, best-effort delivery |
| **OVERWRITE** | Discard oldest data | Logging, rolling buffers, sensor data |
| **FAIL** | Return 0 elements written | Critical systems requiring explicit handling |
| **TOEND** | Discard new data that does not fit | Sensor data streams, Logging bursts |

### Event Callbacks

When monitoring buffer state changes, you can set up a single callback function to handle all events, or use a different callback function for each event. All callbacks should be marked as noexcept.

```cpp
class myclass {
public:
    void handle(const rb::BufferEvent &evt) noexcept {
        switch (evt.type) {
            case rb::EventType::DATA_AVAILABLE:
                printf("Data available: %zu elements\n", evt.current_size);
                break;
            default:
                break;
        }
    }
};

void handler(const rb::BufferEvent &evt, void *ctx) noexcept {
    myclass *c = static_cast<myclass *>(ctx);
    c->handle(evt);
}

int main(){
    rb::spsc_ringbuf<int, 8, false> rb;
    myclass myclass;

    // 1. Handle available data
    rb.subscribe(rb::EventType::DATA_AVAILABLE, &myclass, &handler);

    // 2. Handle buffer exhaustion
    rb.subscribe(rb::EventType::BUFFER_FULL, &myclass, &handler);

    // 3. Handle empty buffer
    rb.subscribe(rb::EventType::BUFFER_EMPTY, &myclass, &handler);

    // 4. Handle data drops
    rb.subscribe(rb::EventType::BUFFER_OVERFLOW, &myclass, &handler);

    // 5. Handle buffer reset
    rb.subscribe(rb::EventType::RESET, &myclass, &handler);

    rb.push_back(1);
}
```

### Statistics

Track buffer performance:

```cpp
auto stats = buffer.get_statistics();
printf("Total pushes: %zu\n", stats.total_pushes);
printf("Total pops: %zu\n", stats.total_pops);
printf("Overflow events: %zu\n", stats.overflow_events);
printf("Peak occupancy: %zu\n", stats.max_occupancy);
printf("Total bytes written: %lu\n", stats.total_bytes_written);
printf("Total bytes read: %lu\n", stats.total_bytes_read);
```

## Template Parameters

```cpp
template <typename T, size_t MaxSize, bool ThreadSafe, OverflowPolicy Policy>
class spsc_ringbuf;
```

| Parameter | Description | Constraints |
|-----------|-------------|-------------|
| **T** | Element type | Any copyable/moveable type |
| **MaxSize** | Buffer capacity in elements | Must be power of 2 (2, 4, 8, 16, ..., 65536) |
| **ThreadSafe** | Enable atomic operations | true: uses std::atomic, false: uses plain size_t |
| **Policy** | Overflow handling | DROP (default), OVERWRITE, FAIL, TOEND |
