# ringbuf

## Overview

Simple lock-free SPSC FIFO ring buffer. Suitable for both desktop and embedded transfers. Suitable for DMA transfers from and to memory with zero-copy overhead.

## Usage

```cpp
#include "ringbuf.hpp"

int main() {
    spsc_ringbuf<char, 16, false> test;
    std::string_view st("Hello world", sizeof("Hello world"));
    
    test.append(st.data(), st.size());
    
    return 0;
}
```
