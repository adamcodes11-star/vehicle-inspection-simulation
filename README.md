
# Vehicle Inspection Center Simulation

A multithreaded vehicle inspection center simulation developed in C as an individual Operating Systems course project.

## Overview

The program simulates a vehicle inspection center where multiple cars pass through three processing stages:

1. Mechanical Inspection
2. Pollution Testing
3. Payment

The simulation uses POSIX threads and semaphores to coordinate concurrent operations and manage limited shared resources.

## Features

- Supports up to 10 cars inside the inspection center simultaneously.
- Simulates 20 arriving cars with regular and VIP priorities.
- Uses three mechanic workers and two pollution testers.
- Synchronizes access to limited tools using semaphores.
- Implements priority-based linked-list queues.
- Includes an aging mechanism to reduce starvation.
- Displays timestamped activity logs.
- Manages dynamically allocated memory.

## Technologies

- C
- Linux / POSIX
- POSIX Threads (Pthreads)
- POSIX Semaphores
- Linked Lists and Queues

## Compilation and Execution

Compile on Linux using GCC:

```bash
gcc -pthread vehicle-inspection.c -o vehicle_inspection```

Run:

```bash
./vehicle_inspection
```

## Project Type

Individual academic project — Operating Systems course.
