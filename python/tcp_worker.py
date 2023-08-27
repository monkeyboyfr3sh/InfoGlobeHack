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

class TcpConnectWorker(QObject):
    finished = pyqtSignal()  # Signal to indicate that the worker has finished
    connect_status = pyqtSignal(int)  # Signal to indicate that the worker has finished

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
            self.connect_status.emit(0)
        except socket.gaierror as err:
            print(f"Could not connect due to a DNS resolution error: {err}")
            self.connect_status.emit(-1)
        except Exception as err:
            print("An unexpected error occurred:", err)
            self.connect_status.emit(-2)

        # Simulate some work
        self.finished.emit()

class TcpTextWorker(QObject):
    finished = pyqtSignal()  # Signal to indicate that the worker has finished
    connect_status = pyqtSignal(int)  # Signal to indicate that the worker has finished

    def __init__(self, host, port, message, blinky_enable):
        super().__init__()
        self.host = host
        self.port = port
        self.message = message
        self.blinky_enable = blinky_enable

    @pyqtSlot()
    def run(self):
        
        try:
            # Connect to host
            print(f"Connecting to InfoGlobe: {self.host}:{self.port}")
            globe = InfoGlobeController(self.host, int(self.port))

            msg = f"{self.message}"
            blink_idx = len(msg)+1 if self.blinky_enable else 0
            shift_idx = len(msg)

            tx_data = bytes([0x05,0x00])
            tx_data += msg.encode()
            tx_data += bytes([0x0])
            tx_data += bytes([blink_idx,shift_idx])
            globe.send_bytes(tx_data)

            self.connect_status.emit(0)
        except socket.gaierror as err:
            print(f"Could not connect due to a DNS resolution error: {err}")
            self.connect_status.emit(-1)
        except Exception as err:
            print("An unexpected error occurred:", err)
            self.connect_status.emit(-2)

        # Simulate some work
        self.finished.emit()

class TcpRawByteWorker(QObject):
    finished = pyqtSignal()  # Signal to indicate that the worker has finished
    connect_status = pyqtSignal(int)  # Signal to indicate that the worker has finished

    def __init__(self, host, port, byte_buffer):
        super().__init__()
        self.host = host
        self.port = port
        self.byte_buffer = byte_buffer

    @pyqtSlot()
    def run(self):
        
        try:
            # Connect to host
            print(f"Connecting to InfoGlobe: {self.host}:{self.port}")
            globe = InfoGlobeController(self.host, int(self.port))

            tx_data = bytes(self.byte_buffer)
            # tx_data = bytes([self.byte_buffer])
            globe.send_bytes(tx_data)

            self.connect_status.emit(0)
        except socket.gaierror as err:
            print(f"Could not connect due to a DNS resolution error: {err}")
            self.connect_status.emit(-1)
        except Exception as err:
            print("An unexpected error occurred:", err)
            self.connect_status.emit(-2)

        # Simulate some work
        self.finished.emit()

class TcpOTAtWorker(QObject):
    finished = pyqtSignal()  # Signal to indicate that the worker has finished
    connect_status = pyqtSignal(int)  # Signal to indicate that the worker has finished
    progress_percent = pyqtSignal(float)

    def __init__(self, host, port, binary_file_path, chunk_size):
        super().__init__()
        self.host = host
        self.port = port
        self.binary_file_path = binary_file_path
        self.binary_size = self.get_file_total_bytes(self.binary_file_path)
        self.chunk_size = chunk_size

    @pyqtSlot()
    def run(self):
        
        try:
            # Connect to host
            print(f"Connecting to InfoGlobe: {self.host}:{self.port}")
            globe = InfoGlobeController(self.host, int(self.port))

            # Inside the run method after the connection is established
            try:
                with open(self.binary_file_path, 'rb') as binary_file:

                    while True:
                        chunk = binary_file.read(self.chunk_size)  # Define 'chunk_size' appropriately
                        if not chunk:
                            break  # No more data to send

                        globe.send_bytes(chunk)  # Use the appropriate method to send data

                        # Update percentage
                        self.progress_percent.emit(len(chunk)/self.binary_size)

            except FileNotFoundError:
                print("Binary file not found.")
            except Exception as err:
                print("An error occurred while reading and sending data:", err)

            self.connect_status.emit(0)
        except socket.gaierror as err:
            print(f"Could not connect due to a DNS resolution error: {err}")
            self.connect_status.emit(-1)
        except Exception as err:
            print("An unexpected error occurred:", err)
            self.connect_status.emit(-2)

        # Simulate some work
        self.finished.emit()

    def get_file_total_bytes(self, file_path):
        total_bytes = 0
        with open(file_path, 'rb') as binary_file:
            total_bytes = len(binary_file.read())
        return total_bytes