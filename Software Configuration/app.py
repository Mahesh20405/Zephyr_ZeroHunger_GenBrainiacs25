import os
import json
import time
import csv
import requests
import pandas as pd
import subprocess
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from flask import Flask, render_template, request, jsonify, send_file, Response
from datetime import datetime
import io
import re
import base64
import glob
import gspread
from oauth2client.service_account import ServiceAccountCredentials


app = Flask(__name__)

# Configuration
THINGSPEAK_CHANNEL = "<YOUR_CHANNEL_NUMBER>"
THINGSPEAK_API_KEY = "<YOUR_THINGSPEAK_API_KEY>"
PINATA_API_KEY = "<YOUR_PINATA_API_KEY>"
PINATA_SECRET_KEY = "<YOUR_PINATA_SECRET_KEY>"
GEMINI_API_KEY = "<YOUR_GEMINI_API_KEY>"
GSCRIPT_EMAIL_ENDPOINT = "<YOUR_GSCRIPT_EMAIL_ENDPOINT>"
GSHEET_URL = "<YOUR_GOOGLE_SHEET_URL>"
METRICS_FOLDER = "<YOUR_METRICS_FOLDER_PATH>"
TRAINING_SCRIPT_PATH = "<YOUR_TRAINING_SCRIPT_PATH>"
GOOGLE_SHEET_URL = "<YOUR_GOOGLE_SHEET_URL>"
CREDENTIALS_PATH = "<YOUR_CREDENTIALS_FILE_PATH>"

# Function to get the latest metrics file
def get_latest_metrics_file():
    # Match files with pattern 'training_metrics_<number>.csv'
    pattern = os.path.join(METRICS_FOLDER, "training_metrics_*.csv")
    files = glob.glob(pattern)
    
    if not files:
        return None
    
    # Extract numeric portion from filenames and sort numerically
    def extract_number(file_path):
        filename = os.path.basename(file_path)
        match = re.search(r'training_metrics_(\d+)\.csv$', filename)
        return int(match.group(1)) if match else -1
    
    # Sort files by their numeric identifier in descending order
    files.sort(key=extract_number, reverse=True)
    
    return files[0] if files else None

def get_sheet_data():
    """Get data from Google Sheets"""
    try:
        # Set up credentials
        scope = ['https://spreadsheets.google.com/feeds', 'https://www.googleapis.com/auth/drive']
        credentials = ServiceAccountCredentials.from_json_keyfile_name(CREDENTIALS_PATH, scope)
        client = gspread.authorize(credentials)
        
        # Open the spreadsheet and get the SensorData sheet
        spreadsheet = client.open_by_url(GOOGLE_SHEET_URL)
        sensor_sheet = spreadsheet.worksheet("SensorData")
        
        # Get all data
        sensor_data = sensor_sheet.get_all_records()
        
        return sensor_data
    except Exception as e:
        print(f"Error accessing Google Sheets: {str(e)}")
        return None
    
