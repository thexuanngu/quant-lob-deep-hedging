# LOB Engine: Milestone 2 Handover

*Snapshot 12 Oct 2026. Written for the project owner and for any LLM resuming this work in a fresh conversation.*

## 0. How to use this document

Paste this into a new conversation together with `README.md`, `NAVIGATING_THE_SCAFFOLD.md` and `MILESTONE_ROADMAP.md`. It records what was reviewed, what was broken, what was fixed, what is still open, and the working agreement with the project owner. Sections 3 to 6 answer: (a) the problems, (b) the solutions, (c) the current state, (d) the next steps. Items are numbered (P1, S1, ...) so they can be cross-referenced.

## 1. Project and working agreement

**Project.** A discrete-event limit order book (LOB) simulator in C++20, bridged to Python with pybind11, feeding (i) an Avellaneda-Stoikov market-making policy and (ii) a deep-hedging network (Buehler et al. 2019) trained on Expected Shortfall. Target: a portfolio artifact for a Tier-1 US quant/ML role in about six months. **Novel contribution:** sensitivity of hedging PnL to mis-specification of the LSV mixing weight lambda.

**Role the LLM plays: supervisor.**

1. Keep steering to the core artifact: C++ LOB + pybind11 + PyTorch deep hedging + the lambda-sensitivity result.
2. Prevent yak-shaving: generic data-science libraries and basic backtesters belong in a separate repo.
3. Enforce production standards: unit tests, sanitizers (asan, ubsan, tsan), Docker, CI, purged walk-forward CV later.
4. Call out unrealistic assumptions (latency, fees, structural breaks, queue priority).
5. Convention: the decision layer (market making, hedging) and the design of new components are given as hints and mechanisms, not finished code. Bug fixes and plumbing may be small snippets. Ask before writing any decision-layer code.
6. Verify before asserting. Every claim in this review was checked by building the uploaded files (g++ 13, Catch2 3.4, Google Benchmark 1.8.3, ASan/UBSan/TSan in a Linux sandbox) and running probes. Keep doing this, and say plainly when something was not run.
7. Do not accept another LLM's claims uncritically (see P17 and S17).

**Environment.** MacBook Pro, Apple Silicon (arm64), Apple clang, Homebrew, VS Code, Catch2 v3.16. A Windows 11 PC is planned to use WSL2 + Ubuntu 24.04 (not yet set up; native MSVC is a parked fallback). CMake presets in `cpp/`: `debug`, `release`, `asan`, `tsan`. On macOS build and test with:

```
cmake --build --preset debug --target unit_tests -j$(sysctl -n hw.ncpu)
ctest --preset debug --output-on-failure
```

`nproc` does not exist on macOS. Building only `unit_tests` stops a failing benchmark target from blocking the test link (alternatively add `-- -k 0`).

## 2. Architecture in intuitive terms

```
PRODUCER THREAD      InputBuffer (SPSC)      ENGINE THREAD                   OutputBuffer (SPSC)     CONSUMER (M5)
LOBFeed (stub)  --->  OrderEvent  --------->  runEngine: pop -> process()  --->  ExecutionEvent  --->  Strategy
                                              OrderBook + OrderPool
```

Think of the book as a ledger kept by one clerk (the engine thread). Producers hand the clerk slips (OrderEvents) through a one-way conveyor, and the clerk puts fills on a second conveyor. Only the clerk touches the ledger, so the ledger needs no locks. This is **thread confinement**. The only shared objects are the two queues, which is why the acquire/release reasoning in the SPSC buffer is sufficient. Consequences:

- Book getters are only safe after `stop()` (the join gives a happens-before edge), never while the engine runs.
- A second producer on one SPSC queue is undefined behaviour.
- The engine object is pinned (neither copyable nor movable) because its running thread captured `this`.

The `OrderPool` is a fixed set of pre-allocated `Order` nodes with a free list. Each price level is an intrusive doubly linked FIFO list of those nodes, and `order_map_` (id to `Order*`) gives O(1) cancel. Prices are integer ticks. Level index = price minus `tick_price_offset`, so the valid window is `[offset, offset + num_levels)`.

**File map** (`cpp/include/lob` and `cpp/src`)

