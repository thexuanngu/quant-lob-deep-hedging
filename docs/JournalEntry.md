## 09/02/2026
- Working with Claude to try and get my Windows environment properly set up. Ran out of my free messages unfortunately, but I think I will feed in my MSc setup instructions to see if Claude can borrow inspiration. It might be worthwhile also consulting the other LLMs to get the job done.

## 09/04/2026
- Actually 'finding' time (01:30) to go through at least one step of the installation.

## 09/07/2026
- Now that everything is set up, I'm going to begin diving into the technicals of the project (starting with the implementation of the `OrderBook` in the `cpp/src/`)
- I'm also going to read to understand what is happening with the `spsc_ring_buffer.hpp` file (because I don't truly understand what is happening there).
- Learning about why `Capacity` is set to a power of 2 and how the `kMask` operates as essentially the modulo function (wrapping around the last elements back to the beginning)
    - Example: Capacity = 8, kMask = 0b0111, and index = 7 (last seat), (7+1) & 7 = 8 & 7 = 0 — correctly wraps to seat 0 instead of walking off the end of the array.
- Learning about `alignas(kCacheLinesSize)` on `head_` and `tail_`
    - CPUs move memory between RAM and cache in 64-byte chunks (*cache lines*)
    - `head_` and `tail_` are just 8-byte `size_t` -> Placed next to each other, they would share one cache line
        - This is problematic because of *false sharing* -> since producers *only* ever writes `tail_` and consumers *only* overy writes `head_`, every write by one thread would force the other to discard and re-fetch its entire cached copy of that line—including the variable it actually cares about (which hadn't changed yet)
        - `alginas(64)` reduces the 10-50x throughput killer on hot lock-free structures by padding each one onto its own cache line, so the two threads' `head_` and `tail_` cache traffic never collides.
        (basically stops unnecessary interference between the two threads]
        )

## 09/08/2026 (MM/DD/YYYY)
- Another day of deciphering the scaffold:
    - `tail_` -> index of the next empty slot the producer will write to
    - `head_` -> index of the next filled slot the consumer will read from
        - `head_ == tail_` -> empty buffer
        -  *Full buffer* -> advancing `tail_` by one would make it equal `head_`
            - A buffer of `Capacity` slots can only hold `Capacity - 1` items (sacrificing a slot to distinguish between full and empty with just two indices)
    - `try_push`: Reads its own index (`tail_`) with `relaxed` ordering (safe because ONLY the PRODUCER ever writes to `tail_`)
        - Then reads the other thread's index (`head_`) with `acquire`, which DOES REQUIRE SYNCHRONIZATION (in contract with `try_pop`'s `release` store on `head_`)
            - Producer guaranteed to see consumer's latest freed slot (not stale cached value), never overwriteing data the consumer is reading.
            - If space, value written into slot THEN publish new `tail_` with `release` (data 1st, index 2nd) prevents consumer seeing 'new item ready' before item itself is actually in memory.
            - Overloads exist enables a temporary/`std::move` value gets into slot
    - `try_pop`: 
    - Why atomics + acquire/release instead of just a `std::mutex`: 
    - `size_approx`: 
    - The deleted copy constructor: 