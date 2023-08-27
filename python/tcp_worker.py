import sys
import time
import socket
from PyQt5.QtGui import QIcon
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QTabWidget, QLabel, QDesktopWidget, QCheckBox
from PyQt5.QtCore import Qt, QObject, pyqtSignal, pyqtSlot, QThread

from file_helper import get_file_total_bytes
from tcp_client import InfoGlobeController

class TcpWorkerBase(QObject):
    finished = pyqtSignal()  # Signal to indicate that the worker has finished
    connect_status = pyqtSignal(int)  # Signal to indicate that the worker has finished

    def __init__(self, host, port):
        super().__init__()
        self.host = host
        self.port = port
        self.globe = None

    def connect_to_globe(self) -> InfoGlobeController:
        try:
            # Connect to host
            print(f"Connecting to InfoGlobe: {self.host}:{self.port}")
            globe = InfoGlobeController(self.host, int(self.port))
            # Successful connect
            self.connect_status.emit(0)
            return globe
        except socket.gaierror as err:
            print(f"Could not connect due to a DNS resolution error: {err}")
            self.connect_status.emit(-1)
            return None
        except Exception as err:
            print("An unexpected error occurred:", err)
            self.connect_status.emit(-2)
            return None

    @pyqtSlot()
    def run(self):
        
        # Connect to the globe
        globe = self.connect_to_globe()
        if globe is None:
            print(f"Failed to connect to globe @ {self.host}:{self.port}")

        # Exit
        self.finished.emit()

class TcpConnectWorker(TcpWorkerBase):
    pass

class TcpTextWorker(TcpWorkerBase):

    def __init__(self, host, port, message, blinky_enable):
        super().__init__(host,port)
        self.message = message
        self.blinky_enable = blinky_enable

    @pyqtSlot()
    def run(self):
        
        # Connect to the globe
        globe = self.connect_to_globe()
        if globe is not None:
            msg = f"{self.message}"
            blink_idx = len(msg)+1 if self.blinky_enable else 0
            shift_idx = len(msg)

            tx_data = bytes([0x05,0x00])
            tx_data += msg.encode()
            tx_data += bytes([0x0])
            tx_data += bytes([blink_idx,shift_idx])
            globe.send_bytes(tx_data)
        else:
            print(f"Failed to connect to globe @ {self.host}:{self.port}")

        # Exit
        self.finished.emit()

class TcpRawByteWorker(TcpWorkerBase):

    def __init__(self, host, port, byte_buffer):
        super().__init__(host,port)
        self.byte_buffer = byte_buffer

    @pyqtSlot()
    def run(self):

        # Connect to the globe
        globe = self.connect_to_globe()
        if globe is not None:
            tx_data = bytes(self.byte_buffer)
            globe.send_bytes(tx_data)
        else:
            print(f"Failed to connect to globe @ {self.host}:{self.port}")


        # Simulate some work
        self.finished.emit()

class TcpOTAtWorker(TcpWorkerBase):
    progress_percent = pyqtSignal(float)

    def __init__(self, host, port, binary_file_path, chunk_size):
        super().__init__(host,port)
        self.binary_file_path = binary_file_path
        self.binary_size = get_file_total_bytes(self.binary_file_path)
        self.chunk_size = chunk_size

    @pyqtSlot()
    def run(self):
        
        # Connect to the globe
        globe = self.connect_to_globe()
        if globe is None:
            print(f"Failed to connect to globe @ {self.host}:{self.port}")
            tx_data = bytes(self.byte_buffer)
            globe.send_bytes(tx_data)
            # Simulate some work
            self.finished.emit()
            return
        
        # Inside the run method after the connection is established
        try:
            total_tx_size = 0
            with open(self.binary_file_path, 'rb') as binary_file:

                while True:
                    chunk = binary_file.read(self.chunk_size)  # Define 'chunk_size' appropriately
                    total_tx_size += len(chunk)
                    if not chunk:
                        break  # No more data to send

                    globe.send_bytes(chunk)  # Use the appropriate method to send data

                    # Update percentage
                    self.progress_percent.emit(total_tx_size/self.binary_size)

                    # TODO: Need to check for ACK/NACK
                    data = globe.s.recv(1024)
                    # if not data:
                    #     print("got empty data, exiting")
                    # else:
                    #     print("Received: ",end='')
                    #     print(data)

        except FileNotFoundError:
            print("Binary file not found.")
        except Exception as err:
            print("An error occurred while reading and sending data:", err)

        self.finished.emit()