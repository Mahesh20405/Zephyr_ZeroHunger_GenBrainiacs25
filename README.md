# Food Monitoring System with Deep Q-Learning

## Overview
This project implements a comprehensive food monitoring system that uses IoT sensors and Deep Q-Learning to optimize food distribution decisions. The system monitors environmental conditions, predicts food quality, and recommends optimal distribution strategies.

## Key Features
- Real-time monitoring of temperature, humidity, and gas levels
- Deep Q-Learning based decision making
- Multi-language support (English, Tamil, Telugu, Kannada, Malayalam)
- Web dashboard for visualization and control
- Email notification system
- Integration with Google Sheets for data storage
- IPFS for storing training metrics

## System Architecture
The system consists of three main components:

1. **Hardware Layer**
   - Arduino-based sensors for temperature, humidity, and gas detection
   - ESP module for WiFi connectivity

2. **Software Layer**
   - Flask web application
   - Deep Q-Learning model
   - Data visualization dashboard
   - Email notification system

3. **Cloud Integration**
   - Google Sheets for data storage
   - IPFS for decentralized metric storage

## Installation

### Prerequisites
- Python 3.8+
- Arduino IDE
- Google Cloud account
- Google Sheets API enabled

### Setup
1. Clone the repository:
   ```bash
   git clone https://github.com/yourusername/food-monitoring-system.git
   cd food-monitoring-system
   ```

2. Install Python dependencies:
   ```bash
   pip install -r requirements.txt
   ```

3. Set up Google Sheets API:
   - Create a Google Cloud project
   - Enable Google Sheets API
   - Create service account credentials
   - Save credentials as `service_account_credentials.json`

4. Configure Arduino:
   - Upload `ARD.ino` to Arduino board
   - Upload `ESP.ino` to ESP module

5. Configure Flask application:
   - Update `app.py` with your Google Sheets URL
   - Set up email credentials in `config.py`

## Usage

### Running the System
1. Start the Flask application:
   ```bash
   python app.py
   ```

2. Access the dashboard at `http://localhost:5000`

3. Monitor sensor data and view recommendations

### Training the Model
1. Collect at least 100 sensor readings
2. Start training from the dashboard
3. View training metrics and progress

## Documentation

### File Structure
```
├── Hardware Configuration/
│   ├── Arduino/
│   │   ├── ARD/
│   │   │   └── ARD.ino
│   │   └── ESP/
│   │       └── ESP.ino
├── Software Configuration/
│   ├── Deep_Q_Reinforcement/
│   │   ├── DeepQ_Reinforcement.ipynb
│   │   ├── deepq_reinforcement.py
│   │   └── service_account_credentials.json.json
│   ├── app.py
│   ├── dashboard.py
│   ├── train.py
│   ├── templates/
│   │   ├── dashboard.html
│   │   └── index.html
│   └── metrics/
│       └── service_account_credentials.json.json
└── README.md
```

### API Endpoints
- `/` - Main dashboard
- `/api/sensor_data` - Get latest sensor data
- `/api/train` - Start model training
- `/api/recommendation` - Get AI recommendation
- `/api/send_email` - Send email notification

## Troubleshooting
**Issue:** Google Sheets API not working  
**Solution:** Verify service account credentials and ensure the account has access to the spreadsheet

**Issue:** Sensor data not updating  
**Solution:** Check Arduino and ESP connections, verify WiFi credentials

**Issue:** Training not starting  
**Solution:** Ensure at least 100 sensor readings are available

