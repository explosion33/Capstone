import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.figure import Figure
from matplotlib import style
import matplotlib.animation as animation

import tkinter as tk
import json
import serial
import time

graph_width_ct = 1
graph_height_ct = 1
window_width = 720
window_height = 480
max_save_frames = 100
graph_fps = 30

with open("GUIConfig.json", 'r') as file:
    gui_config = json.load(file)
with open("SerialConfig.json", 'r') as file:
    serial_config = json.load(file)

if 'graph_width_ct' in gui_config:
    graph_width_ct = gui_config['graph_width_ct']
if 'graph_height_ct' in gui_config:
    graph_height_ct = gui_config['graph_height_ct']
if 'window_width' in gui_config:
    window_width = gui_config['window_width']
if 'window_height' in gui_config:
    window_height = gui_config['window_height']
if 'max_save_frames' in gui_config:
    max_save_frames = gui_config['max_save_frames']
if 'graph_fps' in gui_config:
    graph_fps = gui_config['graph_fps']

ser = serial.Serial()
ser.baudrate=serial_config['baudrate']
ser.timeout=serial_config['timeout']
ser.port=serial_config['port']


LARGE_FONT= ("Verdana", 12)

style.use("ggplot")

buttons = dict()

f = Figure(figsize=(5,5), dpi=100)
data = dict()
state = dict()
info = dict()
mutually_inclusive = dict()
mutually_exclusive = dict()

for i in gui_config['graphs']:
    ax = f.add_subplot(graph_width_ct, graph_height_ct, i['graph_index'], title=i['label'])
    for j in range(len(i['data_keys'])):
        data[i['data_keys'][j]] = [ax, list(), i['kwargs'][j]]

print(ser.is_open)
for i in gui_config['state'].keys():
    state[i] = {
        "lastUpdated": time.time(),
        "currentState": gui_config['state'][i]['currentState'],
        "commandedState": gui_config['state'][i]['commandedState'],
        "nominalState": gui_config['state'][i]['nominalState']
        }
    if state[i]['nominalState'] != None:
        mutually_inclusive[i] = gui_config['state'][i]['mutuallyInclusive']
        mutually_exclusive[i] = gui_config['state'][i]['mutuallyExclusive']

for i in gui_config['info'].keys():
    info[i] = gui_config['info'][i]

print(ser.is_open)
def readSerialData(read_info=False):
    if ser.is_open and ser.in_waiting > 0:
        newData = json.loads(ser.readline())
        for key in newData.keys():
            if key in data:
                data[key][1].append((time.time(), newData[key]))
            if key in state:
                state[key]['lastUpdated'] = time.time()
                state[key]['currentState'] = newData[key]
            if read_info and key in info:
                info[key] = newData[key]

def initSerialConnection():
    if ser.is_open:
        return 1
    ser.open()
    readSerialData(read_info=True)
    updateButtons()
    
def sendCommand(key, commandedState):
    if ser.is_open:
        ser.write(str(key + ":" + str(commandedState) + "\n").encode("utf-8"))
        state[key]['commandedState'] = commandedState

def updateSerialButton(state_key, toggle=True):
    if(toggle):
        if ser.is_open:
            ser.close()
        else:
            ser.open()
    buttons[state_key]['button'].config(text="Close Serial Connection" if ser.is_open else "Open Serial Connection", bg='green' if ser.is_open else 'red')

def updateValveButton(state_key, toggle=True):
    if state[state_key]['currentState'] == None:
        bg="white"
        text=str(state_key) + " [WAITING]"
    else:
        isMovingState = state[state_key]['currentState'] == (not state[state_key]['commandedState'])
        if isMovingState:
            text = str(state_key) + " [MOVING]"
            bg = "orange"
        else:
            if state[state_key]['nominalState'] == True:
                if state[state_key]['currentState'] == True:
                    bg = "green"
                    text = str(state_key) + " [OPEN]"
                else:
                    bg = "red"
                    text = str(state_key) + " [CLOSED]"
            elif state[state_key]['nominalState'] == False:
                if state[state_key]['currentState'] == True:
                    bg = "green"
                    text = str(state_key) + " [CLOSED]"
                else:
                    bg = "red"
                    text = str(state_key) + " [OPEN]"
            
            if toggle:
                allowed = True
                if(state[state_key]['commandedState']):
                    for key in state.keys():
                        if state[key]['commandedState'] != (key not in mutually_inclusive[state_key]):
                            allowed = False
                else:
                    for key in state.keys():
                        if state[key]['commandedState'] == (key not in mutually_exclusive[state_key]):
                            allowed = False
                if allowed:
                    sendCommand(state_key, not state[state_key]['commandedState'])
                    updateValveButton(state_key, toggle=False)
    buttons[state_key]['button'].config(text=text, bg=bg)

