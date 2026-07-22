# Parts list

Everything except the ESP32 is generic. If you have a hobbyist parts drawer
you probably only need to buy the board and the buttons.

## Required

| # | Part | Spec | Notes |
|---|------|------|-------|
| 1 | ESP32 dev board | ESP-WROOM-32, 30-pin ("DOIT DEVKIT V1", "ESP32 DevKitC") **or** Seeed XIAO ESP32C3 | Both are supported and build clean; see the note below. 38-pin ESP-WROOM-32 boards also work — same GPIO numbers, wider footprint. |
| 2 | Ethernet patch cable | Cat5e, ~1 m, cheapest available | Cut one end off. The surviving end is an intact RJ45 plug that goes straight into the control box — no jack or breakout needed. Anchor the cable so the solder joints take no strain; solid core snaps if it flexes. |
| 3 | Momentary push buttons | ×2 (×3 with optional stop), SPST normally-open, 12 mm panel mount | Any tactile switch works for a bench test; panel-mount reads better screwed under a desk. No resistors needed — internal pull-ups. |
| 4 | Resistor | 1.8 kΩ, ¼ W, ×1 | RX divider, upper leg |
| 5 | Resistor | 3.3 kΩ, ¼ W, ×1 | RX divider, lower leg |
| 6 | Resistor | 330 Ω, ¼ W, ×1 | TX series protection |
| 7 | Schottky diode | 1N5817 (or SS14, SS34), ×1 | Blocks USB back-feed into the control box |
| 8 | Capacitor | 1000 µF, 16 V electrolytic, ×1 | Bulk, for WiFi current spikes |
| 9 | Capacitor | 0.1 µF ceramic, ×1 | Decoupling |
| 10 | Hookup wire | 24 AWG stranded | Buttons and jumpers |

Ballpark: **$12–18** if you buy everything, most of it being the ESP32 and
the button pair.

## Choosing the board

Either works. The firmware is the same; `src/config.h` swaps pins on the
chip macro and `platformio.ini` has an environment per board.

| | ESP-WROOM-32 (30-pin) | XIAO ESP32C3 |
|---|---|---|
| Build env | `stage1/2/3` | `xiao1/2/3` |
| Size | 55 × 28 mm | **21 × 17.5 mm** |
| Cores | 2 | 1 (RISC-V) |
| USB | micro-USB via UART bridge | **native USB-C** |
| Antenna | onboard, fine | **external, must be plugged in** |
| User LED | GPIO2 | none |
| Flash used (stage 3) | 48% | 47% |

**Pick the XIAO if** you care about fitting the enclosure under a desk, or
you want USB-C and no driver install. It is the nicer physical object.

**Pick the ESP-WROOM-32 if** you want breadboard-friendly pin spacing, a
status LED, or margin from a second core. Note the safety design never
relied on that second core — the desk task and the Arduino loop were always
pinned to the same one, so what protects button latency is priority
preemption, which behaves identically on the C3.

One real C3 caveat: HomeKit pairing does its crypto on a single RISC-V core,
so the initial pairing handshake takes noticeably longer. It completes; it
just is not instant.

## Substitutions that work

- **Divider resistors:** any pair landing between 3.0 V and 3.3 V out of
  5 V. 1 kΩ / 2 kΩ → 3.33 V. 2.2 kΩ / 3.9 kΩ → 3.20 V. 10 kΩ / 20 kΩ works
  electrically but is a high enough impedance that a long cable starts to
  matter; keep the total under ~10 kΩ.
- **Level shifter instead of resistors 4–6:** one 4-channel BSS138 module
  (~$1) replaces the divider and the series resistor. See
  [WIRING.md §3](WIRING.md). Functionally equivalent at 9600 baud.
- **Bulk cap:** 470 µF is usually enough. 2200 µF does no harm.

## Explicitly not needed

- **Logic level converter for TX.** The control box reads 3.3 V as high.
  The 330 Ω is wiring-mistake insurance, not level shifting.
- **Pull-up or pull-down resistors for the buttons.** The ESP32's internal
  pull-ups are enabled in firmware.
- **External power supply**, unless the desk's 5 V rail turns out to brown
  out — see [WIRING.md §4](WIRING.md) for the symptoms. If it does, any
  5 V ≥ 1 A USB brick and a USB cable.
- **A Raspberry Pi, Home Assistant box, or Homebridge server.** HomeSpan
  pairs the ESP32 to HomeKit directly.
- **Anything that plugs into the original handset.** It is being replaced,
  not tapped.

## Tools

- Soldering iron, solder
- Wire strippers
- **Multimeter** — you have one. It is used in
  [WIRING.md §2](WIRING.md) to confirm GND and +5 V before anything is
  connected, which is the step that keeps a wrong assumption from becoming
  a dead board.
- USB cable matching your dev board (usually micro-USB, sometimes USB-C)
