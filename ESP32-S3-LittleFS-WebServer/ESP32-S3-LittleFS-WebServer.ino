#include <WiFi.h>
#include <WebServer.h>
#include <FFat.h>

// ==================== Wi-Fi settings ====================
const char *WIFI_SSID = "ps4hen";
const char *WIFI_PASSWORD = "88880000";
const char *AP_SSID = "ps4hen";
const char *AP_PASSWORD = "88880000";
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

WebServer server(80);
bool accessPointMode = false;

// Состояние текущей загрузки
File uploadFile;
String uploadPath;
bool uploadFailed = false;

// ==================== MIME ====================
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

// ==================== Helpers ====================

void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void sendJson(int code, const String &body) {
  addCorsHeaders();
  server.send(code, "application/json; charset=utf-8", body);
}

// Рекурсивное создание всех родительских папок в пути
void ensureDirs(const String &path) {
  int idx = 0;
  while (true) {
    idx = path.indexOf('/', idx + 1);
    if (idx < 0) break;
    String dir = path.substring(0, idx);
    if (dir.length() > 0 && !FFat.exists(dir)) {
      FFat.mkdir(dir);
    }
  }
}

bool sendFileFromFFat(const String &requestedPath) {
  String path = requestedPath;
  if (path == "/") path = "/index.html";
  if (!path.startsWith("/") || path.indexOf("..") >= 0) return false;
  if (!FFat.exists(path)) return false;

  File file = FFat.open(path, "r");
  if (!file || file.isDirectory()) return false;

  server.sendHeader("Cache-Control", "no-cache");
  server.streamFile(file, contentTypeFor(path));
  file.close();
  return true;
}

// ==================== API ====================

void handleStatus() {
  const IPAddress ip = accessPointMode ? WiFi.softAPIP() : WiFi.localIP();
  String json = "{\"mode\":\"";
  json += accessPointMode ? "AP" : "STA";
  json += "\",\"ip\":\"" + ip.toString() + "\"";
  json += ",\"fs_total\":" + String(FFat.totalBytes());
  json += ",\"fs_used\":" + String(FFat.usedBytes());
  json += ",\"free_heap\":" + String(ESP.getFreeHeap());
  json += "}";
  sendJson(200, json);
}

// Список всех файлов в FFat — плоский, построчно
void handleListFiles() {
  String out;
  File root = FFat.open("/");
  if (!root) { server.send(500, "text/plain", "cannot open root"); return; }

  std::function<void(File&, const String&)> walk = [&](File &dir, const String &prefix) {
    File f = dir.openNextFile();
    while (f) {
      String name = f.name();
      if (name.startsWith("/")) name = name.substring(1);
      String full = prefix + name;
      if (f.isDirectory()) {
        out += "[DIR]  " + full + "\n";
        walk(f, full + "/");
      } else {
        out += "       " + full + "  (" + String(f.size()) + ")\n";
      }
      f = dir.openNextFile();
    }
  };
  walk(root, "");
  root.close();

  if (out.length() == 0) out = "(empty)\n";
  server.send(200, "text/plain; charset=utf-8", out);
}

// Удаление одного файла: DELETE /delete?path=/ran/foo.js
void handleDelete() {
  if (!server.hasArg("path")) { sendJson(400, "{\"error\":\"missing path\"}"); return; }
  String p = server.arg("path");
  if (FFat.remove(p)) sendJson(200, "{\"ok\":true}");
  else sendJson(500, "{\"error\":\"remove failed\"}");
}

// Форматирование FFat (для отладки): GET /format
void handleFormat() {
  if (FFat.format()) server.send(200, "text/plain", "FFat formatted\n");
  else server.send(500, "text/plain", "format failed\n");
}

// ==================== Upload ====================

void handleUploadDone() {
  if (uploadFailed) {
    server.send(500, "text/plain", "upload failed\n");
  } else {
    server.send(200, "text/plain", "OK\n");
  }
}

void handleUploadStream() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadFailed = false;
    uploadPath = server.arg("path");
    if (!uploadPath.startsWith("/")) uploadPath = "/" + uploadPath;
    uploadPath.replace("//", "/");

    Serial.printf("[UPLOAD] start -> %s\n", uploadPath.c_str());

    ensureDirs(uploadPath);

    if (FFat.exists(uploadPath)) FFat.remove(uploadPath);
    uploadFile = FFat.open(uploadPath, "w");
    if (!uploadFile) {
      Serial.println("[UPLOAD] cannot open file for writing");
      uploadFailed = true;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && !uploadFailed) {
      size_t written = uploadFile.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize) {
        Serial.printf("[UPLOAD] write failed (%u/%u)\n",
                      (unsigned)written, (unsigned)upload.currentSize);
        uploadFailed = true;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    Serial.printf("[UPLOAD] end   %s (%u bytes)%s\n",
                  uploadPath.c_str(),
                  (unsigned)upload.totalSize,
                  uploadFailed ? "  [FAILED]" : "");
  }
}

// ==================== WiFi ====================

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

// ==================== Setup ====================

void setupRoutes() {
  const char *headers[] = { "Accept" };
  server.collectHeaders(headers, 1);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/list",   HTTP_GET, handleListFiles);
  server.on("/api/delete", HTTP_GET, handleDelete);
  server.on("/format",     HTTP_GET, handleFormat);

  // Загрузка файлов: POST /upload?path=ran/foo.js (multipart, поле "file")
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadStream);

  server.onNotFound([]() {
    if (server.uri().startsWith("/api/")) {
      sendJson(404, "{\"error\":\"API not found\"}");
      return;
    }
    if (server.method() == HTTP_GET && sendFileFromFFat(server.uri())) return;
    // SPA-фолбэк: неизвестные GET-пути отдаём как index.html
    if (server.method() == HTTP_GET && sendFileFromFFat("/index.html")) return;
    server.send(404, "text/plain", "404 Not Found");
  });
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nPS4HEN ESP32-S3 Web Installer starting...");

  if (!FFat.begin(false)) {
    Serial.println("WARNING: FFat not mounted. Formatting...");
    if (FFat.begin(true)) {
      Serial.println("FFat formatted and mounted");
    } else {
      Serial.println("ERROR: FFat mount failed");
    }
  } else {
    Serial.printf("FFat mounted: %u / %u bytes used\n",
                  (unsigned)FFat.usedBytes(), (unsigned)FFat.totalBytes());
  }

  connectToWiFi();
  setupRoutes();
  server.begin();
  Serial.println("HTTP server started on port 80");
  Serial.println("Upload files: see batch script on PC");
}

void loop() {
  server.handleClient();
}