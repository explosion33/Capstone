# SARP OTV DAQ GUI
# Ethan Armstrong
# warmst@uw.edu
#
# implements GUI over the serial output layer from the DAQ

import matplotlib
import matplotlib.pyplot as plt
from matplotlib.widgets import Button
from matplotlib.animation import FuncAnimation

from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.backends.backend_qt5agg import NavigationToolbar2QT as NavigationToolbar

from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QApplication, QMainWindow, QWidget, QHBoxLayout, QVBoxLayout

import sys
import time
import serial
import json
import queue
from threading import Thread
from datetime import datetime
from functools import partial


# Configuration

CHARTS = ["HBPT"  , "OBPT"  , "OVPT", "RTD0",
          "HBTT"  , "OBTT"  , "FTPT", "RTD1",
          "OMPT"  , "PCPT"  , "FRMPT", "RTD2",
          "HE MFR", "OX MFR", "LC1" , "RTD3",
          ]

TITLES = ["HBPT"  , "OBPT" , "OVPT", "RTD0",
          "HBTT"  , "OBTT" , "FTPT", "RTD1",
          "OMPT"  , "PCPT" , "FRMPT", "RTD2",
          "HE MFR", "N MFR", "LC1" , "RTD3",
          ]


y_scale = [
    [-1000, 5200],
    [-1000, 5200],
    [-100, 600],
    [-80, 160],
    [-80, 160],
    [-80, 160],
    [-100, 600],
    [-80, 160],
    [-2, 7],
    [-2, 7],
    [-2, 7],
    [-80, 160],
    [-0.1, 0.3],
    [-0.1, 0.3],
    [-200, 5000],
    [-80, 160],
]

"""
y_scale = [
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-1, 6],
]
"""

BUTTONS        = ["Mount", "Eject", "Fire", "Abort", "Pulse OX", "Pulse HE", "Pulse Fuel", "FMV Toggle", "OMV Toggle", "IGN Toggle", "FVV Toggle", "HBV Toggle", "OPV Toggle", "OBV Toggle", "OVV Toggle"]
ACTUATOR_INDEX = 7

ser = None
ser_lock = False
tx_queue = queue.Queue()
run_threads = True

    
# =============== Setup MatPlotLib charts and buttons ===============
matplotlib.use('QtAgg')

# Create a separate figure for charts
fig_charts = plt.figure(figsize=(12, 8))
fig_charts.canvas.manager.set_window_title("SARP OTV DAQ - Charts")
plt.get_current_fig_manager().window.setWindowIcon(QIcon("icon.png"))
gs_plots = fig_charts.add_gridspec(4, 4, hspace=0.2)

# Create a second figure for buttons
fig_buttons = plt.figure(figsize=(3, 8))
fig_buttons.canvas.manager.set_window_title("SARP OTV DAQ - Controls")
plt.get_current_fig_manager().window.setWindowIcon(QIcon("icon.png"))
gs_buttons = fig_buttons.add_gridspec(len(BUTTONS), 1, hspace=0.3)



lines = []
buttons = []
actuator_states      = [0,0,0,0,0,0,0,0]

# button callback
def on_button_clicked(index, _event):
    print("Button Clicked: ", index)

    if (ser == None):
        return

    if index == 0:
        print("Mounting at /sd/log.txt")
        tx_queue.put(b"{DM/sd/log.txt}\n")
    elif index == 1:
        print("ejecting")
        tx_queue.put(b"{DE}\n")
    elif index == 2:
        print("Firing")
        tx_queue.put(b"{CFI}\n")
    elif index == 3:
        print("Aborting")
        tx_queue.put(b"{CAB}\n")
    elif index == 4:
        print("Pulsing Ox")
        tx_queue.put(b"{COP}\n")
    elif index == 5:
        print("Pulsing HE")
        tx_queue.put(b"{CHP}\n")
    elif index == 6:
        print("Pulsing Fuel")
        tx_queue.put(b"{CFP}\n")
    elif index >= 7:
        print("Toggling Actuator")
        
        new = actuator_states.copy()
        new[index - ACTUATOR_INDEX] = 1 if new[index - ACTUATOR_INDEX] == 0 else 0
        print(new)
        print(actuator_states)

        state_str = ''.join(str(bit) for bit in new)
        cmd_str = "{S" + state_str + "}\n"
        cmd_bytes = cmd_str.encode('utf-8')

        tx_queue.put(cmd_bytes)
        

data_x = []
data_y = []

for chart in CHARTS:
    data_x.append([])
    data_y.append([])


