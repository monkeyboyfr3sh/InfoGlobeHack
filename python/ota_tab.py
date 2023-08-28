from PyQt5.QtWidgets import QApplication, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QLabel, QFileDialog, QProgressBar
from PyQt5.QtCore import Qt, pyqtSlot, QThread

from file_helper import get_file_total_bytes
from tcp_worker import TcpOTAtWorker

from working_tab_base import WorkingTabBase
from connect_tab import ConnectTab

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
        host = self.connect_tab.host_entry.text()
        port = self.connect_tab.ota_port_entry.text()
        chunk_size = 4096
        file_size = get_file_total_bytes(binary_file_path)
        print(f"Connect button clicked! Host: {host}, Port: {port}")
        print(f"File Size: {file_size}, Chunk Size: {chunk_size}")

        tcp_worker = TcpOTAtWorker(host, port, binary_file_path, chunk_size)
        tcp_worker.progress_percent.connect(self.progress_percent_update)

        # Make the worker
        self.make_worker_thread(tcp_worker, True)

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
