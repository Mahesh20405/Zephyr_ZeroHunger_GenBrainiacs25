# Food Monitoring System - Deep Q-Learning Training Script
# This script trains a reinforcement learning model to optimize food distribution decisions
# based on sensor data from the monitoring system

import numpy as np
import pandas as pd
import torch
import torch.nn as nn
import torch.optim as optim
import torch.nn.functional as F
import random
import time
import requests
import csv
import os
from datetime import datetime
from collections import deque, namedtuple

# For Google Sheets API
import gspread
from google.oauth2.service_account import Credentials

# Constants
BATCH_SIZE = 64
GAMMA = 0.99  # discount factor
EPS_START = 0.9
EPS_END = 0.05
EPS_DECAY = 200
TARGET_UPDATE = 10
MEMORY_SIZE = 10000
MIN_ENTRIES = 100  # Minimum entries before training

# Google Sheets and GAS configuration
SPREADSHEET_URL = "spreadsheet_url"
GAS_URL = "gas_url"

# Configure Google Sheets authentication for VSCode
# You need to download service account credentials from Google Cloud Console
def setup_google_sheets():
    # Path to your service account credentials file
    SERVICE_ACCOUNT_FILE = 'E:/Zephyr/Software Configuration/Deep_Q_Reinforcement/service_account_credentials.json.json'
    
    if not os.path.exists(SERVICE_ACCOUNT_FILE):
        print(f"ERROR: {SERVICE_ACCOUNT_FILE} not found.")
        print("Please follow these steps to set up authentication:")
        print("1. Go to Google Cloud Console (https://console.cloud.google.com/)")
        print("2. Create a project or select an existing one")
        print("3. Enable the Google Sheets API")
        print("4. Create a service account")
        print("5. Download the JSON key file")
        print("6. Save it as 'service_account_credentials.json' in the same directory as this script")
        return None
    
    # Define the scopes
    SCOPES = [
        'https://www.googleapis.com/auth/spreadsheets',
        'https://www.googleapis.com/auth/drive'
    ]
    
    # Create credentials object
    creds = Credentials.from_service_account_file(SERVICE_ACCOUNT_FILE, scopes=SCOPES)
    
    # Create gspread client
    client = gspread.authorize(creds)
    
    # Return the client
    return client

# Named tuple for storing transitions
Transition = namedtuple('Transition', ('state', 'action', 'next_state', 'reward'))

# Replay Memory
class ReplayMemory:
    def __init__(self, capacity):
        self.memory = deque([], maxlen=capacity)

    def push(self, *args):
        self.memory.append(Transition(*args))

    def sample(self, batch_size):
        return random.sample(self.memory, batch_size)

    def __len__(self):
        return len(self.memory)

# Neural Network for Deep Q Learning
class DQN(nn.Module):
    def __init__(self, input_size, output_size):
        super(DQN, self).__init__()
        self.fc1 = nn.Linear(input_size, 128)
        self.fc2 = nn.Linear(128, 128)
        self.fc3 = nn.Linear(128, output_size)

    def forward(self, x):
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        return self.fc3(x)

# Function to preprocess data
def preprocess_data(data):
    # Convert data to appropriate formats
    data_df = pd.DataFrame(data[1:], columns=data[0])

    # Convert numerical columns, coercing errors to NaN
    for col in ['Temperature', 'Humidity', 'Gas']:
        data_df[col] = pd.to_numeric(data_df[col], errors='coerce')  # Handle invalid values

    # Drop rows with NaN in critical sensor columns
    data_df.dropna(subset=['Temperature', 'Humidity', 'Gas'], inplace=True)

    # Proceed only if there's data remaining
    if data_df.empty:
        raise ValueError("No valid data remaining after preprocessing.")

    # Create time feature (hours since storage)
    data_df['Timestamp'] = pd.to_datetime(data_df['Timestamp'])
    data_df = data_df.sort_values('Timestamp')
    data_df['TimeSinceStart'] = (data_df['Timestamp'] - data_df['Timestamp'].min()).dt.total_seconds() / 3600

    # One-hot encode status
    status_map = {'Normal': 0, 'At Risk': 1, 'Spoiled': 2}
    data_df['StatusCode'] = data_df['Status'].map(status_map)

    return data_df

# Function to calculate reward based on food status and action
def calculate_reward(status, action):
    # Action: 0 = Keep, 1 = Market, 2 = Food Bank/NGO

    if status == 'Normal':  # Normal condition
        if action == 0:  # Keep in storage
            return 1
        elif action == 1:  # Send to market
            return 0
        else:  # Send to NGO
            return -1

    elif status == 'At Risk':  # At risk condition
        if action == 0:  # Keep in storage
            return -1
        elif action == 1:  # Send to market
            return 1
        else:  # Send to NGO
            return 0

    else:  # Spoiled condition
        if action == 0:  # Keep in storage
            return -2
        elif action == 1:  # Send to market
            return -3
        else:  # Send to NGO
            return 1