texts = []
axs = []
plot_index = 0

for row in range(4):
    for col in range(4):
        ax = fig_charts.add_subplot(gs_plots[row, col])
        ax.set_ylim(y_scale[plot_index][0], y_scale[plot_index][1])
        ax.set_xlim(0, 20)
        ax.set_title(TITLES[plot_index])
        ax.autoscale(False)
        ax.set_autoscale_on(False)
        ax.set_xticklabels([])

        line, = ax.plot(0, 0)
        line.set_animated(True)
        lines.append(line)

        text = ax.text(0.95, 0.95, '', transform=ax.transAxes,
            ha='right', va='top', fontsize=8, color='red')
        text.set_animated(True)
        texts.append(text)

        axs.append(ax)
        plot_index += 1

plt.tight_layout()

for i, label in enumerate(BUTTONS):
    ax_btn = fig_buttons.add_subplot(gs_buttons[i, 0])
    btn = Button(ax_btn, label)
    btn.on_clicked(partial(on_button_clicked, i))
    buttons.append(btn)

def update(frame):
    global actuator_states
    for i in range(ACTUATOR_INDEX, len(BUTTONS)):
        color = "#90EE90" if (actuator_states[i-ACTUATOR_INDEX] == 1) else "#FFA500"
        hcolor = "#BDFCC9" if (color == "#90EE90") else "#FFD580"
        if buttons[i].color != color:
            buttons[i].color = color
            buttons[i].hovercolor = hcolor
            buttons[i].ax.set_facecolor(color)  # Set the Axes background directly
            buttons[i].ax.figure.canvas.draw_idle()

    for i, line in enumerate(lines):
        # New x and y data


        line.set_data(data_x[i], data_y[i])

        ax = line.axes
        if len(data_x[i]) > 1:
            ax.set_xlim(data_x[i][0], data_x[i][-1])

            current_value = data_y[i][-1]
            texts[i].set_text(f"{current_value:.2f}")
        #ax.set_ylim(data_y[i][0], data_y[i][-1] + 1)


    return lines + texts

# ===================================================================

# ====================== Setup Serial Threads =======================

def serial_rx():
    """Continuously reads from serial and logs complete lines."""
    buffer = b''
    global run_threads
    global actuator_states
    while run_threads:
        if ser == None or ser_lock:
            time.sleep(0.5)
            continue
        try:
            data = ser.read(ser.in_waiting or 1)
            if data:
                buffer += data
                while b'\n' in buffer:
                    line, buffer = buffer.split(b'\n', 1)
                    line = line.strip().decode('utf-8', errors='replace')
                    
                    if line:
                        if not line.startswith("log: "):
                            try:
                                parsed = json.loads(line)
                                parsed["EMPTY"] = [0, 0]
                                #print(parsed)

                                actuator_states = parsed["actuators"]

                                for i, chart in enumerate(CHARTS):
                                    if TITLES[i] == '---':
                                        continue

                                    try:
                                        data_x[i].append(parsed[chart][0] / 1000)
                                        data_y[i].append(parsed[chart][1])

                                        if (len(data_x[i]) > 75):
                                            data_x[i].pop(0)
                                        if (len(data_y[i]) > 75):
                                            data_y[i].pop(0)

                                            
                                    except Exception as e:
                                        print("Chart Failure |", chart)
                                        print("\t", parsed)
                                        print(e)

                            except json.JSONDecodeError as e:
                                print(line[line.find("log")::])
                                
                        else:
                            #if in_use and ("done" in line.lower()):
                            #    in_use = False
                            if not (("recieved" in line) or ("UH OH" in line)):
                                print(f"[{datetime.now()}] {line}")
        except Exception as e:
            print(f"[{datetime.now()}] Read error: {e}")

def serial_tx():
    """Sends data periodically over the serial port."""
    global run_threads
    last_execute = time.time()
    while run_threads:
        if ser == None or ser_lock:
            time.sleep(0.5)
            continue

        try:
            while not tx_queue.empty():            
                ser.write(tx_queue.get_nowait())
                ser.flush()
                print("sent command")

            if time.time() - last_execute > 0.06:
                ser.write(b"{}")
                last_execute = time.time()
        except Exception as e:
            print(f"TX Error | {e}")
        time.sleep(0.0005)

