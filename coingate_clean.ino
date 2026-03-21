#include <WiFi.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>

// LED pin (built-in LED on most ESP32 boards)
#define LED_PIN 2

// WiFi and MikroTik configuration
String wifiSSID = "";
String wifiPass = "";
String mikrotikIP = "";
String mikrotikUser = "";
String mikrotikPass = "";
uint16_t mikrotikPort = 8728;

// ESP32 web admin configuration
String espAdminUser = "";
String espAdminPass = "";
bool webAdminEnabled = true;

// Preferences namespace
Preferences prefs;

// Web server (runs on port 80)
AsyncWebServer server(80);

// Connection state
enum ConnectionState {
  DISCONNECTED,
  CONNECTING_WIFI,
  AUTHENTICATING_API,
  READY,
  CONFIG_MODE
};
ConnectionState connectionState = DISCONNECTED;

// LED state patterns (for visual feedback without serial)
enum LEDState {
  LED_OFF,
  LED_ON,
  LED_SLOW_BLINK,   // 1 Hz - Connecting to WiFi
  LED_FAST_BLINK,   // 5 Hz - Authenticating with MikroTik API
  LED_SOLID_ON,     // Connected and Ready
  LED_DOUBLE_BLINK  // Configuration Mode (AP)
};
LEDState currentLEDState = LED_OFF;
unsigned long lastLEDChange = 0;
int blinkCount = 0;
bool ledOn = false;

// Forward declarations
bool connectToWiFi();
bool connectToMikroTikAPI();
void setupWebServer();
void updateLED();
void saveCredentials();
void loadCredentials();

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  // Initialize preferences
  prefs.begin("mikrotik-config", false);
  
  // Load credentials from NVS
  loadCredentials();
  
  // Start WiFi in AP mode for configuration if WiFi credentials missing
  if (wifiSSID.length() == 0 || wifiPass.length() == 0) {
    Serial.println("WiFi credentials missing, starting in AP mode");
    connectionState = CONFIG_MODE;
    
    // Start AP
    WiFi.softAP("HotspotConfig", "admin123");
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    
    currentLEDState = LED_DOUBLE_BLINK;
    
    // Setup web server for configuration
    setupWebServer();
  } else {
    // Try to connect to MikroTik network
    if (connectToWiFi()) {
      if (connectToMikroTikAPI()) {
        // Success
      } else {
        // Fall back to AP mode
        connectionState = CONFIG_MODE;
        WiFi.softAP("HotspotConfig", "admin123");
        IPAddress IP = WiFi.softAPIP();
        Serial.print("AP IP address: ");
        Serial.println(IP);
        
        currentLEDState = LED_DOUBLE_BLINK;
        setupWebServer();
      }
    } else {
      // Fall back to AP mode
      connectionState = CONFIG_MODE;
      WiFi.softAP("HotspotConfig", "admin123");
      IPAddress IP = WiFi.softAPIP();
      Serial.print("AP IP address: ");
      Serial.println(IP);
      
      currentLEDState = LED_DOUBLE_BLINK;
      setupWebServer();
    }
  }
}

void loop() {
  updateLED(); // Update LED state non-blockingly
  
  // Main logic would go here when in READY state
  // For now, just handle web server requests (handled by AsyncWebServer)
  
  delay(10); // Small delay to prevent watchdog timeout
}

bool connectToWiFi() {
  connectionState = CONNECTING_WIFI;
  currentLEDState = LED_SLOW_BLINK;
  
  Serial.println("Connecting to WiFi...");
  Serial.print("SSID: ");
  Serial.println(wifiSSID);
  
  WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nWiFi connection failed");
    WiFi.disconnect();
    return false;
  }
}

