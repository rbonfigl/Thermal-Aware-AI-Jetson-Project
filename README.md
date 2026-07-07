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

### Diagram
-TO DO

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

## Phase 2: Kernel-Space Thermal Bridge (In Progress)

## Phase 3: Full Integration (Planned)
