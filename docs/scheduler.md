# Scheduler

## Approach

A single fixed-rate 1 kHz loop pinned to **core 1**, with sub-rate tasks
dispatched by tick-counter modulo. No RTOS scheduling in the control path.

The ESP32 is dual-core and the radio stack is not optional, so the split is:

| Core | Runs |
|---|---|
| **1** | The control loop. Nothing else. |
| **0** | ESP-NOW / WiFi radio stack, telemetry, logging flush, parameter I/O |

The WiFi radio stack (which ESP-NOW rides on) runs on core 0 by default and
has unpredictable, sometimes multi-millisecond, execution times. Sharing a
core with it injects that jitter straight into the attitude loop. Pin the
control task to core 1 and keep it there.

Communication between cores is a lock-free single-producer/single-consumer
buffer — the control loop must **never** block on a mutex held by the radio.

## Rates

| Rate | Task | Budget |
|---|---|---|
| **1000 Hz** | IMU read, estimator, mode manager, hop sequencer, controller, mixer, safety, motor write | **< 400 µs** |
| 200 Hz | link RX poll (drain to newest) | 20 µs |
| 100 Hz | magnetometer, battery ADC | 30 µs |
| 50 Hz | VL53L0X collect + height fuse | 60 µs |
| 20 Hz | telemetry TX (queue to core 0) | 20 µs |
| 10 Hz | status LED, buzzer, parameter service, log flush | — |

The 1 kHz budget of 400 µs out of 1000 µs is deliberate. Headroom absorbs
worst-case sensor retries and interrupt latency. If the hot path routinely runs
past ~600 µs, fix it — do not lower the loop rate to make the number look
better.

## Rules

**Never `delay()` in the loop.** Not for a sensor, not for anything. A driver
that needs to wait is a driver that needs a state machine: kick off the
transaction, return, collect the result on a later tick. This is exactly why
`IStateEstimator::update()` is specified as pull-based and non-blocking.

**Sub-rate tasks are staggered.** If every task fires on tick 0, tick 0 blows
the budget and the other nine ticks idle. Offset them:

```
if (tick % 5  == 0) magnetometer();     // 200 Hz group, phase 0
if (tick % 10 == 1) battery();          // phase 1
if (tick % 20 == 2) tof_collect();      // phase 2
if (tick % 50 == 3) telemetry();        // phase 3
```

**Measure the loop, every loop.** Track execution time and the jitter between
tick starts. Publish max and mean over telemetry. An overrun beyond two tick
periods trips the watchdog failsafe — see [safety.md](safety.md). Loop timing
is a first-class health signal, not a debugging afterthought; a hopcopter that
starts missing ticks during launch bursts will fail in ways that look like
tuning problems.

**Use the IMU data-ready interrupt** rather than polling on a timer. It keeps
the sample-to-control latency fixed instead of letting it drift by up to a full
tick relative to the loop, which shows up as phase lag in the rate loop and
limits how high the D gain can go.

## Timing during hops

Nothing in the rate table changes when hopping — that is the point. The phase
machine runs in the 1 kHz group precisely so touchdown is caught within a
couple of milliseconds. Contact detection at 50 Hz would put 20 ms of latency
into launch timing, which is a substantial fraction of the compression stroke
and enough to miss the burst window entirely.

The one thing that *does* change is that logging becomes more valuable and more
expensive. Buffer hop-phase records to RAM at high rate and flush after
landing; never flush to serial or ESP-NOW inside the hot path.
