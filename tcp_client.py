# echo-client.py

import socket
import time
from typing import Any

HOST = "192.168.0.106"  # The server's hostname or IP address
PORT = 55555  # The port used by the server

class InfoGlobeController():
    def __init__(self, host, port) -> None:
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.connect((host, port))
    
    def __call__(self, my_string) -> Any:
        # self.scrolling_blank()
        # tx_data = self.construct_infoglobe_message(my_string,0x00,0x12)
        # for i in range(20):
            # input(f"Press 'enter' to trigger {i} animation")
            # tx_data = self.construct_infoglobe_message(my_string,i,0x04)
            tx_data = self.construct_infoglobe_message(my_string,0x00,0x05)
            self.s.sendall(tx_data)

    '''
        Commands
    '''
    def scrolling_blank(self):
        self.send_header_footer(header_byte_value=0x00,tail_byte_value=0x00)

    def instant_blank(self):
        self.send_header_footer(0x08,0x01)

    '''
        Stationary
    '''
    def set_text_stationary_front(self, my_string):
        tx_data = self.construct_infoglobe_message(my_string,0x01)
        self.s.sendall(tx_data)

    def set_text_stationary_right(self, my_string):
        tx_data = self.construct_infoglobe_message(my_string,0x02)
        self.s.sendall(tx_data)

    '''
        Set Text (0xZZ, 0x01)
    '''
    def set_text_sweep_left(self, my_string):
        tx_data = self.construct_infoglobe_message(my_string,0x00,0x01)
        self.s.sendall(tx_data)

    def set_text_single_scroll_left(self, my_string):
        tx_data = self.construct_infoglobe_message(my_string,0x03,0x5f)
        self.s.sendall(tx_data)

    def set_text_many_scroll_left(self, my_string):
        tx_data = self.construct_infoglobe_message(my_string,0x04)
        self.s.sendall(tx_data)

    def set_text_w_pause(self, my_string):
        tx_data = self.construct_infoglobe_message(my_string,0x00,0x10)
        self.s.sendall(tx_data)

    def set_text_w_flip(self, my_string):
        tx_data = self.construct_infoglobe_message(my_string,0x00,0x19)
        self.s.sendall(tx_data)

    def send_header_footer(self, header_byte_value=None,tail_byte_value=None):
        tx_data = self.construct_infoglobe_message(header_byte_value=header_byte_value,tail_byte_value=tail_byte_value)
        self.s.sendall(tx_data)

    '''
        Set Text (0xZZ, 0x02)
    '''
    def set_text_fade_in(self, my_string):
        tx_data = self.construct_infoglobe_message(my_string,0x00,0x03)
        self.s.sendall(tx_data)

    '''
        Helpers
    '''
    def send_bytes(self,tx_data):
        self.s.sendall(tx_data)

    def construct_infoglobe_message(self,my_string = None, header_byte_value = None, tail_byte_value = None):
        # Init message
        message = None

        # Add header byte
        if header_byte_value is not None:
            message = bytes([header_byte_value])
        # Add string
        if my_string is not None:
            message += my_string.encode()
        # Add tail byte
        if tail_byte_value is not None:
            message += bytes([tail_byte_value])
        
        return message


if __name__ == "__main__":
    # Create an info globe controller
    globe = InfoGlobeController(HOST, PORT)

    blink_idx = 0xc
    shift_idx = 13

    # tx_data = bytes([0x05,0x00])
    # tx_data += "New ".encode()
    # tx_data += hex(blink_idx).encode()
    # tx_data += " New".encode()
    # tx_data += bytes([0x0])
    # tx_data += bytes([blink_idx,shift_idx])
    # globe.send_bytes(tx_data)
    # time.sleep(2)

    # globe("hell11o")
    globe.set_text_single_scroll_left("192.168.00.15")
    # globe.set_text_stationary_front("my_string")
    # globe.set_text_stationary_right("my_string")
    # globe.instant_blank()