| File | Role |
| --- | --- |
| `types.hpp` | Side, Action, Order (intrusive node), PriceLevel, OrderEvent {ts\_ns, order\_id, price, qty, side, action}, ExecutionEvent {ts\_ns, maker\_id, taker\_id, price, qty, aggressor}. Plain aggregates, no constructors. |
| `queues.hpp` | `InputBuffer` = SPSC of OrderEvent (4096), `OutputBuffer` = SPSC of ExecutionEvent (4096) |
| `spsc_ring_buffer.hpp` | Lock-free SPSC ring buffer (acquire/release, alignas(64) head and tail) |
| `order_pool.hpp/.cpp` | Fixed-capacity pool. `allocate()` returns nullptr when exhausted and counts it (`exhausted()`). Copy deleted. |
| `order_book.hpp/.cpp` | Levels, matching, cancel, modify, best bid/ask, counters, `set_time`. Copy deleted. |
| `engine.hpp/.cpp` | `LOBEngine`: pop, `process()`, book. Drains on stop, `processed()` counter, yield-based idle. |
| `lob_feed.hpp/.cpp` | Still an unimplemented stub (by-value vector constructor, unnamed parameter) |
| `bindings/py_lob.cpp` | pybind11, only `RingBufferU64` so far |
| `tests/`, `benchmarks/` | Catch2 (book, SPSC) and Google Benchmark (add resting, matching) |

**Semantics fixed so far**

