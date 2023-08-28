import json
from PyQt5.QtWidgets import QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QLabel, QFileDialog, QFrame
from PyQt5.QtCore import Qt, pyqtSlot, QThread
from PyQt5.QtGui import QColor

from tcp_worker import TcpConnectWorker
from working_tab_base import WorkingTabBase

class ConnectTab(WorkingTabBase):

    '''
        Connect Tab Definition
    '''
    def __init__(self):
        super().__init__()

        # Make labels
        self.host_entry_label = QLabel("Enter host:")
        self.host_entry_label.setStyleSheet("font-size: 30px;")
        self.host_entry_label.setAlignment(Qt.AlignCenter)

        self.message_port_label = QLabel("Enter message port:")
        self.message_port_label.setStyleSheet("font-size: 30px;")
        self.message_port_label.setAlignment(Qt.AlignCenter)

        self.ota_port_label = QLabel("Enter OTA port:")
        self.ota_port_label.setStyleSheet("font-size: 30px;")
        self.ota_port_label.setAlignment(Qt.AlignCenter)

        # Make Line entry
        self.host_entry = QLineEdit()
        self.meessage_port_entry = QLineEdit()
        self.ota_port_entry = QLineEdit()

        # Enter key does connect action
        self.host_entry.returnPressed.connect(self.connect_message_button_click)
        self.meessage_port_entry.returnPressed.connect(self.connect_message_button_click)
        self.ota_port_entry.returnPressed.connect(self.connect_ota_button_click)

        # Make buttons
        self.message_connect_button = QPushButton('Connect')
        self.message_connect_button.clicked.connect(self.connect_message_button_click)

        self.ota_connect_button = QPushButton('Connect')
        self.ota_connect_button.clicked.connect(self.connect_ota_button_click)

        # Make Save and Load buttons
        self.save_button = QPushButton('Save')
        self.save_button.clicked.connect(self.show_save_dialog)

        self.load_button = QPushButton('Load')
        self.load_button.clicked.connect(self.show_load_dialog)

        # Wrap the widgets together
        connect_layout = QVBoxLayout(self)
        connect_layout.addWidget(self.host_entry_label)
        connect_layout.addWidget(self.host_entry)

        connect_layout.addWidget(self.message_port_label)
        connect_layout.addWidget(self.meessage_port_entry)
        connect_layout.addWidget(self.message_connect_button)

        connect_layout.addWidget(self.ota_port_label)
        connect_layout.addWidget(self.ota_port_entry)
        connect_layout.addWidget(self.ota_connect_button)

        # Add a horizontal separator (line) between save and load buttons
        separator_line = QFrame()
        separator_line.setFrameShape(QFrame.HLine)
        separator_line.setFrameShadow(QFrame.Sunken)
        connect_layout.addWidget(separator_line)

        button_layout = QHBoxLayout()
        button_layout.addWidget(self.load_button)
        button_layout.addWidget(self.save_button)
        connect_layout.addLayout(button_layout)

        self.setLayout(connect_layout)

        # Load default host and port from file
        self.load_host_port("./python/config.json")

    def connect_message_button_click(self):
        host = self.host_entry.text()
        port = self.meessage_port_entry.text()
        print(f"Connect button clicked! Host: {host}, Port: {port}")

        tcp_worker = TcpConnectWorker(host, port)
        tcp_worker.connect_status.connect(self.message_on_connect_status)

        # Make the worker
        self.make_worker_thread(tcp_worker, True)

    def connect_ota_button_click(self):
        host = self.host_entry.text()
        port = self.ota_port_entry.text()
        print(f"Connect button clicked! Host: {host}, Port: {port}")

        tcp_worker = TcpConnectWorker(host, port)
        tcp_worker.connect_status.connect(self.ota_on_connect_status)

        # Make the worker
        self.make_worker_thread(tcp_worker, True)

    def show_save_dialog(self):
        file_dialog = QFileDialog()
        file_dialog.setAcceptMode(QFileDialog.AcceptSave)
        file_dialog.setNameFilter("JSON files (*.json)")
        if file_dialog.exec_() == QFileDialog.Accepted:
            filename = file_dialog.selectedFiles()[0]
            self.save_host_port(filename, self.host_entry.text(), self.meessage_port_entry.text(), self.ota_port_entry.text())

    def show_load_dialog(self):
        file_dialog = QFileDialog()
        file_dialog.setFileMode(QFileDialog.ExistingFile)
        file_dialog.setNameFilter("JSON files (*.json)")
        if file_dialog.exec_() == QFileDialog.Accepted:
            filename = file_dialog.selectedFiles()[0]
            self.load_host_port(filename)

    def load_host_port(self, filename):
        try:
            with open(filename, 'r') as file:
                data = json.load(file)
                self.host_entry.setText(data["host"])
                self.meessage_port_entry.setText(data["message_port"])
                self.ota_port_entry.setText(data["ota_port"])
        except (FileNotFoundError, KeyError, json.JSONDecodeError):
            # If there's any error reading the file or the required data, 
            # we can simply pass and continue. This ensures that the application 
            # doesn't crash if the file doesn't exist yet or if it's malformed.
            pass

    def save_host_port(self, filename, host, message_port, ota_port):
        with open(filename, 'w') as file:
            json.dump({"host": host, "message_port": message_port, "ota_port": ota_port}, file)

    # Slot to be called when the connect_success signal is emitted
    @pyqtSlot(int)
    def message_on_connect_status(self, connect_status):
        if(connect_status>=0):
            print("Connection successful!")  # You can add any logic you want here
            self.set_connect_box_color('green', self.message_connect_button)

        else:
            print(f"Connect error{connect_status}")
            self.set_connect_box_color('red', self.message_connect_button)

    # Slot to be called when the connect_success signal is emitted
    @pyqtSlot(int)
    def ota_on_connect_status(self, connect_status):
        if(connect_status>=0):
            print("Connection successful!")  # You can add any logic you want here
            self.set_connect_box_color('green', self.ota_connect_button)

        else:
            print(f"Connect error{connect_status}")
            self.set_connect_box_color('red', self.ota_connect_button)

    def set_connect_box_color(self, status, button: QPushButton):

        if status == 'green':
            background_color = QColor(0, 255, 0)  # Green
        elif status == 'red':
            background_color = QColor(255, 0, 0)  # Red
        else:
            return  # Invalid status, do nothing
        
        # Set the background color using a stylesheet
        button.setStyleSheet(f"background-color: {background_color.name()};")
