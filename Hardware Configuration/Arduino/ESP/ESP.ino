/*
 * Decentralized Smart Food Monitoring System - ESP8266
 * Receives sensor data from Arduino UNO
 * Analyzes data for food spoilage
 * Sends email alerts based on thresholds
 * Logs data to ThingSpeak
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ThingSpeak.h>
#include <EMailSender.h>

// WiFi credentials
const char* ssid = "your-ssid"; //Credentials Removed for confidentiality
const char* password = "pass";

// Email settings (SMTP)
const char* smtpUser = "sender@gmail.com";
const char* smtpPassword = "app-pass";
const char* smtpServer = "smtp.gmail.com";
const uint16_t smtpPort = 465;

// Email recipients
const char* ngoEmail = "ngoreceiver@gmail.com";       // NGO contact (main emergency)
const char* kitchenEmail = "kitchenreceiver@gmail.com";   // Community kitchen (use same email for demo)
const char* managerEmail = "manager@gmail.com";   // Manager (use same email for demo)

// ThingSpeak credentials
unsigned long channelNumber = --channelNo--;
const char* writeAPIKey = "api-key";

// Thresholds for food monitoring
// Normal zone: suitable for standard storage
const float TEMP_NORMAL_MIN = 4.0;   // °C
const float TEMP_NORMAL_MAX = 15.0;  // °C
const float HUM_NORMAL_MIN = 30.0;   // %
const float HUM_NORMAL_MAX = 60.0;   // %
const int GAS_NORMAL_MAX = 300;      // MQ3 sensor value

// At risk zone: should be used soon
const float TEMP_RISK_MAX = 25.0;    // °C
const float HUM_RISK_MAX = 80.0;     // %
const int GAS_RISK_MAX = 600;        // MQ3 sensor value

// Status tracking to avoid duplicate alerts
String currentFoodStatus = "Initializing";
String lastFoodStatus = "Initializing";
unsigned long lastAlertTime = 0;
const unsigned long ALERT_COOLDOWN = 300000;  // 5 minutes cooldown between similar alerts

// Data variables
float temperature = 0.0;
float humidity = 0.0;
int gasLevel = 0;

// Clients for WiFi, Email, and ThingSpeak
WiFiClient client;
EMailSender emailSender(smtpUser, smtpPassword, smtpUser, smtpServer, smtpPort);

// Webserver for monitoring
ESP8266WebServer server(80);

void setup() {
  // Initialize Serial communication
  Serial.begin(9600);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.print("Connected to WiFi. IP address: ");
  Serial.println(WiFi.localIP());
  
  // Initialize ThingSpeak
  ThingSpeak.begin(client);
  Serial.println("ThingSpeak client started");
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("HTTP server started");
  
  Serial.println("ESP8266 Ready to receive data from Arduino UNO");
}

void loop() {
  // Handle incoming HTTP requests
  server.handleClient();
  
  // Check if there's data available on the Serial port
  if (Serial.available() > 0) {
    String receivedData = Serial.readStringUntil('\n');
    receivedData.trim();
    
    // Process data if it's in the expected format <temperature,humidity,gasValue>
    if (receivedData.startsWith("<") && receivedData.endsWith(">")) {
      // Parse the data
      int firstComma = receivedData.indexOf(',');
      int secondComma = receivedData.indexOf(',', firstComma + 1);
      
      if (firstComma != -1 && secondComma != -1) {
        // Extract values
        temperature = receivedData.substring(1, firstComma).toFloat();
        humidity = receivedData.substring(firstComma + 1, secondComma).toFloat();
        gasLevel = receivedData.substring(secondComma + 1, receivedData.length() - 1).toInt();
        
        // Determine food status
        updateFoodStatus();
        
        // Log data to ThingSpeak
        logToThingSpeak();
        
        // Check if status changed and send alert if needed
        checkStatusAndAlert();
      }
    }
  }
}

// Update the food status based on sensor readings
void updateFoodStatus() {
  lastFoodStatus = currentFoodStatus;
  
  // Determine food status
  if (temperature <= TEMP_NORMAL_MAX && temperature >= TEMP_NORMAL_MIN && 
      humidity <= HUM_NORMAL_MAX && humidity >= HUM_NORMAL_MIN && 
      gasLevel <= GAS_NORMAL_MAX) {
    currentFoodStatus = "Normal";
  } else if (temperature > TEMP_RISK_MAX || 
             humidity > HUM_RISK_MAX || 
             gasLevel > GAS_RISK_MAX) {
    currentFoodStatus = "Spoiled";
  } else {
    currentFoodStatus = "At Risk";
  }
  
  // Print current status
  Serial.print("Food Status: ");
  Serial.println(currentFoodStatus);
}

// Log data to ThingSpeak
void logToThingSpeak() {
  // Set fields
  ThingSpeak.setField(1, temperature);
  ThingSpeak.setField(2, humidity);
  ThingSpeak.setField(3, gasLevel);
  
  // Status as numerical value for graphing (1=Normal, 2=At Risk, 3=Spoiled)
  int statusValue = 1;
  if (currentFoodStatus == "At Risk") statusValue = 2;
  if (currentFoodStatus == "Spoiled") statusValue = 3;
  ThingSpeak.setField(4, statusValue);
  
  // Write to ThingSpeak
  int httpCode = ThingSpeak.writeFields(channelNumber, writeAPIKey);
  
  if (httpCode == 200) {
    Serial.println("ThingSpeak update successful");
  } else {
    Serial.print("ThingSpeak error code: ");
    Serial.println(httpCode);
  }
}

// Check if status changed and send alert if needed
void checkStatusAndAlert() {
  unsigned long currentTime = millis();
  
  // Send alert if status changed to At Risk or Spoiled, or if cooldown period passed
  if ((currentFoodStatus != lastFoodStatus || currentTime - lastAlertTime > ALERT_COOLDOWN) && 
      currentFoodStatus != "Normal" && currentFoodStatus != "Initializing") {
    
    sendAlertEmail();
    lastAlertTime = currentTime;
  }
}

// Send alert email based on food status
void sendAlertEmail() {
  Serial.print("Sending ");
  Serial.print(currentFoodStatus);
  Serial.println(" alert email...");
  
  // Create email message
  EMailSender::EMailMessage message;
  
  // Determine recipients based on status
  String recipient;
  
  // Build alert message based on food status
  if (currentFoodStatus == "Spoiled") {
    message.subject = "URGENT: Food Spoilage Detected!";
    message.message = "The system has detected food spoilage:\n\n"
                    "Temperature: " + String(temperature, 1) + " °C\n"
                    "Humidity: " + String(humidity, 1) + " %\n"
                    "Gas Level: " + String(gasLevel) + "\n\n"
                    "RECOMMENDATION: Please transfer food to NGOs or food banks immediately.\n"
                    "Link to dashboard: http://" + WiFi.localIP().toString();
    
    // Send to NGO for immediate action
    recipient = ngoEmail;
  } else {
    message.subject = "WARNING: Food at Risk of Spoilage";
    message.message = "The system has detected food at risk of spoilage:\n\n"
                    "Temperature: " + String(temperature, 1) + " °C\n"
                    "Humidity: " + String(humidity, 1) + " %\n"
                    "Gas Level: " + String(gasLevel) + "\n\n"
                    "RECOMMENDATION: Consider transferring food to local markets or community kitchens.\n"
                    "Link to dashboard: http://" + WiFi.localIP().toString();
    
    // Send to community kitchen for at-risk food
    recipient = kitchenEmail;
  }
  
  // Send email
  EMailSender::Response resp = emailSender.send(recipient, message);
  
  if (resp.status) {
    Serial.println("Email alert sent successfully");
  } else {
    Serial.print("Email sending failed with error: ");
    Serial.println(resp.desc);
  }
  
  // Also send notification to manager regardless of status
  if (currentFoodStatus == "Spoiled") {
    // If already sent to NGO, now send to manager
    EMailSender::Response resp2 = emailSender.send(managerEmail, message);
    if (resp2.status) {
      Serial.println("Manager notification sent");
    }
  }
}

// Web server root page handler
void handleRoot() {
  String html = "<html><head>";
  html += "<title>Food Monitoring System</title>";
  html += "<meta http-equiv='refresh' content='10'>"; // Auto-refresh every 10 seconds
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; }";
  html += "h1 { color: #0066cc; }";
  html += ".container { max-width: 800px; margin: 0 auto; }";
  html += ".data-card { border: 1px solid #ddd; padding: 15px; border-radius: 5px; margin-bottom: 20px; }";
  html += ".normal { background-color: #d4edda; }";
  html += ".at-risk { background-color: #fff3cd; }";
  html += ".spoiled { background-color: #f8d7da; }";
  html += ".sensor-data { font-size: 18px; margin: 10px 0; }";
  html += ".thresholds { margin-top: 20px; font-size: 14px; }";
  html += "table { width: 100%; border-collapse: collapse; }";
  html += "table, th, td { border: 1px solid #ddd; padding: 8px; }";
  html += "th { background-color: #f2f2f2; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>Smart Food Monitoring System Dashboard</h1>";
  
  // Determine status class
  String statusClass = "normal";
  if (currentFoodStatus == "At Risk") {
    statusClass = "at-risk";
  } else if (currentFoodStatus == "Spoiled") {
    statusClass = "spoiled";
  }
  
  html += "<div class='data-card " + statusClass + "'>";
  html += "<h2>Current Status: " + currentFoodStatus + "</h2>";
  html += "<div class='sensor-data'><strong>Temperature:</strong> " + String(temperature, 1) + " &deg;C</div>";
  html += "<div class='sensor-data'><strong>Humidity:</strong> " + String(humidity, 1) + " %</div>";
  html += "<div class='sensor-data'><strong>Gas Level:</strong> " + String(gasLevel) + "</div>";
  html += "<div class='sensor-data'><strong>Last Updated:</strong> " + String(millis() / 1000) + " seconds ago</div>";
  
  // Add recommendations based on status
  html += "<div class='thresholds'><strong>Recommendation:</strong> ";
  if (currentFoodStatus == "Normal") {
    html += "Food conditions are optimal for storage.";
  } else if (currentFoodStatus == "At Risk") {
    html += "Consider transferring food to local markets or community kitchens.";
  } else if (currentFoodStatus == "Spoiled") {
    html += "Please transfer food to NGOs or food banks immediately.";
  }
  html += "</div>";
  html += "</div>";
  
  // Add threshold table
  html += "<h3>Monitoring Thresholds</h3>";
  html += "<table>";
  html += "<tr><th>Parameter</th><th>Normal Range</th><th>At Risk Range</th><th>Spoiled Range</th></tr>";
  html += "<tr><td>Temperature</td><td>" + String(TEMP_NORMAL_MIN) + "-" + String(TEMP_NORMAL_MAX) + " &deg;C</td><td>" + String(TEMP_NORMAL_MAX) + "-" + String(TEMP_RISK_MAX) + " &deg;C</td><td>&gt; " + String(TEMP_RISK_MAX) + " &deg;C</td></tr>";
  html += "<tr><td>Humidity</td><td>" + String(HUM_NORMAL_MIN) + "-" + String(HUM_NORMAL_MAX) + " %</td><td>" + String(HUM_NORMAL_MAX) + "-" + String(HUM_RISK_MAX) + " %</td><td>&gt; " + String(HUM_RISK_MAX) + " %</td></tr>";
  html += "<tr><td>Gas Level</td><td>&lt; " + String(GAS_NORMAL_MAX) + "</td><td>" + String(GAS_NORMAL_MAX) + "-" + String(GAS_RISK_MAX) + "</td><td>&gt; " + String(GAS_RISK_MAX) + "</td></tr>";
  html += "</table>";
  
  html += "<p>This page auto-refreshes every 10 seconds.</p>";
  html += "<p><a href='/data'>View Raw JSON Data</a></p>";
  html += "<p><a href='https://thingspeak.com/channels/" + String(channelNumber) + "' target='_blank'>View ThingSpeak Dashboard</a></p>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

// Raw data endpoint for API access
void handleData() {
  String jsonData = "{";
  jsonData += "\"temperature\":" + String(temperature, 1) + ",";
  jsonData += "\"humidity\":" + String(humidity, 1) + ",";
  jsonData += "\"gasLevel\":" + String(gasLevel) + ",";
  jsonData += "\"status\":\"" + currentFoodStatus + "\",";
  jsonData += "\"timestamp\":" + String(millis() / 1000);
  jsonData += "}";
  
  server.send(200, "application/json", jsonData);
}