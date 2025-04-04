/*
 * Simplified Smart Food Monitoring System - ESP8266
 * Receives sensor data from Arduino UNO
 * Analyzes data for food spoilage
 * Sends enhanced email alerts using Gemini API
 * Logs data to ThingSpeak
 * Provides simplified dashboard
 */

 #include <ESP8266WiFi.h>
 #include <ESP8266WebServer.h>
 #include <ESP8266HTTPClient.h>
 #include <ThingSpeak.h>
 #include <EMailSender.h>
 #include <ArduinoJson.h>
 #include <WiFiClientSecure.h>
 #include <time.h>
 
 // WiFi credentials
 const char* ssid = "Red";
 const char* password = "10201004";
 
 // Email settings (SMTP)
 const char* smtpUser = "food59682@gmail.com";
 const char* smtpPassword = "ijoh xuuv rcvz nrmk";
 const char* smtpServer = "smtp.gmail.com";
 const uint16_t smtpPort = 465;
 
 // Email recipients
 const char* ngoEmail = "redlucario0@gmail.com";       // NGO contact (main emergency)
 const char* kitchenEmail = "redlucario0@gmail.com";   // Community kitchen (use same email for demo)
 const char* managerEmail = "redlucario0@gmail.com";   // Manager (use same email for demo)
 
 // ThingSpeak credentials
 unsigned long channelNumber = 2342262;
 const char* writeAPIKey = "S76DJ2LJBVDJUT4E";
 const char* readAPIKey = "OXQOTBXDJ9OJV5O0";  // Added read API key for dashboard
 
 // Gemini API settings
 const char* geminiHost = "generativelanguage.googleapis.com";
 const char* geminiApiKey = "AIzaSyB_yqvGumJtNFquprLHF8wvjQ0DDqxVazY"; // Replace with your actual Gemini API key
 const String geminiModel = "gemini-1.0-pro";
 
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
 
 // Device status monitoring
 unsigned long lastArduinoHeartbeat = 0;
 bool arduinoOnline = false;
 const unsigned long DEVICE_TIMEOUT = 60000;  // 1 minute timeout
 
 // Alert history
 struct AlertRecord {
   String timestamp;
   String status;
   String recipient;
   String message;
   String aiRecommendation;
 };
 
 const int MAX_ALERTS = 30;
 AlertRecord alertHistory[MAX_ALERTS];
 int alertCount = 0;
 
 // Time management
 const char* ntpServer = "pool.ntp.org";
 const long gmtOffset_sec = 19800;  // IST is UTC+5:30, so 5.5 hours = 19800 seconds
 
 // Clients for WiFi, Email, and ThingSpeak
 WiFiClient client;
 WiFiClientSecure secureClient;
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
   
   // Set up time server for timestamps
   configTime(gmtOffset_sec, 0, ntpServer);
   
   // Initialize secure client
   secureClient.setInsecure();  // Skip certificate validation for simplicity
   
   // Setup web server routes
   server.on("/", handleRoot);
   server.on("/data", handleData);
   server.on("/alerts", handleAlerts);
   server.on("/status", handleStatus);
   server.on("/dashboard", handleDashboard);
   server.begin();
   Serial.println("HTTP server started");
   
   Serial.println("ESP8266 Ready to receive data from Arduino UNO");
 }
 
 void loop() {
   // Handle incoming HTTP requests
   server.handleClient();
   
   // Check Arduino heartbeat
   checkArduinoStatus();
   
   // Check if there's data available on the Serial port
   if (Serial.available() > 0) {
     String receivedData = Serial.readStringUntil('\n');
     receivedData.trim();
     
     // Check for heartbeat message
     if (receivedData == "<HEARTBEAT>") {
       lastArduinoHeartbeat = millis();
       arduinoOnline = true;
       return;
     }
     
     // Process data if it's in the expected format <temperature,humidity,gasValue>
     if (receivedData.startsWith("<") && receivedData.endsWith(">")) {
       // Update Arduino status
       lastArduinoHeartbeat = millis();
       arduinoOnline = true;
       
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
 
 // Check if Arduino is still online
 void checkArduinoStatus() {
   if (arduinoOnline && (millis() - lastArduinoHeartbeat > DEVICE_TIMEOUT)) {
     arduinoOnline = false;
     Serial.println("Arduino offline!");
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
 
 // Get current timestamp as string
 String getTimestamp() {
   time_t now;
   struct tm timeinfo;
   time(&now);
   localtime_r(&now, &timeinfo);
   
   char buffer[30];
   strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
   return String(buffer);
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
 
 // Get AI recommendation from Gemini API
 String getGeminiRecommendation() {
   if (WiFi.status() != WL_CONNECTED) {
     return "Unable to get AI recommendation: WiFi not connected";
   }
   
   WiFiClientSecure secureGeminiClient;
   secureGeminiClient.setInsecure(); // Skip SSL certificate verification
   
   if (!secureGeminiClient.connect(geminiHost, 443)) {
     return "Connection to Gemini API failed";
   }
   
   // Create a prompt for the Gemini API based on current food conditions
   String prompt = "I have food with the following conditions:\n";
   prompt += "- Temperature: " + String(temperature, 1) + " °C\n";
   prompt += "- Humidity: " + String(humidity, 1) + " %\n";
   prompt += "- Volatile gas level (from MQ3 sensor): " + String(gasLevel) + "\n";
   prompt += "- Current status determination: " + currentFoodStatus + "\n\n";
   prompt += "Based on this data, please provide a short, specific recommendation about what I should do with this food. Should it be sent to a food bank (NGO), community kitchen, or other facility? Include safety considerations and urgency level. Keep it under 150 words.";
   
   // Create JSON payload for Gemini API
   DynamicJsonDocument requestDoc(2048);
   JsonObject contents = requestDoc["contents"].createNestedObject();
   JsonObject parts = contents["parts"].createNestedObject();
   parts["text"] = prompt;
   
   requestDoc["generationConfig"]["temperature"] = 0.2;
   requestDoc["generationConfig"]["topK"] = 40;
   requestDoc["generationConfig"]["topP"] = 0.95;
   requestDoc["generationConfig"]["maxOutputTokens"] = 300;
   
   String requestBody;
   serializeJson(requestDoc, requestBody);
   
   // Build Gemini API request
   String url = "/v1/models/" + geminiModel + ":generateContent?key=" + geminiApiKey;
   String request = "POST " + url + " HTTP/1.1\r\n";
   request += "Host: " + String(geminiHost) + "\r\n";
   request += "Content-Type: application/json\r\n";
   request += "Content-Length: " + String(requestBody.length()) + "\r\n";
   request += "Connection: close\r\n\r\n";
   request += requestBody;
   
   // Send request
   secureGeminiClient.print(request);
   
   // Wait for response
   unsigned long timeout = millis() + 10000; // 10 second timeout
   while (secureGeminiClient.available() == 0) {
     if (millis() > timeout) {
       secureGeminiClient.stop();
       return "Request timeout";
     }
     delay(100);
   }
   
   // Read response
   String response = "";
   while (secureGeminiClient.available()) {
     response += secureGeminiClient.readStringUntil('\n');
   }
   
   // Parse response to get the generated text
   int jsonStart = response.indexOf('{');
   if (jsonStart >= 0) {
     response = response.substring(jsonStart);
     
     DynamicJsonDocument responseDoc(4096);
     DeserializationError error = deserializeJson(responseDoc, response);
     
     if (!error) {
       String aiText = responseDoc["candidates"][0]["content"]["parts"][0]["text"];
       return aiText;
     }
   }
   
   return "Unable to parse AI recommendation";
 }
 
 // Send alert email based on food status
 void sendAlertEmail() {
   Serial.print("Sending ");
   Serial.print(currentFoodStatus);
   Serial.println(" alert email...");
   
   // Create email message with shorter content first
   EMailSender::EMailMessage message;
   String recipient;
   
   // Build alert message based on food status
   if (currentFoodStatus == "Spoiled") {
     message.subject = "URGENT: Food Spoilage Detected!";
     message.message = "The system has detected food spoilage:\n\n"
                     "Temperature: " + String(temperature, 1) + " °C\n"
                     "Humidity: " + String(humidity, 1) + " %\n"
                     "Gas Level: " + String(gasLevel) + "\n\n"
                     "Check dashboard: http://" + WiFi.localIP().toString() + "/dashboard";
     
     // Send to NGO for immediate action
     recipient = ngoEmail;
   } else {
     message.subject = "WARNING: Food at Risk of Spoilage";
     message.message = "The system has detected food at risk of spoilage:\n\n"
                     "Temperature: " + String(temperature, 1) + " °C\n"
                     "Humidity: " + String(humidity, 1) + " %\n"
                     "Gas Level: " + String(gasLevel) + "\n\n"
                     "Check dashboard: http://" + WiFi.localIP().toString() + "/dashboard";
     
     // Send to community kitchen for at-risk food
     recipient = kitchenEmail;
   }
   
   // Send email first without the AI recommendation
   EMailSender::Response resp = emailSender.send(recipient, message);
   
   if (resp.status) {
     Serial.println("Email alert sent successfully");
     
     // Get AI recommendation in a separate step after email is sent
     String aiRecommendation = getGeminiRecommendation();
     
     // Record the alert in history (doing this after the email send conserves memory)
     if (alertCount >= MAX_ALERTS) {
       // Shift alerts to make room for new one
       for (int i = 0; i < MAX_ALERTS - 1; i++) {
         alertHistory[i] = alertHistory[i+1];
       }
       alertCount = MAX_ALERTS - 1;
     }
     
     // Add new alert to history
     alertHistory[alertCount].timestamp = getTimestamp();
     alertHistory[alertCount].status = currentFoodStatus;
     alertHistory[alertCount].recipient = recipient;
     alertHistory[alertCount].message = message.subject;
     alertHistory[alertCount].aiRecommendation = aiRecommendation;
     alertCount++;
     
     // Now try to send a follow-up email with the AI recommendation if needed
     if (aiRecommendation.length() > 0 && aiRecommendation != "Connection to Gemini API failed" && aiRecommendation != "Request timeout") {
       // Allow some time between emails
       delay(1000);
       
       // Create a separate email for AI recommendation
       EMailSender::EMailMessage aiMessage;
       aiMessage.subject = "AI Recommendation: " + currentFoodStatus + " Food";
       aiMessage.message = "AI RECOMMENDATION:\n\n" + aiRecommendation;
       
       EMailSender::Response respAI = emailSender.send(recipient, aiMessage);
       if (respAI.status) {
         Serial.println("AI recommendation email sent");
       } else {
         Serial.print("AI recommendation email failed: ");
         Serial.println(respAI.desc);
       }
     }
     
     // Also send notification to manager regardless of status
     if (currentFoodStatus == "Spoiled") {
       // Allow some time between emails
       delay(1000);
       
       // If already sent to NGO, now send to manager
       EMailSender::Response resp2 = emailSender.send(managerEmail, message);
       if (resp2.status) {
         Serial.println("Manager notification sent");
         
         // Record this alert too
         if (alertCount >= MAX_ALERTS) {
           // Shift alerts to make room for new one
           for (int i = 0; i < MAX_ALERTS - 1; i++) {
             alertHistory[i] = alertHistory[i+1];
           }
           alertCount = MAX_ALERTS - 1;
         }
         
         // Add manager alert to history
         alertHistory[alertCount].timestamp = getTimestamp();
         alertHistory[alertCount].status = currentFoodStatus;
         alertHistory[alertCount].recipient = managerEmail;
         alertHistory[alertCount].message = message.subject + " (Manager Copy)";
         alertHistory[alertCount].aiRecommendation = aiRecommendation;
         alertCount++;
       }
     }
   } else {
     Serial.print("Email sending failed with error: ");
     Serial.println(resp.desc);
   }
 }
 
 // Web server handlers
 void handleRoot() {
   String html = "<html><head>";
   html += "<title>Food Monitoring System</title>";
   html += "<meta http-equiv='refresh' content='5; url=/dashboard'>";
   html += "</head><body>";
   html += "<h1>Redirecting to Dashboard...</h1>";
   html += "<p>If you are not redirected automatically, <a href='/dashboard'>click here</a>.</p>";
   html += "</body></html>";
   
   server.send(200, "text/html", html);
 }
 
 // Raw data endpoint for API access
 void handleData() {
   String jsonData = "{";
   jsonData += "\"temperature\":" + String(temperature, 1) + ",";
   jsonData += "\"humidity\":" + String(humidity, 1) + ",";
   jsonData += "\"gasLevel\":" + String(gasLevel) + ",";
   jsonData += "\"status\":\"" + currentFoodStatus + "\",";
   jsonData += "\"timestamp\":\"" + getTimestamp() + "\",";
   jsonData += "\"device_status\":{";
   jsonData += "\"arduino\":" + String(arduinoOnline ? "true" : "false") + ",";
   jsonData += "\"esp\":true";
   jsonData += "}";
   jsonData += "}";
   
   server.send(200, "application/json", jsonData);
 }
 
 // Alerts history endpoint
 void handleAlerts() {
   String jsonData = "[";
   
   for (int i = 0; i < alertCount; i++) {
     if (i > 0) jsonData += ",";
     jsonData += "{";
     jsonData += "\"timestamp\":\"" + alertHistory[i].timestamp + "\",";
     jsonData += "\"status\":\"" + alertHistory[i].status + "\",";
     jsonData += "\"recipient\":\"" + alertHistory[i].recipient + "\",";
     jsonData += "\"message\":\"" + alertHistory[i].message + "\",";
     jsonData += "\"aiRecommendation\":\"" + alertHistory[i].aiRecommendation + "\"";
     jsonData += "}";
   }
   
   jsonData += "]";
   
   server.send(200, "application/json", jsonData);
 }
 
 // System status endpoint
 void handleStatus() {
   String jsonData = "{";
   jsonData += "\"arduino\":{";
   jsonData += "\"online\":" + String(arduinoOnline ? "true" : "false") + ",";
   jsonData += "\"lastSeen\":\"" + (arduinoOnline ? "now" : String((millis() - lastArduinoHeartbeat) / 1000) + "s ago") + "\"";
   jsonData += "},";
   jsonData += "\"esp\":{";
   jsonData += "\"online\":true,";
   jsonData += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
   jsonData += "\"uptime\":\"" + String(millis() / 1000 / 60) + " minutes\"";
   jsonData += "},";
   jsonData += "\"thingspeak\":{";
   jsonData += "\"channel\":" + String(channelNumber) + ",";
   jsonData += "\"lastUpdate\":\"" + getTimestamp() + "\"";
   jsonData += "}";
   jsonData += "}";
   
   server.send(200, "application/json", jsonData);
 }
 
 // Main dashboard handler
 void handleDashboard() {
   String html = R"(
 <!DOCTYPE html>
 <html>
 <head>
   <title>Smart Food Monitoring Dashboard</title>
   <meta charset="UTF-8">
   <meta name="viewport" content="width=device-width, initial-scale=1.0">
   <style>
     body {
       font-family: 'Segoe UI', Arial, sans-serif;
       background-color: #f5f5f5;
       color: #333;
       margin: 0;
       padding: 0;
     }
     
     .container {
       max-width: 1000px;
       margin: 0 auto;
       padding: 20px;
     }
     
     header {
       background-color: #4CAF50;
       color: white;
       padding: 15px;
       border-radius: 5px;
       margin-bottom: 20px;
       display: flex;
       justify-content: space-between;
       align-items: center;
     }
     
     .status-dot {
       display: inline-block;
       width: 10px;
       height: 10px;
       border-radius: 50%;
       margin-right: 5px;
     }
     
     .online { background-color: #4CAF50; }
     .offline { background-color: #F44336; }
     
     .status-indicator {
       display: flex;
       align-items: center;
       margin-left: 15px;
     }
     
     .food-status {
       text-align: center;
       padding: 15px;
       border-radius: 5px;
       margin-bottom: 20px;
       font-size: 24px;
       font-weight: bold;
     }
     
     .status-normal { background-color: #A5D6A7; }
     .status-risk { background-color: #FFE082; }
     .status-spoiled { background-color: #EF9A9A; }
     
     .card {
       background-color: white;
       border-radius: 5px;
       padding: 15px;
       margin-bottom: 20px;
       box-shadow: 0 2px 4px rgba(0,0,0,0.1);
     }
     
     .card-title {
       font-size: 18px;
       font-weight: bold;
       margin-bottom: 15px;
       color: #4CAF50;
     }
     
     .readings {
       display: grid;
       grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
       gap: 15px;
     }
     
     .reading-item {
       border-bottom: 1px solid #eee;
       padding-bottom: 10px;
     }
     
     .reading-label {
       font-weight: 500;
       color: #666;
     }
     
     .reading-value {
       font-size: 20px;
       font-weight: bold;
     }
     
     .recommendation {
       padding: 15px;
       border-radius: 5px;
       margin-top: 10px;
     }
     
     .table {
       width: 100%;
       border-collapse: collapse;
     }
     
     .table th, .table td {
       padding: 8px;
       text-align: left;
       border-bottom: 1px solid #ddd;
     }
     
     .table th {
       background-color: #f2f2f2;
     }
     
     .chart-container {
       height: 250px;
       margin-top: 15px;
     }
     
     footer {
       text-align: center;
       margin-top: 30px;
       padding: 10px;
       font-size: 12px;
       color: #777;
     }
     
     @media (max-width: 768px) {
       .readings {
         grid-template-columns: 1fr;
       }
     }
   </style>
   
   <script>
     // Update dashboard data periodically
     function updateDashboard() {
       fetch('/data')
         .then(response => response.json())
         .then(data => {
           // Update sensor values
           document.getElementById('temperature-value').textContent = data.temperature + ' °C';
           document.getElementById('humidity-value').textContent = data.humidity + ' %';
           document.getElementById('gas-value').textContent = data.gasLevel;
           document.getElementById('timestamp').textContent = data.timestamp;
           
           // Update food status
           const statusElement = document.getElementById('food-status');
           statusElement.textContent = 'Status: ' + data.status;
           statusElement.className = 'food-status';
           
           // Add appropriate class based on status
           if (data.status === 'Normal') {
             statusElement.classList.add('status-normal');
             document.getElementById('recommendation').textContent = 'Food is in optimal storage conditions.';
             document.getElementById('recommendation').className = 'recommendation status-normal';
           } else if (data.status === 'At Risk') {
             statusElement.classList.add('status-risk');
             document.getElementById('recommendation').textContent = 'Consider transferring food to local markets or community kitchens.';
             document.getElementById('recommendation').className = 'recommendation status-risk';
           } else if (data.status === 'Spoiled') {
             statusElement.classList.add('status-spoiled');
             document.getElementById('recommendation').textContent = 'Please transfer food to NGOs or food banks immediately.';
             document.getElementById('recommendation').className = 'recommendation status-spoiled';
           }
           
           // Update device status indicators
           updateDeviceStatus(data.device_status.arduino, 'arduino-status');
           updateDeviceStatus(data.device_status.esp, 'esp-status');
         })
         .catch(error => console.error('Error fetching data:', error));
       
       // Update alerts
       fetch('/alerts')
         .then(response => response.json())
         .then(alerts => {
           const alertsTable = document.getElementById('alerts-table');
           alertsTable.innerHTML = ''; // Clear existing rows
           
           if (alerts.length === 0) {
             const row = alertsTable.insertRow();
             const cell = row.insertCell(0);
             cell.colSpan = 3;
             cell.textContent = 'No alerts yet';
             cell.style.textAlign = 'center';
           } else {
             // Add table headers
             const headerRow = alertsTable.insertRow();
             const headers = ['Time', 'Status', 'AI Recommendation'];
             headers.forEach(header => {
               const th = document.createElement('th');
               th.textContent = header;
               headerRow.appendChild(th);
             });
             
             // Add alert data (showing most recent 3 alerts)
             const recentAlerts = alerts.slice(-3).reverse();
             recentAlerts.forEach(alert => {
               const row = alertsTable.insertRow();
               row.insertCell(0).textContent = alert.timestamp;
               row.insertCell(1).textContent = alert.status;
               row.insertCell(2).textContent = alert.aiRecommendation;
             });
           }
         })
         .catch(error => console.error('Error fetching alerts:', error));
       
       // Update system status
       fetch('/status')
         .then(response => response.json())
         .then(status => {
           // Update ThingSpeak info
           document.getElementById('thingspeak-channel').textContent = status.thingspeak.channel;
           document.getElementById('thingspeak-update').textContent = status.thingspeak.lastUpdate;
           
           // Update device status details
           document.getElementById('arduino-last-seen').textContent = status.arduino.lastSeen;
           document.getElementById('esp-uptime').textContent = status.esp.uptime;
           document.getElementById('esp-ip').textContent = status.esp.ip;
         })
         .catch(error => console.error('Error fetching status:', error));
     }
     
     function updateDeviceStatus(isOnline, elementId) {
       const statusDot = document.getElementById(elementId);
       if (isOnline) {
         statusDot.classList.remove('offline');
         statusDot.classList.add('online');
       } else {
         statusDot.classList.remove('online');
         statusDot.classList.add('offline');
       }
     }
     
     // Load ThingSpeak chart
     function loadThingSpeakChart() {
       const iframe = document.getElementById('thingspeak-chart');
       iframe.src = `https://thingspeak.com/channels/${channelNumber}/charts/1?bgcolor=%23ffffff&color=%234CAF50&dynamic=true&results=30&title=Temperature+History&type=line&xaxis=Time&yaxis=Temperature+%28%C2%B0C%29`;
     }
     
     // Constants
     const channelNumber = 2342262; // Your ThingSpeak channel
     
     // Initial dashboard update and set interval
     document.addEventListener('DOMContentLoaded', function() {
       updateDashboard();
       loadThingSpeakChart();
       setInterval(updateDashboard, 10000); // Update every 10 seconds
     });
   </script>
 </head>
 <body>
   <div class="container">
     <header>
       <h1>Food Monitoring System</h1>
       <div style="display: flex;">
         <div class="status-indicator">
           <div id="arduino-status" class="status-dot offline"></div>
           <span>Arduino</span>
         </div>
         <div class="status-indicator">
           <div id="esp-status" class="status-dot online"></div>
           <span>ESP8266</span>
        </div>
      </div>
    </header>
    
    <div id="food-status" class="food-status status-normal">Status: Initializing</div>
    
    <div class="card">
      <div class="card-title">Current Readings</div>
      <div class="readings">
        <div class="reading-item">
          <div class="reading-label">Temperature</div>
          <div id="temperature-value" class="reading-value">0.0 °C</div>
        </div>
        <div class="reading-item">
          <div class="reading-label">Humidity</div>
          <div id="humidity-value" class="reading-value">0.0 %</div>
        </div>
        <div class="reading-item">
          <div class="reading-label">Gas Level</div>
          <div id="gas-value" class="reading-value">0</div>
        </div>
      </div>
      <div class="reading-item">
        <div class="reading-label">Last Updated</div>
        <div id="timestamp" class="reading-value" style="font-size: 14px;">-</div>
      </div>
      <div id="recommendation" class="recommendation status-normal">
        Food is in optimal storage conditions.
      </div>
    </div>
    
    <div class="card">
      <div class="card-title">Temperature History</div>
      <div class="chart-container">
        <iframe id="thingspeak-chart" width="100%" height="250" frameborder="0"></iframe>
      </div>
    </div>
    
    <div class="card">
      <div class="card-title">Recent Alerts</div>
      <table id="alerts-table" class="table">
        <tr>
          <td colspan="3" style="text-align:center;">Loading alerts...</td>
        </tr>
      </table>
    </div>
    
    <div class="card">
      <div class="card-title">System Information</div>
      <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px;">
        <div>
          <div class="reading-label">ESP8266 IP</div>
          <div id="esp-ip" class="reading-value" style="font-size: 16px;">-</div>
        </div>
        <div>
          <div class="reading-label">ESP8266 Uptime</div>
          <div id="esp-uptime" class="reading-value" style="font-size: 16px;">-</div>
        </div>
        <div>
          <div class="reading-label">Arduino Last Seen</div>
          <div id="arduino-last-seen" class="reading-value" style="font-size: 16px;">-</div>
        </div>
        <div>
          <div class="reading-label">ThingSpeak Channel</div>
          <div id="thingspeak-channel" class="reading-value" style="font-size: 16px;">-</div>
        </div>
        <div>
          <div class="reading-label">Last ThingSpeak Update</div>
          <div id="thingspeak-update" class="reading-value" style="font-size: 16px;">-</div>
        </div>
      </div>
    </div>
    
    <footer>
      Smart Food Monitoring System v1.0 &copy; 2023
    </footer>
  </div>
</body>
</html>
  )";
  
  server.send(200, "text/html", html);
}