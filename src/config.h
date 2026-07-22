#pragma once

// ===========================================================================
// Build stages
// ---------------------------------------------------------------------------
// Bring the system up one layer at a time. Each stage is independently
// testable, and stage 1 proves the desk link works before any radio is
// involved. See docs/SETUP.md.
//
//   pio run -e stage1 -t upload    buttons + serial console, no radio
//   pio run -e stage2 -t upload    + web dashboard
//   pio run -e stage3 -t upload    + native HomeKit   (default env)
//
// The environments set these via build flags. The defaults below only apply
// if you build some other way.
// ===========================================================================
#ifndef ENABLE_WIFI
#define ENABLE_WIFI 1
#endif
#ifndef ENABLE_HOMEKIT
#define ENABLE_HOMEKIT 1
#endif

// ===========================================================================
// Pins
// ===========================================================================
#if CONFIG_IDF_TARGET_ESP32C3
// --- Seeed XIAO ESP32C3 ----------------------------------------------------
// Labels are the silkscreen pads; the numbers are the GPIOs the code wants.
//
//   D4 / GPIO6  desk RX      D1 / GPIO3  UP button
//   D5 / GPIO7  desk TX      D2 / GPIO4  DOWN button
//                            D3 / GPIO5  STOP button (optional)
//
// Deliberately NOT using D6/D7 (GPIO21/20) for the desk even though they are
// the "natural" UART pads: GPIO21 is the ROM bootloader's log output, so
// every reset would spray 115200-baud boot chatter down the desk's HTX line.
// Harmless (bad checksums) but pointless. The GPIO matrix puts UART1 on any
// pin, so D4/D5 cost nothing.
//
// Avoid GPIO2, GPIO8, GPIO9 — strapping pins. GPIO9 is BOOT: a button
// holding it low at reset drops the chip into download mode.
#define PIN_DESK_RX 6   // <- desk DTX, RJ45 pin 2, THROUGH A DIVIDER OR SHIFTER
#define PIN_DESK_TX 7   // -> desk HTX, RJ45 pin 4

#define PIN_BTN_UP 3     // momentary to GND
#define PIN_BTN_DOWN 4   // momentary to GND
#define PIN_BTN_STOP -1  // optional third button, e.g. 5; -1 disables

#define PIN_STATUS_LED -1  // XIAO ESP32C3 has no user-controllable LED

#else
// --- 30-pin ESP-WROOM-32 devkit -------------------------------------------
// GPIO16/17 are UART2 and are free of strapping duty. GPIO32/33 are ordinary
// GPIOs with usable internal pullups. Avoid 6-11 (flash), 12 (strapping, must
// read low at boot), and 34-39 (input only, no internal pullup).
#define PIN_DESK_RX 16  // <- desk DTX, RJ45 pin 2, THROUGH A DIVIDER OR SHIFTER
#define PIN_DESK_TX 17  // -> desk HTX, RJ45 pin 4

#define PIN_BTN_UP 32    // momentary to GND
#define PIN_BTN_DOWN 33  // momentary to GND
#define PIN_BTN_STOP -1  // optional third button, e.g. 25; -1 disables

#define PIN_STATUS_LED 2  // onboard LED on most devkits
#endif

// ===========================================================================
// Chip differences
// ---------------------------------------------------------------------------
// UART: the classic ESP32 has three hardware UARTs and exposes Serial2. The
// C3 has two and exposes only Serial1. Either way the GPIO matrix lets us
// choose the pins, so only the object name differs.
//
// Cores: the C3 is single-core. The safety design never depended on true
// parallelism — the desk task and loopTask were already pinned to the same
// core, so what keeps buttons responsive is priority preemption, which works
// identically on one core. WiFi shares that core on the C3, which adds a
// little jitter; the 400 ms deadman has three orders of magnitude of margin.
// ===========================================================================
#if CONFIG_IDF_TARGET_ESP32C3
#define DESK_SERIAL Serial1
#define DESK_TASK_CORE 0
#else
#define DESK_SERIAL Serial2
#define DESK_TASK_CORE 1
#endif

// ===========================================================================
// Desk protocol / safety timings
// ===========================================================================

// Step packets are resent this often while a button is held. The real
// handset repeats at roughly this rate.
#define NUDGE_PERIOD_MS 150

// Deadman. If no fresh nudge request arrives within this window the driver
// sends STOP on its own. This is what makes held-button movement safe even
// if the caller crashes, the browser closes, or WiFi drops mid-hold.
#define NUDGE_DEADMAN_MS 400

// Absolute (go-to-height) move guards.
#define MAX_MOVE_MS 25000       // hard ceiling on any single move
#define STALL_TIMEOUT_MS 1500   // no height change while moving -> abort
#define GOTO_TOLERANCE_RAW 3    // 0.3 in: close enough, stop chasing

// The controller sleeps when the handset is idle. If we have not heard from
// it in this long, send a STOP first to wake it before the real command.
#define WAKE_IDLE_MS 5000

// Sanity window for accepted height reports, in raw protocol units
// (tenths of an inch in IN mode, millimetres in CM mode). Anything outside
// is treated as a corrupt frame. The wide upper bound covers CM mode.
#define HEIGHT_RAW_MIN 200
#define HEIGHT_RAW_MAX 1500

// Ask the controller to report in inches at boot. The raw height word means
// tenths-of-an-inch in IN mode and millimetres in CM mode, so pinning the
// unit makes the numbers deterministic. Set to 0 to leave the desk alone.
#define FORCE_UNITS_INCHES 1

// Button debounce: consecutive identical 5 ms samples required.
#define BTN_DEBOUNCE_SAMPLES 3

// ===========================================================================
// Identity / network
// ===========================================================================
#define DEVICE_HOSTNAME "desk"  // -> http://desk.local
#define DEVICE_NAME "Desk"

// Optional. If WIFI_SSID/WIFI_PASS are defined the firmware connects on its
// own and you never touch HomeSpan's WiFi setup flow. Leave undefined to
// configure over the serial console ('W' command) instead.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

// HomeKit setup code. Must be 8 digits and must not be a rejected trivial
// code (all-same digits, 12345678, 87654321, ...). The default is HomeSpan's
// public default; override it in secrets.h.
#ifndef HOMEKIT_PAIRING_CODE
#define HOMEKIT_PAIRING_CODE "46637726"
#endif
#define HOMEKIT_SETUP_ID "DESK"
