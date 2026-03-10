# tc4d7_coremark

CoreMark benchmark project for Infineon AURIX TC4D7.

This repository now contains the cleaned high-performance 6-core implementation:
- 1-core and 6-core runs are supported
- `STACK` and `MALLOC` memory modes are supported
- final 6-core runs are valid and scale close to 6x in Release mode

## Final Release Results

All results below are valid CoreMark runs with runtime above 10 seconds and successful CRC validation.

| Build | Threads | MemMethod | Total time (s) | Iterations/Sec | Speedup | Efficiency |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| Release | 1 | STACK | 15.036789 | 1995.106750 | 1.0000x | 100.00% |
| Release | 6 | STACK | 15.049913 | 11960.201780 | 5.9948x | 99.91% |
| Release | 1 | MALLOC | 14.987649 | 2001.648112 | 1.0000x | 100.00% |
| Release | 6 | MALLOC | 15.058318 | 11953.526303 | 5.9718x | 99.53% |

Conclusion:
- `STACK` and `MALLOC` both reach near-linear 6-core scaling on TC4D7
- the final 6-core implementation preserves correctness while delivering almost full parallel efficiency
- `MALLOC` no longer suffers from the earlier shared-context instability

## Implementation Notes

The multicore performance version uses a conservative execution model:
- each worker core receives a shared template only
- each worker rebuilds its own local CoreMark context in local memory
- only compact synchronization state and CRC results are exchanged through shared mailbox data

This removes the earlier failure mode caused by running worker threads directly on shared benchmark state while keeping the fast 6-core path.

## Build and Flash

Run commands from the `tc4d7_coremark` directory.

```powershell
# 1-core, Release, STACK
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod STACK
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod STACK

# 6-core, Release, STACK
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod STACK
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod STACK

# 1-core, Release, MALLOC
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod MALLOC
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod MALLOC

# 6-core, Release, MALLOC
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod MALLOC
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod MALLOC
```

Useful parameters:
- `-BuildType`: `Debug | Release | RelWithDebInfo | MinSizeRel`
- `-CoremarkThreads`: `1..6`
- `-CoremarkMemMethod`: `STACK | MALLOC | STATIC`
- `-CoremarkIterations`: fixed CoreMark iteration seed, `0` means auto calibration

Note:
- `STATIC` is single-context only. CoreMark upstream does not support `MEM_STATIC` with `MULTITHREAD > 1`.

## Validity Checklist

Record a score only when all of the following are true:
- `Correct operation validated` is printed
- all CRC checks pass
- runtime is at least 10 seconds
- build flags match the intended benchmark mode

## Build-System Cleanup

The project scripts were also cleaned up:
- `build.ps1` no longer runs duplicate configure steps during `build`, `rebuild`, or `all`
- `cmake/AurixProject.cmake` now scans included source directories recursively without globbing excluded build output trees, which avoids the previous `GLOB mismatch!` noise
