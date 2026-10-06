#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
using HttpServer = ESP8266WebServer;
#else
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
using HttpServer = WebServer;
#endif

const char *WIFI_SSID = "ps4hen";
const char *WIFI_PASSWORD = "88880000";
const char *AP_SSID = "ps4hen";
const char *AP_PASSWORD = "88880000";
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

HttpServer server(80);
bool accessPointMode = false;

bool mountLittleFS(bool formatOnFailure) {
#if defined(ESP8266)
  if (LittleFS.begin()) return true;
  if (!formatOnFailure || !LittleFS.format()) return false;
  return LittleFS.begin();
#else
  return LittleFS.begin(formatOnFailure);
#endif
}

uint32_t littleFSTotalBytes() {
#if defined(ESP8266)
  FSInfo info;
  return LittleFS.info(info) ? info.totalBytes : 0;
#else
  return LittleFS.totalBytes();
#endif
}

uint32_t littleFSUsedBytes() {
#if defined(ESP8266)
  FSInfo info;
  return LittleFS.info(info) ? info.usedBytes : 0;
#else
  return LittleFS.usedBytes();
#endif
}

const char *contentTypeFor(const String &path) {
  if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js"))  return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".manifest")) return "text/cache-manifest; charset=utf-8";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gif")) return "image/gif";
  if (path.endsWith(".webp")) return "image/webp";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".woff")) return "font/woff";
  if (path.endsWith(".woff2")) return "font/woff2";
  if (path.endsWith(".ttf")) return "font/ttf";
  if (path.endsWith(".txt")) return "text/plain; charset=utf-8";
  if (path.endsWith(".wasm")) return "application/wasm";
  if (path.endsWith(".bin") || path.endsWith(".elf")) return "application/octet-stream";
  return "application/octet-stream";
}

void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void sendJson(int code, const String &body) {
  addCorsHeaders();
  server.send(code, "application/json; charset=utf-8", body);
}

bool sendFileFromFS(const String &requestedPath) {
  String path = requestedPath;
  if (path == "/") path = "/index.html";
  if (!path.startsWith("/") || path.indexOf("..") >= 0) return false;
  if (!LittleFS.exists(path)) return false;

  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) return false;

  server.sendHeader("Cache-Control", "no-cache");
  server.streamFile(file, contentTypeFor(path));
  file.close();
  return true;
}

void handleStatus() {
  const IPAddress ip = accessPointMode ? WiFi.softAPIP() : WiFi.localIP();
  String json = "{\"mode\":\"";
  json += accessPointMode ? "AP" : "STA";
  json += "\",\"ip\":\"" + ip.toString() + "\"";
  json += ",\"fs_total\":" + String(littleFSTotalBytes());
  json += ",\"fs_used\":" + String(littleFSUsedBytes());
  json += ",\"free_heap\":" + String(ESP.getFreeHeap());
  json += "}";
  sendJson(200, json);
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to Wi-Fi: %s", WIFI_SSID);

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    accessPointMode = false;
    Serial.print("Wi-Fi STA connected. Open: http://");
    Serial.println(WiFi.localIP());
    return;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("ERROR: AP could not be started");
    return;
  }
  accessPointMode = true;
  Serial.print("AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());
}

void setupRoutes() {
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/status", HTTP_OPTIONS, []() {
    addCorsHeaders();
    server.send(204);
  });

  server.onNotFound([]() {
    if (server.uri().startsWith("/api/")) {
      sendJson(404, "{\"error\":\"API not found\"}");
      return;
    }
    if (server.method() == HTTP_GET && sendFileFromFS(server.uri())) return;
    if (server.method() == HTTP_GET && sendFileFromFS("/index.html")) return;
    server.send(404, "text/plain", "404 Not Found");
  });
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nPS4 HEN Web Server starting...");

  if (!mountLittleFS(false)) {
    Serial.println("WARNING: LittleFS not mounted. Formatting...");
    if (!mountLittleFS(true)) {
      Serial.println("ERROR: LittleFS mount failed");
    }
  }
  Serial.printf("LittleFS: %u / %u bytes used\n",
                (unsigned)littleFSUsedBytes(), (unsigned)littleFSTotalBytes());

  connectToWiFi();
  setupRoutes();
  server.begin();
  Serial.println("HTTP server started on port 80");
}

void loop() {
  server.handleClient();
}
