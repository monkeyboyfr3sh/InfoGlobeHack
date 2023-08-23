import sys
from PyQt5.QtGui import QIcon
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QTabWidget, QLabel, QDesktopWidget, QCheckBox
from PyQt5.QtCore import Qt, QObject, pyqtSignal, pyqtSlot, QThread

from tcp_worker import TcpWorker

class QtAppWithTabs(QWidget):
    def __init__(self):
        super().__init__()
        self.init_ui()  # Initialize the UI components
        self.tcp_worker = None
        self.tcp_worker_thread = None

    def connect_button_click(self):
        host = self.text_entry_connect.text()
        port = self.port_entry_connect.text()
        print(f"Connect button clicked! Host: {host}, Port: {port}")

        if self.tcp_worker_thread is None:
            self.tcp_worker = TcpWorker(host, port)
            self.tcp_worker_thread = QThread()
            self.tcp_worker.moveToThread(self.tcp_worker_thread)
            self.tcp_worker.finished.connect(self.tcp_worker_finished)
            self.tcp_worker_thread.started.connect(self.tcp_worker.run)
            self.tcp_worker_thread.start()

    def tcp_worker_finished(self):
        print("TCP worker finished")
        self.tcp_worker_thread.quit()
        self.tcp_worker_thread.wait()
        self.tcp_worker_thread.deleteLater()
        self.tcp_worker_thread = None
        self.tcp_worker = None

    # Callback function for the Send button click
    def send_button_click(self):
        input_text = self.text_entry_tx_data.text()  # Get text from the input field
        print(f"Send button clicked! Text: {input_text}")  # Print the clicked button and input text

    # Initialize the UI components
    def init_ui(self):
        self.setWindowTitle("Qt App with Tabs and Buttons")  # Set window title
        self.setWindowIcon(QIcon('./python/Dakirby309-Simply-Styled-Xbox.ico'))  # Set window icon

        self.resize(600, 400)  # Set initial size of the window

        # Calculate the position to center the window on the screen
        screen_geometry = QDesktopWidget().screenGeometry()
        x = (screen_geometry.width() - self.width()) // 2
        y = (screen_geometry.height() - self.height()) // 2
        self.move(x, y)

        layout = QVBoxLayout(self)  # Main layout for the entire window
        tab_widget = QTabWidget()  # Widget to hold tabs

        # Create the Connect tab
        connect_tab = QWidget()
        connect_layout = QVBoxLayout(connect_tab)

        connect_label = QLabel("Enter host:")
        connect_label.setAlignment(Qt.AlignCenter)
        self.text_entry_connect = QLineEdit()

        port_label = QLabel("Enter port:")
        port_label.setAlignment(Qt.AlignCenter)
        self.port_entry_connect = QLineEdit()
        
        # Connect the "returnPressed" signal of the port input field to the "connect_button_click" function
        self.port_entry_connect.returnPressed.connect(self.connect_button_click)

        connect_button = QPushButton('Connect')
        connect_button.clicked.connect(self.connect_button_click)

        connect_layout.addWidget(connect_label)
        connect_layout.addWidget(self.text_entry_connect)
        connect_layout.addWidget(port_label)
        connect_layout.addWidget(self.port_entry_connect)
        connect_layout.addWidget(connect_button)
        connect_tab.setLayout(connect_layout)

        # Create the Tx Data tab
        tx_data_tab = QWidget()
        tx_data_layout = QVBoxLayout(tx_data_tab)

        tx_data_label = QLabel("Enter Tx Data:")
        tx_data_label.setAlignment(Qt.AlignCenter)

        options_layout = QHBoxLayout()
        self.checkbox_option1 = QCheckBox("Option 1")
        self.checkbox_option2 = QCheckBox("Option 2")
        options_layout.addWidget(self.checkbox_option1)
        options_layout.addWidget(self.checkbox_option2)

        self.text_entry_tx_data = QLineEdit()
        
        # Connect the "returnPressed" signal of the Tx data input field to the "send_button_click" function
        self.text_entry_tx_data.returnPressed.connect(self.send_button_click)

        send_button = QPushButton('Send')
        send_button.clicked.connect(self.send_button_click)

        tx_data_layout.addWidget(tx_data_label)
        tx_data_layout.addLayout(options_layout)
        tx_data_layout.addWidget(self.text_entry_tx_data)
        tx_data_layout.addWidget(send_button)
        tx_data_tab.setLayout(tx_data_layout)


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
