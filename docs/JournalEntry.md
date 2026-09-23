## 09/02/2026 (MM/DD/YYYY)
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

## 09/08/2026 
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
    - `try_pop`: The mirror of `try_push`; reads `head_` relaxed and checks against `tail_.load(acquire)` for emptiness
        - If not empty, then `std::move`'s the value out of the slot (no copy) then publishes the freed slot (via `head_.store(..., release)`) which the producer's next `try_push` acquire-reads
        - Checks to see if producer has produced things, takes the `tail_`, and clears it, informing the producer when the 'index' is free
    - Why atomics + acquire/release instead of just a `std::mutex`: 
        Allows threads to progress independently (instead of being 'blocked' by thread locks (adding latency and jitter)), with sufficient synchronization to stay correct (yet never putting a thread to sleep)
    - `size_approx`: 
        - Loads both atomics (each with `acquire`) and computes `(tail-head) & kMask`, but not technically a *true* snapshot (since either index can move (hence `_approx`))
        - Glancing view at 'queue_length'
    - The deleted copy constructor: 
        - Copying is forbidden at compile time (because atomics concurrently mutated) -> prduces torn, internally-inconsistent snapshots of the 'queues'
            - Deleting avoids all of these issues  

## 09/09/2026
- Finished fleshing out the 'notes' for my `spsc_ring_buffer.hpp`.
- Recap / Background of what I'm trying to implement:
1. LOB: Makers vs. Takers, Price-Time Priority (FIFO) => State machine (`OrderBook`) maintains ledger, processing incoming events (adding, cancelling, modifying orders) AND generating **trade events when bids & asks cross**
2. Data Pipeline: Producer -> Bridge -> Consumer
    - Producer: Separate thread generating order events (right now synthetic, but soon either Hawkes process or real L2/L3 data)
    - Bridge (`spsc_ring_buffer.hpp`) Generates events and pushes to buffer
    - Consumer: Order Book logic thread (popping events from buffer and updating internal state)
3. Logistical Execution
    1. Created the order book `.hpp` and `.cpp` files.
    2. Contiguous memory: Represent pricers as integer tick offset (`int64_t`) mapped to flat vector to max cache prefetching -> avoid floating point math for LOB levels
        - "tick": minimum price movement allowed for an asset (i.e., $0.01)
        - `double`/`float` avoided because of float imprecision (and non-matching levels, while they should be) => solution is to use `int` (i.e., if BTC's tick size is $5, then $60,005=`12001`)
            - Allows deterministic equality checks AND mapping prices directly to flat, continuous array indices for cache prefetching
    3. Constant-Time Cancellations: intrusive doubly linked list for orders within a level, paired / hashMap linking orderIDs to node pointers for instantaneous cancellations
        - Intrusive: The data holds its own `prev` and `next` ptrs, allowing it to 'intrude' in the list (provides constant O(1) access, with no extra heap allocation (to wrap data in node))
- One more thing: I want to add a 'logger' type system using the 'currying' functions methods I learn in CodeSignal.

## 10/09/2026
- Finally digging into the meat and bones of this order book. I know that I should pre-allocate a pool of Order structs equal to the ring buffer's capacity (Object Pool/ Slab Allocator) to 'starve the heap allocator on a hot path'
- 'hot path' (fast/critical path) code executed most frequently & performance most paramount
    - No heap allocations, No system calls, No locks, cache locality is king

## 22/09/2026
- While on Holiday in China, I managed to complete Milestone 1: Create a functional LOB with orders being added, matched, modified, and cancelled. I definitely learnt some more things about being careful with the types used in the comparison, the intrusive doubly-linked lists, and the need to know essentially create a 'domain' of OrderEvent that acts as the translator between the engine and the actual LOB.

- Goal is to test it with some synthetic data, but I need to create an engine class that will have a run method.

According to Gemini, I need to implemenet the LOBEngineWrapper, and write a new Catch2 test (i.e., `test_engine_concurrency.cpp`) to spawn a producer thread, push 500k synthetic `OrderEvent` structs into the buffer, and asserts that the engine processes them all.

Important to evaluate the translation logic: Event -> Pool -> Book -> Action Routing SINGLE THREADED FIRST (before adding in concurrency)
    