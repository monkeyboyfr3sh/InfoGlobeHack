from PyQt5.QtWidgets import QApplication, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QLabel, QFileDialog, QProgressBar
from PyQt5.QtCore import Qt, pyqtSlot, QThread

from file_helper import get_file_total_bytes
from tcp_worker import TcpOTAtWorker

from working_tab_base import WorkingTabBase
from connect_tab import ConnectTab
import os

class OTATab(WorkingTabBase):

    '''
        OTA Tab Definition
    '''
    def __init__(self, connect_tab : ConnectTab):
        super().__init__()
        self.tcp_worker = None

        ota_label = QLabel("Select OTA Binary:")
        ota_label.setAlignment(Qt.AlignCenter)
        ota_label.setStyleSheet("font-size: 45px;")

        self.file_path_text = QLineEdit()  # Create the line edit widget for file path input
        self.file_path_text.returnPressed.connect(self.send_binary_button_click)

        self.file_path_label = QLabel("Selected binary file: ")
        load_file_button = QPushButton('Load File')
        load_file_button.clicked.connect(self.load_file_button_click)  # Connect to the load file function

        # Add a QLabel for status display
        self.status_label = QLabel("Status: Disconnected")
        self.status_label.setAlignment(Qt.AlignCenter)
        self.status_label.setStyleSheet("font-size: 30px;")

        send_button = QPushButton('Send Binary')
        send_button.clicked.connect(self.send_binary_button_click)  # Connect to the send binary function

        self.progress_bar = QProgressBar(self)
        self.progress_bar.setValue(0)

        # Group ota file select widgets in a horizontal layout
        ota_file_layout = QHBoxLayout()
        ota_file_layout.addWidget(self.file_path_label)
        ota_file_layout.addWidget(self.file_path_text)
        ota_file_layout.addWidget(load_file_button)

        # Create the main layout for the tab
        ota_layout = QVBoxLayout(self)

        # Add all widgets and layouts to the main layout
        ota_layout.addWidget(ota_label)
        ota_layout.addLayout(ota_file_layout)
        ota_layout.addWidget(send_button)
        ota_layout.addWidget(self.progress_bar)

        # Add the status label to the layout
        ota_layout.addWidget(self.status_label)

        self.setLayout(ota_layout)  # Set the main layout for the tab
        self.connect_tab = connect_tab
    
    # Function to update the status label
    def update_status(self, status):
        self.status_label.setText(f"Status: {status}")

    # Callback function for the Send button click
    def send_binary_button_click(self):
        
        # Get user input 
        binary_file_path = self.file_path_text.text()  # Get text from the input field
        print(f"Sending file: {binary_file_path}")  # Print the clicked button and input text

        # Check for empty path
        if binary_file_path=="":
            print("Empty path string!")
            self.update_status("Invalid Path")
            return

        # Check if the file exists
        if not os.path.exists(binary_file_path):
            print("File does not exist!")
            self.update_status("File does not exist!")
            return

        # Now connect!
        host = self.connect_tab.host_entry.text()
        port = self.connect_tab.ota_port_entry.text()
        chunk_size = 4096
        file_size = get_file_total_bytes(binary_file_path)
        print(f"Connect button clicked! Host: {host}, Port: {port}")
        print(f"File Size: {file_size}, Chunk Size: {chunk_size}")

        # Don't want to start these up for nothing
        if self.tcp_worker is None:

            tcp_worker = TcpOTAtWorker(host, port, binary_file_path, chunk_size)
            tcp_worker.progress_percent.connect(self.progress_percent_update)
            tcp_worker.exit_status.connect(self.ota_exit_status)
            tcp_worker.connect_status.connect(self.on_connect_status)

            # Make the worker
            self.make_worker_thread(tcp_worker, True)
        else:
            print("OTA worker is currently active!")

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
        
        # Update status to "Updating"
        self.update_status("Updating")

        # Force the progress bar to repaint to immediately show the changes
        QApplication.processEvents()

    # Function to update status when OTA update is complete
    @pyqtSlot(int)
    def ota_exit_status(self, exit_status):
        
        if(exit_status==0):
            self.update_status("OTA Complete!")
        elif(exit_status==1):
            self.update_status("Invalid file!")
        else:
            self.update_status("OTA Failed!")

    # Slot to be called when the connect_success signal is emitted
    @pyqtSlot(int)
    def on_connect_status(self, connect_status):
        if(connect_status>=0):
            print("Connection successful!")  # You can add any logic you want here
            self.update_status("Connected")

        else:
            print(f"Connect error{connect_status}")
            self.update_status("Could not connect!")
