#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "config.h"

// Web server on port 80
WebServer server(80);

// Function declarations
void readArduinoData();
void parseArduinoData(String data);
void handleData();
void setupWebPage();

// Sensor data structure
struct SensorData {
  int co2 = 0;
  int pm25 = 0;
  int o3 = 0;
  int temp = 0;
  int hum = 0;
  int tvoc = 0;
  unsigned long lastUpdate = 0;
} sensorData;

// Serial monitoring with UART2
HardwareSerial ArduinoSerial(2);

// Buffer for reading serial data
String serialBuffer = "";
unsigned long lastSerialCheck = 0;

void setup() {
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  // sync with NTP for timekeeping
  configTime(0, 0, "pool.ntp.org");

  // Init Serial for PlatformIO Serial Monitor
  Serial.begin(115200);
  delay(1000);
  
  // Init UART2 for Arduino Monitoring
  int rxPinGPIO = 16;
  int txPinGPIO = 17;
  ArduinoSerial.begin(9600, SERIAL_8N1, rxPinGPIO, txPinGPIO);
  
  // Connect to WiFi with Credential from ConfigFile
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());

  setupWebPage();
  server.on("/data", handleData);
  
  server.begin();
  Serial.println("Web Server Started");
}

void loop() {
  server.handleClient();
  
  readArduinoData();
  
  // Debug (5sec)
  if (millis() - lastSerialCheck > 5000) {
    lastSerialCheck = millis();
    Serial.print("Serial available: ");
    Serial.print(ArduinoSerial.available());
    Serial.print(" bytes, Last update: ");
    Serial.print((millis() - sensorData.lastUpdate) / 1000);
    Serial.println(" seconds ago");
  }
}

void setupWebPage(){
    server.on("/", HTTP_GET, [](){
    File file = SPIFFS.open("/index.html", "r");
    if(file){
      server.streamFile(file, "text/html");
      file.close();
    } else {
      server.send(404, "text/plain", "File not found");
    }
  });
  
  server.on("/style.css", HTTP_GET, [](){
    File file = SPIFFS.open("/style.css", "r");
    if(file){
      server.streamFile(file, "text/css");
      file.close();
    } else {
      server.send(404, "text/plain", "File not found");
    }
  });
}

void readArduinoData() {
  static int ffCount = 0;
  while (ArduinoSerial.available()) {
    byte inByte = ArduinoSerial.read();
    if (inByte == 0xFF) { // Check for Messages Delimiters 0xFF
      ffCount++;
      if (ffCount >= 3) { // If 3 consecutive 0xFF bytes, end of message and process the buffer
        if (serialBuffer.length() > 0) {
          Serial.print("Complete message received: '");
          Serial.print(serialBuffer);
          Serial.println("'");
          parseArduinoData(serialBuffer);
          serialBuffer = ""; // Clear buffer
        }
        ffCount = 0;
      }
    } else { // If not 0xFF byte reset counter and add to buffer
      ffCount = 0;
      if (inByte >= 32 && inByte <= 126) {
        serialBuffer += (char)inByte;
      }
    }
    if (serialBuffer.length() > 100) { // Check for Buffer Overflow
      Serial.println("Buffer overflow, clearing");
      serialBuffer = "";
      ffCount = 0;
    }
  }
}

// Parses the Arduino data format and updates sensorData
void parseArduinoData(String data) {
  // Parse Arduino Pro Mini data format: "co2V.val=400"
  if (data.startsWith("co2V.val=")) {
    int value = data.substring(9).toInt();
    if (value > 0 && value < 10000) { // Sanity check
      sensorData.co2 = value;
      sensorData.lastUpdate = millis();
      Serial.println("CO2 updated: " + String(value) + " ppm");
    }
  }
  else if (data.startsWith("pm25V.val=")) {
    int value = data.substring(10).toInt();
    if (value >= 0 && value < 2000) { // Sanity check
      sensorData.pm25 = value;
      sensorData.lastUpdate = millis();
      Serial.println("PM2.5 updated: " + String(value) + " µg/m³");
    }
  }
  else if (data.startsWith("o3V.val=")) {
    int value = data.substring(8).toInt();
    if (value >= 0 && value < 2000) { // Sanity check
      sensorData.o3 = value;
      sensorData.lastUpdate = millis();
      Serial.println("O3 updated: " + String(value) + " ppb");
    }
  }
  else if (data.startsWith("tempV.val=")) {
    int value = data.substring(10).toInt();
    if (value >= -50 && value < 100) { // Sanity check
      sensorData.temp = value;
      sensorData.lastUpdate = millis();
      Serial.println("Temperature updated: " + String(value) + " °C");
    }
  }
  else if (data.startsWith("humV.val=")) {
    int value = data.substring(9).toInt();
    if (value >= 0 && value <= 100) { // Sanity check
      sensorData.hum = value;
      sensorData.lastUpdate = millis();
      Serial.println("Humidity updated: " + String(value) + " %");
    }
  }
  else if (data.startsWith("tvocV.val=")) {
    int value = data.substring(10).toInt();
    if (value >= 0 && value < 2000) { // Sanity check
      sensorData.tvoc = value;
      sensorData.lastUpdate = millis();
      Serial.println("TVOC updated: " + String(value) + " raw");
    }
  }
  else {
    Serial.print("Unknown command: ");
    Serial.println(data);
  }
}

// collects data from the sensor and sends it as JSON
void handleData() {
  JsonDocument doc;
  doc["co2"] = sensorData.co2;
  doc["pm25"] = sensorData.pm25;
  doc["o3"] = sensorData.o3;
  doc["temp"] = sensorData.temp;
  doc["hum"] = sensorData.hum;
  doc["tvoc"] = sensorData.tvoc;
  doc["lastUpdate"] = millis();

  String jsonString;
  serializeJson(doc, jsonString);
  int httpResponseCode = 200;
  Serial.print("sedning JSON data: ");
  Serial.println(jsonString);
  server.send(httpResponseCode, "application/json", jsonString);
}