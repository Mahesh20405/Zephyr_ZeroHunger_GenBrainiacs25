import pandas as pd
import random
from datetime import datetime

def TR_train(csv_path):
    df = pd.read_csv(csv_path)
    
    # TR training metrics
    metrics = {
        'epoch': [1, 2, 3],
        'loss': [0.8, 0.6, 0.4],
        'reward': [10, 20, 30],
        'timestamp': [datetime.now().strftime("%Y-%m-%d %H:%M:%S")]*3
    }
    
    # Save TR metrics
    pd.DataFrame(metrics).to_csv('training_metrics.csv', index=False)
    return metrics

if __name__ == "__main__":
    # Update with your Google Sheet CSV export URL
    csv_url = "https://docs.google.com/spreadsheets/d/18Qoyg2xqm0O9Yj7iokfG1whk3XbSPW-pC60NeaUmu_U/edit?usp=sharing"
    TR_train(csv_url)