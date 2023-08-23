import sys
from PyQt5.QtGui import QIcon
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QTabWidget, QLabel
from PyQt5.QtCore import Qt  # Import the Qt module

class QtAppWithTabs(QWidget):
    def __init__(self):
        super().__init__()
        self.init_ui()

    def connect_button_click(self):
        input_text = self.text_entry_connect.text()
        print(f"Connect button clicked! Text: {input_text}")

    def send_button_click(self):
        input_text = self.text_entry_tx_data.text()
        print(f"Send button clicked! Text: {input_text}")

    def init_ui(self):
        self.setWindowTitle("Qt App with Tabs and Buttons")
        self.setGeometry(100, 100, 400, 300)
        self.setWindowIcon(QIcon('./python/Dakirby309-Simply-Styled-Xbox.ico'))  # Replace with actual icon path

        layout = QVBoxLayout(self)
        tab_widget = QTabWidget()

        connect_tab = QWidget()
        connect_layout = QVBoxLayout(connect_tab)

        connect_label = QLabel("Enter host:")
        connect_label.setAlignment(Qt.AlignCenter)  # Use Qt.AlignCenter here
        self.text_entry_connect = QLineEdit()
        connect_button = QPushButton('Connect')
        connect_button.clicked.connect(self.connect_button_click)

        connect_layout.addWidget(connect_label)
        connect_layout.addWidget(self.text_entry_connect)
        connect_layout.addWidget(connect_button)
        connect_tab.setLayout(connect_layout)

        tx_data_tab = QWidget()
        tx_data_layout = QVBoxLayout(tx_data_tab)

        tx_data_label = QLabel("Enter Tx Data:")
        self.text_entry_tx_data = QLineEdit()
        send_button = QPushButton('Send')
        send_button.clicked.connect(self.send_button_click)

        tx_data_layout.addWidget(tx_data_label)
        tx_data_layout.addWidget(self.text_entry_tx_data)
        tx_data_layout.addWidget(send_button)
        tx_data_tab.setLayout(tx_data_layout)

        tab_widget.addTab(connect_tab, "Connect")
        tab_widget.addTab(tx_data_tab, "Tx Data")

        layout.addWidget(tab_widget)
        self.setLayout(layout)
        self.show()

if __name__ == '__main__':
    app = QApplication(sys.argv)
    qt_app = QtAppWithTabs()
    sys.exit(app.exec_())
