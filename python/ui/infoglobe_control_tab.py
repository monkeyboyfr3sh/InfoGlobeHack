from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QLabel, QCheckBox, QFileDialog, QFrame, QProgressBar
from PyQt5.QtCore import Qt, pyqtSlot, QThread
from PyQt5.QtGui import QColor
import re

from file_helper import get_file_total_bytes
from tcp_worker import TcpConnectWorker,TcpTextWorker,TcpRawByteWorker,TcpOTAtWorker,TcpButtonWorker,TcpButtonConfigWorker

from working_tab_base import WorkingTabBase
from connect_tab import ConnectTab
from tcp_worker import TcpWorkerBase

class InfoGlobeControlPanelTab(WorkingTabBase):

    '''
        TX Data Tab Definition
    '''
    def __init__(self, connect_tab: ConnectTab):
        super().__init__()

        # Create the tab components
        tx_data_layout = QVBoxLayout(self)

        tx_data_label = QLabel("InfoGlobe Control Panel")
        tx_data_label.setStyleSheet("font-size: 45px;")
        tx_data_label.setAlignment(Qt.AlignCenter)

        # Create a horizontal layout for the text entry and checkbox
        text_entry_checkbox_layout = QHBoxLayout()

        self.command_entry = QLineEdit()
        self.update_checkbox = QCheckBox("Update Command")

        # Add the text entry and checkbox to the horizontal layout
        text_entry_checkbox_layout.addWidget(self.command_entry)
        text_entry_checkbox_layout.addWidget(self.update_checkbox)

        # Creating two columns of buttons
        button_column1 = QVBoxLayout()
        button_column2 = QVBoxLayout()
        num_buttons = 6
        for i in range(num_buttons):
            button = QPushButton(f'Button {i+1}')
            # Using a lambda function to pass custom input to the click function
            button.clicked.connect(lambda checked, button_number=i+1: self.send_button_click(button_number))

            if (i % 2) == 0:
                button_column1.addWidget(button)
            else:
                button_column2.addWidget(button)

        buttons_layout = QHBoxLayout()
        buttons_layout.addLayout(button_column1)
        buttons_layout.addLayout(button_column2)

        tx_data_layout.addWidget(tx_data_label)
        tx_data_layout.addLayout(text_entry_checkbox_layout)  # Add the horizontal layout
        tx_data_layout.addLayout(buttons_layout)  # Add the new buttons layout to the main layout

        self.setLayout(tx_data_layout)
        self.connect_tab = connect_tab

    # Callback function for the Send button click
    def send_button_click(self, button_number):
        print(f'Button {button_number} clicked')
        config_checked = self.update_checkbox.isChecked()
        self.update_checkbox.setChecked(False)

        # Get user input 
        input_text = self.command_entry.text()  # Get text from the input field
        print(f"Send button clicked! Text: {input_text}")  # Print the clicked button and input text

        # Now connect!
        host = self.connect_tab.host_entry.text()
        port = 44444
        print(f"Connect button clicked! Host: {host}, Port: {port}")

        # Make the worker
        if config_checked:
            tcp_worker = TcpButtonConfigWorker(host, port, button_number, input_text)
        else:
            tcp_worker = TcpButtonWorker(host, port, button_number)
        
        self.make_worker_thread(tcp_worker, True)

        # Clear the field
        self.command_entry.setText("")

    def get_host(self):
        host = self.connect_tab.host_entry.text()
        return host

    def get_message_port(self):
        message_port = self.connect_tab.meessage_port_entry.text()
        return message_port

    def get_ota_port(self):
        message_port = self.connect_tab.meessage_port_entry.text()
        return message_port