import sys
import time
import socket
from PyQt5.QtGui import QIcon
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QTabWidget, QLabel, QDesktopWidget, QCheckBox
from PyQt5.QtCore import Qt, QObject, pyqtSignal, pyqtSlot, QThread

from tcp_client import InfoGlobeController

def globe_action(globe: InfoGlobeController):
    # blink_idx = 0xc
    blink_idx = 0x0
    shift_idx = 13

    tx_data = bytes([0x05,0x00])
    tx_data += "< Booting >".encode()
    # tx_data += "New ".encode()
    # tx_data += hex(blink_idx).encode()
    # tx_data += " New".encode()
    tx_data += bytes([0x0])
    tx_data += bytes([blink_idx,shift_idx])
    globe.send_bytes(tx_data)
    # time.sleep(2)

    # globe("hell11o")
    # globe.set_text_single_scroll_left("Connecting...")
    # globe.set_text_single_scroll_left("\x81")
    # globe.set_text_stationary_front("my_string")
    # globe.set_text_stationary_right("my_string")
    # globe.instant_blank()

class TcpWorker(QObject):
    finished = pyqtSignal()  # Signal to indicate that the worker has finished

    def __init__(self, host, port):
        super().__init__()
        self.host = host
        self.port = port

    @pyqtSlot()
    def run(self):
        
        try:
            # Connect to host
            print(f"Connecting to InfoGlobe: {self.host}:{self.port}")
            globe = InfoGlobeController(self.host, int(self.port))
        except socket.gaierror as err:
            print(f"Could not connect due to a DNS resolution error: {err}")
        except Exception as err:
            print("An unexpected error occurred:", err)

        # Simulate some work
        self.finished.emit()