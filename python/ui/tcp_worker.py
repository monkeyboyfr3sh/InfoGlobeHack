import sys
import time
import socket
from PyQt5.QtGui import QIcon
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QLineEdit, QVBoxLayout, QHBoxLayout, QTabWidget, QLabel, QDesktopWidget, QCheckBox
from PyQt5.QtCore import Qt, QObject, pyqtSignal, pyqtSlot, QThread

from file_helper import get_file_total_bytes
from tcp_client import InfoGlobeController

class TcpWorkerBase(QObject):
    failed_connect = pyqtSignal()  # Signal to indicate failed to connect
    finished = pyqtSignal()  # Signal to indicate that the worker has finished
    connect_status = pyqtSignal(int)  # Signal to indicate that the worker has finished

    def __init__(self, host, port):
        super().__init__()
        self.host = host
        self.port = port
        self.globe = None
        self.worker_started = False

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

    def perform_task(self, globe : InfoGlobeController):

        # Do work!
        pass

    @pyqtSlot()
    def run(self):

        # Connect to the globe
        globe = self.connect_to_globe()

        # Could not connect to the globe
        if globe is None:
            print(f"Failed to connect to globe @ {self.host}:{self.port}")
            self.failed_connect.emit()

        # Successful connect
        else:
            # Indicate that worker has started
            self.worker_started = True

            # Perform task
            self.perform_task(globe)
        
        #  Indicate exit
        self.finished.emit()

class TcpConnectWorker(TcpWorkerBase):
    pass

class TcpTextWorker(TcpWorkerBase):

    def __init__(self, host, port, message, blinky_enable):
        super().__init__(host,port)
        self.message = message
        self.blinky_enable = blinky_enable

    def perform_task(self, globe : InfoGlobeController):
        
        # Successful connect
        msg = f"{self.message}"
        blink_idx = len(msg)+1 if self.blinky_enable else 0
        shift_idx = len(msg)

        tx_data = bytes([0x05,0x00])
        tx_data += msg.encode()
        tx_data += bytes([0x0])
        tx_data += bytes([blink_idx,shift_idx])
        globe.send_bytes(tx_data)

class TcpRawByteWorker(TcpWorkerBase):

    def __init__(self, host, port, byte_buffer):
        super().__init__(host,port)
        self.byte_buffer = byte_buffer

    def perform_task(self, globe : InfoGlobeController):

        # Successful connect
        tx_data = bytes(self.byte_buffer)
        globe.send_bytes(tx_data)

class TcpOTAtWorker(TcpWorkerBase):
    progress_percent = pyqtSignal(float)
    exit_status = pyqtSignal(int)

    def __init__(self, host, port, binary_file_path, chunk_size):
        super().__init__(host,port)
        self.binary_file_path = binary_file_path
        self.chunk_size = chunk_size

    def perform_task(self, globe : InfoGlobeController):
        
        if self.binary_file_path is None:
            print("Empty path")
            self.exit_status.emit(1)
            return

        # Load binary size
        self.binary_size = get_file_total_bytes(self.binary_file_path)

        # Set a receive timeout of 5 seconds
        globe.s.settimeout(5)

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

                    try:
                        response = globe.s.recv(1024)
                        if "ACK" not in response.decode():
                            print("Received from server:", response.decode())
                            self.exit_status.emit(-2)
                            return
                                                    
                    except socket.timeout:
                        print("Receive operation timed out.")
                        self.exit_status.emit(-2)
                        return

                # Look for final ACK/NAK
                try:
                    response = globe.s.recv(1024)
                    print("Received from server:", response.decode())
                except socket.timeout:
                    print("Receive operation timed out.")
                    self.exit_status.emit(-3)
                    return

        except FileNotFoundError:
            print("Binary file not found.")
        except Exception as err:
            print("An error occurred while reading and sending data:", err)

        # Didn't do all writes 
        if ( total_tx_size!=self.binary_size ):
            self.exit_status.emit(-1)

        # Successful exit
        self.exit_status.emit(0)

class TcpButtonWorker(TcpWorkerBase):

    def __init__(self, host, port, button):
        super().__init__(host,port)
        self.button = button

    def perform_task(self, globe : InfoGlobeController):
        
        # Successful connect
        tx_data = bytes([0x76])
        tx_data += bytes([self.button])
        globe.send_bytes(tx_data)

class TcpButtonConfigWorker(TcpWorkerBase):

    def __init__(self, host, port, button, command):
        super().__init__(host,port)
        self.button = button
        self.command = command

    def perform_task(self, globe : InfoGlobeController):
        
        # Successful connect
        string_cmd = f'{self.command}'
        tx_data = bytes([0x76])
        tx_data += bytes([self.button, 0x01])
        tx_data += string_cmd.encode()
        tx_data += bytes([0x00])
        globe.send_bytes(tx_data)