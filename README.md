# Smart Food Monitoring System

A decentralized food safety monitoring system that uses Arduino UNO and ESP8266 to track environmental conditions and prevent food waste by alerting when food is at risk of spoilage.

## System Overview

This system consists of two main components:
1. **Arduino UNO (ARD.ino)** - Collects sensor data:
   - Temperature (DHT11 sensor)
   - Humidity (DHT11 sensor)
   - Gas levels (MQ3 sensor)
   - Sends data to ESP8266 via serial communication

2. **ESP8266 (ESP.ino)** - Processes data and provides:
   - Real-time monitoring via web dashboard
   - Email alerts when food is at risk
   - Data logging to ThingSpeak
   - Three-tier food status classification (Normal, At Risk, Spoiled)

## Hardware Components

- **Arduino UNO**
  - DHT11 Temperature/Humidity Sensor
  - MQ3 Gas Sensor
  - Buzzer (for local alerts)
  
- **ESP8266 NodeMCU**
  - WiFi connectivity
  - Web server for dashboard
  - Email alert capabilities

## Installation

1. **Arduino UNO Setup**:
   - Connect DHT11 to pin 2
   - Connect MQ3 to analog pin A0
   - Connect buzzer to pin 3
   - Upload `ARD.ino` to Arduino

2. **ESP8266 Setup**:
   - Connect ESP8266 to Arduino via Serial (RX/TX)
   - Configure WiFi credentials in `ESP.ino`
   - Configure email settings (SMTP) in `ESP.ino`
   - Configure ThingSpeak credentials in `ESP.ino`
   - Upload `ESP.ino` to ESP8266

## Usage

1. After setup, the system will automatically:
   - Collect sensor data every 10 seconds
   - Classify food status based on thresholds
   - Log data to ThingSpeak
   - Provide web dashboard at ESP8266's IP address

2. **Alert Thresholds**:
   - **Normal**:
     - Temperature: 4-15°C
     - Humidity: 30-60%
     - Gas Level: <300
   - **At Risk**:
     - Temperature: 15-25°C
     - Humidity: 60-80%
     - Gas Level: 300-600
   - **Spoiled**:
     - Temperature: >25°C
     - Humidity: >80%
     - Gas Level: >600

3. **Dashboard Features**:
   - Real-time sensor readings
   - Food status indicator
   - Historical data via ThingSpeak
   - Raw JSON API endpoint

## Alert System

The system sends email alerts to:
- Community kitchens (for At Risk food)
- NGOs (for Spoiled food)
- Managers (for all alerts)

Alerts include:
- Current sensor readings
- Food status
- Recommended actions
- Dashboard link

## File Structure

```
Hardware Configuration/
├── Arduino/
│   ├── ARD/
│   │   └── ARD.ino (Arduino UNO code)
│   └── ESP/
│       └── ESP.ino (ESP8266 code)
```

## Dependencies

- Arduino IDE with libraries:
  - DHT sensor library
  - ESP8266WiFi
  - ESP8266WebServer
  - ThingSpeak
  - EMailSender

## License

This project is open-source and available for community use.