- `ExecutionEvent.price` is the maker's resting price. `ExecutionEvent.aggressor` is the taker's side.
- Fills are stamped with the simulated time of the event that caused them (`OrderBook::set_time`, called by `LOBEngine::process`). Time is simulated nanoseconds, never wall clock, so runs stay reproducible.
- Best bid/ask update eagerly when a level empties.
- A modify is cancel + re-add at the back of the queue (priority is lost). Zero quantity means cancel.
- Order ids must be unique and non-zero (0 is the pool's free marker).

## 3. (a) Problems encountered, and why they mattered

- **P1. Circular include** (`order_book.hpp` and `engine.hpp` included each other). The project did not compile in either include order.
- **P2. `ExecutionEvent` had a constructor and no default constructor.** `SpscRingBuffer` does `T buffer_[N]{}`, so it could not instantiate.
- **P3. Ask-side fills were labelled `Side::Bid`** (copy-paste between two near-identical 45-line branches). Downstream inventory and PnL would have been inverted.
- **P4. `cancel_order` used the raw price as a vector index.** Undefined behaviour whenever the offset is non-zero, and silent at offset 0.
- **P5. Inconsistent empty-side sentinels.** Bid 0 is a valid index, and `INT64_MAX + offset` overflows a signed integer, so `best_ask()` returned garbage (UBSan). Later it compared against the wrong sentinel (`kNoBid`).
- **P6. The ask-side best-price scan ran one past the end** of the vector when the last ask was removed (ASan heap-buffer-overflow).
- **P7. Engine lifecycle.** No destructor (a joinable `std::thread` destroyed means `std::terminate`), `stop()` discarded queued events (flaky tests), and no way to tell when N events had been consumed.
- **P8. Silent drops.** The fills queue was never drained, so it saturates and drops. Pool exhaustion and out-of-window prices also vanished without a trace, so the simulator would measure a different market than intended.
- **P9. `modify_order` bugs.** It could lose the order when the pool was exhausted. It read `side` after the node went back to the pool, and `deallocate` resets side to Bid, so asks silently became bids (no sanitizer can see this). An out-of-window modify destroyed the original.
- **P10. Every trade emitted two fills** (a second `try_push` left after merging a snippet). The queue saturated at 2048 trades, and no test looked at fills.
- **P11. Stale tests.** Assertions encoded the old lazy best-bid behaviour. The constructor change broke tests and benchmark, and the fixture passed a pointer where a reference was required.
- **P12. Build workflow.** ninja stops at the first failing target, so a benchmark error meant tests never linked. `nproc` is not on macOS.
- **P13. `processed()` returned `std::atomic` by value** (not copyable). `ts_ns` never reached the book, so every fill had time 0.
- **P14. Copy semantics.** Copying `OrderPool` or `OrderBook` copies raw pointers (shallow), giving use-after-free and two writers on one structure. The copy deletes on `OrderBook` were commented out at one point.
- **P15. Bugs in the owner's latest tests.**
  - `Exhasuted Pool Modification` loops 257 times on a 256-slot pool and passes the resulting nullptr to `add_order` (SIGSEGV). It also starts ids at 0 and asserts on a Bid level although the orders are asks.
  - The tests labelled offset = 0 and offset = 1000 call `BookFixture(0);` and `BookFixture(100);` inside the body. That constructs and immediately destroys a temporary, so they still run at the default offset 1 and test nothing about offsets.
  - Counter-zero checks were being added by hand, test by test.
- **P16. Design gaps (not yet bugs).**
  - No time model.
  - Concurrency versus reproducibility: the M9 sweep needs bit-reproducible seeded runs.
  - The roadmap suggested a strategy as a second producer on the same SPSC queue (undefined behaviour).
  - A queue capacity of 4096 is small for Hawkes bursts.
  - The roadmap suggested pushing events one at a time from Python (per-event pybind overhead).
  - Realism gaps: no IOC/market order type, the price window silently rejects when the mid drifts, modify forfeits priority, no latency model, no fees.
- **P17. Overstated or wrong claims in Gemini's review** (corrected in S17).
- **P18. Concept gaps the owner raised.** What `processed_` is for, what a contract comment means, what RMW is, what `= delete` does, why moving an engine is dangerous, optional overhead, how to use the fixture, how to change offsets in tests.

## 4. (b) Solutions, and how they addressed the problems

- **S1 (P1).** `types.hpp` is dependency-free and `queues.hpp` holds the aliases. Include arrows point one way: types, then queues and order\_pool, then order\_book, then engine, then feed. Verified: the library compiles warning-free under `-Wall -Wextra -Wpedantic`.
- **S2 (P2).** `ExecutionEvent` and `OrderEvent` are plain aggregates with `static_assert(is_trivially_copyable)`. Fills are built with designated initialisers.
- **S3 (P3).** A fill carries maker\_id, taker\_id, price (maker's), qty, aggressor (taker's side) and ts\_ns, documented in `types.hpp`. Tests cover bid and ask aggressors.
- **S4 (P4).** `cancel_order` indexes with `price - tick_price_offset_`. Tests run at offsets 0, 1 and 1000 (S15).
- **S5 (P5, P6).** Sentinels `kNoBid = -1` and `kNoAsk = INT64_MAX` (`inline constexpr`). Internal indices stay raw `int64_t` on the hot path, and public `best_bid()` / `best_ask()` return `std::optional<int64_t>`. That is 16 bytes returned in registers and compiles to cmp/je/add when inlined, so it costs essentially nothing. It is also the market maker's main input, so correctness beats micro-optimisation. The bid walk-back loops down to -1, and the ask scan checks `idx < n`.
- **S7 (P7).** The destructor calls `stop()`. `runEngine` drains the queue after `running_` goes false (contract: the producer is already joined). `process()` is extracted so threaded and synchronous drivers share it. `processed_` is a single-writer atomic (plain load + release store, since only the engine thread writes), and `processed()` does an acquire load. Tests wait for `processed() == N`, then `stop()`, then read the book.
- **S8 (P8).** Counters `OrderBook::dropped_fills()`, `OrderBook::out_of_window()` and `OrderPool::exhausted()`. The test fixture checks all three in its destructor with `CHECK`, and tests that provoke them on purpose set `expect_clean = false`. Verified: it reports a stray counter even while a `REQUIRE` is failing, with no terminate.
- **S9 (P9).** `modify_order` order of operations: find, then zero qty means cancel, then window check (reject and count; the original survives), then read side before cancel, then allocate (nullptr means return and the original survives), then cancel, then add.
- **S10 (P10).** Duplicate `try_push` removed. Test: one trade emits exactly one fill.
- **S11 (P11).** Eager best is the intended semantics, so assertions were updated (best bid 150 to 100 after cancelling the top level, 140 to 130, best ask 110 to 120). `BookFixture {pool, fills, book}` is built in declaration order, and `TEST_CASE_METHOD` handles the default offset.
- **S12 (P12).** Use `--target unit_tests` and `sysctl -n hw.ncpu`.
- **S13 (P13).** `std::uint64_t processed() const noexcept` is defined inline. `book_.set_time(ev.ts_ns)` is the first line of `process()`, with an assert that time never goes backwards. The book keeps a clock rather than threading a timestamp through every call, because `cancel_order` is called internally from matching. Never call a wall clock inside the book.
- **S14 (P14).** Copy and assignment are deleted on `OrderPool`, `OrderBook` and `LOBEngine`. Demo: copying a pool, destroying the original and allocating from the copy gave an ASan heap-use-after-free. The delete turns silent runtime corruption into a compile error. `LOBEngine` is pinned for a different reason: its thread holds `this`.
- **S15 (P15).** Corrected tests, all verified (5 cases, 324 assertions, clean under ASan+UBSan):
  - Exhausted pool: allocate exactly 256, `REQUIRE` non-null before `add_order`, ids from 1, assert `exhausted() == 0` before the modify and `== 1` after, 256 still resting, the Ask level head is id 1 with its original qty.
  - Offsets: a plain `TEST_CASE` with `const int64_t off = GENERATE(0, 1, 1000); BookFixture f(off);` and prices written as `off + p`, because the window is `[off, off + 1000)`. `TEST_CASE_METHOD` cannot take constructor arguments.
  - Window edges: `off + 0` and `off + 999` are valid, `off - 1` and `off + 1000` are rejected, so `out_of_window() == 2`.
  - Fixture destructor counter check (S8).
- **S16 (P16).** Design decisions:
  - SPSC per link. Two drivers over one `OrderBook` core: a threaded one as the HPC showcase and a deterministic single-threaded one for research runs.
  - A strategy as a second producer gets its own SPSC queue, merged by `ts_ns` in the engine.
  - Simulator backpressure is spin-retry and never drop (a live feed would drop or disconnect). Queue types live in one alias.
  - The Python bridge works in batches: `engine.submit(np.ndarray)` over a structured dtype matching `OrderEvent` via `PYBIND11_NUMPY_DTYPE`, releasing the GIL, plus `engine.drain_fills()` returning a structured array once per decision step.
  - Use C++20 concepts and templates for `EventSource` and the idle policy, and virtual dispatch only at the Strategy boundary with a pybind11 trampoline. Skip functors and monads here.
- **S17 (P17), corrections to Gemini.**
  - The cancel index bug is UB (it works silently at offset 0), not an instant segfault.
  - Only the ask branch mislabelled the side.
  - `yield()` is a cheap syscall that returns immediately if nothing else is runnable. `pause` is a pipeline and power hint with no memory-ordering effect (acquire/release provides that), and a pure spin can starve the producer on a few-core box.
  - `_mm_pause` and `immintrin.h` are x86-only. On Apple Silicon use a portable `cpu_relax()` (x86 `pause`, arm64 `yield`) and a hybrid backoff (about 64 spins, then `yield`, reset on success).
  - `LOBFeed` should not hold the ring buffer. Keep a pure seeded `next() -> std::optional<OrderEvent>` and let a driver loop push, so the same generator runs single-threaded and threaded.
  - Deep hedging (M7) trains on simulated LSV paths, not LOB fills. Fills feed M5.
- **S18 (P18), concept answers given.**
  - A contract is a caller precondition the compiler cannot check (join the producer before `stop()`).
  - RMW means read-modify-write (`lock xadd`). A single-writer counter can use load + store.
  - Atomics are not copyable.
  - Moving an engine leaves its thread pointing at the old address.
  - A raw-pointer member makes the default copy shallow.

## 5. (c) Current state of affairs

**Verified (Linux sandbox, g++ 13, owner's latest files)**

- The library compiles with zero warnings under `-Wall -Wextra -Wpedantic`.
- The owner's last ctest run on the Mac: 17 of 18 pass. Only `Exhasuted Pool Modification` fails (SIGSEGV), and that is a test bug (P15), not a book bug.
- The replacement tests (S15) pass under ASan+UBSan.
- The benchmark file compiles and runs (constructor fixed, fills drained in the matching benchmark). Sandbox timings (about 300 ns add, about 360 ns match) are x86 Linux and are not comparable with the Mac.
- **Not yet done:** the owner has not run the `asan` and `tsan` presets locally. The TSan run of the engine thread in the sandbox was only a smoke test (200k events, producer thread, `processed()` accessor), not the replay-equality test.

**Intuitive summary.** The book is correct on every path exercised so far: add, cancel, modify, multi-level sweeps, empty and non-empty sides, offsets 0/1/1000, window edges, fill contents, and pool exhaustion. The plumbing around it (engine thread, queues, counters, simulated time) is in place and instrumented, so failures are visible instead of silent. What is missing is what turns the engine into a simulator: an event source (the feed), a global invariant checker, and a determinism/concurrency test proving the threaded path equals a single-threaded replay.

**Known remaining nits**

- `order_map_` is `std::unordered_map` (a heap allocation on insert). Benchmark before replacing it with a flat structure.
- Test helper getters are non-const with unchecked bounds, and `get_order_count_at_level` actually returns ids.
- `BM_OrderBookAddResting` calls `PauseTiming`/`ResumeTiming` every iteration, which is the overhead its own comment warns about.
- Calling `start()` twice would terminate.
- Duplicate order ids silently overwrite `order_map_`. The feed must guarantee unique non-zero ids, and a debug assert would help.
- Remove dead commented-out tests, and confirm `types.hpp` does not include `spsc_ring_buffer.hpp`.
- Windows WSL2 setup is not done, and CI/Docker were not re-verified after the refactor.

## 6. (d) Next steps

### Finish Milestone 2

**A. Tests and tooling (do first)**

- [ ] Replace the two fake offset tests with the `GENERATE(0, 1, 1000)` version. Replace the exhausted-pool test. Add the window-edge test and the two fill-content tests (bid aggressor trades at the maker's price with the right `ts_ns`; ask aggressor sweeps two levels in price-time order with quantity conserved). Add the fixture-destructor counter check.
- [ ] Delete dead commented-out tests.
- [ ] Run the `asan` and `tsan` presets locally and keep the output.
- [ ] Add `check_invariants()` (debug builds) and call it at the end of every test:
  - best bid < best ask
  - each level's `total_qty` equals the sum of its resting orders' qty
  - prev/next links are consistent and head/tail are correct
  - `order_map_` size equals the orders reachable from the levels
  - pool free count + resting orders = capacity (needs `OrderPool::free_count()`)

**B. Single-threaded synthetic feed**

- [ ] `SyntheticFeed` takes a seed and a small params struct. It exposes a pure `next() -> std::optional<OrderEvent>` with no queue and no thread.
- [ ] Zero-intelligence flow (Cont, Stoikov, Talreja 2010): prices as geometric tick offsets around a reference mid with a small marketable fraction; a weighted Add/Cancel/Modify mix via `std::discrete_distribution`; a live-id vector with swap-and-pop for O(1) random cancel; cancels of already-filled ids are fine (they model cancel-after-fill races); ids unique and starting at 1.
- [ ] A driver loop calling `engine.process()` directly.
- [ ] Determinism test: the same seed gives the same final book hash.

**C. Threaded**

- [ ] Producer thread + real queue, with the backpressure policy decided and commented (simulator: spin-retry, never drop).
- [ ] `test_engine_concurrency.cpp`: 500k events, join the producer, wait for `processed() == N`, `stop()`, invariants hold, **final book equals the single-threaded replay**, clean under `tsan`.
- [ ] End-to-end events/sec benchmark, logged in the README diary.
- [ ] Optional: hybrid backoff with a portable `cpu_relax()`.

**D. Understanding (explain without notes)**

- [ ] Why the book needs no atomics (thread confinement) and why getters are unsafe mid-run.
- [ ] Why a second producer breaks SPSC.
- [ ] What happens when the queue is full, and which policy fits the simulator versus a live feed.
- [ ] Why `processed_` uses load + store, and what a second writer would break.
- [ ] Why copies of the pool and book are deleted, and why the engine is pinned.

### Milestone 3 (Hawkes, Python), once M2 is green

- [ ] Univariate Hawkes with an exponential kernel, simulated by Ogata thinning, with an O(1) intensity update via a running state variable.
- [ ] Stationarity check: branching ratio alpha/beta < 1.
- [ ] Known-signal test: MLE on your own simulation recovers mu, alpha and beta, and the Fano factor of binned counts exceeds 1.
- [ ] Bridge: `engine.submit(np.ndarray)` over a structured dtype, not per-event ring-buffer pushes. Convert float seconds to integer nanoseconds at the boundary.
- [ ] Marks (buy/sell, size) with timestamps carried through.
- [ ] The so-what experiment: Poisson versus Hawkes at the same mean rate, comparing spread widening, depth depletion and peak queue occupancy.
- [ ] Optional: make market-order intensity decay as exp(-kappa \* delta) so kappa is a known ground truth for the M5 Avellaneda-Stoikov check.

* Keep M3 Python on the Hawkes simulator and the numpy bridge. The generic data-science library stays in a separate repo.

## 7. Open decisions

- **Decision-layer scaffolding** (unanswered): (a) interface stubs only, (b) a pure math/design reference doc, or (c) nothing yet. Ask before writing any market-making or hedging code.
- **Windows machine:** WSL2 + Ubuntu 24.04 setup is still pending.
- **Timing of templates:** whether `SyntheticFeed` ships as a concept-based `EventSource` now or after the first working version. Recommendation: after.

## 8. Seeds for the 'What would break this strategy?' section

- The price window silently rejects orders when the mid drifts under Hawkes volatility.
- Modify forfeits queue priority, while real exchanges keep it on size reductions.
- There is no latency model and no fees yet (add them as an event delay and a field on `ExecutionEvent`).
- There is no IOC/market order type, which the hedger will need.
- Zero-intelligence flow is not calibrated to real data.
- Deribit data will bring out-of-order messages, sequence gaps and snapshot + incremental reconciliation.
