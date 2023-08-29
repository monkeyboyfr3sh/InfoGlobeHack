from PyQt5.QtWidgets import QWidget
from tcp_worker import TcpWorkerBase
from PyQt5.QtCore import QThread

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

    def make_worker_thread(self, worker_thread : TcpWorkerBase, start_worker : bool):

        # Only one worker active at a time
        if self.tcp_worker_thread is None:

            # Create a worker
            self.tcp_worker = worker_thread
            self.tcp_worker_thread = QThread()
            self.tcp_worker.moveToThread(self.tcp_worker_thread)
            self.tcp_worker_thread.started.connect(self.tcp_worker.run)

            # Connect the connect_success signal to a slot
            self.tcp_worker.finished.connect(self.tcp_worker_finished)

            # Start on input
            if start_worker:
                self.tcp_worker_thread.start()
