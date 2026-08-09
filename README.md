# RainPurify — multi-stage rainwater treatment controller

Firmware for the RainPurify unit: an automated multi-stage rainwater filter with
turbidity and pH sensing and a recirculation loop that decides for itself
whether a batch is finished.

I was **Lead Engineer** on this project from 2023, responsible for the IoT
system, turbidity–pH sensing, and embedded automation. It reached the
**Hult Prize 2026 Indonesia national finals** (ITB), took **1st place at Roots
SDGs 2025**, and was entered at GEMASTIK's Smart City division and UNITY UNY.

> **On this repository.** The unit was built and deployed in households; field
> results took raw roof water from **11.09 NTU down to 1.44 NTU** with pH
> stabilised from 9.67. The original firmware is gone, so this is the control
> layer rebuilt from the same design and the same state machine. It builds clean
> for `esp32dev`. No field data is included.

**Related but separate:**
[rainwater-harvesting-sizing](https://github.com/lesaullianachmadargito/rainwater-harvesting-sizing)
answers *how big should the tank be*. This repository is the device that treats
what lands in it.

---

## The thing this README has to say before anything else

**Filtration is not disinfection.**

Sediment, activated carbon and a fine membrane reduce turbidity and adsorb some
organics. They do not reliably remove bacteria, viruses, or protozoa, and no
sensor in this system can detect any of them. A turbidity reading of 1.4 NTU
says the water is *clear*. It says nothing about whether it is *safe to drink*.

So the firmware never reports a batch as safe. The passing verdict is literally
named `MEETS_THRESHOLDS`, and every time one is announced the console prints:

```
# PASSED TURBIDITY AND pH THRESHOLDS.
# Filtration is not disinfection. This water has not been tested for
# microbes. For washing, irrigation, and water that will still be boiled —
# not for drinking directly.
```

That distinction is not linguistic caution. It is the difference between a
device that helps a household and one that makes it ill.

## Recirculation that knows when to stop

The core loop is straightforward — pump water through the filter train, let it
settle, test it, and send it round again if it is still cloudy. What matters is
the stopping rule, because a naive loop runs forever on water that will never
clear.

```
IDLE ──► FILTERING ──► SETTLING ──► TESTING ──┬──► DELIVERING ──► IDLE
           ▲                                   │
           └────────── RECIRCULATING ◄─────────┤
                                               └──► REJECTED
```

A batch goes round again only if **both** hold:

1. **Passes remain.** Four maximum.
2. **Turbidity actually improved** by at least 0.5 NTU since the last test.

The second condition is the one that earns its place. If two consecutive passes
produce the same number, the sediment and carbon media are saturated or blocked
— and a fifth pass will produce that same number too. The batch is rejected with
`no_improvement_media_likely_spent`, and the console says to check the media
before running another cycle. A loop without that check quietly burns the pump
while reporting that it is working.

**pH is rejected immediately, not recirculated.** Nothing in this filter train
corrects pH; activated carbon barely moves it. High pH in roof water usually
comes from cement or galvanised gutters, and no number of passes will change
that. Sending it round four times is pure waste, so `PH_OUT_OF_RANGE` goes
straight to `REJECTED`.

## Water never reaches the clean tank untested

During filtering and recirculating, the output valve is set to **return to the
raw tank**. The clean tank is only opened after a batch passes.

The reason is asymmetric cost: once bad water enters the clean tank, the entire
contents are contaminated and the whole tank has to be dumped. Routing an extra
loop costs a few minutes of pump time.

## Not knowing is not passing

```cpp
if (!s.turbidity_valid || !s.ph_valid) return Verdict::SENSOR_UNRELIABLE;
```

A batch whose quality could not be measured is treated exactly like one that
failed. A disconnected turbidity probe reads at the rail, and a system that
interpreted that as "very clear" would deliver untreated water with a green
light on.

## Turbidity in NTU, not in volts

The single most common error in projects like this is printing the sensor's
analog reading and calling it NTU. It is not. An analog turbidity sensor is
non-linear and **temperature-sensitive**, and on cheap units the temperature
drift alone is enough to move a batch from pass to fail across a day.

`readTurbidityNtu()` applies a quadratic calibrated against formazin standards
and then compensates with the measured water temperature from a DS18B20. The
coefficients in `config.h` are from the prototype and **must be recalibrated per
unit** — the code comment says so, next to the numbers.

Readings above 300 NTU return `NAN` rather than an extrapolated figure, because
the calibration curve does not extend there.

## Pump protection

The raw tank level is checked on every sample while pumping. A diaphragm pump
run dry is destroyed in minutes, so `FAULT_DRY` cuts it immediately and requires
the level to recover to 1.5× the minimum before restarting — hysteresis, so a
sloshing tank does not chatter the pump.

## Log

Every state transition is written the moment it happens, not on the logging
interval, so the full decision sequence of one batch can be reconstructed:

```csv
uptime_ms,state,batch_id,pass,turbidity_ntu,ph,tds_ppm,water_temp_c,raw_level_cm,verdict,rejection
412000,TESTING,3,1,8.42,7.61,180,27.4,62.0,TOO_TURBID,
615000,TESTING,3,2,3.10,7.58,176,27.5,58.0,TOO_TURBID,
818000,TESTING,3,3,1.44,7.55,174,27.6,54.0,MEETS_THRESHOLDS,
```

## Hardware

| Function | Part | Pin |
|---|---|---|
| MCU | ESP32 DevKit V1 | — |
| Turbidity | analog probe | GPIO 34 (ADC1) |
| pH | analog module | GPIO 35 (ADC1) |
| TDS | analog probe | GPIO 32 (ADC1) |
| Water temperature | DS18B20 | GPIO 15 |
| Raw tank level | ultrasonic | trig 5, echo 18 |
| Pump | relay | GPIO 25 |
| Valves | clean / recirculate / waste | GPIO 26 / 27 / 14 |
| Storage | microSD | SPI |

Filter train: pre-filter → sediment → activated carbon → fine membrane.

## Commands

```
START            begin a treatment cycle
STOP             halt, return to IDLE
STATUS           state and latest readings
BATCH            result of the last batch
CALPH <volt>     pH calibration: measured voltage in buffer 7.00
```

## Build

```bash
pio run -t upload && pio device monitor
```

## What is missing before this could be called safe water

- **Disinfection.** UV-C or chlorination, with contact time and dose actually
  measured. UV is also blocked by turbidity, so it has to sit *after* this
  filter train and be interlocked against the turbidity reading.
- **Microbial testing.** Periodic laboratory testing of the output. No sensor
  substitutes for it.
- **First-flush diversion**, sized properly — see the sizing repository.

Until those exist, the honest claim is the one the field results support: water
that was too cloudy to use became clear enough for washing and irrigation.

## License

MIT — see [LICENSE](LICENSE).
