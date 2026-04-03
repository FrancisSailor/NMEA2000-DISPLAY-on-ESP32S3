/*
  ESP32-S3 FFat Web Uploader
  - Folder support
  - Recursive tree view with collapsible folders
  - Erase files (recursive, keeps folders)
  - Format FFat

  Requirements:
    - ESP32-S3
    - Arduino-ESP32 core 3.x
    - Partition scheme with a DATA,FAT FFat partition (label "ffat")
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "FFat.h"

// ==== WiFi AP config ====
const char* AP_SSID = "ESP32S3_FFAT_UPLOAD";
const char* AP_PASS = "12345678";  // min 8 chars

WebServer server(80);

// Forward declarations
String listDirRecursive(const String& dirname, int level);
String listFilesTree();
bool eraseAllFilesOnFFat();
bool eraseInDir(const String& dirname);

// ======= HTML page =======
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>ESP32-S3 FFat Uploader</title>
  <style>
    body { font-family: sans-serif; margin: 20px; }
    h1 { font-size: 20px; }
    .box {
      border: 1px solid #ccc;
      padding: 15px;
      border-radius: 8px;
      max-width: 700px;
      margin-bottom: 15px;
    }
    input[type=file] { margin: 10px 0; }
    input[type=text] { padding: 4px 6px; width: 250px; max-width: 100%; }
    button { padding: 6px 14px; margin-top: 6px; cursor: pointer; }
    ul { list-style: none; padding-left: 16px; }
    li { padding: 2px 0; }
    li.err { color: #b00; }
    .hint { font-size: 11px; color: #666; }

    /* Tree / collapsible folders */
    li.dir { margin: 2px 0; }
    .dir-label {
      cursor: pointer;
      user-select: none;
      font-weight: 600;
      color: #004a8f;
    }
    .dir-label::before {
      content: "▶";
      display: inline-block;
      margin-right: 4px;
      font-size: 9px;
      transform-origin: center;
      transition: transform 0.12s ease-out;
    }
    .dir-label.open::before {
      transform: rotate(90deg);
    }
    .children {
      margin-left: 16px;
      display: none;
    }
    li.dir.open > .children {
      display: block;
    }
  </style>
</head>
<body>
  <h1>ESP32-S3 FFat Uploader</h1>

  <div class="box">
    <p>Verbonden met: <b>ESP32S3_FFAT_UPLOAD</b></p>
    <p>Selecteer één of meerdere bestanden en kies een doelmap.</p>
    <form method="POST" action="/upload" enctype="multipart/form-data">
      <label>Doelmap:</label><br>
      <input type="text" name="target" value="/" placeholder="/ of /mapnaam"><br>
      <div class="hint">
        Voorbeelden: <code>/</code>, <code>/assets</code>, <code>/audio/sets</code>.<br>
        De map moet bestaan of wordt (indien mogelijk) aangemaakt.
      </div>
      <br>
      <input type="file" name="file" multiple><br>
      <button type="submit">Upload</button>
    </form>
  </div>

  <div class="box">
    <h2>Mappen beheren</h2>
    <form method="POST" action="/mkdir">
      <label>Nieuwe mapnaam:</label><br>
      <input type="text" name="dirname" placeholder="bijv. assets of audio/sets" required>
      <div class="hint">
        Geen <code>..</code> gebruiken. Geneste paden zijn toegestaan.
      </div>
      <button type="submit">Map aanmaken</button>
    </form>
  </div>

  <div class="box">
    <h2>FFat beheer</h2>
    <form method="POST" action="/erase"
          onsubmit="return confirm('Alle bestanden in FFat wissen (mappen blijven bestaan)?');">
      <button type="submit">Alle bestanden wissen</button>
    </form>
    <form method="POST" action="/format"
          onsubmit="return confirm('FFat volledig formatteren? ALLE data gaat verloren.');">
      <button type="submit">FFat formatteren</button>
    </form>
  </div>

  <h2>FFat inhoud (volledige boomstructuur)</h2>
  <ul id="filelist">
  %FILE_LIST%
  </ul>

  <script>
  document.addEventListener('DOMContentLoaded', function () {
    document.querySelectorAll('li.dir > .dir-label').forEach(function (label) {
      label.addEventListener('click', function () {
        var li = label.parentElement;
        var isOpen = li.classList.contains('open');
        if (isOpen) {
          li.classList.remove('open');
          label.classList.remove('open');
        } else {
          li.classList.add('open');
          label.classList.add('open');
        }
      });
    });
  });
  </script>
