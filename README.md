# esp32-freertos-telemetry-node
Virtual IoT Telemetry & Fault-Injection Firmware Node
A dual-core, thread-safe embedded system firmware built on ESP-IDF / FreeRTOS and C++17, featuring telemetry sampling, dynamic system state transitions, mutex-protected shared resources, and an interactive serial CLI with fault-injection capabilities. Designed and verified on virtual ESP32 hardware using Wokwi simulation.

## Live Simulation
[Run interactive simulation in browser via Wokwi](https://wokwi.com/projects/https://wokwi.com/projects/480276530490072065)

Architecture Overview
The system divides responsibilities across ESP32 Core 0 and Core 1 to maximize cache efficiency and prevent thread starvation. Inter-Task Communication (IPC) is handled via thread-safe queues and mutexes.

flowchart TD

    subgraph Core0 [ESP32 Core 0]
        SensorTask["vSensorTask (Priority 2)<br/>Simulates/Reads Sensor Data"]
        CLITask["vCLITask (Priority 1)<br/>Serial Command Parser & Diagnostics"]
    end

    subgraph Core1 [ESP32 Core 1]
        ProcTask["vProcessingTask (Priority 2)<br/>State Machine & LED Control"]
    end

    subgraph Memory [Shared Memory & Synchronization]
        Queue[("FreeRTOS Queue<br/>xSensorQueue (Cap: 10)")]
        Mutex[("FreeRTOS Mutex<br/>xStateMutex")]
        State[("Global System State &<br/>Temperature Thresholds")]
    end

    SensorTask -->|Push Telemetry| Queue
    Queue -->|Pop Data| ProcTask
    CLITask <-->|Read / Write Thresholds & State| Mutex
    ProcTask <-->|Evaluate State & Drive LED| Mutex
    Mutex <--> State

Key Engineering Features
Deterministic Multi-Core Scheduling: Tasks are pinned to dedicated physical cores (xTaskCreatePinnedToCore) to prevent multi-core cache thrashing and lock contention.

Thread-Safe IPC & Mutex Protection: Shared configuration parameters and state transitions are guarded by xStateMutex to eliminate data races between Core 0 and Core 1.

Finite State Machine (FSM): Evaluates sensor telemetry against dynamic bounds, executing seamless transitions between SYSTEM_OK, WARNING_DEGRADED, and CRITICAL_ALERT.

Interactive CLI Shell: Supports live threshold adjustments, state queries, and hardware fault injection (FAULT_INJECTED) via non-blocking serial input.

Stack High-Water Profiling: Employs uxTaskGetStackHighWaterMark() and xPortGetFreeHeapSize() to monitor runtime memory consumption and prevent stack overflow.

Getting Started (Wokwi Simulation)
1. Open Wokwi ESP32 Simulator.
2. Replace main.cpp content with project code.
3. Add a green LED on GPIO 2 in diagram.json.
4. Start simulation and open the Serial Monitor at 115200 baud.

CLI Example Usage

Query current system state and free RAM
> status
State: SYSTEM_OK | Free Heap: 294820 bytes | Warn: 24.5 C | Crit: 26.0 C

Lower warning threshold to trigger state transition
> set_warn 22.5
[CLI] Warning threshold set to 22.5 C

Inject synthetic hardware fault
> fault
[CLI] Artificial FAULT injected.

Reset system to normal operation
> clear
[CLI] Fault cleared.



