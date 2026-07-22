// ===========================================================================
// Fully Jarvis / Jiecang desk controller
//
// Task layout:
//
//   deskTask   priority 3   UART parse, button scan, all safety timers.
//                           Preempts loopTask, so a blocked WiFi stack
//                           cannot delay a button release or a deadman stop.
//   loopTask   priority 1   HomeSpan + web server (Arduino default)
//
// Both sit on the same core (see DESK_TASK_CORE), so the guarantee comes
// from priority preemption, not parallelism — which is why this behaves the
// same on the single-core ESP32-C3.
//
// The physical buttons are read inside deskTask and call the desk driver
// directly. They never touch WiFi, HomeKit or HTTP, and they keep working
// with the router unplugged, the firmware unpaired, or WiFi compiled out.
// ===========================================================================

#include <Arduino.h>

#include "JiecangDesk.h"
#include "config.h"

#if ENABLE_WIFI
#include <WiFi.h>
#include "WebUI.h"
#endif

#if ENABLE_HOMEKIT
#include "HomeKitDesk.h"
#endif

static jiecang::Desk desk;

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

struct Button {
  int pin;
  uint8_t stable = HIGH;   // debounced level (HIGH = released, pullup)
  uint8_t candidate = HIGH;
  uint8_t count = 0;

  void begin(int p) {
    pin = p;
    if (pin >= 0) pinMode(pin, INPUT_PULLUP);
  }

  // Sample every ~5 ms. Requires BTN_DEBOUNCE_SAMPLES consecutive agreeing
  // reads before accepting a change.
  void sample() {
    if (pin < 0) return;
    uint8_t raw = digitalRead(pin);
    if (raw != candidate) {
      candidate = raw;
      count = 0;
    } else if (stable != candidate && ++count >= BTN_DEBOUNCE_SAMPLES) {
      stable = candidate;
    }
  }

  bool down() const { return pin >= 0 && stable == LOW; }
};

static Button btnUp, btnDown, btnStop;
static bool anyWasDown = false;

static void scanButtons() {
  btnUp.sample();
  btnDown.sample();
  btnStop.sample();

  bool u = btnUp.down(), d = btnDown.down();

  if (btnStop.down() || (u && d)) {
    // Explicit stop, or both buttons at once (the panic gesture when only
    // two buttons are fitted).
    if (anyWasDown) {
      desk.stop();
      anyWasDown = false;
    }
    return;
  }

  if (u) {
    desk.nudgeUp();  // refreshes the deadman; driver paces the actual packets
    anyWasDown = true;
  } else if (d) {
    desk.nudgeDown();
    anyWasDown = true;
  } else if (anyWasDown) {
    // Release. The deadman would catch this within NUDGE_DEADMAN_MS anyway;
    // sending STOP now just makes it crisp.
    desk.stop();
    anyWasDown = false;
  }
}

// ---------------------------------------------------------------------------
// Realtime task
// ---------------------------------------------------------------------------

static void deskTask(void *) {
  const TickType_t period = pdMS_TO_TICKS(5);
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    scanButtons();
    desk.tick();
    vTaskDelayUntil(&last, period);
  }
}

// ---------------------------------------------------------------------------
// Serial console (stages 1 and 2 only; HomeSpan owns Serial in stage 3)
// ---------------------------------------------------------------------------

