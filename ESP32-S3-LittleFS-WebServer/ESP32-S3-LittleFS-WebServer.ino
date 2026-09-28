#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

// ==================== Wi-Fi settings ====================
// Fill in these values to use your existing Wi-Fi network.
const char *WIFI_SSID = "ps4hen";
const char *WIFI_PASSWORD = "88880000";

// If connecting to the network fails, the ESP32 starts this access point.
const char *AP_SSID = "ps4hen";
const char *AP_PASSWORD = "88880000";  // At least 8 characters.
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

WebServer server(80);
bool accessPointMode = false;

const char *contentTypeFor(const String &path) {
  if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gif")) return "image/gif";
  if (path.endsWith(".webp")) return "image/webp";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".woff")) return "font/woff";
  if (path.endsWith(".woff2")) return "font/woff2";
  if (path.endsWith(".ttf")) return "font/ttf";
  if (path.endsWith(".map")) return "application/json; charset=utf-8";
  if (path.endsWith(".txt")) return "text/plain; charset=utf-8";
  if (path.endsWith(".wasm")) return "application/wasm";
  return "application/octet-stream";
}

void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void sendJson(int statusCode, const String &body) {
  addCorsHeaders();
  server.send(statusCode, "application/json; charset=utf-8", body);
}

bool sendFile(const String &requestedPath) {
  String path = requestedPath;
  if (path == "/") path = "/index.html";
  if (!path.startsWith("/") || path.indexOf("..") >= 0 || !LittleFS.exists(path)) return false;

  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) return false;

  server.sendHeader("Cache-Control", "no-cache");
  server.streamFile(file, contentTypeFor(path));
  file.close();
  return true;
}

bool isSpaNavigation() {
  const String accept = server.header("Accept");
  const String path = server.uri();
  const int lastSlash = path.lastIndexOf('/');
  const int lastDot = path.lastIndexOf('.');
  return accept.indexOf("text/html") >= 0 || lastDot < lastSlash;
}

void handleStatus() {
  const IPAddress ip = accessPointMode ? WiFi.softAPIP() : WiFi.localIP();
  String json = "{\"mode\":\"";
  json += accessPointMode ? "AP" : "STA";
  json += "\",\"ip\":\"" + ip.toString();
  json += "\",\"ssid\":\"" + String(accessPointMode ? AP_SSID : WiFi.SSID());
  json += "\",\"rssi\":" + String(accessPointMode ? 0 : WiFi.RSSI());
  json += ",\"uptime_ms\":" + String(millis());
  json += ",\"free_heap\":" + String(ESP.getFreeHeap());
  json += ",\"littlefs_total\":" + String(LittleFS.totalBytes());
  json += ",\"littlefs_used\":" + String(LittleFS.usedBytes());
  json += "}";
  sendJson(200, json);
}

void handleHello() {
  sendJson(200, "{\"message\":\"Hello from ESP32-S3\"}");
}

void handleApiNotFound() {
  sendJson(404, "{\"error\":\"API endpoint not found\"}");
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
    Serial.println("Wi-Fi STA connected");
    Serial.print("Open: http://");
    Serial.println(WiFi.localIP());
    return;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("ERROR: Access point could not be started");
    return;
  }
  accessPointMode = true;
  Serial.println("Wi-Fi STA connection failed; access point started");
  Serial.print("AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());
}

void setupRoutes() {
  const char *headers[] = { "Accept" };
  server.collectHeaders(headers, 1);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/hello", HTTP_GET, handleHello);
  server.on("/api/status", HTTP_OPTIONS, []() {
    addCorsHeaders();
    server.send(204);
  });
  server.on("/api/hello", HTTP_OPTIONS, []() {
    addCorsHeaders();
    server.send(204);
  });

  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS && server.uri().startsWith("/api/")) {
      addCorsHeaders();
      server.send(204);
      return;
    }
    if (server.uri().startsWith("/api/")) {
      handleApiNotFound();
      return;
    }
    if (server.method() == HTTP_GET && sendFile(server.uri())) return;
    if (server.method() == HTTP_GET && isSpaNavigation() && sendFile("/index.html")) return;
    server.send(404, "text/plain; charset=utf-8", "404 Not Found");
  });
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nESP32-S3 LittleFS Web Server starting...");

  if (!LittleFS.begin(false)) {
    Serial.println("ERROR: LittleFS mount failed. Upload data/ or format the filesystem once.");
  } else {
    Serial.printf("LittleFS: %u / %u bytes used\n", LittleFS.usedBytes(), LittleFS.totalBytes());
  }

  connectToWiFi();
  setupRoutes();
  server.begin();
  Serial.println("HTTP server started on port 80");
}

void loop() {
  server.handleClient();
}