# Function to check if device is online (simple ping)
def check_device_status(ip_address):
    try:
        response = os.system(f"ping -c 1 -W 1 {ip_address}")
        return response == 0
    except:
        return False

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/run_training', methods=['POST'])
def run_training():
    try:
        # Run the Python training script
        result = subprocess.run([
            'python', 
            TRAINING_SCRIPT_PATH
        ], capture_output=True, text=True)
        
        if result.returncode != 0:
            return jsonify({'status': 'error', 'message': result.stderr})
        
        # Check for the latest metrics file
        latest_metrics_file = get_latest_metrics_file()
        if latest_metrics_file:
            return jsonify({'status': 'success', 'message': 'Training completed successfully', 'metrics_file': latest_metrics_file})
        else:
            return jsonify({'status': 'error', 'message': 'Training metrics file not found'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@app.route('/get_training_metrics')
def get_training_metrics():
    latest_metrics_file = get_latest_metrics_file()
    if not latest_metrics_file:
        return jsonify({'status': 'error', 'message': 'No training metrics file found'})
    
    try:
        df = pd.read_csv(latest_metrics_file)
        
        # Create a plot
        plt.figure(figsize=(10, 6))
        
        # Plot accuracy
        plt.subplot(1, 2, 1)
        plt.plot(df['epoch'], df['accuracy'], 'g-', label='Accuracy')
        if 'val_accuracy' in df.columns:
            plt.plot(df['epoch'], df['val_accuracy'], 'g--', label='Validation Accuracy')
        plt.title('Model Accuracy')
        plt.xlabel('Epoch')
        plt.ylabel('Accuracy')
        plt.legend()
        plt.grid(True, linestyle='--', alpha=0.7)
        
        # Plot loss
        plt.subplot(1, 2, 2)
        plt.plot(df['epoch'], df['loss'], 'g-', label='Loss')
        if 'val_loss' in df.columns:
            plt.plot(df['epoch'], df['val_loss'], 'g--', label='Validation Loss')
        plt.title('Model Loss')
        plt.xlabel('Epoch')
        plt.ylabel('Loss')
        plt.legend()
        plt.grid(True, linestyle='--', alpha=0.7)
        
        plt.tight_layout()
        
        # Save plot to a bytes buffer
        buf = io.BytesIO()
        plt.savefig(buf, format='png')
        buf.seek(0)
        plt.close()
        
        # Convert plot to base64 for embedding in HTML
        plot_data = base64.b64encode(buf.read()).decode('utf-8')
        
        # Get the latest metrics - ensure numeric conversion
        latest = df.iloc[-1].to_dict()

        # Convert all numeric values to appropriate types
        numeric_fields = ['epoch', 'loss', 'accuracy', 'val_loss', 'val_accuracy']
        for field in numeric_fields:
            if field in latest:
                try:
                    latest[field] = float(latest[field])
                except (ValueError, TypeError):
                    latest[field] = None

        # Add cycle number
        cycle_num = os.path.basename(latest_metrics_file).split('_')[-1].split('.')[0]
        
        return jsonify({
            'status': 'success', 
            'plot': plot_data,
            'latest_metrics': latest,
            'cycle': cycle_num,
            'metrics_file': latest_metrics_file
        })
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@app.route('/download_metrics')
def download_metrics():
    latest_metrics_file = get_latest_metrics_file()
    if not latest_metrics_file:
        return jsonify({'status': 'error', 'message': 'No training metrics file found'})
    
    return send_file(latest_metrics_file, as_attachment=True)

@app.route('/upload_to_ipfs', methods=['POST'])
def upload_to_ipfs():
    latest_metrics_file = get_latest_metrics_file()
    if not latest_metrics_file:
        return jsonify({'status': 'error', 'message': 'No training metrics file found'})
    
    try:
        url = "https://api.pinata.cloud/pinning/pinFileToIPFS"
        
        # Extract cycle number from filename for metadata
        cycle_num = os.path.basename(latest_metrics_file).split('_')[-1].split('.')[0]
        
        payload = {'pinataMetadata': json.dumps({
            'name': f'trainmetrics_cycle{cycle_num}_{datetime.now().strftime("%Y%m%d_%H%M%S")}'
        })}
        
        files = [
            ('file', (os.path.basename(latest_metrics_file), open(latest_metrics_file, 'rb'), 'text/csv'))
        ]
        
        headers = {
            'pinata_api_key': PINATA_API_KEY,
            'pinata_secret_api_key': PINATA_SECRET_KEY
        }
        
        response = requests.post(url, headers=headers, data=payload, files=files)
        
        if response.status_code == 200:
            ipfs_hash = response.json().get('IpfsHash')
            return jsonify({
                'status': 'success',
                'ipfs_hash': ipfs_hash,
                'gateway_url': f'https://gateway.pinata.cloud/ipfs/{ipfs_hash}',
                'cycle': cycle_num
            })
        else:
            return jsonify({
                'status': 'error',
                'message': f'Pinata error: {response.text}'
            })
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@app.route('/get_thingspeak_data')
def get_thingspeak_data():
    try:
        url = f"https://api.thingspeak.com/channels/{THINGSPEAK_CHANNEL}/feeds.json?api_key={THINGSPEAK_API_KEY}&results=50"
        response = requests.get(url)
        
        if response.status_code == 200:
            data = response.json()
            return jsonify({
                'status': 'success',
                'channel_info': data.get('channel', {}),
                'feeds': data.get('feeds', [])
            })
        else:
            return jsonify({
                'status': 'error',
                'message': f'ThingSpeak API error: {response.text}'
            })
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})
@app.route('/get_gemini_recommendation', methods=['GET'])
def get_gemini_recommendation():
    try:
        # Get language parameter (default to English)
        language = request.args.get('language', 'english')
        
        # Get Google Sheets data
        sensor_data = get_sheet_data()
        
        if not sensor_data:
            return jsonify({'status': 'error', 'message': 'Failed to fetch Google Sheets data'})
        
        # Get only the 5 most recent entries
        recent_data = sensor_data[-5:] if len(sensor_data) >= 5 else sensor_data
        
        # Extract training metrics if available
        training_metrics = "No training metrics available."
        latest_metrics_file = get_latest_metrics_file()
        if latest_metrics_file:
            df = pd.read_csv(latest_metrics_file)
            latest_metrics = df.iloc[-1].to_dict()
            cycle_num = os.path.basename(latest_metrics_file).split('_')[-1].split('.')[0]
            training_metrics = f"Latest training metrics (cycle {cycle_num}): {json.dumps(latest_metrics)}"
        
        # Set the language instruction based on the selected language
        language_instruction = ""
        if language == "tamil":
            language_instruction = "Please respond in Tamil language."
        elif language == "telugu":
            language_instruction = "Please respond in Telugu language."
        elif language == "kannada":
            language_instruction = "Please respond in Kannada language."
        elif language == "malayalam":
            language_instruction = "Please respond in Malayalam language."
        
        # Prepare the prompt for Gemini
        prompt = {
            "contents": [{
                "parts": [{
                    "text": f"""Based on the following data, provide a brief recommendation for system optimization and food status assessment (max 3 sentences):
                    
Sensor Data (most recent): {json.dumps(recent_data)}

{training_metrics}

Keep your recommendation concise, practical and focused on improving system performance and food condition assessment. {language_instruction}"""
                }]
            }]
        }
        
        # Call Gemini API
        gemini_url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key={GEMINI_API_KEY}"
        gemini_response = requests.post(gemini_url, json=prompt)
        
        if gemini_response.status_code != 200:
            return jsonify({'status': 'error', 'message': f'Gemini API error: {gemini_response.text}'})
        
        recommendation = gemini_response.json().get('candidates', [{}])[0].get('content', {}).get('parts', [{}])[0].get('text', "No recommendation available.")
        
        return jsonify({
            'status': 'success',
            'recommendation': recommendation,
            'language': language
        })
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@app.route('/send_email', methods=['POST'])
def send_email():
    try:
        data = request.json
        recommendation = data.get('recommendation', 'No recommendation available')
        recipient = data.get('recipient', '')
        
        if not recipient or '@' not in recipient:
            return jsonify({'status': 'error', 'message': 'Invalid email recipient'})
        
        # Get latest cycle info
        latest_metrics_file = get_latest_metrics_file()
        cycle_info = ""
        if latest_metrics_file:
            cycle_num = os.path.basename(latest_metrics_file).split('_')[-1].split('.')[0]
            cycle_info = f" (based on training cycle {cycle_num})"
        
        # Call Google Apps Script to send email
        payload = {
            'recipient': recipient,
            'subject': f'System Recommendation{cycle_info}',
            'body': f"Here's your recommendation from the dashboard:\n\n{recommendation}",
            'timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        }
        
        response = requests.post(GSCRIPT_EMAIL_ENDPOINT, json=payload)
        
        if response.status_code == 200:
            return jsonify({'status': 'success', 'message': 'Email sent successfully'})
        else:
            return jsonify({'status': 'error', 'message': f'Email sending failed: {response.text}'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@app.route('/get_email_history')
def get_email_history():
    try:
        # For demonstration, we'll return some dummy data
        # In a real app, you would fetch this from the Google Sheet
        # using gspread or similar, which requires OAuth setup
        dummy_history = [
            {'recipient': 'user@example.com', 'timestamp': '2025-04-04 14:30:25', 'status': 'sent'},
            {'recipient': 'admin@example.com', 'timestamp': '2025-04-03 09:15:10', 'status': 'sent'}
        ]
        
        return jsonify({
            'status': 'success',
            'history': dummy_history,
            'sheet_url': GSHEET_URL
        })
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@app.route('/list_all_metrics')
def list_all_metrics():
    try:
        pattern = os.path.join(METRICS_FOLDER, "metricstraining_metrics_*.csv")
        files = glob.glob(pattern)
        
        metrics_files = []
        for file in files:
            cycle_num = os.path.basename(file).split('_')[-1].split('.')[0]
            create_time = datetime.fromtimestamp(os.path.getctime(file)).strftime('%Y-%m-%d %H:%M:%S')
            metrics_files.append({
                'file': os.path.basename(file),
                'cycle': cycle_num,
                'created': create_time,
                'path': file
            })
        
        # Sort by cycle number (newest first)
        metrics_files.sort(key=lambda x: int(x['cycle']), reverse=True)
        
        return jsonify({
            'status': 'success',
            'metrics_files': metrics_files
        })
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

@app.route('/get_metrics_by_cycle/<cycle>')
def get_metrics_by_cycle(cycle):
    try:
        metrics_file = os.path.join(METRICS_FOLDER, f"metricstraining_metrics_{cycle}.csv")
        
        if not os.path.exists(metrics_file):
            return jsonify({'status': 'error', 'message': f'Metrics for cycle {cycle} not found'})
        
        df = pd.read_csv(metrics_file)
        
        # Create a plot
        plt.figure(figsize=(10, 6))
        
        # Plot accuracy
        plt.subplot(1, 2, 1)
        plt.plot(df['epoch'], df['accuracy'], 'g-', label='Accuracy')
        if 'val_accuracy' in df.columns:
            plt.plot(df['epoch'], df['val_accuracy'], 'g--', label='Validation Accuracy')
        plt.title(f'Model Accuracy - Cycle {cycle}')
        plt.xlabel('Epoch')
        plt.ylabel('Accuracy')
        plt.legend()
        plt.grid(True, linestyle='--', alpha=0.7)
        
        # Plot loss
        plt.subplot(1, 2, 2)
        plt.plot(df['epoch'], df['loss'], 'g-', label='Loss')
        if 'val_loss' in df.columns:
            plt.plot(df['epoch'], df['val_loss'], 'g--', label='Validation Loss')
        plt.title(f'Model Loss - Cycle {cycle}')
        plt.xlabel('Epoch')
        plt.ylabel('Loss')
        plt.legend()
        plt.grid(True, linestyle='--', alpha=0.7)
        
        plt.tight_layout()
        
        # Save plot to a bytes buffer
        buf = io.BytesIO()
        plt.savefig(buf, format='png')
        buf.seek(0)
        plt.close()
        
        # Convert plot to base64 for embedding in HTML
        plot_data = base64.b64encode(buf.read()).decode('utf-8')
        
        # Get the latest metrics
        latest = df.iloc[-1].to_dict()
        
        return jsonify({
            'status': 'success', 
            'plot': plot_data,
            'latest_metrics': latest,
            'cycle': cycle,
            'metrics_file': metrics_file
        })
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})

if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5000)