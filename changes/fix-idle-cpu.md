---
fixed: `--measure-idle` reported about 100 % of one core on Windows while the app sat idle: it timed itself with the
  C library's clock, which on Windows counts wall time, not processor time. It now reads the process's real CPU time
  on every OS. Measured from outside on Windows (RTX 3060), idle 0.11.1 and 0.11.2 (a sphere open) use 0.00 % of a core, and Linux uses
  0.7 %: there was no busy loop. The idle tests now also fail if the app uses more than 2 % of a core over 30 s.
---
Reviewer notes: processCpuSeconds() in core/HardwareProfile (GetProcessTimes / getrusage) replaces std::clock() in
App::run's --measure-idle; SF_IDLE_CPU_MAX (default 2) fails the run above it. ui_idle_redraw (4 s) at 10 % catches a
spin; new nightly ui_idle_cpu_30s at 2 %. External check: Get-Process TotalProcessorTime per thread over 20 s idle.