bool connectToMikroTikAPI() {
  connectionState = AUTHENTICATING_API;
  currentLEDState = LED_FAST_BLINK;
  
  // TODO: Implement actual TCP connection to MikroTik API
  // For now, we'll simulate a connection attempt
  Serial.println("Connecting to MikroTik API...");
  Serial.print("MikroTik IP: ");
  Serial.println(mikrotikIP);
  Serial.print("Port: ");
  Serial.println(mikrotikPort);
  Serial.print("User: ");
  Serial.println(mikrotikUser);
  
  // Simulate delay
  delay(2000);
  
  // For now, assume success if we have credentials
  // In reality, we would attempt a TCP connection and then authenticate
  if (mikrotikIP.length() > 0 && mikrotikUser.length() > 0 && mikrotikPass.length() > 0) {
    // Simulate random failure for demonstration (remove in real implementation)
    // if (random(0, 10) < 3) { // 30% chance of failure
    //   Serial.println("MikroTik API connection failed (simulated)");
    //   return false;
    // }
    
    Serial.println("MikroTik API connected (simulated)");
    connectionState = READY;
    currentLEDState = LED_SOLID_ON;
    return true;
  } else {
    Serial.println("MikroTik API credentials missing");
    connectionState = DISCONNECTED;
    currentLEDState = LED_OFF;
    return false;
  }
}

void setupWebServer() {
  // Serve root page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<h1>ESP32 MikroTik Hotspot Manager</h1>";
    html += "<p>Current state: ";
    switch(connectionState) {
      case DISCONNECTED: html += "Disconnected"; break;
      case CONNECTING_WIFI: html += "Connecting WiFi"; break;
      case AUTHENTICATING_API: html += "Authenticating API"; break;
      case READY: html += "Ready"; break;
      case CONFIG_MODE: html += "Configuration Mode"; break;
    }
    html += "</p>";
    
    if (connectionState == CONFIG_MODE) {
      html += "<p>Connect to WiFi network: HotspotConfig</p>";
      html += "<p>Password: admin123</p>";
      html += "<p>Then visit 192.168.4.1 to configure</p>";
      html += "<h2>Configuration</h2>";
      html += "<form action='/save' method='POST'>";
      html += "WiFi SSID: <input type='text' name='wifiSSID'><br>";
      html += "WiFi Password: <input type='password' name='wifiPass'><br>";
      html += "MikroTik IP: <input type='text' name='mikrotikIP' value='192.168.1.1'><br>";
      html += "MikroTik Port: <input type='number' name='mikrotikPort' value='8728'><br>";
      html += "MikroTik Username: <input type='text' name='mikrotikUser'><br>";
      html += "MikroTik Password: <input type='password' name='mikrotikPass'><br>";
      html += "ESP Admin Username: <input type='text' name='espAdminUser'><br>";
      html += "ESP Admin Password: <input type='password' name='espAdminPass'><br>";
      html += "<input type='submit' value='Save and Reboot'>";
      html += "</form>";
    }
    
    request->send(200, "text/html", html);
  });
  
  // Handle form submission
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        // Handle each parameter
        if(p->name() == "wifiSSID") {
          wifiSSID = p->value().c_str();
        } else if(p->name() == "wifiPass") {
          wifiPass = p->value().c_str();
        } else if(p->name() == "mikrotikIP") {
          mikrotikIP = p->value().c_str();
        } else if(p->name() == "mikrotikPort") {
          mikrotikPort = p->value().toInt();
        } else if(p->name() == "mikrotikUser") {
          mikrotikUser = p->value().c_str();
        } else if(p->name() == "mikrotikPass") {
          mikrotikPass = p->value().c_str();
        } else if(p->name() == "espAdminUser") {
          espAdminUser = p->value().c_str();
        } else if(p->name() == "espAdminPass") {
          espAdminPass = p->value().c_str();
        }
        Serial.printf("POST [%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }
    
    // Save credentials to NVS
    saveCredentials();
    
    request->send(200, "text/html", "Settings saved. Rebooting...");
    delay(3000); // Give time for response to send
    ESP.restart();
  });
  
  // Handle ESP admin configuration (if needed)
  server.on("/admin", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<h1>ESP Admin Setup</h1>";
    html += "<form action='/admin/save' method='POST'>";
    html += "Admin Username: <input type='text' name='espAdminUser'><br>";
    html += "Admin Password: <input type='password' name='espAdminPass'><br>";
    html += "<input type='submit' value='Save'>";
    html += "</form>";
    request->send(200, "text/html", html);
  });
  
  server.on("/admin/save", HTTP_POST, [](AsyncWebServerRequest *request){
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        if(p->name() == "espAdminUser") {
          espAdminUser = p->value().c_str();
        } else if(p->name() == "espAdminPass") {
          espAdminPass = p->value().c_str();
        }
        Serial.printf("POST [%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }
    
    // Save credentials to NVS
    saveCredentials();
    
    request->send(200, "text/html", "Admin credentials saved.");
  });
  
  // Not found handler
  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "Not found");
  });
  
  server.begin();
  Serial.println("HTTP server started");
}

