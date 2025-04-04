from flask import Flask, render_template
import pandas as pd
import gspread
from oauth2client.service_account import ServiceAccountCredentials
from datetime import datetime
import requests
import io

app = Flask(__name__)

# Style configuration
THEME = {
    "primary": "#4CAF50",
    "background": "#FFFFFF",
    "text": "#333333"
}

# URL of the web app publishing your Google Sheets
SCRIPT_URL = "Script URL"

def get_data():
    """Get data from Google Sheets using the web app URL"""
    try:
        # Fetch data from SensorData sheet
        sensor_response = requests.get(f"{SCRIPT_URL}?sheet=SensorData")
        sensor_data = pd.read_csv(io.StringIO(sensor_response.text))
        
        # Fetch data from EmailLog sheet
        email_response = requests.get(f"{SCRIPT_URL}?sheet=EmailLog")
        email_log = pd.read_csv(io.StringIO(email_response.text))
        
        # Try to fetch training metrics if available
        try:
            metrics_response = requests.get(f"{SCRIPT_URL}?sheet=training_metrics")
            metrics = pd.read_csv(io.StringIO(metrics_response.text))
        except Exception:
            # If the file doesn't exist yet or there's an error
            metrics = pd.DataFrame()
        
        # Check if there's a second training metrics file
        try:
            metrics1_response = requests.get(f"{SCRIPT_URL}?sheet=trainingmetrics1")
            metrics1 = pd.read_csv(io.StringIO(metrics1_response.text))
            # If it exists, concatenate with the first metrics dataframe
            if not metrics.empty:
                metrics = pd.concat([metrics, metrics1])
        except Exception:
            # If the second file doesn't exist yet
            pass
            
        return {
            'sensor_data': sensor_data.tail(10).to_dict('records') if not sensor_data.empty else [],
            'email_log': email_log.tail(5).to_dict('records') if not email_log.empty else [],
            'metrics': metrics.to_dict('records') if not metrics.empty else [],
            'last_updated': datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            'csv_status': sensor_data.iloc[-1]['Status'] if not sensor_data.empty else "No data"
        }
    except Exception as e:
        print(f"Error fetching data: {e}")
        return {
            'sensor_data': [],
            'email_log': [],
            'metrics': [],
            'last_updated': datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            'csv_status': "Error"
        }

@app.route('/')
def dashboard():
    data = get_data()
    return render_template('dashboard.html', data=data, theme=THEME)

if __name__ == "__main__":
    app.run(debug=True)