# Function to get action from epsilon-greedy policy
def select_action(state, policy_net, steps_done, n_actions=3):
    sample = random.random()
    eps_threshold = EPS_END + (EPS_START - EPS_END) * np.exp(-1. * steps_done / EPS_DECAY)

    if sample > eps_threshold:
        with torch.no_grad():
            return policy_net(state).max(1)[1].view(1, 1)
    else:
        return torch.tensor([[random.randrange(n_actions)]], dtype=torch.long)

# Function to optimize model
def optimize_model(policy_net, target_net, optimizer, memory):
    if len(memory) < BATCH_SIZE:
        return 0

    transitions = memory.sample(BATCH_SIZE)
    batch = Transition(*zip(*transitions))

    non_final_mask = torch.tensor(tuple(map(lambda s: s is not None, batch.next_state)), dtype=torch.bool)
    non_final_next_states = torch.cat([s for s in batch.next_state if s is not None])

    state_batch = torch.cat(batch.state)
    action_batch = torch.cat(batch.action)
    reward_batch = torch.cat(batch.reward)

    state_action_values = policy_net(state_batch).gather(1, action_batch)

    next_state_values = torch.zeros(BATCH_SIZE)
    next_state_values[non_final_mask] = target_net(non_final_next_states).max(1)[0].detach()

    expected_state_action_values = (next_state_values * GAMMA) + reward_batch

    loss = F.smooth_l1_loss(state_action_values, expected_state_action_values.unsqueeze(1))

    optimizer.zero_grad()
    loss.backward()
    for param in policy_net.parameters():
        param.grad.data.clamp_(-1, 1)
    optimizer.step()

    return loss.item()

# Main training function
def train_model(data_df, cycle):
    # Initialize models
    input_size = 4  # [temperature, humidity, gas, time_since_storage]
    output_size = 3  # [keep, market, NGO]

    policy_net = DQN(input_size, output_size)
    target_net = DQN(input_size, output_size)
    target_net.load_state_dict(policy_net.state_dict())
    target_net.eval()

    optimizer = optim.RMSprop(policy_net.parameters())
    memory = ReplayMemory(MEMORY_SIZE)

    steps_done = 0
    num_episodes = 50
    metrics = []

    for i_episode in range(num_episodes):
        # Initialize environment
        episode_data = data_df.sample(n=10).sort_values('TimeSinceStart')
        current_idx = 0
        total_reward = 0
        losses = []
        correct_actions = 0

        state = torch.tensor([episode_data.iloc[current_idx][['Temperature', 'Humidity', 'Gas', 'TimeSinceStart']].values],
                             dtype=torch.float32)

        for t in range(len(episode_data) - 1):
            # Select and perform an action
            action = select_action(state, policy_net, steps_done)
            steps_done += 1

            # Move to next state
            current_idx += 1
            next_state = torch.tensor([episode_data.iloc[current_idx][['Temperature', 'Humidity', 'Gas', 'TimeSinceStart']].values],
                                      dtype=torch.float32)

            # Get status and calculate reward
            status = episode_data.iloc[current_idx]['Status']
            reward_val = calculate_reward(status, action.item())
            reward = torch.tensor([reward_val], dtype=torch.float32)

            total_reward += reward_val

            # Determine correct action based on status
            correct_action = 0  # Keep in storage
            if status == 'At Risk':
                correct_action = 1  # Send to market
            elif status == 'Spoiled':
                correct_action = 2  # Send to NGO

            if action.item() == correct_action:
                correct_actions += 1

            # Store the transition in memory
            memory.push(state, action, next_state, reward)

            # Move to the next state
            state = next_state

            # Perform one step of the optimization
            loss = optimize_model(policy_net, target_net, optimizer, memory)
            if loss > 0:
                losses.append(loss)

            # Update the target network
            if t % TARGET_UPDATE == 0:
                target_net.load_state_dict(policy_net.state_dict())

        # Calculate episode metrics
        avg_loss = np.mean(losses) if losses else 0
        accuracy = correct_actions / (len(episode_data) - 1) * 100

        print(f"Episode {i_episode+1}/{num_episodes} - "
              f"Loss: {avg_loss:.4f}, Reward: {total_reward:.2f}, Accuracy: {accuracy:.2f}%")

        # Save metrics
        metrics.append({
            'epoch': i_episode + 1,
            'loss': avg_loss,
            'reward': total_reward,
            'accuracy': accuracy,
            'timestamp': datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        })

    # Create models directory if it doesn't exist
    os.makedirs('models', exist_ok=True)
    
    # Save model
    model_path = f'models/food_monitoring_model_cycle_{cycle}.pth'
    torch.save(policy_net.state_dict(), model_path)
    print(f"Model saved to {model_path}")

    return metrics

