# Import necessary modules
import sys
from PyQt5.QtGui import QIcon
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QTabWidget, QLabel
from PyQt5.QtCore import Qt

# Create a class for the Qt application with tabs
class QtAppWithTabs(QWidget):
    def __init__(self):
        super().__init__()
        self.init_ui()  # Initialize the UI components

    # Callback function for the Connect button click
    def connect_button_click(self):
        input_text = self.text_entry_connect.text()  # Get text from the input field
        print(f"Connect button clicked! Text: {input_text}")  # Print the clicked button and input text

    # Callback function for the Send button click
    def send_button_click(self):
        input_text = self.text_entry_tx_data.text()  # Get text from the input field
        print(f"Send button clicked! Text: {input_text}")  # Print the clicked button and input text

    # Initialize the UI components
    def init_ui(self):
        self.setWindowTitle("Qt App with Tabs and Buttons")  # Set window title
        self.setGeometry(100, 100, 600, 400)  # Set window geometry
        self.setWindowIcon(QIcon('./python/Dakirby309-Simply-Styled-Xbox.ico'))  # Set window icon

        layout = QVBoxLayout(self)  # Main layout for the entire window
        tab_widget = QTabWidget()  # Widget to hold tabs

        # Create the Connect tab
        connect_tab = QWidget()
        connect_layout = QVBoxLayout(connect_tab)

        connect_label = QLabel("Enter host:")  # Label for the input field
        connect_label.setAlignment(Qt.AlignCenter)  # Align the label to the center
        self.text_entry_connect = QLineEdit()  # Input field for host entry
        connect_button = QPushButton('Connect')  # Button to trigger connection
        connect_button.clicked.connect(self.connect_button_click)  # Connect button click to callback

        connect_layout.addWidget(connect_label)
        connect_layout.addWidget(self.text_entry_connect)
        connect_layout.addWidget(connect_button)
        connect_tab.setLayout(connect_layout)  # Set layout for the Connect tab

        # Create the Tx Data tab
        tx_data_tab = QWidget()
        tx_data_layout = QVBoxLayout(tx_data_tab)

        tx_data_label = QLabel("Enter Tx Data:")  # Label for the input field
        tx_data_label.setAlignment(Qt.AlignCenter)  # Align the label to the center
        self.text_entry_tx_data = QLineEdit()  # Input field for Tx data entry
        send_button = QPushButton('Send')  # Button to trigger data send
        send_button.clicked.connect(self.send_button_click)  # Connect button click to callback

        tx_data_layout.addWidget(tx_data_label)
        tx_data_layout.addWidget(self.text_entry_tx_data)
        tx_data_layout.addWidget(send_button)
        tx_data_tab.setLayout(tx_data_layout)  # Set layout for the Tx Data tab

        tab_widget.addTab(connect_tab, "Connect")  # Add Connect tab to the tab widget
        tab_widget.addTab(tx_data_tab, "Tx Data")  # Add Tx Data tab to the tab widget

        layout.addWidget(tab_widget)  # Add tab widget to the main layout
        self.setLayout(layout)  # Set main layout for the window
        self.show()  # Show the window

# Entry point of the program
if __name__ == '__main__':
    app = QApplication(sys.argv)  # Create the application instance
    qt_app = QtAppWithTabs()  # Create an instance of the QtAppWithTabs class
    sys.exit(app.exec_())  # Execute the application event loop and handle exit
