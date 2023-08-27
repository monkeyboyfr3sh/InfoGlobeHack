import sys
from PyQt5.QtWidgets import QApplication, QMainWindow, QVBoxLayout, QPushButton, QProgressBar, QWidget
from PyQt5.QtCore import Qt, QTimer

class ProgressBarWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.initUI()

    def initUI(self):
        self.setWindowTitle('Progress Bar with Text')
        self.setGeometry(100, 100, 400, 200)

        self.central_widget = QWidget(self)
        self.setCentralWidget(self.central_widget)
        
        layout = QVBoxLayout()

        self.progress_bar = QProgressBar(self)
        layout.addWidget(self.progress_bar)

        self.start_button = QPushButton('Start', self)
        self.start_button.clicked.connect(self.start_progress)
        layout.addWidget(self.start_button)

        self.central_widget.setLayout(layout)

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update_progress)

        self.progress_value = 0

    def start_progress(self):
        self.progress_value = 0
        self.timer.start(100)

    def update_progress(self):
        self.progress_value += 1
        self.progress_bar.setValue(self.progress_value)

        if self.progress_value >= 100:
            self.timer.stop()

if __name__ == '__main__':
    app = QApplication(sys.argv)
    window = ProgressBarWindow()
    window.show()
    sys.exit(app.exec_())
