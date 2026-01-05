# Automatic Digital Blood Pressure Cuff (Oscillometric Method)

An automatic digital blood pressure cuff that inflates and deflates autonomously and estimates **Systolic Blood Pressure (SBP)**, **Diastolic Blood Pressure (DBP)**, **Mean Arterial Pressure (MAP)**, and **Heart Rate** using the oscillometric method.

> ⚠️ This project is for educational purposes only and is **not** a medical device.

---

## Overview

This system uses a pressure sensor and analog signal conditioning circuitry to measure cuff pressure and extract the oscillometric waveform caused by arterial pulsations. An Arduino controls the pump and valve, samples the signals, and computes blood pressure values during cuff deflation.

**[INSERT IMAGE: Full system photo]**  
**[INSERT IMAGE: System block diagram]**

---

## Hardware Summary

- Pressure sensor
- Instrumentation amplifier
- Active band-pass filter (~0.5–5 Hz)
- Arduino microcontroller
- Air pump and normally-open solenoid valve
- Pushbutton for measurement start

**[INSERT IMAGE: Band-pass filter schematic]**

---

## Signal Processing Summary

- Sample rate: **30 Hz**
- Measurement duration: **~30 seconds**
- Oscillometric amplitude computed in 1-second windows
- MAP determined at maximum oscillation amplitude
- SBP and DBP estimated using relative amplitude thresholds
- Heart rate estimated from oscillation timing

**[INSERT IMAGE: Pressure + oscillometric signal plot with SBP/MAP/DBP markers]**

---

## Output

Computed values are printed to the Arduino Serial Monitor:
- Systolic Blood Pressure
- Diastolic Blood Pressure
- Mean Arterial Pressure
- Heart Rate

**[INSERT IMAGE: Arduino serial monitor output]**

---

## How to Run

1. Assemble the hardware according to the design
2. Upload the Arduino code
3. Open the Serial Monitor (115200 baud)
4. Press the start button to begin a measurement cycle

---

## Notes

- Sensor calibration is required to convert voltage to pressure (mmHg)
- Inflation cutoff thresholds should be adjusted for safety
- Results may vary based on sensor placement and user motion

---

## Documentation

See `Final_Report.pdf` for full circuit design, analysis, and validation results.
