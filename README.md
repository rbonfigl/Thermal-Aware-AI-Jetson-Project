# Thermal-Aware AI Inference Pipeline (Jetson Orin)

## Author

-Roman Bonfiglio

## Overview
This project implements a thermal-aware, concurrent AI inference pipeline designed for the NVIDIA Jetson Orin Nano. Running a sustained 
workload that must balance camera throughput against thermal limits — this system senses SoC thermal state directly from the Linux kernel and 
dynamically adjusts pipeline behavior in response. 

It has three main stages

1. **User-space concurrency engine** — a multi-threaded C++ producer/consumer/governor pipeline with a custom ring buffer and drop-based backpressure policy
2. **Kernel-space thermal bridge** — a custom Linux character device driver that reads real thermal zone data from the kernel and exposes it to user space
3. **Full integration** — real GStreamer camera input and TensorRT inference tied to the kernel-driven thermal governor


## Phase 1: User-Space Concurrency Engine (Complete)

### What it does
- Producer thread simulates camera frame ingestion
- Consumer thread simulates TensorRT inference
- Governor thread runs a thermal FSM (NORMAL/WARM/HOT/CRITICAL) with hysteresis
- Fixed-size ring buffer connects them with a drop-on-full backpressure policy

### Key design decisions
- **Drop-incoming vs overwrite-oldest**: chose drop-incoming so frame ordering 
  is preserved and backpressure produces an explicit, countable metric 
  rather than a silent overwrite
- **Hysteresis in the FSM**: asymmetric enter/exit thresholds per state 
  prevent oscillation when temperature hovers near a boundary

### Validation
- Verified with ThreadSanitizer (clean across multiple runs, varied durations)
- Verified with AddressSanitizer (clean)
- Verified with Valgrind (no memory leaks)


## Build & Run
g++ -std=c++17 -pthread src/main.cpp src/RingBuffer.cpp -Iinclude -o thermal_sim
./thermal_sim


## Repository Structure
src/        - source files

include/    - headers

## Phase 2: Kernel-Space Thermal Bridge
### What it does
- Custom Linux loadable kernel module (`orin_thermal.ko`) implementing a character
  device at `/dev/JETSON`
- Reads real GPU and CPU junction temperatures directly from the Jetson's Tegra SoC
  via the Linux kernel's built-in thermal framework — not simulated or hardcoded data
- Exposes telemetry to user space through a structured `ioctl` interface, and
  demonstrates a zero-copy `mmap` interface for high-frequency access
- The Phase 1 governor thread now consumes this real hardware telemetry in place
  of the earlier simulated random-walk temperature model, closing the loop between
  kernel-space sensing and the user-space FSM
### Kernel Module Interfaces
 
**Character device registration**
- Dynamic major number allocation via `alloc_chrdev_region`, with `cdev_init`/`cdev_add`
  registering the device against a custom `file_operations` table
- `class_create`/`device_create` used to expose the device at `/dev/JETSON`
**ioctl interface** — the primary telemetry path
- `JETSON_THERMAL_GPU_READ` / `JETSON_THERMAL_CPU_READ` — return a structured
  `struct thermal_telemetry` (temperature in whole degrees Celsius, FSM state)
  pulled live from the kernel's thermal framework at the moment of the call
- `JETSON_THERMAL_WRITE` / `JETSON_THERMAL_RESET` — scaffolding for driver-side
  configuration (e.g. adjustable thresholds), implemented and validated but not
  yet load-bearing in the current pipeline
- Chosen over a plain `read()` interface because the driver needed to return
  multiple typed fields per call rather than an unstructured byte stream; an
  initial `read()` implementation was used early on to validate character device
  registration and was removed once `ioctl` replaced it, to avoid maintaining a
  redundant, superseded interface
**mmap interface** — zero-copy access, demonstrated independently
- A dedicated physically-contiguous page (`get_zeroed_page`) is mapped directly
  into user-space via `remap_pfn_range`, allowing a user-space program to read
  telemetry through a plain pointer dereference with no syscall per access
- Chosen over `vmalloc` specifically because the telemetry struct is small enough
  to fit in a single page, and `remap_pfn_range` requires physically contiguous
  memory — `vmalloc`'s scattered-page allocation would require a more complex
  page-by-page mapping strategy for no benefit at this data size
- Validated end-to-end with a standalone user-space test program: confirmed the
  mapped memory reflects driver-written values, and that repeated reads after the
  initial `mmap()` call trigger no further kernel code execution
- Currently demonstrated as a standalone mechanism rather than wired into the live
  pipeline — doing so meaningfully would require a kernel-side periodic updater
  (timer/workqueue) to keep the mapped buffer fresh, which introduces real
  concurrent-access requirements (see Concurrency Considerations below).
  Deprioritized given the project timeline; documented here as an architectural
  decision rather than an oversight.
**Real hardware thermal integration**
- Verified symbol availability (`thermal_zone_get_zone_by_name`,
  `thermal_zone_get_temp`) against the running kernel via `/proc/kallsyms` before
  committing to this approach, since these functions are only usable from a
  loadable module if explicitly exported
- Both GPU and CPU thermal zones are read on demand, per ioctl call, rather than
  via a continuously-updating background poll — this was a deliberate scope
  decision (see below)
### Validation
- Character device load/unload cycle confirmed clean via `dmesg`, with
  `/dev/JETSON` correctly appearing and disappearing on `insmod`/`rmmod`
- ioctl GPU/CPU reads validated against a standalone user-space test program,
  confirming live, changing values across repeated calls (not a cached or
  hardcoded result)
- mmap validated against a standalone user-space test program using `mmap()`
  and `munmap()`, confirming correct zero-copy access to driver-written data
- Full pipeline integration validated by wiring real ioctl-sourced GPU
  temperature into the Phase 1 governor thread, replacing the simulated
  temperature model and driving the same FSM logic against live hardware data
## Phase 3: Full Integration (In Progress)