void saveCredentials() {
  prefs.begin("mikrotik-config", false);
  
  prefs.putString("wifiSSID", wifiSSID);
  prefs.putString("wifiPass", wifiPass);
  prefs.putString("mikrotikIP", mikrotikIP);
  prefs.putUShort("mikrotikPort", mikrotikPort);
  prefs.putString("mikrotikUser", mikrotikUser);
  prefs.putString("mikrotikPass", mikrotikPass);
  prefs.putString("espAdminUser", espAdminUser);
  prefs.putString("espAdminPass", espAdminPass);
  prefs.putBool("webAdmin", webAdminEnabled);
  
  prefs.end();
  
  Serial.println("Credentials saved to NVS");
}

void loadCredentials() {
  wifiSSID = prefs.getString("wifiSSID", "");
  wifiPass = prefs.getString("wifiPass", "");
  mikrotikIP = prefs.getString("mikrotikIP", "");
  mikrotikPort = prefs.getUShort("mikrotikPort", 8728);
  mikrotikUser = prefs.getString("mikrotikUser", "");
  mikrotikPass = prefs.getString("mikrotikPass", "");
  espAdminUser = prefs.getString("espAdminUser", "");
  espAdminPass = prefs.getString("espAdminPass", "");
  webAdminEnabled = prefs.getBool("webAdmin", true);
  
  Serial.println("Credentials loaded from NVS");
  Serial.printf("WiFi SSID: %s\n", wifiSSID.c_str());
  Serial.printf("MikroTik IP: %s\n", mikrotikIP.c_str());
  Serial.printf("ESP Admin User: %s\n", espAdminUser.c_str());
}

void updateLED() {
  unsigned long now = millis();
  
  switch(currentLEDState) {
    case LED_OFF:
      digitalWrite(LED_PIN, LOW);
      ledOn = false;
      break;
      
    case LED_ON:
      digitalWrite(LED_PIN, HIGH);
      ledOn = true;
      break;
      
    case LED_SLOW_BLINK: // 1 Hz
      if (now - lastLEDChange >= 500) {
        lastLEDChange = now;
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
      }
      break;
      
    case LED_FAST_BLINK: // 5 Hz
      if (now - lastLEDChange >= 100) {
        lastLEDChange = now;
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
      }
      break;
      
    case LED_SOLID_ON:
      digitalWrite(LED_PIN, HIGH);
      ledOn = true;
      break;
      
    case LED_DOUBLE_BLINK: // Two quick blinks then pause
      {
        static int blinkPhase = 0;
        static unsigned long phaseStart = 0;
        
        if (now - phaseStart >= (blinkPhase < 2 ? 100 : 800)) {
          phaseStart = now;
          blinkPhase++;
          
          if (blinkPhase >= 4) { // Two blinks (on-off-on-off) then wait
            blinkPhase = 0;
          }
          
          ledOn = (blinkPhase % 2 == 1); // On during odd phases
          digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
        }
      }
      break;
  }
}