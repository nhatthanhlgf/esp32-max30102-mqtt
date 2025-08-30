#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

MAX30105 particleSensor;

WebServer webServer(80);
WebSocketsServer webSocket(81);

const char* ssid = "Thanh"; 
const char* password = "12345678"; 

#define ledPin 2
unsigned long lastTime = 0;
bool measuring = false;   // bật-tắt đo

#define BUFFER_SIZE 100
uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];
int32_t spo2;
int8_t validSpO2;
int32_t heartRate;
int8_t validHeartRate;

//giao diện web
const char html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>ESP32 Heart & SpO2</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background: #f0f4f8; margin: 0; padding: 20px; }
    h1 { color: #1e3a8a; margin-bottom: 20px; }
    .card { display: inline-block; background: white; padding: 20px; border-radius: 15px;
            box-shadow: 0px 4px 10px rgba(0,0,0,0.1); margin-bottom: 20px; min-width: 250px; }
    .label { font-size: 20px; color: #333; }
    .value { font-size: 28px; font-weight: bold; color: #2563eb; }
    .btn-container { margin-top: 15px; }
    button { width: 100px; height: 40px; margin: 10px; border: none; border-radius: 10px;
             font-size: 18px; font-weight: bold; color: white; cursor: pointer; transition: 0.3s; }
    .on { background-color: #16a34a; }
    .off { background-color: #dc2626; }
    button:hover { opacity: 0.8; }
  </style>
</head>
<body>
  <h1>ESP32 Heart Rate & SpO2 Monitor</h1>

  <div class="card">
    <div class="label">❤️ Heart Rate</div>
    <div class="value" id="hr">--</div> BPM
  </div>

  <div class="card">
    <div class="label">🩸 SpO2</div>
    <div class="value" id="spo2">--</div> %
  </div>

  <div class="card">
    <div class="label">💡 Measurement Control</div>
    <div class="btn-container">
      <button class="on" onclick="send('ON')">ON</button>
      <button class="off" onclick="send('OFF')">OFF</button>
    </div>
  </div>

  <script>
    var Socket;
    window.onload = function(){
      Socket = new WebSocket('ws://' + window.location.hostname + ':81');
      Socket.onmessage = function(event){
        let data = JSON.parse(event.data);
        document.getElementById("hr").innerHTML = data.hr;
        document.getElementById("spo2").innerHTML = data.spo2;
      }
    }
    function send(cmd){ Socket.send(cmd); }
  </script>
</body>
</html>
)rawliteral";

// WebSocket
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){
  if(type == WStype_TEXT){
    String cmd = String((char*)payload);
    if(cmd == "ON"){
      measuring = true;
      digitalWrite(ledPin, HIGH);
      Serial.println("Đang bật đo...");
    }
    else if(cmd == "OFF"){
      measuring = false;
      digitalWrite(ledPin, LOW);
      Serial.println("Đã tắt đo.");
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);

  // Kết nối WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi!");
  Serial.println("IP Address: " + WiFi.localIP().toString());

  //  web server
  webServer.on("/", []{ webServer.send(200, "text/html", html); });
  webServer.begin();

  // websocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  //  MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 not found. Check wiring!");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);
}

void loop() {
  webServer.handleClient();
  webSocket.loop();

  // Nếu đang bật đo thì lấy dữ liệu
  if (measuring && millis() - lastTime >= 1000) {
    for (byte i = 0; i < BUFFER_SIZE; i++) {
      while (!particleSensor.available()) {
        particleSensor.check();
      }
      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
    }

    maxim_heart_rate_and_oxygen_saturation(
      irBuffer, BUFFER_SIZE, redBuffer, 
      &spo2, &validSpO2, &heartRate, &validHeartRate
    );

    int hrVal = validHeartRate ? heartRate : 0;
    int spo2Val = validSpO2 ? spo2 : 0;

    String json = "{\"hr\":" + String(hrVal) + ",\"spo2\":" + String(spo2Val) + "}";
    webSocket.broadcastTXT(json);

    Serial.println(json);
    lastTime = millis();
  }
}
  