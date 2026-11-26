# Intelligent Memory Allocation & Fragmentation Simulator

A modular C11 Data Structures Lab project based on the supplied combined and student guides. It models memory allocation inside a controlled pool; it does **not** manage physical RAM.

## Included functionality

- Real-file measurement with a real file/directory check; size rounded up to KB.
- Seeded file-type and program-profile workload generators with light, balanced and heavy distributions.
- Process table (array), FIFO waiting queue, simulated clock and process lifecycle states.
- Address-ordered linked list with half-open ranges `[start, end)`, splitting, freeing, coalescing and compaction.
- First Fit, Best Fit, Worst Fit and Smart allocation.
- Strategy search (`find_block`) is in `policy.c`; only the memory manager changes the block list.
- Correct first-failure classification, waiting duration, preserved completed-process addresses and compaction address refresh.
- Invariant checker contract: **returns 0 when valid, nonzero when invalid**.
- Metrics: success rate, fragmentation/OOM failures, average wait, makespan, fragmentation, search length, final used/free memory, largest free block, compactions and units moved.
- Event log (`events.log`) opened once per interactive run, plus per-tick `simulation_trace.csv` snapshots.
- Fair comparison: all four strategies replay the same process list with a fresh pool; rank-sum recommendation reports ties.
- Optional small byte-storage demonstration; stored bytes survive compaction.
- 30-seed experiment runner for file and program profiles, all three patterns and three pool sizes.
- Timing experiments for 1,000, 5,000 and 10,000 processes (three repetitions per strategy).
- CSV outputs: `experiment_results.csv` and `timing_results.csv`; chart script creates PNGs.

## Build and run

GCC / MinGW-w64 (C11):

```bash
make
make test
./simulator
```

Windows PowerShell:

```powershell
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 main.c process.c memory.c policy.c metrics.c -o simulator.exe
.\simulator.exe

gcc -std=c11 -Wall -Wextra -Wpedantic -O2 tests.c process.c memory.c policy.c metrics.c -o tests.exe
.\tests.exe
```

The menu accepts line-based input (not `scanf`/`getchar` mixing), so stray spaces and invalid tokens do not desynchronize subsequent prompts.

## Experiments and charts

Choose menu option 5. The runner:

1. Uses the same generated process list for First, Best, Worst and Smart for each seed.
2. Repeats 30 seeds for small (1,024 KB / 10 processes), medium (4,096 KB / 50) and large (16,384 KB / 100) pools.
3. Runs both file-type and program-profile generators for light, balanced and heavy patterns.
4. Writes all per-seed strategy metrics and rank sums to `experiment_results.csv`.
5. Measures each strategy at 1,000, 5,000 and 10,000 processes, three times, in `timing_results.csv`.

For charts, install matplotlib and run:

```bash
python -m pip install matplotlib
python plot_results.py
```

This creates `fragmentation_file_profiles.png`, `fragmentation_program_profiles.png`, `timing_by_process_count.png`, and (after an interactive real-file run has created `simulation_trace.csv`) `fragmentation_trace.png`.

## Team ownership

- **M1 – `process.c/.h`:** file input/size measurement, profiles, process lifecycle, FIFO queue and simulation loop.
- **M2 – `memory.c/.h`:** block list, allocation mutation, release, merge, compaction, storage array and invariants.
- **M3 – `policy.c/.h`:** candidate search, First/Best/Worst Fit, Smart engine, failure classification and rank-sum recommendation.
- **M4 – `metrics.c/.h`, `main.c`, `tests.c`:** metrics, event/trace logs, experiment runner, timing, CSV, tests and report integration.

## Tests

`make test` runs the regression suite. It covers N1–N20 checks plus strategy selection, double free, duplicate owner, waiting, deadlock, map immutability after failure and completed address retention. The suite prints the actual pass/fail count; the included `TEST_RESULTS.txt` records 43 passed and 0 failed for this archive.

## Modelling limitations to state in the viva

- File size is a measurable model for the memory request, not a claim about actual runtime RAM usage.
- Program-profile sizes/durations are illustrative simulation parameters, not measurements of those applications' RAM.
- Addresses are simulated offsets inside the pool, not physical addresses.
- Smart allocation and recommendation are rule-based, not machine learning.
- FIFO retry can delay a large request; this is a known limitation.

Before submission, run `make clean && make && make test`, run menu option 5, generate charts, and add your own screenshots/team names/faculty/date to the report.
