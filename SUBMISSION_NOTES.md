# Submission revision notes

This revision keeps the existing C allocator and adds targeted fixes rather than replacing the project.

## Confirmed bugs fixed

1. **Recommendation ties:** rank-sum calculation now gives equal metrics equal ranks and explicitly reports a tie. The comparison prints all four rank sums.
2. **Waiting time:** elapsed wait is `successful_start_tick - first_wait_tick`, so a four-tick wait is reported as four.
3. **Completed addresses:** completion releases memory but preserves the process's original `[start, end)` range in the process table.
4. **Live memory map:** the real-file demo prints a memory snapshot on every simulation tick, while processes are still active.
5. **Event logging:** one file handle is opened/truncated at run start and closed at run end. A per-tick `simulation_trace.csv` is also recorded.
6. **Directory input:** file measurement uses a filesystem status check and rejects directories/non-regular files before measuring.

## Guide additions completed

- Program-profile workload mode and file/program profile ranges.
- Strongly differentiated light/balanced/heavy profile weights.
- 10,000-process capacity and 1,000 / 5,000 / 10,000 timing runs (three repetitions per strategy).
- 30-seed experiments for both profile modes, all patterns, and all three suggested pool sizes.
- `experiment_results.csv` and `timing_results.csv` with metrics, rank sums, and timing data.
- Chart generator `plot_results.py` and generated fragmentation/timing/trace PNGs.
- Final used/free memory and largest-free-block metrics.
- Policy search moved to `policy.c`; the memory manager performs block-list mutations.
- Invariant checker convention is now `0 = valid`, nonzero = invalid.
- Line-based menu input replaces mixed `scanf`/`getchar` parsing.
- N1–N20 regression coverage plus strategy-choice, duplicate-owner, double-free, Smart worked-example, waiting and overlap checks.

## Verification performed on this archive

- GCC C11 build with `-Wall -Wextra -Wpedantic -O2`: successful, no compiler warnings.
- Automated tests: **43 passed, 0 failed** (`TEST_RESULTS.txt`).
- Full experiment runner completed and produced 2,160 per-seed strategy rows plus a header in `experiment_results.csv`.
- Timing runner completed for 1,000, 5,000 and 10,000 processes, three repetitions per strategy (`timing_results.csv`).
- Real-file demo completed and produced live map output, `demo_events.log`, and `simulation_trace.csv`.
- Charts generated with `plot_results.py`.

## Before final academic submission

Add the actual four member names/enrollment numbers, faculty name, submission date, and your own screenshots to the report. Explain the modelling limitation honestly: file size and program profiles are simulation request estimates, not measured runtime RAM. Re-run the build/tests on the lab computer before presenting.
