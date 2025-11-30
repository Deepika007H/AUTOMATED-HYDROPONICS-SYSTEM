#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <Wire.h>

// === WiFi Credentials ===
const char* ssid = "pwm";
const char* password = "12345678";

// === Google Apps Script URL ===
const char* host = "script.google.com";
const int httpsPort = 443;
const char* googleScriptURL = "/macros/s/AKfycbx5rbm38N_jSkkmmTgEmfJUPnQHCi1JEH2XeCwWez7fqkgZsap1RrKz0JpGE3lCa2Qj4Q/exec";

// === Sensor Pins ===
#define DHTPIN D2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define PH_SENSOR_ANALOG A0
#define LDR_PIN D5
#define TDS_PIN D6
#define TANK_FULL_BUTTON D7

// === Relay Pins ===
#define RELAY_ABOVE_NOMINAL D1
#define RELAY_BELOW_NOMINAL D3
#define RELAY_PUMP D4

// === Blue LED Pin ===
#define BLUE_LED D8

ESP8266WebServer server(80);

float temperature, humidity;
int ldrValue, tdsRaw, phRaw;
float pH;
bool tankFull = false;
String ldrStatus;
String tdsStatus;

WiFiClientSecure client;

// === Timing Variables ===
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 5000;

void setup() {
  Serial.begin(9600);
  delay(100);

  // Relay setup
  pinMode(RELAY_ABOVE_NOMINAL, OUTPUT);
  pinMode(RELAY_BELOW_NOMINAL, OUTPUT);
  pinMode(RELAY_PUMP, OUTPUT);
  
  // Blue LED setup
  pinMode(BLUE_LED, OUTPUT);

  digitalWrite(RELAY_ABOVE_NOMINAL, HIGH);
  digitalWrite(RELAY_BELOW_NOMINAL, HIGH);
  digitalWrite(RELAY_PUMP, HIGH);
  digitalWrite(BLUE_LED, LOW); // LED OFF initially

  // Sensor setup
  dht.begin();
  pinMode(TANK_FULL_BUTTON, INPUT_PULLUP);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Web server routes
  server.on("/", handleRoot);
  server.on("/data", handleJSON);
  server.on("/pump", handlePumpToggle);
  server.begin();
  client.setInsecure();
}

void loop() {
  server.handleClient();

  if (millis() - lastUpdateTime >= updateInterval) {
    readSensors();
    controlRelays();
    sendToGoogleSheet();
    lastUpdateTime = millis();
  }
}

void readSensors() {
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  phRaw = analogRead(PH_SENSOR_ANALOG);
  pH = map(phRaw, 0, 1023, 0, 1400) / 100.0;

  ldrValue = analogRead(LDR_PIN);
  ldrStatus = (ldrValue < 500) ? "Yes" : "No";

  tdsRaw = analogRead(TDS_PIN);
  tdsStatus = (tdsRaw < 500) ? "Yes" : "No";

  tankFull = !digitalRead(TANK_FULL_BUTTON);

  Serial.println("Temperature: " + String(temperature));
  Serial.println("Humidity: " + String(humidity));
  Serial.println("PH: " + String(pH));
  Serial.println("Tank Full: " + String(tankFull ? "Yes" : "No"));
  Serial.println("LDR Light: " + ldrStatus);
  Serial.println("TDS Detected: " + tdsStatus);
}

void controlRelays() {
  // Auto pH Control
  if (pH > 14.5) {
    digitalWrite(RELAY_ABOVE_NOMINAL, LOW);  // ON
    digitalWrite(RELAY_BELOW_NOMINAL, HIGH); // OFF
  } else if (pH < 12.0) {
    digitalWrite(RELAY_ABOVE_NOMINAL, HIGH); // OFF
    digitalWrite(RELAY_BELOW_NOMINAL, LOW);  // ON
  } else {
    digitalWrite(RELAY_ABOVE_NOMINAL, HIGH);
    digitalWrite(RELAY_BELOW_NOMINAL, HIGH);
  }

  // LDR Control
  if (ldrValue < 500) {
    digitalWrite(BLUE_LED, LOW);  // Turn OFF LED when light detected
  } else {
    digitalWrite(BLUE_LED, HIGH); // Turn ON LED when dark
  }
}

void sendToGoogleSheet() {
  if (!client.connect(host, httpsPort)) {
    Serial.println("Connection failed to Google Apps Script");
    return;
  }

  String url = String(googleScriptURL) +
               "?temp=" + temperature +
               "&humidity=" + humidity +
               "&ph=" + pH +
               "&tank=" + (tankFull ? "Yes" : "No") +
               "&ldr=" + ldrStatus +
               "&tds=" + tdsStatus;

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: ESP8266\r\n" +
               "Connection: close\r\n\r\n");

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }
  while (client.available()) {
    String line = client.readStringUntil('\n');
    Serial.println(line);
  }
  client.stop();
}