def updateCommandButton(state_key, toggle=True):
    if state[state_key]['currentState'] == None:
        bg="white"
        text=str(state_key) + " [WAITING]"
    else:
        if state[state_key]['currentState'] == (not state[state_key]['commandedState']):
            text = str(state_key) + " [PENDING]"
            bg = "orange"
        else:
            if state[state_key]['currentState'] == True:
                text = str(state_key) + " [ACTIVE]"
                bg = "green"
            else:
                text = str(state_key) + " [INACTIVE]"
                bg = "red"
        if toggle:
            sendCommand(state_key, not state[state_key]['commandedState'])
            updateCommandButton(state_key, toggle=False)
    buttons[state_key]['button'].config(text=text, bg=bg)

def updateButtons():
    for key in buttons.keys():
        if buttons[key]['type'] == "serial":
            updateSerialButton(key, toggle=False)
        elif buttons[key]['type'] == "cmd":
            updateCommandButton(key, toggle=False)
        elif buttons[key]['type'] == "valve":
            updateValveButton(key, toggle=False)

def animate(i):
    if ser.is_open:
        readSerialData()
        updateButtons()
        for axis in f.axes:
            axis.clear()
        for data_plot in data:
            data_plot[0].plot(data_plot[1], **data_plot[2])

def initSerialButton(cl, s, key):
    serialButton = tk.Button(cl, text="Close Serial Connection" if s.is_open else "Open Serial Connection", bg="white")
    buttons[key] = {"button": serialButton, "type": "serial"}
    serialButton.config(command=updateSerialButton(key))
    serialButton.pack()

def initCommandButton(cl, key):
    cmdButton = tk.Button(cl, text=key + " [WAITING]", bg="white")
    buttons[key] = {"button": cmdButton, "type": "cmd"}
    cmdButton.config(command=updateCommandButton(key))
    cmdButton.pack()

def initValveButton(cl, key):
    valveButton = tk.Button(cl, text=key + " [WAITING]", bg="white")
    buttons[key] = {"button": valveButton, "type": "valve"}
    valveButton.config(command=updateValveButton(key))
    valveButton.pack()
    
class CapstoneGSE(tk.Tk):

    def __init__(self, *args, **kwargs):
        pages = (MainPage, PageTwo)
        tk.Tk.__init__(self, *args, **kwargs)

        tk.Tk.iconbitmap(self, default="clienticon.ico")
        tk.Tk.wm_title(self, "SARP 2025 Capstone GSE")
        
        container = tk.Frame(self)
        container.pack(side="top", fill="both", expand = True)
        container.grid_rowconfigure(0, weight=1)
        container.grid_columnconfigure(0, weight=1)
        print(str(ser.is_open) + "xx")

        self.frames = {}

        for F in pages:

            frame = F(container, self)

            self.frames[F] = frame

            frame.grid(row=0, column=0, sticky="nsew")
        
        self.show_frame(MainPage)
        print(str(ser.is_open) + "x")

    def show_frame(self, cont):

        frame = self.frames[cont]
        frame.tkraise()

class MainPage(tk.Frame):

    def __init__(self, parent, controller):
        tk.Frame.__init__(self, parent)
        label = tk.Label(self, text="2025 Capstone Hotfire Test Software", font=LARGE_FONT)
        label.pack(pady=10,padx=10)

        initSerialButton(self, ser, "serial")

        initCommandButton(self, "FIRE")
        initCommandButton(self, "ABORT")
        initCommandButton(self, "SW ARM")

        initValveButton(self, "HBV")
        initValveButton(self, "FVV")
        initValveButton(self, "OBV")
        initValveButton(self, "OPV")
        initValveButton(self, "OVV")
        initValveButton(self, "OMV")
        initValveButton(self, "FMV")

        canvas = FigureCanvasTkAgg(f, self)
        canvas.draw()
        canvas.get_tk_widget().pack(side=tk.BOTTOM, fill=tk.BOTH, expand=True)

        toolbar = NavigationToolbar2Tk(canvas, self)
        toolbar.update()
        canvas._tkcanvas.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

class PageTwo(tk.Frame):

    def __init__(self, parent, controller):
        tk.Frame.__init__(self, parent)
        label = tk.Label(self, text="Page Two!!!", font=LARGE_FONT)
        label.pack(pady=10,padx=10)

        button1 = tk.Button(self, text="Back to Home",
                            command=lambda: controller.show_frame(MainPage))
        button1.pack()

        button2 = tk.Button(self, text="Page One",
                            command=lambda: controller.show_frame(MainPage))
        button2.pack()

        
        
print(str(ser.is_open) + "xxxx")
app = CapstoneGSE()

print(str(ser.is_open) + "xxx")
ani = animation.FuncAnimation(f, animate, interval=(1/graph_fps)*1000, save_count=max_save_frames)
app.mainloop()