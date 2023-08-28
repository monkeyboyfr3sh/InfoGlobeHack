from PyQt5.QtWidgets import QWidget

class WorkingTabBase(QWidget):
    def __init__(self):
        super().__init__()
        self.tcp_worker = None
        self.tcp_worker_thread = None

    def tcp_worker_finished(self):
        print("TCP worker finished")
        if self.tcp_worker_thread is not None:
            self.tcp_worker_thread.quit()
            self.tcp_worker_thread.wait()
            self.tcp_worker_thread.deleteLater()
            self.tcp_worker_thread = None
            self.tcp_worker = None
