# Pulse Windows Implementation Plan

**Goal:** Deliver a polished, runnable Windows CPU/GPU/RAM monitor that previews and sends the existing board's five-segment breathing effect over CH340.

**Architecture:** PySide6 owns the interface and local animation. A worker owns telemetry and the serial port; the GUI never blocks on either. Device readiness requires a versioned handshake and matching acknowledgement. User explicitly chose Windows program and protocol only; firmware remains unchanged.

**Tech Stack:** Python 3.13, PySide6, psutil, NVML, pyserial, PyInstaller.

## Tasks

- [x] Add protocol and effect tests: CRC corruption, fragmented packets, out-of-range input, segment boundaries, smoothing and serial acknowledgement state. Run `desktop/.venv/Scripts/python.exe -m pytest desktop/tests -q` before and after implementation.
- [x] Implement `desktop/pulse/protocol.py`, `effect.py`, `telemetry.py`, `device.py`: 115200 8N1, RTS/DTR deasserted, explicit connect, bounded reconnect, handshake then SET at 2 Hz, STOP on disconnect. Missing sensors remain invalid.
- [x] Build `desktop/pulse/ui.py`, `widgets.py`, `workers.py`, `desktop/main.py`: three metric cards, live graph, native animated seven-lamp preview, GPU/port selection, intensity/speed controls, pause, connection states, settings persistence.
- [x] Exercise the UI offscreen with real telemetry; save a screenshot, inspect at minimum and normal sizes. Exercise controls and fake serial transport in tests.
- [x] Package `Pulse.exe`, run packaged smoke test, include Chinese README/protocol and a reproducible build command. Do not claim physical board control without hardware acknowledgement.

## Accepted design defaults

Dark graphite surfaces, mint CPU accent, violet GPU accent, amber memory accent; Chinese labels. Default CPU, 80% overall brightness, 1.0x speed, 500 ms sampling, no automatic port open at startup. Q2..Q6 represent 20% each; Q1/Q7 remain off. No sensor data is fabricated. Serial status distinguishes port access from firmware readiness. Existing firmware is unchanged unless integration is requested.
