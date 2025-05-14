import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from threading import Thread
import time
import serial
from datetime import datetime
import json

# Configuration
COM_PORT = 'COM14'       # Replace with your actual COM port
BAUD_RATE = 115200
SEND_INTERVAL = 0.05       # seconds
SEND_PAYLOAD = b'{}'    # bytes to send periodically


CHARTS = ["HBPT", "OBPT", "OVPT", "HBTT", "OBTT", "FTPT", "ADC6", "ADC7", "ADC8", "HE MFR", "OX MFR", "EMPTY"]
TITLES = ["HBPT", "OBPT", "OVPT", "HBTT", "OBTT", "FTPT", "---", "---", "---", "HE MFR", "OX MFR", "EMPTY"]


# Create a 3x3 grid of subplots
fig, axs = plt.subplots(4, 3, figsize=(10, 8))
lines = []

# Initial data

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
for i, ax in enumerate(axs.flat):
    line, = ax.plot(0, 0)
    ax.set_ylim(y_scale[i][0], y_scale[i][1])
    ax.set_xlim(0, 2000)
    ax.set_title(TITLES[i])
    lines.append(line)

    text = ax.text(0.95, 0.95, '', transform=ax.transAxes,
    ha='right', va='top', fontsize=8, color='red')
    texts.append(text)
    

def update(frame):
    for i, line in enumerate(lines):
        # New x and y data


        line.set_data(data_x[i], data_y[i])

        ax = line.axes
        if len(data_x[i]) > 1:
            ax.set_xlim(data_x[i][0], data_x[i][-1] + 0.1)
            current_value = data_y[i][-1]
            texts[i].set_text(f"{current_value:.2f}")
        #ax.set_ylim(data_y[i][0], data_y[i][-1] + 1)


    return lines + texts



# Open serial port
ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=1)

import json
from datetime import datetime

def serial_rx():
    """Continuously reads from serial and logs complete lines."""
    buffer = b''
    while True:
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

                                for i, chart in enumerate(CHARTS):
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
                                print(line)
                        else:
                            print(f"[{datetime.now()}] {line}")
        except Exception as e:
            print(f"[{datetime.now()}] Read error: {e}")


def serial_tx():
    """Sends data periodically over the serial port."""
    while True:
        ser.write(SEND_PAYLOAD + b'\n')
        #print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Sent: {SEND_PAYLOAD.decode()}")
        time.sleep(SEND_INTERVAL)


if "__main__" in __name__:
    trx = Thread(target=serial_rx, daemon=True)
    ttx = Thread(target=serial_tx, daemon=True)
    
    trx.start()
    ttx.start()

    ani = FuncAnimation(fig, update, interval=100, blit=False)
    plt.tight_layout()
    plt.show()

    while True:
        pass

    tttp.join()
