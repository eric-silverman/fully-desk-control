#include "WebUI.h"

#include <ESPmDNS.h>
#include <WebServer.h>

#include "web_page.h"

namespace webui {

static WebServer server(80);
static jiecang::Desk *g_desk = nullptr;

static void sendJson(const String &s) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

static void handleState() {
  jiecang::Desk &d = *g_desk;

  String j = "{";
  if (d.heightKnown()) j += "\"height\":" + String(d.heightInches(), 1) + ",";
  else j += "\"height\":null,";

  float p = d.percent();
  j += isnan(p) ? String("\"pct\":null,") : "\"pct\":" + String(p, 1) + ",";

  if (d.limitsKnown()) {
    j += "\"min\":" + String(d.limitMin() / 10.0f, 1) + ",";
    j += "\"max\":" + String(d.limitMax() / 10.0f, 1) + ",";
  } else {
    j += "\"min\":null,\"max\":null,";
  }

  const char *m = d.motion() == jiecang::Motion::Up     ? "up"
                  : d.motion() == jiecang::Motion::Down ? "down"
                                                        : "idle";
  j += "\"motion\":\"" + String(m) + "\",";
  j += "\"link\":" + String(d.linkUp() ? "true" : "false") + ",";
  j += "\"moving\":" + String(d.moving() ? "true" : "false") + ",";
  j += d.fault() ? "\"fault\":\"" + String(d.fault()) + "\"," : String("\"fault\":null,");
  j += "\"rx_ok\":" + String(d.framesGood()) + ",";
  j += "\"rx_bad\":" + String(d.framesBad());
  j += "}";

  sendJson(j);
}

void begin(jiecang::Desk *desk) {
  g_desk = desk;

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "max-age=600");
    server.send_P(200, "text/html", PAGE_INDEX);
  });

  server.on("/api/state", HTTP_GET, handleState);

  // Hold-to-move. The page repeats this every 120 ms; the driver's deadman
  // (NUDGE_DEADMAN_MS) halts the desk if the repeats stop for any reason,
  // so a closed tab or dropped WiFi cannot leave the desk running.
  server.on("/api/nudge", HTTP_POST, []() {
    String dir = server.arg("dir");
    if (dir == "up") g_desk->nudgeUp();
    else if (dir == "down") g_desk->nudgeDown();
    else { server.send(400, "text/plain", "dir must be up|down"); return; }
    server.send(204, "text/plain", "");
  });

  server.on("/api/stop", HTTP_POST, []() {
    g_desk->stop();
    server.send(204, "text/plain", "");
  });

  server.on("/api/goto", HTTP_POST, []() {
    if (!server.hasArg("pct")) { server.send(400, "text/plain", "pct required"); return; }
    bool ok = g_desk->gotoPercent(server.arg("pct").toFloat());
    server.send(ok ? 204 : 409, "text/plain", ok ? "" : "desk limits not known yet");
  });

  server.on("/api/preset", HTTP_POST, []() {
    int n = server.arg("n").toInt();
    if (n < 1 || n > 4) { server.send(400, "text/plain", "n must be 1..4"); return; }
    g_desk->gotoPreset((uint8_t)n);
    server.send(204, "text/plain", "");
  });

  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });

  server.begin();

  if (MDNS.begin(DEVICE_HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

void handle() { server.handleClient(); }

}  // namespace webui