</body>
</html>
)HTML";

// ======= Helpers =======

// Normalize a directory-like string to a clean absolute form
String normDir(const String& in)
{
  String d = in;
  if (d.length() == 0) d = "/";
  if (!d.startsWith("/")) d = "/" + d;
  while (d.endsWith("/") && d.length() > 1) {
    d.remove(d.length() - 1);
  }
  return d;
}

// Build absolute path from parent dir + entry name
String makeFullPath(const String& parentDir, const String& entryName)
{
  if (entryName.startsWith("/")) {
    // Some cores already return absolute paths
    return entryName;
  }
  String parent = normDir(parentDir);
  if (parent == "/") {
    return "/" + entryName;
  }
  return parent + "/" + entryName;
}

// Recursive directory listing with collapsible markup
String listDirRecursive(const String& dirname, int level)
{
  String out;
  String dirPath = normDir(dirname);

  File dir = FFat.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    return out;
  }

  File entry = dir.openNextFile();
  if (!entry) {
    if (level == 0) {
      out += "<li>(FFat is leeg)</li>";
    }
    dir.close();
    return out;
  }

  while (entry) {
    String name = String(entry.name());
    String fullPath = makeFullPath(dirPath, name);

    if (entry.isDirectory()) {
      int slash = fullPath.lastIndexOf('/');
      String label = (slash >= 0 && slash < (int)fullPath.length() - 1)
                     ? fullPath.substring(slash + 1)
                     : fullPath;

      bool openByDefault = (level == 0); // top-level dirs expanded on load

      out += "<li class='dir";
      if (openByDefault) out += " open";
      out += "'>";

      out += "<span class='dir-label";
      if (openByDefault) out += " open";
      out += "'>";
      out += label;
      out += "</span>";

      out += "<ul class='children'>";

      String inner = listDirRecursive(fullPath, level + 1);
      if (inner.length() == 0) {
        inner = "<li class='hint'>(leeg)</li>";
      }
      out += inner;

      out += "</ul></li>";
    } else {
      int slash = fullPath.lastIndexOf('/');
      String label = (slash >= 0 && slash < (int)fullPath.length() - 1)
                     ? fullPath.substring(slash + 1)
                     : fullPath;

      out += "<li>";
      out += label;
      out += " (";
      out += (unsigned long)entry.size();
      out += " bytes)";
      if (level > 0 || dirPath != "/") {
        out += " <span class='hint'>";
        out += fullPath;
        out += "</span>";
      }
      out += "</li>";
    }

    entry = dir.openNextFile();
  }

  dir.close();
  return out;
}

String listFilesTree()
{
  String out = listDirRecursive("/", 0);
  if (out.length() == 0) {
    out = "<li>(FFat is leeg)</li>";
  }
  return out;
}

// Recursive delete of files only; keep directories
bool eraseInDir(const String& dirname)
{
  String dirPath = normDir(dirname);
  File dir = FFat.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    Serial.printf("Erase: kan dir niet openen: %s\n", dirPath.c_str());
    return false;
  }

  bool ok = true;
  File entry = dir.openNextFile();
  while (entry) {
    String name = String(entry.name());
    String fullPath = makeFullPath(dirPath, name);

    if (entry.isDirectory()) {
      if (!eraseInDir(fullPath)) {
        ok = false;
      }
    } else {
      Serial.printf("Erase: verwijder %s\n", fullPath.c_str());
      if (!FFat.remove(fullPath)) {
        Serial.printf("Erase: verwijderen mislukt voor %s\n", fullPath.c_str());
        ok = false;
      }
    }

    entry = dir.openNextFile();
  }

  dir.close();
  return ok;
}

bool eraseAllFilesOnFFat()
{
  return eraseInDir("/");
}

// ======= HTTP handlers =======

void handleRoot()
{
  String html = INDEX_HTML;
  html.replace("%FILE_LIST%", listFilesTree());
  server.send(200, "text/html", html);
}

void handleNotFound()
{
  server.send(404, "text/plain", "Not found");
}

