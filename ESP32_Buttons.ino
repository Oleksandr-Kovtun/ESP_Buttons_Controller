#include <WiFi.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// --- Конфігурація кнопок ---
const int buttonPins[] = {13, 12, 14, 27, 26, 25, 33, 32};
int lastStates[8];
String btnPaths[8];
WiFiUDP Udp;
AsyncWebServer server(80);

// --- Налаштування ---
String s_ssid, s_pass, s_ip;
int s_port = 8000;
int s_alive_int = 2;
bool s_alive_en = true;
unsigned long lastAlive = 0;

// --- Функція збереження в пам'ять ---
void saveSettings() {
  File f = LittleFS.open("/config.json", "w");
  JsonDocument doc;
  doc["ssid"] = s_ssid;
  doc["pass"] = s_pass;
  doc["ip"] = s_ip;
  doc["port"] = s_port;
  doc["a_en"] = s_alive_en;
  doc["a_int"] = s_alive_int;
  JsonArray btns = doc["btns"].to<JsonArray>();
  for(int i=0; i<8; i++) btns.add(btnPaths[i]);
  serializeJson(doc, f);
  f.close();
}

// --- Функція завантаження з пам'яті ---
void loadSettings() {
  if(!LittleFS.exists("/config.json")) {
    for(int i=0; i<8; i++) btnPaths[i] = "/b" + String(i+1);
    return;
  }
  File f = LittleFS.open("/config.json", "r");
  JsonDocument doc;
  deserializeJson(doc, f);
  s_ssid = doc["ssid"].as<String>();
  s_pass = doc["pass"].as<String>();
  s_ip = doc["ip"].as<String>();
  s_port = doc["port"] | 8000;
  s_alive_en = doc["a_en"] | true;
  s_alive_int = doc["a_int"] | 2;
  for(int i=0; i<8; i++) btnPaths[i] = doc["btns"][i].as<String>();
  f.close();
}

// --- HTML сторінка (Динамічна збірка) ---
void handleRoot(AsyncWebServerRequest *request) {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif; background:#f0f2f5; padding:15px;} .card{background:white; padding:15px; border-radius:10px; box-shadow:0 2px 10px rgba(0,0,0,0.1); max-width:450px; margin:auto;}";
  html += "input{width:95%; padding:10px; margin:5px 0; border:1px solid #ccc; border-radius:5px;} .btn{width:100%; padding:12px; background:#1a73e8; color:white; border:none; border-radius:6px; cursor:pointer; font-weight:bold; margin-top:10px;}</style></head><body>";
  html += "<div class='card'><h2>OSC Controller Setup</h2><form action='/save' method='POST'>";
  
  html += "<h4>WiFi & OSC Target</h4>";
  html += "<input type='text' name='ssid' placeholder='WiFi SSID' value='" + s_ssid + "'>";
  html += "<input type='text' name='pass' placeholder='Password' value='" + s_pass + "'>";
  html += "<input type='text' name='ip' placeholder='Target IP' value='" + s_ip + "'>";
  html += "<input type='number' name='port' placeholder='Port' value='" + String(s_port) + "'>";
  
  html += "<h4>Alive Settings</h4>";
  html += "Enable: <input type='checkbox' name='a_en' " + String(s_alive_en ? "checked" : "") + " style='width:auto;'><br>";
  html += "Interval (sec): <input type='number' name='a_int' value='" + String(s_alive_int) + "'>";
  
  html += "<h4>Button OSC Paths</h4>";
  for(int i=0; i<8; i++) {
    html += "<input type='text' name='b" + String(i) + "' value='" + btnPaths[i] + "'>";
  }
  
  html += "<button type='submit' class='btn'>SAVE & RESTART</button></form></div></body></html>";
  request->send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  
  if(!LittleFS.begin(true)) Serial.println("LittleFS Error");
  loadSettings();

  for(int i=0; i<8; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    lastStates[i] = HIGH;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32_OSC_CONFIG", ""); 
  
  if(s_ssid != "") {
    WiFi.begin(s_ssid.c_str(), s_pass.c_str());
    Serial.println("Connecting to WiFi...");
  }

  // Головна сторінка
  server.on("/", HTTP_GET, handleRoot);

  // Обробка збереження
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    if(request->hasArg("ssid")) s_ssid = request->arg("ssid");
    if(request->hasArg("pass")) s_pass = request->arg("pass");
    if(request->hasArg("ip")) s_ip = request->arg("ip");
    if(request->hasArg("port")) s_port = request->arg("port").toInt();
    
    s_alive_en = request->hasArg("a_en");
    if(request->hasArg("a_int")) s_alive_int = request->arg("a_int").toInt();

    for(int i=0; i<8; i++) {
      String key = "b" + String(i);
      if(request->hasArg(key.c_str())) btnPaths[i] = request->arg(key.c_str());
    }

    saveSettings();
    request->send(200, "text/plain", "Settings Saved. ESP32 is restarting...");
    delay(2000);
    ESP.restart();
  });

  server.begin();
  Serial.println("Server started on 192.168.4.1");
}

void loop() {
  // Логіка Alive (Serial)
  if(s_alive_en && (millis() - lastAlive > (unsigned long)s_alive_int * 1000)) {
    Serial.print("alive | IP: ");
    if(WiFi.status() == WL_CONNECTED) {
      Serial.print(WiFi.localIP().toString());
    } else {
      Serial.print("Not Connected (AP Mode: 192.168.4.1)");
    }
    Serial.write(13);
    lastAlive = millis();
  }

  // Обробка кнопок
  for(int i=0; i<8; i++) {
    int val = digitalRead(buttonPins[i]);
    if(val != lastStates[i]) {
      delay(25);
      if(digitalRead(buttonPins[i]) == val) {
        int state = (val == LOW) ? 1 : 0;
        
        // Вивід у Serial (EOL 13)
        Serial.print("b" + String(i+1) + "/" + String(state));
        Serial.write(13);
        
        // Відправка OSC
        if(WiFi.status() == WL_CONNECTED && s_ip != "") {
          OSCMessage msg(btnPaths[i].c_str());
          msg.add(state);
          Udp.beginPacket(s_ip.c_str(), s_port);
          msg.send(Udp);
          Udp.endPacket();
        }
        lastStates[i] = val;
      }
    }
  }

  // Команди через Serial
  if(Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if(cmd.equalsIgnoreCase("alive off")) { s_alive_en = false; Serial.println("Alive OFF"); }
    if(cmd.equalsIgnoreCase("alive on")) { s_alive_en = true; Serial.println("Alive ON"); }
    if(cmd.equalsIgnoreCase("reboot")) ESP.restart();
  }
}
