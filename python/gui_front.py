import json
import sys
from PyQt5.QtGui import QIcon
from PyQt5.QtWidgets import QApplication, QWidget, QVBoxLayout, QTabWidget, QDesktopWidget
from PyQt5.QtCore import QFile
from PyQt5.QtGui import QPalette, QColor

from connect_tab import ConnectTab
from tx_data_tab import TXDataTab
from ota_tab import OTATab

class InfoGlobeCC(QWidget):

    def __init__(self):
        super().__init__()
        self.init_ui()  # Initialize the UI components

    def construct_layout(self):

        connect_tab = ConnectTab()  # Initialize the Connect tab
        tx_data_tab = TXDataTab(connect_tab)    # Initialize the Tx Data tab
        ota_tab = OTATab(connect_tab)    # Initialize the OTA tab

        layout = QVBoxLayout(self)  # Main layout for the entire window
        tab_widget = QTabWidget()  # Widget to hold tabs

        tab_widget.addTab(connect_tab, "Connect")  # Add Connect tab to the tab widget
        tab_widget.addTab(tx_data_tab, "Tx Data")  # Add Tx Data tab to the tab widget
        tab_widget.addTab(ota_tab, "Over-The-Air")  # Add Tx Data tab to the tab widget

        layout.addWidget(tab_widget)  # Add tab widget to the main layout

        self.setLayout(layout)  # Set the main layout for the window        

    # Initialize the main UI components
    def init_ui(self):
        self.setWindowTitle("InfoGlobe Command Center")  # Set window title
        self.setWindowIcon(QIcon('./python/Dakirby309-Simply-Styled-Xbox.ico'))  # Set window icon

        self.resize(800, 400)  # Set initial size of the window

        # Calculate the position to center the window on the screen
        screen_geometry = QDesktopWidget().screenGeometry()
        x = (screen_geometry.width() - self.width()) // 2
        y = (screen_geometry.height() - self.height()) // 2
        self.move(x, y)

        # Make the layout, and show the window
        self.construct_layout()
        self.show()


    def load_palette_from_file(file_path):
        with open(file_path, 'r') as f:
            palette_data = json.load(f)

        palette = QPalette()
        for role, color_values in palette_data.items():
            color = QColor(*color_values)
            palette.setColor(getattr(QPalette, role), color)

        return palette

def set_dark_fusion_style(app: QApplication):

    # TODO: I think i want to set all settings inside qss
    app.setStyle("Fusion")
    
    # Load the QSS file
    qss_file = QFile("./python/yellow_material.qss")
    qss_file.open(QFile.ReadOnly | QFile.Text)
    qss = qss_file.readAll()
    qss = bytes(qss).decode("utf-8")
    app.setStyleSheet(qss)  # Apply the loaded QSS to the application

# Entry point of the program
if __name__ == '__main__':  

    # Make an app
    app = QApplication(sys.argv)

    # Custom Dark Fusion Inspired
    set_dark_fusion_style(app)

    qt_app = InfoGlobeCC()  # Create an instance of the InfoGlobeCC class
    sys.exit(app.exec_())  # Execute the application event loop and handle exit