#if !ENABLE_HOMEKIT
static void console() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  char c = line[0];
  if (c == 'u') {
    // One second of held-up, honouring the same deadman path as a button.
    uint32_t end = millis() + 1000;
    while (millis() < end) { desk.nudgeUp(); delay(50); }
    desk.stop();
  } else if (c == 'd') {
    uint32_t end = millis() + 1000;
    while (millis() < end) { desk.nudgeDown(); delay(50); }
    desk.stop();
  } else if (c == 's') {
    desk.stop();
  } else if (c == 'g') {
    float pct = line.substring(1).toFloat();
    Serial.printf("goto %.0f%% -> %s\n", pct, desk.gotoPercent(pct) ? "ok" : "REFUSED (no limits yet)");
  } else if (c == 'p') {
    desk.gotoPreset((uint8_t)line.substring(1).toInt());
  } else if (c == '?') {
    Serial.printf(
        "height=%.1f in  pct=%.1f  limits=%.1f..%.1f  motion=%d  link=%d  "
        "frames ok=%lu bad=%lu  fault=%s\n",
        desk.heightInches(), desk.percent(), desk.limitMin() / 10.0f,
        desk.limitMax() / 10.0f, (int)desk.motion(), (int)desk.linkUp(),
        (unsigned long)desk.framesGood(), (unsigned long)desk.framesBad(),
        desk.fault() ? desk.fault() : "none");
  } else {
    Serial.println("u=up 1s  d=down 1s  s=stop  g<pct>=goto  p<n>=preset  ?=status");
  }
}
#endif

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);

  btnUp.begin(PIN_BTN_UP);
  btnDown.begin(PIN_BTN_DOWN);
  btnStop.begin(PIN_BTN_STOP);

  desk.begin(DESK_SERIAL, PIN_DESK_RX, PIN_DESK_TX);

  // Start the safety-critical task before any radio comes up, so buttons are
  // live even if WiFi association hangs.
  xTaskCreatePinnedToCore(deskTask, "desk", 4096, nullptr, 3, nullptr, DESK_TASK_CORE);

#if ENABLE_HOMEKIT
#if PIN_STATUS_LED >= 0
  homeSpan.setStatusPin(PIN_STATUS_LED);
#endif
  homeSpan.setPairingCode(HOMEKIT_PAIRING_CODE);
  homeSpan.setQRID(HOMEKIT_SETUP_ID);
  homeSpan.setHostNameSuffix("");
  // No enableOTA(): the huge_app partition table has no OTA slot, and
  // flashing is over USB anyway.
#if defined(WIFI_SSID) && defined(WIFI_PASS)
  homeSpan.setWifiCredentials(WIFI_SSID, WIFI_PASS);
#endif
  // Fires on every (re)connection, not just the first, so guard the
  // one-time server and mDNS setup.
  homeSpan.setConnectionCallback([](int) {
    static bool started = false;
    if (started) return;
    started = true;
    webui::begin(&desk);
  });

  homeSpan.begin(Category::WindowCoverings, DEVICE_NAME, DEVICE_HOSTNAME);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name(DEVICE_NAME);
  new Characteristic::Manufacturer("Jiecang");
  new Characteristic::Model("Fully Jarvis");
  new Characteristic::SerialNumber("desk-01");
  new Characteristic::FirmwareRevision("1.0");
  new DeskCover(&desk);

#elif ENABLE_WIFI
#if PIN_STATUS_LED >= 0
  pinMode(PIN_STATUS_LED, OUTPUT);
#endif
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
#if defined(WIFI_SSID) && defined(WIFI_PASS)
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("wifi");
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
#else
#error "Stage 2 (ENABLE_WIFI 1, ENABLE_HOMEKIT 0) needs WIFI_SSID / WIFI_PASS in src/secrets.h"
#endif
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("http://%s/  (or http://" DEVICE_HOSTNAME ".local/)\n",
                  WiFi.localIP().toString().c_str());
    webui::begin(&desk);
  } else {
    Serial.println("wifi failed; buttons still work");
  }

#else
#if PIN_STATUS_LED >= 0
  pinMode(PIN_STATUS_LED, OUTPUT);
#endif
  Serial.println("stage 1: buttons + serial only. '?' for status.");
#endif
}

void loop() {
#if ENABLE_HOMEKIT
  homeSpan.poll();
#endif
#if ENABLE_WIFI
  webui::handle();
#endif
#if !ENABLE_HOMEKIT
  console();
#if PIN_STATUS_LED >= 0
  // Slow blink = talking to the desk, fast blink = no link.
  digitalWrite(PIN_STATUS_LED, desk.linkUp() ? ((millis() / 1000) & 1) : ((millis() / 150) & 1));
#endif
#endif
}
