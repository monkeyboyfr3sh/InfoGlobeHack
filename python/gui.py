import sys
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QTabWidget, QLabel

def connect_button_click():
    input_text = text_entry_connect.text()
    print(f"Connect button clicked! Text: {input_text}")

def send_button_click():
    input_text = text_entry_tx_data.text()
    print(f"Send button clicked! Text: {input_text}")

if __name__ == '__main__':
    app = QApplication(sys.argv)
    
    # Create a main window
    window = QWidget()
    window.setWindowTitle("Qt App with Tabs and Buttons")
    window.setGeometry(100, 100, 400, 300)
    
    # Create a vertical layout for the window
    layout = QVBoxLayout()
    
    # Create a tab widget
    tab_widget = QTabWidget()
    
    # Create a widget for the "Connect" tab
    connect_tab = QWidget()
    connect_layout = QVBoxLayout(connect_tab)
    
    # Create a text entry spot for "Connect" tab
    text_entry_connect = QLineEdit()
    
    # Create a button for the "Connect" tab
    connect_button = QPushButton('Connect')
    connect_button.clicked.connect(connect_button_click)
    
    # Add the text entry and connect button to the "Connect" tab layout
    connect_layout.addWidget(text_entry_connect)
    connect_layout.addWidget(connect_button)
    
    # Set the "Connect" tab layout for the "Connect" tab
    connect_tab.setLayout(connect_layout)
    
    # Create a widget for the "Tx Data" tab
    tx_data_tab = QWidget()
    tx_data_layout = QVBoxLayout(tx_data_tab)

    # Add content to the "Tx Data" tab
    tx_data_label = QLabel("Enter Tx Data:")
    text_entry_tx_data = QLineEdit()  # Text entry for the "Tx Data" tab
    
    # Create a button for the "Tx Data" tab
    send_button = QPushButton('Send')
    send_button.clicked.connect(send_button_click)

    # Add the "Tx Data" content, text entry, and send button to the layout
    tx_data_layout.addWidget(tx_data_label)
    tx_data_layout.addWidget(text_entry_tx_data)
    tx_data_layout.addWidget(send_button)

    # Set the "Tx Data" tab layout for the "Tx Data" tab
    tx_data_tab.setLayout(tx_data_layout)
    
    # Add both tabs to the tab widget
    tab_widget.addTab(connect_tab, "Connect")
    tab_widget.addTab(tx_data_tab, "Tx Data")
    
    # Add the tab widget to the main layout
    layout.addWidget(tab_widget)
    
    # Set the main layout for the window
    window.setLayout(layout)
    
    # Show the window
    window.show()
    
    sys.exit(app.exec_())