# ===================================================================

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QComboBox, QPushButton, QHBoxLayout, QWidget, QToolButton
from matplotlib.backends.backend_qt5agg import NavigationToolbar2QT
import serial.tools.list_ports
from PyQt6.QtWidgets import QComboBox, QPushButton, QLabel, QWidget, QHBoxLayout, QSizePolicy
from matplotlib.backends.backend_qt5agg import NavigationToolbar2QT

class CustomToolbar(NavigationToolbar2QT):
    def __init__(self, canvas, parent=None):
        super().__init__(canvas, parent)

        self.running = True

        # Step 1: Remove all default actions
        for action in self.actions():
            self.removeAction(action)

        # Step 2: Create left section widgets
        self.port_dropdown = QComboBox()
        self.connect_button = QPushButton("Connect")
        self.connect_button.clicked.connect(self.connect_serial)

        # Step 3: Create center title widget
        self.title_label = QLabel("OTV DAQ Ground Station")
        self.title_label.setStyleSheet("font-weight: bold; font-size: 14px;")
        #self.title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.title_label.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)

        # Step 4: Create a flexible container widget to hold all parts
        container = QWidget()
        layout = QHBoxLayout()
        layout.setContentsMargins(0, 0, 0, 0)

        # Add widgets in order
        layout.addWidget(self.port_dropdown)
        layout.addWidget(self.connect_button)
        layout.addStretch()  # spacer before title
        layout.addWidget(self.title_label, alignment=Qt.AlignmentFlag.AlignCenter)
        layout.addStretch()  # spacer after title

        edit_button = QToolButton()
        edit_button.setDefaultAction(self._actions['edit_parameters'])
        layout.addWidget(edit_button)

        #layout.addWidget(.)  # wrench tool
        container.setLayout(layout)

        # Add final container widget to toolbar
        self.addWidget(container)

        self.refresh_ports()

    def __del__(self):
        super().__del__()
        self.running = False

    def refresh_ports(self):
        ports = []
        for port in serial.tools.list_ports.comports():
            ports.append(port.device)

        curr_ports = []
        for i in range(len(self.port_dropdown)):
            curr_ports.append(self.port_dropdown.itemText(i))

        if ports != curr_ports:
            self.port_dropdown.clear()
            self.port_dropdown.addItems(ports)

        if ser != None and ser.port not in ports:
            self.connect_serial()

    def connect_serial(self):
        global ser
        global ser_lock

        if ser == None: # connect
            try:
                selected_port = self.port_dropdown.currentText()
                ser = serial.Serial(selected_port, 115200, timeout=1)
                time.sleep(0.1)
                self.connect_button.setText("Disconnect")
            except Exception as e:
                print(f"Serial Connect Error | {e}")
        else:
            ser_lock = True
            time.sleep(0.1)
            
            ser.close()
            ser = None

            ser_lock = False

            self.connect_button.setText("Connect")

    def update_ports_thread(self):
        global run_threads
        while run_threads:
            self.refresh_ports()
            self.update()
            time.sleep(0.5)

if __name__ == "__main__":
    app = QApplication(sys.argv)

    # Configure Charts to be animated, and have proper margins
    fig_charts.subplots_adjust(left=0.05, right=0.95, top=0.95, bottom=0.05, hspace=0.25, wspace=0.25)
    ani = FuncAnimation(fig_charts, update, interval=5, blit=True)

    # Setup Canvas' and Layouts
    canvas_charts = FigureCanvas(fig_charts)
    canvas_buttons = FigureCanvas(fig_buttons)

    main_window = QMainWindow()
    main_widget = QWidget()
    main_layout = QVBoxLayout()
    layout = QHBoxLayout()

    # Add figures to data layout
    layout.addWidget(canvas_buttons, 1)
    layout.addWidget(canvas_charts, 10)

    # add toolbar and data layout to main layout
    toolbar = CustomToolbar(canvas_charts, None)
    main_layout.addWidget(toolbar)
    main_layout.addLayout(layout)

    # setup main widget
    main_widget.setLayout(main_layout)
    main_window.setCentralWidget(main_widget)
    main_window.setWindowTitle("SARP OTV DAQ GUI")
    main_window.setWindowIcon(QIcon("icon.png"))


    # Start threads
    trx = Thread(target=serial_rx, daemon=True)
    ttx = Thread(target=serial_tx, daemon=True)
    ttb = Thread(target=toolbar.update_ports_thread)
    trx.start()
    ttx.start()
    ttb.start()

    # run GUI
    main_window.show()
    exit_code = app.exec()

    # Threads cleanup
    run_threads = False
    trx.join()
    ttx.join()
    ttb.join()
    sys.exit(exit_code)