void handleErase()
{
  Serial.println("HTTP: verzoek om alle FFat-bestanden te wissen (mappen blijven).");

  if (!eraseAllFilesOnFFat()) {
    server.send(500, "text/plain", "Fout bij wissen FFat-bestanden. Zie seriële monitor.");
    return;
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleFormat()
{
  Serial.println("HTTP: verzoek om FFat te formatteren");

  FFat.end();

  if (!FFat.format()) {
    Serial.println("FFat.format() mislukt");

    if (!FFat.begin(false, "/ffat", 10, "ffat")) {
      Serial.println("FFat remount na mislukte format() ook mislukt");
    }

    server.send(500, "text/plain", "FFat formatteren mislukt. Zie seriële monitor.");
    return;
  }

  if (!FFat.begin(false, "/ffat", 10, "ffat")) {
    Serial.println("FFat remount na format() mislukt");
    server.send(500, "text/plain",
                "FFat geformatteerd, maar remount mislukt. Herstart de ESP32-S3.");
    return;
  }

  Serial.println("FFat succesvol geformatteerd en opnieuw gemount.");
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleMkdir()
{
  if (!server.hasArg("dirname")) {
    server.send(400, "text/plain", "Geen mapnaam ontvangen.");
    return;
  }

  String name = server.arg("dirname");
  name.trim();

  if (name.length() == 0) {
    server.send(400, "text/plain", "Lege mapnaam is ongeldig.");
    return;
  }

  if (name.indexOf("..") >= 0) {
    server.send(400, "text/plain", "Ongeldige mapnaam.");
    return;
  }

  String path = normDir(name);

  if (FFat.exists(path)) {
    server.send(409, "Map of bestand bestaat al.");
    return;
  }

  if (!FFat.mkdir(path)) {
    Serial.printf("Aanmaken map mislukt: %s\n", path.c_str());
    server.send(500, "Aanmaken map mislukt. Zie seriële monitor.");
    return;
  }

  Serial.printf("Map aangemaakt: %s\n", path.c_str());
  server.sendHeader("Location", "/");
  server.send(303);
}

// Upload handler (streaming, met doelmap)
void handleFileUpload()
{
  HTTPUpload& upload = server.upload();
  static File uploadFile;
  static String currentTargetDir = "/";

  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.length()) {
      Serial.println("Upload start zonder bestandsnaam");
      return;
    }

    // Target directory from form
    String targetDir = server.arg("target");
    targetDir.trim();
    if (targetDir == "" || targetDir == "/") {
      targetDir = "/";
    }
    currentTargetDir = normDir(targetDir);

    // Create target dir if needed
    if (!FFat.exists(currentTargetDir)) {
      if (!FFat.mkdir(currentTargetDir)) {
        Serial.printf("Kan doelmap %s niet maken; val terug op root.\n",
                      currentTargetDir.c_str());
        currentTargetDir = "/";
      }
    }

    String fullPath = makeFullPath(currentTargetDir, filename);

    Serial.printf("Upload start: %s -> %s\n",
                  filename.c_str(), fullPath.c_str());

    if (FFat.exists(fullPath)) {
      FFat.remove(fullPath);
    }

    uploadFile = FFat.open(fullPath, FILE_WRITE);
    if (!uploadFile) {
      Serial.println("Kon bestand niet openen voor schrijven");
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.printf("Upload gereed: %s (%u bytes)\n",
                    upload.filename.c_str(),
                    (unsigned)upload.totalSize);
    }
  }
  else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) {
      uploadFile.close();
    }
    Serial.println("Upload afgebroken");
  }
}

// ======= Setup & loop =======

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("ESP32-S3 FFat Web Uploader");
  Serial.println("--------------------------");

  // Mount FFat (format on first failure)
  if (!FFat.begin(false, "/ffat", 10, "ffat")) {
    Serial.println("FFat mount failed, trying to format...");
    if (!FFat.begin(true, "/ffat", 10, "ffat")) {
      Serial.println("FFat format failed. Halt.");
      while (true) {
        delay(1000);
      }
    }
  }
  Serial.println("FFat mounted.");

  // Start WiFi AP
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASS)) {
    Serial.println("Error starting softAP");
    while (true) {
      delay(1000);
    }
  }

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(ip);

  // Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/erase", HTTP_POST, handleErase);
  server.on("/format", HTTP_POST, handleFormat);
  server.on("/mkdir", HTTP_POST, handleMkdir);
  server.onNotFound(handleNotFound);

  // Upload endpoint
  server.on(
    "/upload",
    HTTP_POST,
    []() {
      server.sendHeader("Location", "/");
      server.send(303);
    },
    handleFileUpload
  );

  server.begin();
  Serial.println("HTTP server gestart.");
}

void loop()
{
  server.handleClient();
}