# Function to save metrics via GAS
def save_metrics_to_sheet(metrics, cycle):
    # Create a metrics directory if it doesn't exist
    os.makedirs('metrics', exist_ok=True)
    
    # Save locally as CSV
    df = pd.DataFrame(metrics)
    csv_filename = f'E:/Zephyr/Software Configuration/metrics/training_metrics_{cycle}.csv'
    df.to_csv(csv_filename, index=False)
    print(f"Metrics saved locally to {csv_filename}")

    # Read CSV data
    with open(csv_filename, 'r') as f:
        csv_data = f.read()

    try:
        # Prepare payload for GAS
        payload = {
            'action': 'append',
            'sheetName': 'TrainingMetrics',
            'csvData': csv_data
        }

        # Send POST request to GAS
        response = requests.post(GAS_URL, data=payload)
        response.raise_for_status()
        print(f"Training metrics for cycle {cycle} sent to Google Sheets via GAS.")
    except requests.exceptions.RequestException as e:
        print(f"Failed to send metrics to Google Sheets: {e}")
        print("Continuing with local data only.")

# Function to read existing metrics
def get_existing_metrics():
    try:
        params = {'action': 'get', 'sheetName': 'TrainingMetrics'}
        response = requests.get(GAS_URL, params=params)
        response.raise_for_status()
        csv_data = response.text
        existing_metrics = list(csv.reader(csv_data.splitlines()))
        
        if len(existing_metrics) > 1:
            # Check if the first column contains cycle numbers
            try:
                current_cycle = max(int(row[0]) for row in existing_metrics[1:])
                return current_cycle
            except (ValueError, IndexError):
                print("Could not parse cycle numbers from metrics data")
                return 0
        else:
            return 0
    except Exception as e:
        print(f"Error fetching existing metrics: {e}")
        print("Will start from cycle 0")
        return 0
    
def get_local_cycle():
    try:
        with open('training_cycle.txt', 'r') as f:
            return int(f.read())
    except:
        return 0

def update_local_cycle(cycle):
    with open('training_cycle.txt', 'w') as f:
        f.write(str(cycle))

# Main execution loop
def main():
    # Setup Google Sheets connection
    gc = setup_google_sheets()
    if gc is None:
        print("Failed to set up Google Sheets authentication. Exiting.")
        return
    
    try:
        # Open the spreadsheet
        spreadsheet = gc.open_by_url(SPREADSHEET_URL)
        
        # Access the sheets
        sensor_data_sheet = spreadsheet.worksheet("SensorData")
        recipients_sheet = spreadsheet.worksheet("Recipients")
        
        print("Successfully connected to Google Sheets")
    except Exception as e:
        print(f"Error connecting to Google Sheets: {e}")
        print("Please make sure:")
        print("1. Your service account credentials are correct")
        print("2. The service account has been granted access to the spreadsheet")
        print("3. The spreadsheet URL is correct")
        return

    # Get current training cycle
    current_cycle = get_existing_metrics()
    last_entry_count = 0

    print(f"Starting monitoring for training. Current cycle: {current_cycle}")
    print(f"Will begin training when sensor data reaches {MIN_ENTRIES} entries.")

    while True:
        try:
            # Get current sensor data
            sensor_data = sensor_data_sheet.get_all_values()
            current_entry_count = len(sensor_data) - 1  # Subtract header row

            print(f"Current entries: {current_entry_count}, Last checked: {last_entry_count}")

            # Check if we've collected enough new data for training
            if current_entry_count >= last_entry_count + MIN_ENTRIES:
                current_cycle += 1
                print(f"Starting training cycle {current_cycle}")

                # Preprocess data
                data_df = preprocess_data(sensor_data)

                # Train model
                metrics = train_model(data_df, current_cycle)

                # Save metrics
                save_metrics_to_sheet(metrics, current_cycle)

                # Update last entry count
                last_entry_count = current_entry_count

                print(f"Completed training cycle {current_cycle}")
                print(f"Waiting for {MIN_ENTRIES} more entries before next cycle")

        except Exception as e:
            print(f"Error during execution: {e}")
            print("Will retry in 60 seconds...")

        # Wait before checking again
        time.sleep(60)  # Check every minute

if __name__ == "__main__":
    main()