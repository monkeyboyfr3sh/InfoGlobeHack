import json
import sys
from PyQt5.QtGui import QIcon
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QTabWidget, QLabel, QDesktopWidget, QCheckBox, QFileDialog, QFrame, QProgressBar, QStyleFactory
from PyQt5.QtCore import Qt, QObject, pyqtSignal, pyqtSlot, QThread, QFile
from PyQt5.QtGui import QPalette, QColor
from qt_material import apply_stylesheet
import re

from file_helper import get_file_total_bytes
from tcp_worker import TcpConnectWorker,TcpTextWorker,TcpRawByteWorker,TcpOTAtWorker

class WorkingTabBase(QWidget):
    def __init__(self):
        super().__init__()
        self.tcp_worker = None
        self.tcp_worker_thread = None

    def tcp_worker_finished(self):
        print("TCP worker finished")
        self.tcp_worker_thread.quit()
        self.tcp_worker_thread.wait()
        self.tcp_worker_thread.deleteLater()
        self.tcp_worker_thread = None
        self.tcp_worker = None

class ConnectTab(WorkingTabBase):

    '''
        Connect Tab Definition
    '''
    def __init__(self):
        super().__init__()

        # Create the tab components
        connect_layout = QVBoxLayout(self)

        connect_label = QLabel("Enter host:")
        connect_label.setStyleSheet("font-size: 30px;")
        connect_label.setAlignment(Qt.AlignCenter)
        self.text_entry_connect = QLineEdit()

        port_label = QLabel("Enter port:")
        port_label.setStyleSheet("font-size: 30px;")
        port_label.setAlignment(Qt.AlignCenter)
        self.port_entry_connect = QLineEdit()

        # Connect the "returnPressed" signal of the port input field to the "connect_button_click" function
        self.text_entry_connect.returnPressed.connect(self.connect_button_click)
        self.port_entry_connect.returnPressed.connect(self.connect_button_click)

        self.connect_button = QPushButton('Connect')  # Store the button as an attribute
        self.connect_button.clicked.connect(self.connect_button_click)

        connect_layout.addWidget(connect_label)
        connect_layout.addWidget(self.text_entry_connect)
        connect_layout.addWidget(port_label)
        connect_layout.addWidget(self.port_entry_connect)
        connect_layout.addWidget(self.connect_button)  # Add the button to the layout
        self.setLayout(connect_layout)

        # Load default host and port from file
        self.load_host_port()

    def connect_button_click(self):
        host = self.text_entry_connect.text()
        port = self.port_entry_connect.text()
        print(f"Connect button clicked! Host: {host}, Port: {port}")

        if self.tcp_worker_thread is None:
            self.tcp_worker = TcpConnectWorker(host, port)
            self.tcp_worker_thread = QThread()
            self.tcp_worker.moveToThread(self.tcp_worker_thread)
            self.tcp_worker.finished.connect(self.tcp_worker_finished)
            # Connect the connect_success signal to a slot
            self.tcp_worker.connect_status.connect(self.on_connect_status)
            self.tcp_worker_thread.started.connect(self.tcp_worker.run)
            self.tcp_worker_thread.start()

    # Slot to be called when the connect_success signal is emitted
    @pyqtSlot(int)
    def on_connect_status(self, connect_status):
        if(connect_status>=0):
            print("Connection successful!")  # You can add any logic you want here
            self.set_connect_box_color('green')

            # Save the host and port after successfully connecting
            self.save_host_port(self.tcp_worker.host, self.tcp_worker.port)

        else:
            print(f"Connect error{connect_status}")
            self.set_connect_box_color('red')

    def load_host_port(self):
        try:
            with open('./python/config.json', 'r') as file:
                data = json.load(file)
                self.text_entry_connect.setText(data["host"])
                self.port_entry_connect.setText(data["port"])
        except (FileNotFoundError, KeyError, json.JSONDecodeError):
            # If there's any error reading the file or the required data, 
            # we can simply pass and continue. This ensures that the application 
            # doesn't crash if the file doesn't exist yet or if it's malformed.
            pass

    def save_host_port(self, host, port):
        with open('./python/config.json', 'w') as file:
            json.dump({"host": host, "port": port}, file)

    def set_connect_box_color(self, status):
        palette = self.text_entry_connect.palette()
        if status == 'green':
            background_color = QColor(0, 255, 0)  # Green
        elif status == 'red':
            background_color = QColor(255, 0, 0)  # Red
        else:
            return  # Invalid status, do nothing
        
        # Set the background color using a stylesheet
        self.connect_button.setStyleSheet(f"background-color: {background_color.name()};")

