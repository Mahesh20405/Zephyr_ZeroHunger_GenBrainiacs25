# Food Monitoring System Dashboard
# Visualizes training metrics and system status using data from GAS endpoints

import pandas as pd
import streamlit as st
import requests
import plotly.express as px
from datetime import datetime

# Configuration
GAS_URL = "https://script.google.com/macros/s/AKfycbz6jmwEbuMyKvYidRMaNCmQjII9nFamC1oDGLOuki7BuBHmH2vO7yloBRkGQXVqahxj/exec"
REFRESH_INTERVAL = 300  # 5 minutes

def fetch_csv_data(sheet_name):
    """Fetch CSV data from Google Apps Script endpoint"""
    try:
        params = {'action': 'get', 'sheetName': sheet_name}
        response = requests.get(GAS_URL, params=params)
        response.raise_for_status()
        return pd.read_csv(pd.compat.StringIO(response.text))
    except Exception as e:
        st.error(f"Error fetching {sheet_name} data: {str(e)}")
        return pd.DataFrame()

def load_training_metrics():
    """Load training metrics from GAS endpoint"""
    df = fetch_csv_data('TrainingMetrics')
    if not df.empty:
        # Convert numeric columns
        numeric_cols = ['Cycle', 'Epoch', 'Loss', 'Reward', 'Accuracy']
        df[numeric_cols] = df[numeric_cols].apply(pd.to_numeric, errors='coerce')
        
        # Convert timestamp
        df['Timestamp'] = pd.to_datetime(df['Timestamp'])
    return df

def load_email_logs():
    """Load email logs from GAS endpoint"""
    df = fetch_csv_data('EmailLog')
    if not df.empty:
        df['Timestamp'] = pd.to_datetime(df['Timestamp'])
    return df

def main():
    st.set_page_config(
        page_title="Food Monitoring Dashboard",
        layout="wide",
        initial_sidebar_state="expanded"
    )

    st.title("Food Distribution Optimization Dashboard")
    
    # Create layout containers
    header = st.container()
    metrics_row = st.container()
    charts_row = st.container()
    alerts_col, recipients_col = st.columns(2)

    with header:
        st.markdown("""
        ### Real-time System Monitoring
        *Latest updates refresh every 5 minutes*
        """)
    
    # Auto-refresh logic
    last_refresh = datetime.now()
    if 'last_refresh' not in st.session_state:
        st.session_state.last_refresh = last_refresh
    else:
        if (datetime.now() - st.session_state.last_refresh).seconds > REFRESH_INTERVAL:
            st.experimental_rerun()
    
    # Load data
    metrics_df = load_training_metrics()
    email_logs_df = load_email_logs()
    
    with metrics_row:
        st.header("Training Performance Metrics")
        
        if not metrics_df.empty:
            latest_cycle = metrics_df['Cycle'].max()
            latest_metrics = metrics_df[metrics_df['Cycle'] == latest_cycle]
            
            col1, col2, col3, col4 = st.columns(4)
            with col1:
                st.metric("Current Training Cycle", latest_cycle)
            with col2:
                avg_loss = latest_metrics['Loss'].mean()
                st.metric("Average Loss", f"{avg_loss:.4f}")
            with col3:
                avg_reward = latest_metrics['Reward'].mean()
                st.metric("Average Reward", f"{avg_reward:.2f}")
            with col4:
                avg_accuracy = latest_metrics['Accuracy'].mean()
                st.metric("Decision Accuracy", f"{avg_accuracy:.1f}%")
        else:
            st.warning("No training metrics data available")

    with charts_row:
        if not metrics_df.empty:
            tab1, tab2, tab3 = st.tabs(["Loss Progression", "Reward Trends", "Accuracy Development"])
            
            with tab1:
                fig = px.line(metrics_df, x='Epoch', y='Loss', color='Cycle',
                              title="Training Loss Across Cycles")
                st.plotly_chart(fig, use_container_width=True)
            
            with tab2:
                fig = px.line(metrics_df, x='Epoch', y='Reward', color='Cycle',
                              title="Average Reward Across Cycles")
                st.plotly_chart(fig, use_container_width=True)
            
            with tab3:
                fig = px.line(metrics_df, x='Epoch', y='Accuracy', color='Cycle',
                              title="Decision Accuracy Across Cycles")
                st.plotly_chart(fig, use_container_width=True)
    
    with alerts_col:
        st.header("Recent System Alerts")
        if not email_logs_df.empty:
            email_logs_df = email_logs_df.sort_values('Timestamp', ascending=False)
            for _, row in email_logs_df.head(5).iterrows():
                st.markdown(f"""
                **{row['Timestamp'].strftime('%Y-%m-%d %H:%M')}**  
                {row['Message']}  
                *Status: {row['Status']} | Sent to: {row['Recipient']}*
                """)
                st.divider()
        else:
            st.info("No recent alerts to display")

    # Display refresh status
    st.sidebar.markdown(f"**Last Updated:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    if st.sidebar.button("Manual Refresh"):
        st.experimental_rerun()

if __name__ == "__main__":
    main()