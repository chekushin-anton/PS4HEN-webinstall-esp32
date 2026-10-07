#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <SPIFFS.h>

const char *WIFI_SSID = "ps4hen";
const char *WIFI_PASSWORD = "88880000";
const char *AP_SSID = "ps4hen";
const char *AP_PASSWORD = "88880000";
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

WebServer server(80);
bool accessPointMode = false;
const char *activeFS = "";

bool fsBegin() {
  if (LittleFS.begin(false)) {
    activeFS = "littlefs";
    Serial.println("[FS] LittleFS mounted");
    return true;
  }
  Serial.println("[FS] LittleFS not found");

  if (SPIFFS.begin(false)) {
    activeFS = "spiffs";
    Serial.println("[FS] SPIFFS mounted");
    return true;
  }
  Serial.println("[FS] SPIFFS not found");

  Serial.println("[FS] Formatting as LittleFS...");
  if (LittleFS.begin(true)) {
    activeFS = "littlefs";
    Serial.println("[FS] LittleFS formatted and mounted");
    return true;
  }
  Serial.println("[FS] ERROR: cannot mount or format");
  return false;
}

bool fsExists(const String &path) {
  if (strcmp(activeFS, "littlefs") == 0) return LittleFS.exists(path);
  if (strcmp(activeFS, "spiffs")   == 0) return SPIFFS.exists(path);
  return false;
}

File fsOpen(const String &path, const char *mode) {
  if (strcmp(activeFS, "littlefs") == 0) return LittleFS.open(path, mode);
  if (strcmp(activeFS, "spiffs")   == 0) return SPIFFS.open(path, mode);
  return File();
}

size_t fsTotal() {
  if (strcmp(activeFS, "littlefs") == 0) return LittleFS.totalBytes();
  if (strcmp(activeFS, "spiffs")   == 0) return SPIFFS.totalBytes();
  return 0;
}

size_t fsUsed() {
  if (strcmp(activeFS, "littlefs") == 0) return LittleFS.usedBytes();
  if (strcmp(activeFS, "spiffs")   == 0) return SPIFFS.usedBytes();
  return 0;
}

const char *contentTypeFor(const String &path) {
  if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js"))  return "application/javascript; charset=utf-8";
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
  if (!fsExists(path)) return false;

  File file = fsOpen(path, "r");
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
  json += ",\"fs\":\"" + String(activeFS) + "\"";
  json += ",\"fs_total\":" + String(fsTotal());
  json += ",\"fs_used\":" + String(fsUsed());
  json += ",\"free_heap\":" + String(ESP.getFreeHeap());
  json += "}";
  sendJson(200, json);
}

void handleDebug() {
  String out = "Active FS: " + String(activeFS) + "\n\n";
  out += "Files:\n";

  File root;
  if (strcmp(activeFS, "littlefs") == 0) root = LittleFS.open("/");
  else if (strcmp(activeFS, "spiffs") == 0) root = SPIFFS.open("/");

  if (!root || !root.isDirectory()) {
    out += "(cannot open root)\n";
  } else {
    File f = root.openNextFile();
    int count = 0;
    while (f) {
      out += "  " + String(f.name()) + " (" + String(f.size()) + " bytes)\n";
      f = root.openNextFile();
      count++;
    }
    if (count == 0) out += "(empty)\n";
  }

  out += "\nTotal: " + String(fsTotal()) + " bytes\n";
  out += "Used:  " + String(fsUsed()) + " bytes\n";
  server.send(200, "text/plain; charset=utf-8", out);
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
  server.on("/debug", HTTP_GET, handleDebug);

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
  Serial.println("\nESP32-S3 Universal FS Web Server starting...");

  fsBegin();

  Serial.printf("Active FS: %s | Used: %u / %u bytes\n",
                activeFS, (unsigned)fsUsed(), (unsigned)fsTotal());

  connectToWiFi();
  setupRoutes();
  server.begin();
  Serial.println("HTTP server started on port 80");
  Serial.println("Debug: http://<ip>/debug");
}

void loop() {
  server.handleClient();
}