class TXDataTab(WorkingTabBase):

    '''
        TX Data Tab Definition
    '''
    def __init__(self, connect_tab : ConnectTab):
        super().__init__()

        # Create the tab components
        tx_data_layout = QVBoxLayout(self)

        tx_data_label = QLabel("Enter Tx Data:")
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
        host = self.connect_tab.text_entry_connect.text()
        port = self.connect_tab.port_entry_connect.text()
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
        host = self.text_entry_connect.text()
        port = self.port_entry_connect.text()
        print(f"Connect button clicked! Host: {host}, Port: {port}")

        if self.tcp_worker_thread is None:
            blinky_enable = self.checkbox_option1.isChecked()
            self.tcp_worker = TcpRawByteWorker(host, port, bytes_result)
            self.tcp_worker_thread = QThread()
            self.tcp_worker.moveToThread(self.tcp_worker_thread)
            self.tcp_worker.finished.connect(self.tcp_worker_finished)
            # Connect the connect_success signal to a slot
            self.tcp_worker_thread.started.connect(self.tcp_worker.run)
            self.tcp_worker_thread.start()

        # Clear the field
        self.text_entry_tx_data.setText("")

class OTATab(WorkingTabBase):

    '''
        OTA Tab Definition
    '''
    def __init__(self, connect_tab : ConnectTab):
        super().__init__()
        self.tcp_worker = None
        self.tcp_worker_thread = None

        ota_layout = QVBoxLayout(self)  # Create the main layout for the tab

        ota_label = QLabel("Select OTA Binary:")
        ota_label.setAlignment(Qt.AlignCenter)
        ota_label.setStyleSheet("font-size: 45px;")

        self.file_path_text = QLineEdit()  # Create the line edit widget for file path input

        self.file_path_label = QLabel("Selected binary file: ")
        load_file_button = QPushButton('Load File')
        load_file_button.clicked.connect(self.load_file_button_click)  # Connect to the load file function

        send_button = QPushButton('Send Binary')
        send_button.clicked.connect(self.send_binary_button_click)  # Connect to the send binary function

        self.progress_bar = QProgressBar(self)

        # Group ota file select widgets in a horizontal layout
        ota_file_layout = QHBoxLayout()
        ota_file_layout.addWidget(self.file_path_label)
        ota_file_layout.addWidget(self.file_path_text)
        ota_file_layout.addWidget(load_file_button)

        # Add all widgets and layouts to the main layout
        ota_layout.addWidget(ota_label)
        ota_layout.addLayout(ota_file_layout)
        ota_layout.addWidget(send_button)
        ota_layout.addWidget(self.progress_bar)

        self.setLayout(ota_layout)  # Set the main layout for the tab
        self.connect_tab = connect_tab
    
    # Callback function for the Send button click
    def send_binary_button_click(self):
        
        # Get user input 
        binary_file_path = self.file_path_text.text()  # Get text from the input field
        print(f"Sending file: {binary_file_path}")  # Print the clicked button and input text

        # Now connect!
        host = self.connect_tab.text_entry_connect.text()
        port = self.connect_tab.port_entry_connect.text()
        chunk_size = 4096
        file_size = get_file_total_bytes(binary_file_path)
        print(f"Connect button clicked! Host: {host}, Port: {port}")
        print(f"File Size: {file_size}, Chunk Size: {chunk_size}")

        if self.tcp_worker_thread is None:
            self.tcp_worker = TcpOTAtWorker(host, port, binary_file_path, chunk_size)
            self.tcp_worker_thread = QThread()
            self.tcp_worker.moveToThread(self.tcp_worker_thread)
            self.tcp_worker.finished.connect(self.tcp_worker_finished)
            self.tcp_worker.progress_percent.connect(self.progress_percent_update)
            # Connect the connect_success signal to a slot
            self.tcp_worker_thread.started.connect(self.tcp_worker.run)
            self.tcp_worker_thread.start()

        # TODO: Need to make soem constructor method, and get the start like this
        # if(thread not exist)
            # create tcp worker
            # connect worker signals
            # thread constructor method with optional auto start


    def load_file_button_click(self):
        file_dialog = QFileDialog()
        file_path, _ = file_dialog.getOpenFileName()
        
        if file_path:
            self.file_path_text.setText(file_path)

    @pyqtSlot(float)
    def progress_percent_update(self, percent):
        # Update the progress bar value based on your progress
        progress = int(percent * 100)
        self.progress_bar.setValue(progress)
        
        # Force the progress bar to repaint to immediately show the changes
        QApplication.processEvents()
