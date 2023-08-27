import json
import sys
from PyQt5.QtGui import QIcon
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QTabWidget, QLabel, QDesktopWidget, QCheckBox, QFileDialog, QFrame, QProgressBar
from PyQt5.QtCore import Qt, QObject, pyqtSignal, pyqtSlot, QThread
from PyQt5.QtGui import QPalette, QColor
from qt_material import apply_stylesheet
import re

from tcp_worker import TcpConnectWorker,TcpTextWorker,TcpRawByteWorker,TcpOTAtWorker

class QtAppWithTabs(QWidget):
    def __init__(self):
        super().__init__()
        self.init_ui()  # Initialize the UI components
        self.tcp_worker = None
        self.tcp_worker_thread = None

    def tcp_worker_finished(self):
        print("TCP worker finished")
        self.tcp_worker_thread.quit()
        self.tcp_worker_thread.wait()
        self.tcp_worker_thread.deleteLater()
        self.tcp_worker_thread = None
        self.tcp_worker = None

    '''
        Connect Tab Definition
    '''

    # Initialize the Connect tab UI components
    def connect_tab_init(self):
        connect_tab = QWidget()
        connect_layout = QVBoxLayout(connect_tab)

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
        connect_tab.setLayout(connect_layout)

        # Load default host and port from file
        self.load_host_port()

        return connect_tab
    
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

    '''
        TX Data Tab Definition
    '''

    # Initialize the Tx Data tab UI components
    def tx_data_tab_init(self):
        tx_data_tab = QWidget()
        tx_data_layout = QVBoxLayout(tx_data_tab)

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
        tx_data_tab.setLayout(tx_data_layout)

        return tx_data_tab
    
    # Callback function for the Send button click
    def send_button_click(self):
        
        # Get user input 
        input_text = self.text_entry_tx_data.text()  # Get text from the input field
        print(f"Send button clicked! Text: {input_text}")  # Print the clicked button and input text

        # Now connect!
        host = self.text_entry_connect.text()
        port = self.port_entry_connect.text()
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

    '''
        OTA Tab Definition
    '''

    def ota_tab_init(self):
        ota_tab = QWidget()
        ota_layout = QVBoxLayout(ota_tab)

        ota_label = QLabel("Select OTA Binary:")
        ota_label.setStyleSheet("font-size: 45px;")
        ota_label.setAlignment(Qt.AlignCenter)

        self.file_path_text = QLineEdit()

        self.file_path_label = QLabel("Selected binary file: ")
        load_file_button = QPushButton('Load File')
        load_file_button.clicked.connect(self.load_file_button_click)  # Connect to the new function

        send_button = QPushButton('Send Binary')
        send_button.clicked.connect(self.send_binary_button_click)

        # Inside your existing code
        progress_bar = QProgressBar()

        # Now set the layout
        ota_layout.addWidget(ota_label)

        # Group ota file select
        ota_file_select_layout = QHBoxLayout()  # Use QHBoxLayout for horizontal arrangement
        ota_file_select_layout.addWidget(self.file_path_label)  # Add the file path label
        ota_file_select_layout.addWidget(self.file_path_text)
        ota_file_select_layout.addWidget(load_file_button)  # Add the Load File button

        ota_layout.addLayout(ota_file_select_layout)  # Add the horizontal layout
        ota_layout.addWidget(send_button)

        ota_layout.addWidget(progress_bar)

        ota_tab.setLayout(ota_layout)

        return ota_tab

    
    # Callback function for the Send button click
    def send_binary_button_click(self):
        
        # Get user input 
        binary_file_path = self.file_path_text.text()  # Get text from the input field
        print(f"Sending file: {binary_file_path}")  # Print the clicked button and input text

        # Now connect!
        host = self.text_entry_connect.text()
        port = self.port_entry_connect.text()
        chunk_size = 1024
        print(f"Connect button clicked! Host: {host}, Port: {port}. Chunk size: {chunk_size}")

        if self.tcp_worker_thread is None:
            self.tcp_worker = TcpOTAtWorker(host, port, binary_file_path, chunk_size)
            self.tcp_worker_thread = QThread()
            self.tcp_worker.moveToThread(self.tcp_worker_thread)
            self.tcp_worker.finished.connect(self.tcp_worker_finished)
            # Connect the connect_success signal to a slot
            self.tcp_worker_thread.started.connect(self.tcp_worker.run)
            self.tcp_worker_thread.start()

    def load_file_button_click(self):
        file_dialog = QFileDialog()
        file_path, _ = file_dialog.getOpenFileName()
        
        if file_path:
            self.file_path_text.setText(file_path)

    # Initialize the main UI components
    def init_ui(self):
        self.setWindowTitle("Qt App with Tabs and Buttons")  # Set window title
        self.setWindowIcon(QIcon('./python/Dakirby309-Simply-Styled-Xbox.ico'))  # Set window icon

        self.resize(700, 400)  # Set initial size of the window

        # Calculate the position to center the window on the screen
        screen_geometry = QDesktopWidget().screenGeometry()
        x = (screen_geometry.width() - self.width()) // 2
        y = (screen_geometry.height() - self.height()) // 2
        self.move(x, y)

        layout = QVBoxLayout(self)  # Main layout for the entire window
        tab_widget = QTabWidget()  # Widget to hold tabs

        connect_tab = self.connect_tab_init()  # Initialize the Connect tab
        tx_data_tab = self.tx_data_tab_init()    # Initialize the Tx Data tab
        ota_tab = self.ota_tab_init()    # Initialize the OTA tab

        tab_widget.addTab(connect_tab, "Connect")  # Add Connect tab to the tab widget
        tab_widget.addTab(tx_data_tab, "Tx Data")  # Add Tx Data tab to the tab widget
        tab_widget.addTab(ota_tab, "Over-The-Air")  # Add Tx Data tab to the tab widget

        layout.addWidget(tab_widget)  # Add tab widget to the main layout
        self.setLayout(layout)  # Set the main layout for the window
        self.show()  # Show the window


    def load_palette_from_file(file_path):
        with open(file_path, 'r') as f:
            palette_data = json.load(f)

        palette = QPalette()
        for role, color_values in palette_data.items():
            color = QColor(*color_values)
            palette.setColor(getattr(QPalette, role), color)

        return palette

# Entry point of the program
if __name__ == '__main__':    
    app = QApplication(sys.argv)
    apply_stylesheet(app, theme='light_blue.xml')  # Applying the Material style
    

    qt_app = QtAppWithTabs()  # Create an instance of the QtAppWithTabs class
    sys.exit(app.exec_())  # Execute the application event loop and handle exit
