# tc4d7_coremark

CoreMark benchmark project for Infineon AURIX TC4D7.

This project supports:
- Single-core run (`CoremarkThreads=1`)
- Six-core run (`CoremarkThreads=6`)
- Build profiles (`Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel`)

## 1. Build and Flash

### 1.1 Common commands

From `tc4d7_coremark` directory:

```powershell
# Configure + build
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1

# Flash
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 1

# Configure + build six-core
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6

# Flash six-core
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 6
```

### 1.2 Useful script parameters

- `-BuildType`: `Debug | Release | RelWithDebInfo | MinSizeRel`
- `-CoremarkThreads`: `1..6`
- `-CoremarkMemMethod`: `STACK | MALLOC | STATIC`
- `-CoremarkIterations`: CoreMark `ITERATIONS` seed (`0` means auto calibration)

Notes:
- `STATIC` memory method is only suitable for single-context runs. CoreMark upstream code rejects `MEM_STATIC` with `MULTITHREAD > 1`.

## 2. How to Read CoreMark Output

Key lines:

- `Iterations/Sec`: primary performance metric
- `CoreMark Size`: data size per algorithm context (for this setup typically `666`)
- `Compiler flags`: confirms build mode (`-Og -g3` for Debug, `-O3 -DNDEBUG` for Release)
- CRC lines (`crclist`, `crcmatrix`, `crcstate`, `crcfinal`): correctness checks

If all CRC checks pass and output prints `Correct operation validated`, result is valid.

## 3. Your Current Release Results (Interpretation)

Given values:

- 6-core: `Iterations/Sec = 2622.132599`
- 1-core: `Iterations/Sec = 1995.369858`

Derived metrics:

- Speedup = `2622.132599 / 1995.369858 = 1.314x`
- Parallel efficiency = `1.314 / 6 = 21.9%`

So yes, six-core is not near 6x in current configuration.

## 4. Why 6-Core Is Not 6x Here

This is expected for this implementation and workload shape.

### 4.1 Data placement is not core-local

CoreMark uses `MEM_STACK` now. In `core_main.c`, `stack_memblock` is allocated in core0 call stack and then partitioned for all contexts.

Effect:
- Workers on other cores often access memory physically close to core0 context memory region.
- This creates strong interconnect and memory contention.

### 4.2 Memory-system bound workload

CoreMark (especially list/state/matrix mix) has significant memory traffic and pointer chasing. Scaling is frequently limited by:
- Shared interconnect bandwidth
- Arbitration latency
- Remote memory access costs

### 4.3 Parallel overhead exists

Even with correct synchronization, six contexts add:
- Start/stop coordination overhead
- Contention on shared synchronization flags

### 4.4 Auto iterations differ by mode

`ITERATIONS=0` auto-calibrates run length. Single-core and six-core can end up with different iteration counts, so direct wall-time comparison is less useful than `Iterations/Sec`.

## 5. Optimization Directions

## 5.1 Use Release mode for benchmark reporting

Already done in your latest result and strongly recommended.

## 5.2 Pin data closer to each worker core

Most impactful direction:
- Avoid one shared stack block owned by core0 for all contexts.
- Give each core a dedicated buffer located in memory with better locality for that core.

Expected result: better multicore scaling.

## 5.3 Experiment with memory method

Try switching from `MEM_STACK` to `MEM_MALLOC` (or dedicated LMU/local partitions) and compare:
- 1-core `Iterations/Sec`
- 6-core `Iterations/Sec`
- Speedup and efficiency

## 5.4 Keep benchmark conditions consistent

For apples-to-apples comparisons:
- Same `BuildType`
- Same clocks and board state
- Prefer fixed `CoremarkIterations` for controlled comparison runs

Example:

```powershell
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod STACK -CoremarkIterations 30000
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod STACK -CoremarkIterations 30000
```

Recommended comparison set:

```powershell
# STACK baseline
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod STACK  -CoremarkIterations 30000
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod STACK  -CoremarkIterations 30000

# MALLOC experiment
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod MALLOC -CoremarkIterations 30000
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod MALLOC -CoremarkIterations 30000
```

## 5.5 Optional compiler experiments

For exploration only (must still respect CoreMark run rules for official reporting):
- `-flto`
- `-funroll-loops`
- Tune `-mcpu`/architecture-specific options if available

Always re-check CRC validity and minimum runtime requirements.

## 6. Validity Checklist

Before recording a score:

- `Correct operation validated` appears
- No CRC mismatch errors
- Runtime is long enough for valid reporting (CoreMark recommends at least 10s)
- Build flags are explicitly recorded

## 7. Troubleshooting Notes

- If you ever see timeout message but all CRCs pass, investigate timeout arithmetic/data type first.
- If one core fails CRC while others pass, check core-id mapping and worker-slot mapping.
- If multicore speedup is low but correctness is fine, profile memory placement and contention first.

## 8. Suggested Next Experiments

1. Fix `CoremarkIterations` and compare 1-core vs 6-core across `Debug` and `Release`.
2. Prototype non-`MEM_STACK` data placement for multicore.
3. Capture a small result table: build mode, threads, iterations, total time, iterations/sec, speedup, efficiency.
