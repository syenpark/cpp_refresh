# Atomic Lock-Free (Spinning) VS Mutex (Blocking)

```bash

# Atomic                     # Mutex

Thread A (locked):          Thread A (locked):
+-------------------+       +-------------------+
| Execute section   |       | Lock mutex        |
+-------------------+       | Execute section   |
                            +-------------------+
Thread B (spinning):        Thread B (blocked):
+-------------------+       +-------------------+
| Check condition   |       | Waiting for lock  |
| Uses CPU          |       | Does not use CPU  |
+-------------------+       +-------------------+
```

**Spinning** means a thread keeps checking the condition in a loop, consuming CPU cycles until the condition is met.

**Blocking** means a thread is put to sleep while waiting. It does not use CPU cycles while waiting.

Active spinning is non-blocking but can waste CPU. Blocking saves CPU but adds latency due to context switching.

| **Aspect**             | **Mutex (Blocking)**                                        | **Lock-Free Spinning (Atomic)**                                 |
| ---------------------- | ----------------------------------------------------------- | --------------------------------------------------------------- |
| **Thread Waiting**     | Thread is **blocked** and saved by the OS                   | Thread **actively spins** while using CPU                       |
| **Context Switches**   | **Multiple context switches** (Thread B waits for Thread A) | **No context switching**, active polling                        |
| **CPU Resource Usage** | **Thread B is put to sleep**, no CPU usage                  | **Thread B actively uses CPU** to spin                          |
| **Thread Resumption**  | OS scheduler has to wake up the blocked thread              | **Thread B spins without blocking**                             |
| **Efficiency**         | **Context switching** is inefficient and adds overhead      | **Spin-waiting** can be efficient if conditions are met quickly |
| **Use Case**           | Good for protecting larger critical sections                | Good for fine-grained, low-latency tasks like counters or flags |