// === JSON API ===
void handleJSON() {
  String json = "{";
  json += "\"temp\":" + String(temperature) + ",";
  json += "\"humidity\":" + String(humidity) + ",";
  json += "\"ph\":" + String(pH) + ",";
  json += "\"tankFull\":\"" + String(tankFull ? "Yes" : "No") + "\",";
  json += "\"ldr\":\"" + ldrStatus + "\",";
  json += "\"tds\":\"" + tdsStatus + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// === Pump Toggle API ===
void handlePumpToggle() {
  digitalWrite(RELAY_PUMP, !digitalRead(RELAY_PUMP));
  server.send(200, "text/plain", "Pump Relay toggled");
}

// === Dashboard UI Route ===
void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html><html>
  <head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>Hydroponics Control Panel</title>
    <link href="https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;500;600&display=swap" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/all.min.css">
    <style>
      :root {
        --primary: #4361ee;
        --secondary: #3f37c9;
        --success: #4cc9f0;
        --warning: #f8961e;
        --danger: #f94144;
        --light: #f8f9fa;
        --dark: #212529;
        --gray: #6c757d;
      }
      
      * {
        margin: 0;
        padding: 0;
        box-sizing: border-box;
      }
      
      body {
        font-family: 'Poppins', sans-serif;
        background-color: #f5f7ff;
        color: var(--dark);
        line-height: 1.6;
        padding: 20px;
      }
      
      .container {
        max-width: 1200px;
        margin: 0 auto;
      }
      
      header {
        text-align: center;
        margin-bottom: 30px;
        padding: 20px;
        background: linear-gradient(135deg, var(--primary), var(--secondary));
        color: white;
        border-radius: 10px;
        box-shadow: 0 4px 20px rgba(67, 97, 238, 0.15);
      }
      
      h1 {
        font-size: 2.2rem;
        margin-bottom: 5px;
      }
      
      .subtitle {
        font-size: 1rem;
        opacity: 0.9;
      }
      
      .dashboard {
        display: grid;
        grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
        gap: 20px;
        margin-bottom: 30px;
      }
      
      .card {
        background: white;
        border-radius: 12px;
        padding: 20px;
        box-shadow: 0 4px 12px rgba(0, 0, 0, 0.05);
        transition: all 0.3s ease;
      }
      
      .card:hover {
        transform: translateY(-5px);
        box-shadow: 0 8px 20px rgba(0, 0, 0, 0.1);
      }
      
      .card-header {
        display: flex;
        align-items: center;
        margin-bottom: 15px;
        color: var(--gray);
      }
      
      .card-header i {
        margin-right: 10px;
        font-size: 1.2rem;
      }
      
      .card-value {
        font-size: 2.2rem;
        font-weight: 600;
        margin: 10px 0;
        color: var(--primary);
      }
      
      .card-footer {
        margin-top: 10px;
        font-size: 0.9rem;
      }
      
      .status {
        display: inline-block;
        padding: 5px 12px;
        border-radius: 20px;
        font-size: 0.8rem;
        font-weight: 500;
      }
      
      .status-good {
        background-color: rgba(76, 201, 240, 0.1);
        color: var(--success);
      }
      
      .status-warning {
        background-color: rgba(248, 150, 30, 0.1);
        color: var(--warning);
      }
      
      .status-danger {
        background-color: rgba(249, 65, 68, 0.1);
        color: var(--danger);
      }
      
      .control-panel {
        background: white;
        border-radius: 12px;
        padding: 25px;
        box-shadow: 0 4px 12px rgba(0, 0, 0, 0.05);
        text-align: center;
        margin-bottom: 30px;
      }
      
      .control-btn {
        background: var(--primary);
        color: white;
        border: none;
        padding: 12px 25px;
        border-radius: 8px;
        font-size: 1rem;
        font-weight: 500;
        cursor: pointer;
        transition: all 0.3s ease;
        display: inline-flex;
        align-items: center;
        box-shadow: 0 4px 8px rgba(67, 97, 238, 0.2);
      }
      
      .control-btn:hover {
        background: var(--secondary);
        transform: translateY(-2px);
        box-shadow: 0 6px 12px rgba(67, 97, 238, 0.25);
      }
      
      .control-btn i {
        margin-right: 8px;
      }
      
      .external-link {
        display: inline-flex;
        align-items: center;
        padding: 12px 25px;
        background: var(--success);
        color: white;
        text-decoration: none;
        border-radius: 8px;
        font-weight: 500;
        transition: all 0.3s ease;
        box-shadow: 0 4px 8px rgba(76, 201, 240, 0.2);
      }
      
      .external-link:hover {
        background: #3ab8dd;
        transform: translateY(-2px);
        box-shadow: 0 6px 12px rgba(76, 201, 240, 0.25);
      }
      
      .external-link i {
        margin-right: 8px;
      }
      
      @media (max-width: 768px) {
        .dashboard {
          grid-template-columns: 1fr;
        }
      }
    </style>
  </head>
  <body>
    <div class="container">
      <header>
        <h1><i class="fas fa-seedling"></i> Hydroponics Control Panel</h1>
        <p class="subtitle">Real-time monitoring and control system</p>
      </header>
      
      <div class="dashboard">
        <div class="card">
          <div class="card-header">
            <i class="fas fa-thermometer-half"></i>
            <span>Temperature</span>
          </div>
          <div class="card-value">)rawliteral" + String(temperature) + R"rawliteral(</div>
          <div class="card-footer">
            <span class="card-unit">°C</span>
          </div>
        </div>
        
        <div class="card">
          <div class="card-header">
            <i class="fas fa-tint"></i>
            <span>Humidity</span>
          </div>
          <div class="card-value">)rawliteral" + String(humidity) + R"rawliteral(</div>
          <div class="card-footer">
            <span class="card-unit">%</span>
          </div>
        </div>
        
        <div class="card">
          <div class="card-header">
            <i class="fas fa-flask"></i>
            <span>pH Level</span>
          </div>
          <div class="card-value">)rawliteral" + String(pH) + R"rawliteral(</div>
          <div class="card-footer">
            <span class="status status-)rawliteral" + (pH >= 12.0 && pH <= 12.5 ? "good" : "warning") + R"rawliteral(">
              )rawliteral" + (pH >= 12.0 && pH <= 12.5 ? "Optimal" : "Needs Adjustment") + R"rawliteral(
            </span>
          </div>
        </div>
        
        <div class="card">
          <div class="card-header">
            <i class="fas fa-water"></i>
            <span>Water Tank</span>
          </div>
          <div class="card-value">)rawliteral" + (tankFull ? "Full" : "Low") + R"rawliteral(</div>
          <div class="card-footer">
            <span class="status status-)rawliteral" + (tankFull ? "good" : "danger") + R"rawliteral(">
              )rawliteral" + (tankFull ? "Normal" : "Needs Refill") + R"rawliteral(
            </span>
          </div>
        </div>
        
        <div class="card">
          <div class="card-header">
            <i class="fas fa-lightbulb"></i>
            <span>Light Status</span>
          </div>
          <div class="card-value">)rawliteral" + ldrStatus + R"rawliteral(</div>
          <div class="card-footer">
            <span class="status status-)rawliteral" + (ldrValue < 500 ? "good" : "warning") + R"rawliteral(">
              )rawliteral" + (ldrValue < 500 ? "Light Detected" : "Dark") + R"rawliteral(
            </span>
          </div>
        </div>
        
        <div class="card">
          <div class="card-header">
            <i class="fas fa-vial"></i>
            <span>TDS Status</span>
          </div>
          <div class="card-value">)rawliteral" + tdsStatus + R"rawliteral(</div>
          <div class="card-footer">
            <span class="status status-)rawliteral" + (tdsRaw < 500 ? "good" : "warning") + R"rawliteral(">
              )rawliteral" + (tdsRaw < 500 ? "Normal" : "Check Solution") + R"rawliteral(
            </span>
          </div>
        </div>
      </div>
      
      <div class="control-panel">
        <button class="control-btn" onclick="togglePump()">
          <i class="fas fa-power-off"></i>
          Toggle Water Pump
        </button>
      </div>
      
      <a href="https://docs.google.com/spreadsheets/d/1ADY8WvbVTONGMPo1hFSQWVhlhasvrKhDoanUX8CQ_so/edit?gid=0#gid=0" 
         target="_blank" 
         class="external-link">
        <i class="fas fa-external-link-alt"></i>
        View Data Logs
      </a>
    </div>

    <script>
      function togglePump() {
        fetch('/pump')
          .then(response => response.text())
          .then(data => {
            alert("Water pump toggled successfully!");
          })
          .catch(err => {
            alert("Error toggling pump: " + err);
          });
      }
      
      // Auto-refresh every 10 seconds
      setTimeout(() => {
        window.location.reload();
      }, 10000);
    </script>
  </body>
  </html>
  )rawliteral";

  server.send(200, "text/html", html);
}