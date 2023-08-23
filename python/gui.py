import sys
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton

def button_click():
    print("Button clicked!")

if __name__ == '__main__':
    app = QApplication(sys.argv)
    
    # Create a main window
    window = QWidget()
    window.setWindowTitle("Simple Qt App")
    window.setGeometry(100, 100, 300, 200)  # Set window position (x, y) and size (width, height)
    
    # Create a button and connect it to the button_click function
    button = QPushButton('Click Me', window)
    button.setGeometry(100, 50, 100, 30)  # Set button position (x, y) and size (width, height)
    button.clicked.connect(button_click)
    
    # Show the window
    window.show()
    
    sys.exit(app.exec_())
