from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QLabel, QCheckBox, QFileDialog, QFrame, QProgressBar
from PyQt5.QtCore import Qt, pyqtSlot, QThread
from PyQt5.QtGui import QColor
import re

from file_helper import get_file_total_bytes
from tcp_worker import TcpConnectWorker,TcpTextWorker,TcpRawByteWorker,TcpOTAtWorker

from working_tab_base import WorkingTabBase
from connect_tab import ConnectTab
from tcp_worker import TcpWorkerBase

class InfoGlobeControlPanelTab(WorkingTabBase):

    '''
        TX Data Tab Definition
    '''
    def __init__(self, connect_tab : ConnectTab):
        super().__init__()

        # Create the tab components
        tx_data_layout = QVBoxLayout(self)

        tx_data_label = QLabel("InfoGlobe Control Panel")
        tx_data_label.setStyleSheet("font-size: 45px;")
        tx_data_label.setAlignment(Qt.AlignCenter)

        options_layout = QHBoxLayout()
        self.checkbox_option1 = QCheckBox("Blinky Text")
        self.checkbox_option2 = QCheckBox("Option 2")
        options_layout.addWidget(self.checkbox_option1)
        options_layout.addWidget(self.checkbox_option2)

        self.text_entry_tx_data = QLineEdit()

        # Connect the "returnPressed" signal of the Tx data input field to the "send_button_click" function
        self.text_entry_tx_data.returnPressed.connect(self.send_button_click)

        send_button = QPushButton('Send')
        send_button.clicked.connect(self.send_button_click)

        send_bytes_button = QPushButton('Send Bytes')  # New "Send Bytes" button
        send_bytes_button.clicked.connect(self.send_bytes_button_click)  # Connect to appropriate function

        tx_data_layout.addWidget(tx_data_label)
        tx_data_layout.addLayout(options_layout)
        tx_data_layout.addWidget(self.text_entry_tx_data)
        tx_data_layout.addWidget(send_button)
        tx_data_layout.addWidget(send_bytes_button)  # Add the new button to the layout
        self.setLayout(tx_data_layout)
        self.connect_tab = connect_tab

    # Callback function for the Send button click
    def send_button_click(self):
        
        # Get user input 
        input_text = self.text_entry_tx_data.text()  # Get text from the input field
        print(f"Send button clicked! Text: {input_text}")  # Print the clicked button and input text

        # Now connect!
        host = self.connect_tab.host_entry.text()
        port = self.connect_tab.meessage_port_entry.text()
        print(f"Connect button clicked! Host: {host}, Port: {port}")

        if self.tcp_worker_thread is None:
            blinky_enable = self.checkbox_option1.isChecked()
            self.tcp_worker = TcpTextWorker(host, port, input_text, blinky_enable)
            self.tcp_worker_thread = QThread()
            self.tcp_worker.moveToThread(self.tcp_worker_thread)
            self.tcp_worker.finished.connect(self.tcp_worker_finished)
            # Connect the connect_success signal to a slot
            self.tcp_worker_thread.started.connect(self.tcp_worker.run)
            self.tcp_worker_thread.start()

        # Clear the field
        self.text_entry_tx_data.setText("")

    def get_host_port(self):
        host = self.connect_tab.host_entry.text()
        port = self.connect_tab.meessage_port_entry.text()
        return host, port

    # Callback function for the Send bytes button click
    def send_bytes_button_click(self):
        
        # Get user input
        input_text = self.text_entry_tx_data.text()  # Get text from the input field
        
        # Remove non-hex characters using a regular expression
        input_text_hex = re.sub(r'[^0-9a-fA-F]', '', input_text)

        # Remove spaces
        input_text_without_spaces = input_text_hex.replace(" ", "")

        # Convert to hex
        if len(input_text_without_spaces) % 2 == 1:
            input_text_without_spaces = input_text_without_spaces[:-1] + "0" + input_text_without_spaces[-1]

        bytes_result = bytes.fromhex(input_text_without_spaces)
        print(bytes_result)
        
        print(f"Send bytes button clicked! Text: {input_text}")  # Print the clicked button and input text
        print(f"Hex bytes: {bytes_result}")  # Print the formatted hexadecimal bytes
        
        # Now connect!
        host, port = self.get_host_port()
        print(f"Connect button clicked! Host: {host}, Port: {port}")
        tcp_worker = TcpRawByteWorker(host, port, bytes_result)
        
        # Make the worker
        self.make_worker_thread(tcp_worker, True)
        
        # Clear the field
        self.text_entry_tx_data.setText("")
