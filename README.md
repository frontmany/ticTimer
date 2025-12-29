![ticTimer](ticTimer.png)

# ticTimer - C++ Timer Library

Thread safe C++ timer library with single-shot and repeating timers. Features expire time based callback execution order, don't blocking callbacks, and automatic timers management. Supports unlimited amount of timers with precise timing using condition variables.

## Quick Start

### Example 1: SingleShotTimer

```cpp
#include "ticTimer.h"
#include <iostream>

int main() {
    tic::SingleShotTimer timer;
    
    timer.start(std::chrono::seconds(2), []() {
        std::cout << "Timer fired after 2 seconds!" << std::endl;
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return 0;
}
```

### Example 2: RapidTimer

```cpp
#include "ticTimer.h"
#include <iostream>

int main() {
    int tickCount = 0;
    tic::RapidTimer timer;
    
    timer.start(std::chrono::milliseconds(500), [&tickCount]() {
        tickCount++;
        std::cout << "Tick #" << tickCount << std::endl;
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(3));
    timer.stop();
    return 0;
}
```

### Example 3: Timer (Universal)

```cpp
#include "ticTimer.h"
#include <iostream>

int main() {
    tic::Timer timer(tic::Mode::Rapid);
    
    timer.start(std::chrono::milliseconds(300), []() {
        std::cout << "Rapid timer tick!" << std::endl;
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    timer.stop();
    return 0;
}
```
---
## Documentation
---

#### SingleShotTimer

One-time timer that fires once after specified delay.

**Methods:**
- `void start(delay, callback)` - Start timer with delay and callback
- `void stop()` - Stop/cancel the timer
- `bool isActive() const` - Check if timer is active

---

#### RapidTimer

Repeating timer that fires at regular intervals.

**Methods:**
- `void start(interval, callback, repeatCount?)` - Start repeating timer
  - `interval` - Time between ticks
  - `callback` - Function to call on each tick
  - `repeatCount` (optional) - Number of repetitions (0 = infinite)
- `void stop()` - Stop the timer
- `bool isActive() const` - Check if timer is active

---

#### Timer

Universal timer supporting both single-shot and rapid modes.

**Methods:**
- `Timer(Mode mode)` - Constructor with mode (SingleShot or Rapid)
- `void setMode(Mode mode)` - Change mode (stops current timer)
- `void start(duration, callback, repeatCount?)` - Start timer
- `void stop()` - Stop the timer
- `bool isActive() const` - Check if timer is active

---

#### 1. Timer Cancellation

You can stop a timer before it fires:

```cpp
tic::SingleShotTimer timer;
timer.start(std::chrono::seconds(2), []() {
    std::cout << "This won't be printed" << std::endl;
});
std::this_thread::sleep_for(std::chrono::milliseconds(500));
timer.stop(); // Cancel before firing
```

---

#### 2. Restart Timer

Restarting a timer automatically stops the previous one:

```cpp
tic::SingleShotTimer timer;
timer.start(std::chrono::milliseconds(500), callback1);
std::this_thread::sleep_for(std::chrono::milliseconds(200));
timer.start(std::chrono::milliseconds(500), callback2); // Restarts with new callback
```

---

#### 3. Multiple Timers

Run multiple timers simultaneously:

```cpp
tic::SingleShotTimer timer1;
tic::RapidTimer timer2;

timer1.start(std::chrono::milliseconds(500), []() {
    std::cout << "Timer 1 fired!" << std::endl;
});

timer2.start(std::chrono::milliseconds(200), []() {
    std::cout << "Timer 2 tick!" << std::endl;
});
```

---

#### 4. Timer Mode Switching

Change timer mode dynamically:

```cpp
tic::Timer timer(tic::Mode::SingleShot);
timer.start(std::chrono::milliseconds(200), callback1);

timer.setMode(tic::Mode::Rapid); // Automatically stops current timer
timer.start(std::chrono::milliseconds(300), callback2);
```

---

#### 5. Stopping Timer from Callback

Safely stop a timer from its own callback:

```cpp
int tickCount = 0;
tic::RapidTimer timer;

timer.start(std::chrono::milliseconds(200), [&timer, &tickCount]() {
    tickCount++;
    std::cout << "Tick #" << tickCount << std::endl;
    if (tickCount >= 5) {
        timer.stop(); // Stop from callback
    }
});
```

---

#### 6. Starting Timer from Callback

Start new timers from callback:

```cpp
tic::SingleShotTimer timer1;
tic::SingleShotTimer timer2;

timer1.start(std::chrono::milliseconds(200), [&timer2]() {
    std::cout << "Timer 1 fired!" << std::endl;
    timer2.start(std::chrono::milliseconds(300), []() {
        std::cout << "Timer 2 fired!" << std::endl;
    });
});
```

---

#### 7. Self-Restarting Timer

Create repeating behavior with SingleShotTimer:

```cpp
int count = 0;
tic::SingleShotTimer timer;
std::function<void()> callback;

callback = [&timer, &count, &callback]() {
    count++;
    std::cout << "Tick #" << count << std::endl;
    if (count < 5) {
        timer.start(std::chrono::milliseconds(200), callback);
    }
};

timer.start(std::chrono::milliseconds(200), callback);
```

---

#### 8. Move Semantics

Timers support move semantics:

```cpp
tic::SingleShotTimer timer1;
timer1.start(std::chrono::milliseconds(100), callback);

tic::SingleShotTimer timer2 = std::move(timer1);
// timer1 is now inactive, timer2 is active
```

---

### Thread Safety

- All timer operations are thread-safe
- Callbacks execute in a separate thread sequentially
- Multiple timers can be created and managed from different threads
- No manual synchronization required

### Performance

- Uses priority queue for efficient scheduling
- No busy waiting - threads sleep until needed
- Callbacks execute in separate thread pool
- Minimal overhead for timer management

### Requirements

- C++11 or later
- Standard library with `<chrono>`, `<thread>`, `<mutex>`, `<condition_variable>`, `<queue>`, `<functional>`

### Notes

- TimerDispatcher is automatically initialized on first use
- Timers are automatically stopped in destructor
- Copying timers is disabled (use move semantics)
