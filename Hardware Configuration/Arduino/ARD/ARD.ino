/*
 * Decentralized Smart Food Monitoring System - Arduino UNO
 * Uses DHT11 Temperature/Humidity Sensor and MQ3 Gas Sensor
 * Sends data to ESP8266 via Serial Communication
 */

#include <DHT.h>

// Initialize the DHT11 sensor
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Define the pin for the buzzer
#define BUZZER_PIN 3

// Define the pin for the MQ3 gas sensor
#define GAS_PIN A0

// Data sending interval (milliseconds)
const unsigned long SEND_INTERVAL = 10000;  // 10 seconds
unsigned long previousMillis = 0;

void setup() {
  // Initialize Serial communication
  Serial.begin(9600);
  
  // Initialize the buzzer pin as an output
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(GAS_PIN, INPUT);
  
  // Initialize DHT sensor
  dht.begin();
  
  // Startup message
  Serial.println("FOOD SAFETY MONITORING SYSTEM INITIALIZED");
  delay(1000);
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Check if it's time to read sensors and send data
  if (currentMillis - previousMillis >= SEND_INTERVAL) {
    previousMillis = currentMillis;
    
    // Read temperature and humidity from DHT11
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    // Check if any reads failed
    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }
    
    // Read gas value from MQ3 sensor
    int gasValue = analogRead(GAS_PIN);
    
    // Print sensor readings to Serial Monitor
    Serial.println("===== SENSOR READINGS =====");
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
    Serial.print("Gas Level: ");
    Serial.println(gasValue);
    Serial.println("==========================");
    
    // Check environmental conditions
    boolean alert = false;
    
    // Temperature alert conditions (based on food safety standards)
    if (temperature < 4 || temperature > 25) {
      alert = true;
    }
    
    // Humidity alert conditions
    if (humidity < 30 || humidity > 80) {
      alert = true;
    }
    
    // Gas level alert conditions
    if (gasValue > 400) {
      alert = true;
    }
    
    // // Trigger buzzer if any alert condition is met
    // if (alert) {
    //   digitalWrite(BUZZER_PIN, HIGH);
    //   delay(200);
    //   digitalWrite(BUZZER_PIN, LOW);
    //   delay(200);
    //   digitalWrite(BUZZER_PIN, HIGH);
    //   delay(200);
    //   digitalWrite(BUZZER_PIN, LOW);
    // }
    
    // Send data to ESP8266 in a structured format
    // Format: <temperature,humidity,gasValue>
    String dataPacket = "<" + String(temperature, 1) + "," + 
                           String(humidity, 1) + "," + 
                           String(gasValue) + ">";
    Serial.println(dataPacket);
  }
}