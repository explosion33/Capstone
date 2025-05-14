# SARP OTV DAQ GUI
# Ethan Armstrong
# warmst@uw.edu
#
# implements GUI over the serial output layer from the DAQ

import matplotlib
import matplotlib.pyplot as plt
from matplotlib.widgets import Button
from matplotlib.gridspec import GridSpec
from matplotlib.animation import FuncAnimation
from PyQt6.QtGui import QIcon
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas

import time
import serial
import json
from threading import Thread
from datetime import datetime
from functools import partial


# Configuration
COM_PORT = 'COM14'

CHARTS  = ["HBPT", "OBPT", "OVPT", "HBTT", "OBTT", "FTPT", "ADC6", "ADC7", "ADC8", "HE MFR", "OX MFR", "EMPTY"]
TITLES  = ["HBPT", "OBPT", "OVPT", "HBTT", "OBTT", "FTPT", "---", "---", "---", "HE MFR", "OX MFR", "---"]
BUTTONS = ["Mount", "Eject", "Fire", "Abort", "Pulse OX", "Pulse HE", "Pulse Fuel", "HBV Toggle", "OBV Toggle", "OPV Toggle", "FVV Toggle", "OVV Toggle", "OMV Toggle", "FMV Toggle", "IGN Toggle"]
ACTUATOR_INDEX = 7

ser = serial.Serial(COM_PORT, 115200, timeout=1)
in_use = False
run_threads = True

    
# =============== Setup MatPlotLib charts and buttons ===============
matplotlib.use('QtAgg')

fig = plt.figure(figsize=(12,8))
fig.canvas.manager.set_window_title("SARP OTV DAQ")
plt.get_current_fig_manager().window.setWindowIcon(QIcon("icon.png"))

gs_main = GridSpec(1,2, width_ratios=[1,10], figure=fig)

gs_buttons = gs_main[0,0].subgridspec(len(BUTTONS), 1, hspace=0.3)

gs_plots = gs_main[0, 1].subgridspec(4, 3, hspace=0.4)


lines = []
buttons = []
actuator_states      = [0,0,0,0,0,0,0,0]

# button callback
def on_button_clicked(index, _event):
    print("Button Clicked: ", index)

    in_use = True
    if index == 0:
        print("Mounting at /sd/log.txt")
        ser.write(b"{DM/sd/log.txt}\n")
    elif index == 1:
        print("ejecting")
        ser.write(b"{DE}\n")
    elif index == 2:
        print("Firing")
        ser.write(b"{CFI}\n")
    elif index == 3:
        print("Aborting")
        ser.write(b"{CAB}\n")
    elif index == 4:
        print("Pulsing Ox")
        ser.write(b"{COP}\n")
    elif index == 5:
        print("Pulsing HE")
        ser.write(b"{CHP}\n")
    elif index == 6:
        print("Pulsing Fuel")
        ser.write(b"{CFP}\n")
    elif index >= 7:
        print("Toggling HBV")
        
        new = actuator_states
        new[index - ACTUATOR_INDEX] = 1 if new[index - ACTUATOR_INDEX] == 0 else 0

        state_str = ''.join(str(bit) for bit in new)
        cmd_str = "{S" + state_str + "}\n"
        cmd_bytes = cmd_str.encode('utf-8')

        ser.write(cmd_bytes)
        


data_x = [
    [],
    [],
    [],
    [],
    [],
    [],
    [],
    [],
    [],
    [],
    [],
    [], #empty
]

data_y = [
    [],
    [],
    [],
    [],
    [],
    [],
    [],
    [],
    [],
    [],
    [],
    [], #empty
]

y_scale = [
    [-50, 130],
    [-100, 600],
    [-100, 2200],
    [-50, 130],
    [-100, 3200],
    [-100, 600],
    [-1, 6],
    [-1, 6],
    [-1, 6],
    [-0.1, 0.3],
    [-0.1, 0.3],
    [-1, 6],
]

texts = []

axs = []
plot_index = 0

for row in range(4):
    for col in range(0,3):
        ax = fig.add_subplot(gs_plots[row, col])
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
    ax_btn = fig.add_subplot(gs_buttons[i, 0])
    btn = Button(ax_btn, label)
    btn.on_clicked(partial(on_button_clicked, i))
    buttons.append(btn)

def update(frame):
    global actuator_states
    for i in range(ACTUATOR_INDEX, len(BUTTONS)):
        color = "green" if (actuator_states[i-ACTUATOR_INDEX] == 1) else "lightgrey"
        if buttons[i].color != color:
            buttons[i].color = color
            buttons[i].ax.figure.canvas.draw()

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
    global in_use
    global run_threads
    global actuator_states
    while run_threads:
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
                            if in_use and ("done" in line.lower()):
                                in_use = False
                            if not (("recieved" in line) or ("UH OH" in line)):
                                print(f"[{datetime.now()}] {line}")
        except Exception as e:
            print(f"[{datetime.now()}] Read error: {e}")

def serial_tx():
    """Sends data periodically over the serial port."""
    global run_threads
    while run_threads:
        ser.write(b"{}")
        time.sleep(0.05)

# ===================================================================

if "__main__" in __name__:
    trx = Thread(target=serial_rx, daemon=True)
    ttx = Thread(target=serial_tx, daemon=True)
    
    trx.start()
    ttx.start()

    ani = FuncAnimation(fig, update, interval=10, blit=True)
    plt.show()

    
    run_threads = False
    trx.join()
    ttx.join()