import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.figure import Figure
from matplotlib import style
import matplotlib.animation as animation
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

import tkinter as tk
from tkinter import ttk
import json
import serial
import serial.tools.list_ports
import threading
import queue
import time
from collections import deque
import datetime
import ctypes

# Color scheme
BG_COLOR = "#2E2E2E"
BUTTON_BG = "#404040"
BUTTON_FG = "#FFFFFF"
TEXT_BG = "#2E2E2E"
TEXT_FG = "#FFFFFF"
GRAPH_BG = "#1E1E1E"
GRAPH_FG = "#FFFFFF"
BUTTON_WAITING_BG = "#505050"
BUTTON_WAITING_FG = "#808080"
BUTTON_ACTIVE_BG = "#00A000"  # Green
BUTTON_INACTIVE_BG = "#A00000"  # Red
BUTTON_PENDING_BG = "#FFA500"  # Amber for pending state

# Create queues for thread communication
serial_queue = queue.Queue()
data_queue = queue.Queue()
update_queue = queue.Queue()
command_queue = queue.Queue()  # New queue for commands

# Thread control
stop_thread = threading.Event()
data_thread = None
serial_thread = None
update_thread = None
write_thread = None

# Store the initial connection time and initial time
initial_connection_time = None  # Will be set once when first data is received
initial_time = 0

def get_available_ports():
    """Get list of available COM ports"""
    ports = serial.tools.list_ports.comports()
    return [port.device for port in ports]

def update_serial_config(port):
    """Update SerialConfig.json with new port"""
    try:
        with open("SerialConfig.json", 'r') as file:
            config = json.load(file)
        config['port'] = port
        with open("SerialConfig.json", 'w') as file:
            json.dump(config, file, indent=4)
        # Update the serial port
        ser.port = port
    except Exception as e:
        print(f"Error updating serial config: {e}")

def on_port_select(event):
    """Handle COM port selection"""
    if not ser.is_open:
        selected_port = port_var.get()
        update_serial_config(selected_port)

def refresh_ports():
    """Refresh the list of available ports"""
    if not ser.is_open:
        current_ports = get_available_ports()
        port_dropdown['values'] = current_ports
        if current_ports and serial_config['port'] not in current_ports:
            port_var.set(current_ports[0])
            update_serial_config(current_ports[0])
        elif current_ports:
            port_var.set(serial_config['port'])

def millis_to_hms(ms):
    seconds = ms // 1000
    h = seconds // 3600
    m = (seconds % 3600) // 60
    s = seconds % 60
    return f"{h:02}:{m:02}:{s:02}"

def serial_read_thread():
    while not stop_thread.is_set():
        if ser.is_open:
            try:
                line = ser.readline().decode('utf-8').strip()
                if line:
                    serial_queue.put(line)
            except Exception as e:
                print(f"Error reading serial: {e}")
        else:
            stop_thread.wait(0.1)

def serial_write_thread():
    while not stop_thread.is_set():
        try:
            if not command_queue.empty():
                command_str = command_queue.get_nowait()
                if ser.is_open:
                    ser.write(bytes(command_str, 'utf-8'))
                    ser.flush()
        except Exception as e:
            print(f"Error in serial write thread: {e}")
        time.sleep(0.01)  # Small sleep to prevent CPU hogging

def data_processing_thread():
    global initial_time, initial_connection_time
    while not stop_thread.is_set():
        try:
            # Process serial data
            while not serial_queue.empty() and not stop_thread.is_set():  # Add stop_thread check here
                line = serial_queue.get_nowait()
                try:
                    # Handle received messages differently
                    if '"received"' in line:
                        # Log received message in orange
                        app.frames[MainPage].console_text.insert("end", f"<< {line}\n", "received")
                        app.frames[MainPage].console_text.tag_config("received", foreground="#FFA500")  # Orange for received
                        app.frames[MainPage].console_text.see("end")  # Scroll to bottom
                        continue
                        
                    newData = json.loads(line)
                    # Convert milliseconds to datetime for proper x-axis plotting
                    try:
                        # Use current time if timestamp is invalid
                        current_time = datetime.datetime.fromtimestamp(newData['timeSent'] / 1000.0)
                    except (OSError, ValueError):
                        current_time = datetime.datetime.now()
                    
                    # Set initial_connection_time only once when first data is received
                    if initial_connection_time is None:
                        initial_connection_time = current_time
                    
                    # If this is the first point after a reconnect, update initial_time
                    if initial_time == 0:
                        initial_time = current_time
                    
                    # Calculate time relative to the initial connection time
                    relative_time = current_time - initial_connection_time
                    # Create a new datetime starting at 00:00:00
                    timestamp = datetime.datetime.combine(datetime.date.today(), datetime.time(0, 0, 0)) + relative_time
                    
                    # Log incoming data at the end
                    app.frames[MainPage].console_text.insert("end", f"<< {line}\n", "incoming")
                    app.frames[MainPage].console_text.tag_config("incoming", foreground="#00FF00")  # Green for incoming
                    app.frames[MainPage].console_text.see("end")  # Scroll to bottom
                    
                    for key in newData.keys():
                        if key in data:
                            data[key][1].append((timestamp, newData[key]))
                            if len(data[key][1]) > 1000:
                                data[key][1] = data[key][1][-1000:]
                            # Signal that new data is available
                            update_queue.put(True)
                        if key in state:
                            # Handle command state updates
                            if isinstance(newData[key], list) and len(newData[key]) == 3:
                                # Update state with all three values in correct order: [state, commandedState, inProgress]
                                state[key]['currentState'] = newData[key][0]  # First element is state
                                state[key]['commandedState'] = newData[key][1]  # Second element is commandedState
                                state[key]['inProgress'] = newData[key][2]  # Third element is inProgress
                                state[key]['lastUpdated'] = timestamp
                                # Force immediate button update
                                if key in buttons:
                                    if buttons[key]['type'] == 'cmd':
                                        updateCommandButton(key, toggle=False)
                                    elif buttons[key]['type'] == 'valve':
                                        updateValveButton(key, toggle=False)
                                # Signal that state has been updated
                                update_queue.put(True)
                            else:
                                # Handle non-command state updates
                                state[key]['currentState'] = newData[key]
                                state[key]['lastUpdated'] = timestamp
                                # Signal that state has been updated
                                update_queue.put(True)
                except json.JSONDecodeError as e:
                    # Log invalid data in red at the end
                    app.frames[MainPage].console_text.insert("end", f"!! Invalid JSON: {line}\n", "error")
                    app.frames[MainPage].console_text.tag_config("error", foreground="#FF0000")  # Red for errors
                    app.frames[MainPage].console_text.see("end")  # Scroll to bottom
        except queue.Empty:
            pass
        if not stop_thread.is_set():  # Only sleep if we're not stopping
            time.sleep(0.01)  # Small sleep to prevent CPU hogging

def update_thread_func():
    last_update = 0
    update_interval = 0.1  # Update every 100ms
    
    while not stop_thread.is_set():  # Check stop flag at the start of each iteration
        try:
            # Check if we need to update
            current_time = time.time()
            if current_time - last_update >= update_interval:
                # Process any pending updates
                while not update_queue.empty() and not stop_thread.is_set():  # Check stop flag here too
                    update_queue.get_nowait()
                
                # Only update if we haven't been asked to stop
                if not stop_thread.is_set():
                    # Update graphs
                    updateGraphs()
                    
                    # Update value labels
                    for graph_config in gui_config['graphs']:
                        for j, data_key in enumerate(graph_config['data_keys']):
                            if data_key in data and data[data_key][1]:
                                latest_value = data[data_key][1][-1][1]
                                label = app.frames[MainPage].graph_frames[graph_config['label']][j]
                                label.config(text=f"{data_key}: {latest_value:.2f}")
                    
                    # Update buttons
                    updateButtons()
                    last_update = current_time
                
        except Exception as e:
            print(f"Error in update thread: {e}")
        
        # Check stop flag before sleeping
        if not stop_thread.is_set():
            time.sleep(0.01)  # Small sleep to prevent CPU hogging
        else:
            break  # Exit immediately if stop flag is set
    
    print("Update thread exiting cleanly")

def initSerialConnection():
    global serial_thread, data_thread, update_thread, write_thread, initial_connection_time, initial_time
    if ser.is_open:
        return 1
    else:
        try:
            ser.open()
            # Clear any existing data in the buffer
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            
            # If clear_graphs_on_reset is true, reset all data and timestamps
            if gui_config.get('clear_graphs_on_reset', False):
                initial_connection_time = None
                initial_time = 0
                
                # First clear all data points
                for key in data:
                    data[key][1].clear()
                
                # Then clear and reset all figures
                for graph_config in gui_config['graphs']:
                    # Get the existing figure
                    fig = app.frames[MainPage].figures[graph_config['label']]
                    ax = fig.axes[0]  # Get the existing axis
                    
                    # Clear the axis
                    ax.clear()
                    
                    # Reset the subplot properties
                    ax.set_title(graph_config['label'])
                    fig.subplots_adjust(top=0.85, bottom=0.2, left=0.15, right=0.95)
                    ax.set_facecolor(GRAPH_BG)
                    ax.tick_params(colors=GRAPH_FG, labelsize=8)
                    ax.xaxis.label.set_color(GRAPH_FG)
                    ax.yaxis.label.set_color(GRAPH_FG)
                    ax.title.set_color(GRAPH_FG)
                    ax.title.set_fontsize(10)
                    for spine in ax.spines.values():
                        spine.set_color(GRAPH_FG)
                    ax.grid(True, linestyle='--', alpha=0.3)
                    ax.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M:%S'))
                    plt.setp(ax.get_xticklabels(), rotation=45, ha='right')
                    
                    # Update the data reference with the axis
                    for j, data_key in enumerate(graph_config['data_keys']):
                        data[data_key][0] = ax
                    
                    # Force a redraw
                    fig.canvas.draw()
                
                # Wait a short time to ensure everything is cleared
                time.sleep(0.1)
                
                # Force graph update
                update_queue.put(True)
            
            # Wait for initial data
            line = ser.readline().decode('utf-8').strip()
            print(line)
            while(not line):
                line = ser.readline().decode('utf-8').strip()
            
            # Start all threads
            stop_thread.clear()
            
            # Serial reading thread
            serial_thread = threading.Thread(target=serial_read_thread, daemon=True, name="SerialRead")
            serial_thread.start()
            
            # Serial writing thread
            write_thread = threading.Thread(target=serial_write_thread, daemon=True, name="SerialWrite")
            write_thread.start()
            
            # Data processing thread
            data_thread = threading.Thread(target=data_processing_thread, daemon=True, name="DataProcess")
            data_thread.start()
            
            # Update thread
            update_thread = threading.Thread(target=update_thread_func, daemon=True, name="Update")
            update_thread.start()
            
            updateButtons()
        except Exception as e:
            print(f"Error opening serial port: {e}")
            if ser.is_open:
                ser.close()

def updateSerialButton(state_key, toggle=True):
    if(toggle):
        if ser.is_open:
            # Stop all threads
            stop_thread.set()
            
            # Wait for threads to finish
            threads_to_join = []
            if serial_thread and serial_thread.is_alive():
                threads_to_join.append(serial_thread)
            if data_thread and data_thread.is_alive():
                threads_to_join.append(data_thread)
            if update_thread and update_thread.is_alive():
                threads_to_join.append(update_thread)
            if write_thread and write_thread.is_alive():
                threads_to_join.append(write_thread)
            
            # Join threads with timeout
            for thread in threads_to_join:
                thread.join(timeout=1.0)
                if thread.is_alive():
                    print(f"Warning: Thread {thread.name} did not terminate properly")
            
            ser.close()
            # Enable port selection
            port_dropdown.configure(state="readonly")
        else:
            initSerialConnection()
            # Disable port selection
            port_dropdown.configure(state="disabled")
    buttons[state_key]['button'].config(
        text="Close Serial Connection" if ser.is_open else "Open Serial Connection",
        bg=BUTTON_ACTIVE_BG if ser.is_open else BUTTON_INACTIVE_BG,
        fg=BUTTON_FG
    )

def updateValveButton(state_key, toggle=True):
    if state[state_key]['currentState'] == None:
        bg = BUTTON_WAITING_BG
        fg = BUTTON_WAITING_FG
        text = str(state_key) + " [WAITING]"
    else:
        if state[state_key]['inProgress']:
            bg = BUTTON_PENDING_BG
            fg = BUTTON_FG
            text = str(state_key) + " [PENDING]"
        else:
            if state[state_key]['nominalState'] == True: # Valve is closed when unpowered
                if state[state_key]['currentState'] == True: #if valve is powered
                    bg = BUTTON_ACTIVE_BG
                    text = str(state_key) + " [CLOSED]"
                else:
                    bg = BUTTON_INACTIVE_BG
                    text = str(state_key) + " [OPEN]"
            elif state[state_key]['nominalState'] == False: #valve is open when unpowered
                if state[state_key]['currentState'] == True: #if valve is powered
                    bg = BUTTON_ACTIVE_BG
                    text = str(state_key) + " [OPEN]"
                else:
                    bg = BUTTON_INACTIVE_BG
                    text = str(state_key) + " [CLOSED]"
            fg = BUTTON_FG
        
        if toggle:
            # Always allow toggling and sending command
            state[state_key]['commandedState'] = not state[state_key]['commandedState']
            newCommand[state[state_key]['commandIndex']] = state[state_key]['commandedState']
            # Update button state immediately
            buttons[state_key]['button'].config(text=text, bg=bg, fg=fg)
            # Send command in a separate thread
            threading.Thread(target=sendCommands, daemon=True).start()
    buttons[state_key]['button'].config(text=text, bg=bg, fg=fg)

def updateCommandButton(state_key, toggle=True):
    if state[state_key]['currentState'] == None:
        bg = BUTTON_WAITING_BG
        fg = BUTTON_WAITING_FG
        text = str(state_key) + " [WAITING]"
    else:
        if state[state_key]['inProgress']:
            text = str(state_key) + " [PENDING]"
            bg = BUTTON_PENDING_BG
            fg = BUTTON_FG
        else:
            if state[state_key]['currentState'] == True:
                text = str(state_key) + " [ACTIVE]"
                bg = BUTTON_ACTIVE_BG
            else:
                text = str(state_key) + " [INACTIVE]"
                bg = BUTTON_INACTIVE_BG
            fg = BUTTON_FG
        
        if toggle:
            # Always allow toggling and sending command
            state[state_key]['commandedState'] = not state[state_key]['commandedState']
            newCommand[state[state_key]['commandIndex']] = state[state_key]['commandedState']
            # Update button state immediately
            buttons[state_key]['button'].config(text=text, bg=bg, fg=fg)
            # Send command in a separate thread
            threading.Thread(target=sendCommands, daemon=True).start()
    buttons[state_key]['button'].config(text=text, bg=bg, fg=fg)

def updateButtons():
    for key in buttons.keys():
        if buttons[key]['type'] == "serial":
            updateSerialButton(key, toggle=False)
        elif buttons[key]['type'] == "cmd":
            updateCommandButton(key, toggle=False)
        elif buttons[key]['type'] == "valve":
            updateValveButton(key, toggle=False)
    
    # Update HW ARM banner
    if "HW ARM" in state:
        if state["HW ARM"]['currentState'] is None:
            app.frames[MainPage].hw_arm_banner.config(
                text="HW ARM [WAITING]",
                bg=BUTTON_WAITING_BG,
                fg=BUTTON_WAITING_FG
            )
        else:
            if state["HW ARM"]['inProgress']:
                app.frames[MainPage].hw_arm_banner.config(
                    text="HW ARM [PENDING]",
                    bg=BUTTON_PENDING_BG,
                    fg=BUTTON_FG
                )
            else:
                if state["HW ARM"]['currentState']:
                    app.frames[MainPage].hw_arm_banner.config(
                        text="HW ARM [ACTIVE]",
                        bg=BUTTON_ACTIVE_BG,
                        fg=BUTTON_FG
                    )
                else:
                    app.frames[MainPage].hw_arm_banner.config(
                        text="HW ARM [INACTIVE]",
                        bg=BUTTON_INACTIVE_BG,
                        fg=BUTTON_FG
                    )
        # Force update of the banner
        app.frames[MainPage].hw_arm_banner.update_idletasks()

def sendCommands():
    if ser.is_open:
        try:
            # Convert Python True/False to JSON true/false
            command_str = str(newCommand).replace('True', 'true').replace('False', 'false')
            # Remove brackets
            command_str = command_str.replace('[', '').replace(']', '')
            # Add newline to ensure complete transmission
            command_str = command_str + '\n'
            
            # Log outgoing command
            app.frames[MainPage].console_text.insert("end", f">> {command_str}", "outgoing")
            app.frames[MainPage].console_text.tag_config("outgoing", foreground="#00FFFF")  # Cyan for outgoing
            app.frames[MainPage].console_text.see("end")  # Scroll to bottom
            
            # Queue the command for sending
            command_queue.put(command_str)
            
        except Exception as e:
            # Log any errors
            error_msg = f"!! Error sending command: {str(e)}\n"
            app.frames[MainPage].console_text.insert("end", error_msg, "error")
            app.frames[MainPage].console_text.tag_config("error", foreground="#FF0000")  # Red for errors
            app.frames[MainPage].console_text.see("end")  # Scroll to bottom

def updateGraphs():
    for graph_config in gui_config['graphs']:
        for data_key in graph_config['data_keys']:
            if data_key in data and data[data_key][1]:  # Check data list
                ax = data[data_key][0]
                data_list = data[data_key][1]
                style = data[data_key][2]
                
                if len(data_list) > 0:
                    ax.clear()
                    x_vals, y_vals = zip(*data_list)
                    ax.plot(x_vals, y_vals, **style)
                    ax.set_title(graph_config['label'])
                    ax.grid(True, linestyle='--', alpha=0.3)
                    ax.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M:%S'))
                    ax.set_facecolor(GRAPH_BG)
                    ax.tick_params(colors=GRAPH_FG, labelsize=8)
                    ax.xaxis.label.set_color(GRAPH_FG)
                    ax.yaxis.label.set_color(GRAPH_FG)
                    ax.title.set_color(GRAPH_FG)
                    # Adjust title font size based on length
                    title_length = len(graph_config['label'])
                    if title_length > 20:
                        ax.title.set_fontsize(8)
                    elif title_length > 15:
                        ax.title.set_fontsize(9)
                    else:
                        ax.title.set_fontsize(10)
                    for spine in ax.spines.values():
                        spine.set_color(GRAPH_FG)
                    plt.setp(ax.get_xticklabels(), rotation=45, ha='right')
                    
                    # Set y-axis label using units from config
                    if 'units' in graph_config and graph_config['units']:
                        ax.set_ylabel(graph_config['units'][0])
                    
                    # Auto-scale axes
                    ax.relim()
                    ax.autoscale_view()
                    
                    # Set number of ticks (5-10 ticks)
                    ax.xaxis.set_major_locator(mdates.AutoDateLocator(minticks=5, maxticks=10))
                    
                    # Adjust layout to prevent label overlap
                    ax.figure.subplots_adjust(top=0.85, bottom=0.25, left=0.2, right=0.95)
                    
                    # Update the figure
                    ax.figure.canvas.draw_idle()

def animate(i):
    # Process all available data from the queue
    try:
        while not serial_queue.empty():
            line = serial_queue.get_nowait()
            try:
                # Skip lines that contain the key 'received'
                if '"received"' in line:
                    continue
                newData = json.loads(line)
                # Convert milliseconds to datetime for proper x-axis plotting
                current_time = datetime.datetime.fromtimestamp(newData['timeSent'] / 1000.0)
                
                # If this is the first point, set it as the reference time
                if initial_time == 0:
                    initial_time = current_time
                
                # Calculate time relative to the first point
                relative_time = current_time - initial_time
                # Create a new datetime starting at 00:00:00
                timestamp = datetime.datetime.combine(datetime.date.today(), datetime.time(0, 0, 0)) + relative_time
                
                # Log incoming data at the end
                app.frames[MainPage].console_text.insert("end", f"<< {line}\n", "incoming")
                app.frames[MainPage].console_text.tag_config("incoming", foreground="#00FF00")  # Green for incoming
                app.frames[MainPage].console_text.see("end")  # Scroll to bottom
                
                for key in newData.keys():
                    if key in data:
                        data[key][1].append((timestamp, newData[key]))
                        if len(data[key][1]) > 1000:
                            data[key][1] = data[key][1][-1000:]
                        # Signal that new data is available
                        update_queue.put(True)
                    if key in state:
                        state[key]['lastUpdated'] = timestamp
                        state[key]['currentState'] = newData[key]
            except json.JSONDecodeError as e:
                # Log invalid data in red at the end
                app.frames[MainPage].console_text.insert("end", f"!! Invalid JSON: {line}\n", "error")
                app.frames[MainPage].console_text.tag_config("error", foreground="#FF0000")  # Red for errors
                app.frames[MainPage].console_text.see("end")  # Scroll to bottom
    except queue.Empty:
        pass
    
    # Update graphs less frequently
    if i % 2 == 0:  # Update every other frame
        updateGraphs()
    
    # Update value labels
    for graph_config in gui_config['graphs']:
        for j, data_key in enumerate(graph_config['data_keys']):
            if data_key in data and data[data_key][1]:
                # Get the most recent value
                latest_value = data[data_key][1][-1][1]
                label = app.frames[MainPage].graph_frames[graph_config['label']][j]
                label.config(text=f"{data_key}: {latest_value:.2f}")
    
    updateButtons()

def initSerialButton(cl, s, key):
    # Create a frame for the port selection and serial button
    port_frame = tk.Frame(cl, bg=BG_COLOR)
    port_frame.grid(row=0, column=0, pady=(0, 2), sticky="ew", padx=10)
    
    # Port selection label
    port_label = tk.Label(port_frame, text="COM Port:", bg=BG_COLOR, fg=TEXT_FG)
    port_label.grid(row=0, column=0, padx=(0, 5))
    
    # Port dropdown
    global port_var, port_dropdown
    port_var = tk.StringVar()
    port_dropdown = ttk.Combobox(port_frame, textvariable=port_var, state="readonly", width=10)
    port_dropdown.grid(row=0, column=1, padx=(0, 10))
    
    # Refresh button
    refresh_button = tk.Button(port_frame, text="↻", 
                             bg=BUTTON_BG, fg=BUTTON_FG,
                             activebackground=BUTTON_BG, 
                             activeforeground=BUTTON_FG,
                             command=refresh_ports,
                             width=2)
    refresh_button.grid(row=0, column=2)
    
    # Bind port selection event
    port_dropdown.bind('<<ComboboxSelected>>', on_port_select)
    
    # Initial port list population
    refresh_ports()
    
    # Serial connection button
    serialButton = tk.Button(cl, text="Close Serial Connection" if s.is_open else "Open Serial Connection", 
                           bg=BUTTON_ACTIVE_BG if s.is_open else BUTTON_INACTIVE_BG, fg=BUTTON_FG, 
                           activebackground=BUTTON_BG, activeforeground=BUTTON_FG)
    buttons[key] = {"button": serialButton, "type": "serial"}
    serialButton.config(command=lambda: updateSerialButton(key))
    serialButton.grid(row=1, column=0, pady=2, sticky="ew", padx=10)

def initCommandButton(cl, key):
    cmdButton = tk.Button(cl, text=key + " [WAITING]", 
                         bg=BUTTON_WAITING_BG, fg=BUTTON_WAITING_FG,
                         activebackground=BUTTON_BG, activeforeground=BUTTON_FG)
    buttons[key] = {"button": cmdButton, "type": "cmd"}
    cmdButton.config(command=lambda: updateCommandButton(key))
    # Calculate row based on number of existing command buttons
    cmd_count = len([b for b in buttons.values() if b['type'] == 'cmd'])
    # Start from row 3 (after port frame, serial button, and command label)
    cmdButton.grid(row=cmd_count + 3, column=0, pady=2, sticky="ew", padx=10)

def initValveButton(cl, key):
    valveButton = tk.Button(cl, text=key + " [WAITING]", 
                           bg=BUTTON_WAITING_BG, fg=BUTTON_WAITING_FG,
                           activebackground=BUTTON_BG, activeforeground=BUTTON_FG)
    buttons[key] = {"button": valveButton, "type": "valve"}
    valveButton.config(command=lambda: updateValveButton(key))
    # Calculate row based on number of existing valve buttons
    valve_count = len([b for b in buttons.values() if b['type'] == 'valve'])
    # Start from row 9 (after port frame, serial button, command label, command buttons, and valve label)
    valveButton.grid(row=valve_count + 9, column=0, pady=2, sticky="ew", padx=10)

def clean_shutdown():
    """Perform a clean shutdown of all threads and connections"""
    print("Performing clean shutdown...")
    
    # Stop all threads first
    stop_thread.set()
    
    # Close serial connection if open
    if ser.is_open:
        ser.close()
    
    # Clear all queues first
    while not serial_queue.empty():
        try:
            serial_queue.get_nowait()
        except queue.Empty:
            break
    while not data_queue.empty():
        try:
            data_queue.get_nowait()
        except queue.Empty:
            break
    while not update_queue.empty():
        try:
            update_queue.get_nowait()
        except queue.Empty:
            break
    while not command_queue.empty():
        try:
            command_queue.get_nowait()
        except queue.Empty:
            break
    
    # Wait for threads to finish with a longer timeout
    threads_to_join = []
    if serial_thread and serial_thread.is_alive():
        threads_to_join.append(serial_thread)
    if data_thread and data_thread.is_alive():
        threads_to_join.append(data_thread)
    if update_thread and update_thread.is_alive():
        threads_to_join.append(update_thread)
    if write_thread and write_thread.is_alive():
        threads_to_join.append(write_thread)
    
    # Join threads with longer timeout
    for thread in threads_to_join:
        try:
            # First try a gentle join
            thread.join(timeout=1.0)
            if thread.is_alive():
                print(f"Thread {thread.name} still alive after first join attempt")
                # Force stop the thread if it's still running
                if hasattr(thread, "_thread_id"):
                    thread_id = thread._thread_id
                    res = ctypes.pythonapi.PyThreadState_SetAsyncExc(ctypes.c_long(thread_id), ctypes.py_object(SystemExit))
                    if res == 0:
                        print(f"Failed to set exception for thread {thread.name}")
                    elif res != 1:
                        print(f"Failed to set exception for thread {thread.name}")
                        ctypes.pythonapi.PyThreadState_SetAsyncExc(ctypes.c_long(thread_id), None)
                # Try one more join
                thread.join(timeout=1.0)
                if thread.is_alive():
                    print(f"Warning: Thread {thread.name} did not terminate properly")
        except Exception as e:
            print(f"Error joining thread {thread.name}: {e}")
    
    # Exit the application
    app.quit()

class CapstoneGSE(tk.Tk):

    def __init__(self, *args, **kwargs):
        tk.Tk.__init__(self, *args, **kwargs)

        tk.Tk.iconbitmap(self, default="clienticon.ico")
        tk.Tk.wm_title(self, "SARP 2025 Capstone GSE")
        
        # Make window resizable
        self.grid_rowconfigure(0, weight=1)
        self.grid_columnconfigure(0, weight=1)
        
        container = tk.Frame(self)
        container.grid(row=0, column=0, sticky="nsew")
        container.grid_rowconfigure(0, weight=1)
        container.grid_columnconfigure(0, weight=1)

        self.frames = {}

        frame = MainPage(container, self)

        self.frames[MainPage] = frame

        frame.grid(row=0, column=0, sticky="nsew")
    
        self.show_frame(MainPage)
        
        # Add protocol handler for window close
        self.protocol("WM_DELETE_WINDOW", self.on_closing)

    def show_frame(self, cont):
        """Raise the specified frame to the top"""
        frame = self.frames[cont]
        frame.tkraise()

    def on_closing(self):
        """Handle window closing"""
        print("Closing application...")
        
        # Stop all threads first
        stop_thread.set()
        
        # Close serial connection if open
        if ser.is_open:
            ser.close()
        
        # Clear all queues first
        while not serial_queue.empty():
            try:
                serial_queue.get_nowait()
            except queue.Empty:
                break
        while not data_queue.empty():
            try:
                data_queue.get_nowait()
            except queue.Empty:
                break
        while not update_queue.empty():
            try:
                update_queue.get_nowait()
            except queue.Empty:
                break
        while not command_queue.empty():
            try:
                command_queue.get_nowait()
            except queue.Empty:
                break
        
        # Wait for threads to finish with a longer timeout
        threads_to_join = []
        if serial_thread and serial_thread.is_alive():
            threads_to_join.append(serial_thread)
        if data_thread and data_thread.is_alive():
            threads_to_join.append(data_thread)
        if update_thread and update_thread.is_alive():
            threads_to_join.append(update_thread)
        if write_thread and write_thread.is_alive():
            threads_to_join.append(write_thread)
        
        # Join threads with longer timeout
        for thread in threads_to_join:
            try:
                # First try a gentle join
                thread.join(timeout=1.0)
                if thread.is_alive():
                    print(f"Thread {thread.name} still alive after first join attempt")
                    # Force stop the thread if it's still running
                    if hasattr(thread, "_thread_id"):
                        thread_id = thread._thread_id
                        res = ctypes.pythonapi.PyThreadState_SetAsyncExc(ctypes.c_long(thread_id), ctypes.py_object(SystemExit))
                        if res == 0:
                            print(f"Failed to set exception for thread {thread.name}")
                        elif res != 1:
                            print(f"Failed to set exception for thread {thread.name}")
                            ctypes.pythonapi.PyThreadState_SetAsyncExc(ctypes.c_long(thread_id), None)
                    # Try one more join
                    thread.join(timeout=1.0)
                    if thread.is_alive():
                        print(f"Warning: Thread {thread.name} did not terminate properly")
            except Exception as e:
                print(f"Error joining thread {thread.name}: {e}")
        
        # Destroy the window
        self.destroy()

class MainPage(tk.Frame):

    def __init__(self, parent, controller):
        tk.Frame.__init__(self, parent, bg=BG_COLOR)
        
        # Configure grid weights - give more weight to graph column and console row
        self.grid_columnconfigure(0, weight=0)  # Button column (no weight - fixed width)
        self.grid_columnconfigure(1, weight=0)  # Resize handle column (no weight)
        self.grid_columnconfigure(2, weight=1)  # Graph column (takes remaining space)
        self.grid_rowconfigure(0, weight=0)     # Title row
        self.grid_rowconfigure(1, weight=0)     # Button frame row (no weight - fixed height)
        self.grid_rowconfigure(2, weight=0)     # Resize handle row
        self.grid_rowconfigure(3, weight=1)     # Console frame row (takes remaining vertical space)
        
        # Title frame with title and exit button
        title_frame = tk.Frame(self, bg=BG_COLOR)
        title_frame.grid(row=0, column=0, columnspan=3, sticky="ew", padx=10, pady=10)
        title_frame.grid_columnconfigure(0, weight=1)  # Give weight to title label
        title_frame.grid_columnconfigure(1, weight=0)  # No weight for banner
        title_frame.grid_columnconfigure(2, weight=0)  # No weight for exit button
        
        # Title label
        label = tk.Label(title_frame, text="2025 Capstone Hotfire Test Software", 
                        font=LARGE_FONT, bg=BG_COLOR, fg=TEXT_FG)
        label.grid(row=0, column=0, sticky="w")
        
        # HW ARM banner
        self.hw_arm_banner = tk.Label(title_frame, 
                                    text="HW ARM [WAITING]", 
                                    font=("Verdana", 10, "bold"),
                                    bg=BUTTON_WAITING_BG,
                                    fg=BUTTON_WAITING_FG)
        self.hw_arm_banner.grid(row=0, column=1, padx=10, sticky="ew")
        
        # Exit button in title frame
        exit_button = tk.Button(title_frame, 
                              text="Exit", 
                              bg=BUTTON_INACTIVE_BG,  # Red color
                              fg=BUTTON_FG,
                              activebackground=BUTTON_BG, 
                              activeforeground=BUTTON_FG,
                              command=clean_shutdown,
                              width=8)  # Fixed width for the button
        exit_button.grid(row=0, column=2, padx=(10,0), sticky="e")

        # Left frame for buttons with resizable width
        button_frame = tk.Frame(self, bg=BG_COLOR)
        button_frame.grid(row=1, column=0, sticky="nsew", padx=(10,0), pady=10)
        button_frame.grid_columnconfigure(0, weight=1)
        button_frame.grid_rowconfigure(0, weight=1)  # Make the canvas row expandable
        button_frame.grid_propagate(False)  # Prevent frame from shrinking
        button_frame.configure(width=button_width)  # Use button_width from JSON

        # Create a canvas and scrollbar for the button frame
        button_canvas = tk.Canvas(button_frame, bg=BG_COLOR, highlightthickness=0)
        button_scrollbar = tk.Scrollbar(button_frame, orient="vertical", command=button_canvas.yview)
        button_scrollable_frame = tk.Frame(button_canvas, bg=BG_COLOR)

        # Configure the canvas
        button_scrollable_frame.bind(
            "<Configure>",
            lambda e: button_canvas.configure(scrollregion=button_canvas.bbox("all"))
        )
        
        # Create the window at the top of the canvas with fixed width
        button_canvas.create_window((0, 0), window=button_scrollable_frame, anchor="nw", width=button_width-20)
        button_canvas.configure(yscrollcommand=button_scrollbar.set)

        # Configure the scrollable frame
        button_scrollable_frame.grid_columnconfigure(0, weight=1)

        # Grid the canvas and scrollbar
        button_canvas.grid(row=0, column=0, sticky="nsew")
        button_scrollbar.grid(row=0, column=1, sticky="ns")

        # Custom mousewheel handler to prevent scrolling above top
        def _on_mousewheel(event):
            if button_canvas.yview()[0] > 0 or event.delta < 0:
                button_canvas.yview_scroll(int(-1*(event.delta/120)), "units")
        
        button_canvas.bind_all("<MouseWheel>", _on_mousewheel)

        # Initialize buttons in the scrollable frame
        initSerialButton(button_scrollable_frame, ser, "serial")
        
        # Command buttons
        cmd_label = tk.Label(button_scrollable_frame, text="Commands", bg=BG_COLOR, fg=TEXT_FG, font=("Verdana", 10, "bold"))
        cmd_label.grid(row=2, column=0, pady=(10,5), sticky="ew")
        initCommandButton(button_scrollable_frame, "FIRE")
        initCommandButton(button_scrollable_frame, "ABORT")
        initCommandButton(button_scrollable_frame, "SW ARM")
        initCommandButton(button_scrollable_frame, "PULSE OX 100ms")
        initCommandButton(button_scrollable_frame, "PULSE FUEL 100ms")
        
        # Actuator buttons
        valve_label = tk.Label(button_scrollable_frame, text="Actuators", bg=BG_COLOR, fg=TEXT_FG, font=("Verdana", 10, "bold"))
        valve_label.grid(row=9, column=0, pady=(10,5), sticky="ew")
        initValveButton(button_scrollable_frame, "HBV")
        initValveButton(button_scrollable_frame, "FVV")
        initValveButton(button_scrollable_frame, "OBV")
        initValveButton(button_scrollable_frame, "OPV")
        initValveButton(button_scrollable_frame, "OVV")
        initValveButton(button_scrollable_frame, "OMV")
        initValveButton(button_scrollable_frame, "FMV")
        initValveButton(button_scrollable_frame, "IGNITER")

        # Calculate minimum height needed for all buttons
        button_spacing = 5  # Padding between buttons
        button_height = 30  # Height of each button
        label_height = 30   # Height for section labels
        port_frame_height = 35  # Height for port selection frame
        
        # Count elements in each section
        num_cmd_buttons = len([b for b in buttons.values() if b['type'] == 'cmd'])
        num_valve_buttons = len([b for b in buttons.values() if b['type'] == 'valve'])
        
        # Calculate total height needed
        min_button_height = (
            port_frame_height +  # Port selection frame
            button_height +  # Serial button
            label_height +  # Command label
            (num_cmd_buttons * (button_height + button_spacing)) +  # Command buttons
            label_height +  # Valve label
            (num_valve_buttons * (button_height + button_spacing)) +  # Valve buttons
            (4 * button_spacing)  # Extra padding between sections
        )
        
        # Add some extra padding at the bottom
        min_button_height += 20
        
        # Set the button frame height
        button_frame.configure(height=min_button_height)
        self.min_button_height = min_button_height
        self.button_canvas = button_canvas

        # Force update of the canvas scroll region and ensure it starts at the top
        button_canvas.configure(scrollregion=button_canvas.bbox("all"))
        button_canvas.yview_moveto(0)  # Ensure we start at the top

        # Add a resize handle between button frame and graph frame
        resize_handle = tk.Frame(self, bg="#404040", cursor="sb_h_double_arrow", width=5)
        resize_handle.grid(row=1, column=1, sticky="ns", padx=0, pady=10)
        
        # Bind mouse events for resizing
        resize_handle.bind("<Button-1>", self.start_resize)
        resize_handle.bind("<B1-Motion>", self.do_resize)
        resize_handle.bind("<ButtonRelease-1>", self.stop_resize)
        
        # Store initial button frame width
        self.button_frame = button_frame
        self.resize_handle = resize_handle
        self.initial_button_width = button_width  # Use button_width from JSON
        self.is_resizing = False

        # Add a vertical resize handle between buttons and console
        v_resize_handle = tk.Frame(self, bg="#808080", cursor="sb_v_double_arrow", height=5)
        v_resize_handle.grid(row=2, column=0, columnspan=2, sticky="ew", padx=10, pady=0)
        
        # Make the resize handle more visible
        v_resize_handle.lift()  # Bring to front
        
        # Bind mouse events for vertical resizing
        v_resize_handle.bind("<Button-1>", self.start_v_resize)
        v_resize_handle.bind("<B1-Motion>", self.do_v_resize)
        v_resize_handle.bind("<ButtonRelease-1>", self.stop_v_resize)
        
        # Store vertical resize handle reference
        self.v_resize_handle = v_resize_handle
        self.is_v_resizing = False

        # Console frame below buttons with auto-resize
        console_frame = tk.Frame(self, bg=BG_COLOR)
        console_frame.grid(row=3, column=0, sticky="nsew", padx=10, pady=(0, 5))
        console_frame.grid_columnconfigure(0, weight=1)
        console_frame.grid_rowconfigure(1, weight=1)  # Give weight to the text widget row
        console_frame.grid_propagate(False)  # Prevent frame from shrinking
        
        # Set initial console height from config
        console_frame.configure(height=serial_height)
        
        # Store console frame reference
        self.console_frame = console_frame
        
        # Console label
        console_label = tk.Label(console_frame, text="Serial Monitor", bg=BG_COLOR, fg=TEXT_FG, font=("Verdana", 10, "bold"))
        console_label.grid(row=0, column=0, sticky="ew", pady=(0, 1))
        
        # Console text widget with scrollbar
        console_text = tk.Text(console_frame, 
                             bg="black", 
                             fg="#00FF00",  # Green text like terminal
                             font=("Consolas", 10),  # Monospace font
                             wrap=tk.WORD,
                             height=5)  # Set a minimum height
        console_text.grid(row=1, column=0, sticky="nsew")
        
        # Scrollbar for console
        console_scrollbar = tk.Scrollbar(console_frame, orient="vertical", command=console_text.yview)
        console_scrollbar.grid(row=1, column=1, sticky="ns")
        console_text.configure(yscrollcommand=console_scrollbar.set)
        
        # Store console reference
        self.console_text = console_text

        # Right frame for graphs
        graph_frame = tk.Frame(self, bg=BG_COLOR)
        graph_frame.grid(row=1, column=2, rowspan=3, sticky="nsew", padx=(0,10), pady=10)
        graph_frame.grid_propagate(False)  # Prevent frame from shrinking
        
        # Configure graph frame grid
        num_graphs = len(gui_config['graphs'])
        num_cols = graph_width_ct
        num_rows = (num_graphs + num_cols - 1) // num_cols  # Ceiling division
        
        # Configure grid weights for equal distribution
        for i in range(num_cols):
            graph_frame.grid_columnconfigure(i, weight=1)
        for i in range(num_rows):
            graph_frame.grid_rowconfigure(i, weight=1)
            
        # Create a frame for each graph and its value display
        self.graph_frames = {}
        self.figures = {}  # Store figures for each sensor
        
        for i, graph_config in enumerate(gui_config['graphs']):
            # Calculate grid position
            row = i // num_cols
            col = i % num_cols
            
            graph_container = tk.Frame(graph_frame, bg=BG_COLOR)
            graph_container.grid(row=row, column=col, sticky="nsew", padx=5, pady=5)
            graph_container.grid_columnconfigure(0, weight=1)
            graph_container.grid_rowconfigure(0, weight=1)
            graph_container.grid_rowconfigure(1, weight=0)
            
            # Create a new figure for this sensor
            fig = Figure(figsize=(4, 3), dpi=100)  # Standard figure size
            fig.patch.set_facecolor(GRAPH_BG)
            self.figures[graph_config['label']] = fig
            
            # Create the subplot with adjusted margins
            ax = fig.add_subplot(111, title=graph_config['label'])
            # Adjust margins to prevent label overlap
            fig.subplots_adjust(top=0.85, bottom=0.25, left=0.2, right=0.95)
            ax.set_facecolor(GRAPH_BG)
            ax.tick_params(colors=GRAPH_FG, labelsize=8)
            ax.xaxis.label.set_color(GRAPH_FG)
            ax.yaxis.label.set_color(GRAPH_FG)
            ax.title.set_color(GRAPH_FG)
            # Adjust title font size based on length
            title_length = len(graph_config['label'])
            if title_length > 20:
                ax.title.set_fontsize(8)
            elif title_length > 15:
                ax.title.set_fontsize(9)
            else:
                ax.title.set_fontsize(10)
            for spine in ax.spines.values():
                spine.set_color(GRAPH_FG)
            ax.grid(True, linestyle='--', alpha=0.3)
            ax.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M:%S'))
            plt.setp(ax.get_xticklabels(), rotation=45, ha='right')
            
            # Set y-axis label using units from config
            if 'units' in graph_config and graph_config['units']:
                ax.set_ylabel(graph_config['units'][0])  # Use first unit as y-axis label
            
            # Store the data reference
            for j, data_key in enumerate(graph_config['data_keys']):
                data[data_key] = [ax, list(), graph_config['kwargs'][j]]
            
            # Create canvas for the graph
            canvas = FigureCanvasTkAgg(fig, graph_container)
            canvas.draw()
            canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")
            
            # Create value display frame with smaller font
            value_frame = tk.Frame(graph_container, bg=BG_COLOR)
            value_frame.grid(row=1, column=0, sticky="ew")
            
            # Create labels for each sensor in the graph
            self.graph_frames[graph_config['label']] = []
            for j, data_key in enumerate(graph_config['data_keys']):
                label = tk.Label(value_frame, 
                               text=f"{data_key}: --",
                               bg=TEXT_BG,
                               fg=TEXT_FG,
                               font=("Verdana", 7))
                label.grid(row=0, column=j, padx=5)
                self.graph_frames[graph_config['label']].append(label)
                
        # Bind resize event to ensure proper resizing
        self.bind("<Configure>", self.on_resize)
        
        # Force initial layout update
        self.update_idletasks()
        console_frame.update_idletasks()
        
    def on_resize(self, event):
        """Handle window resize events to ensure proper resizing of components"""
        # Force update of the console text widget
        self.console_text.update_idletasks()
        
        # Ensure the console text widget takes up available space
        self.console_text.grid_configure(sticky="nsew")

    def start_resize(self, event):
        """Start the resize operation"""
        self.is_resizing = True
        self.start_x = event.x_root
        self.start_width = self.button_frame.winfo_width()
        # Store the initial position of the resize handle
        self.resize_handle.lift()  # Bring resize handle to front

    def do_resize(self, event):
        """Handle the resize operation"""
        if self.is_resizing:
            # Calculate new width
            delta = event.x_root - self.start_x
            new_width = max(button_width, self.start_width + delta)  # Use button_width from JSON as minimum
            
            # Update button frame width
            self.button_frame.configure(width=new_width)
            
            # Update canvas window width
            self.button_canvas.itemconfig(1, width=new_width-20)  # Update the canvas window width
            
            # Force update of layout
            self.button_frame.update_idletasks()
            
            # Update the window to ensure smooth resizing
            self.update_idletasks()
            
            # Update the root window's minimum size
            min_width = new_width + 200  # Reduced space for graph area
            app.minsize(min_width, 600)  # Keep the minimum height at 600
            
            # Force the graph frame to update its layout
            self.grid_columnconfigure(2, weight=1)  # Ensure graph column takes remaining space
            self.update_idletasks()
            
            # Force the window to update its layout
            app.update_idletasks()

    def stop_resize(self, event):
        """Stop the resize operation"""
        self.is_resizing = False
        # Ensure the resize handle stays visible
        self.resize_handle.lift()

    def start_v_resize(self, event):
        """Start the vertical resize operation"""
        self.is_v_resizing = True
        self.start_y = event.y_root
        self.start_button_height = self.button_frame.winfo_height()
        self.start_console_height = self.console_frame.winfo_height()
        # Store the initial position of the resize handle
        self.v_resize_handle.lift()  # Bring resize handle to front

    def do_v_resize(self, event):
        """Handle the vertical resize operation"""
        if self.is_v_resizing:
            # Calculate new heights with reversed delta for intuitive control
            delta = -(event.y_root - self.start_y)  # Reversed delta calculation
            
            # Calculate new button height (inverse of console height change)
            new_button_height = max(self.min_button_height, self.start_button_height - delta)
            
            # Calculate new console height
            new_console_height = max(100, self.start_console_height + delta)
            
            # Get the total available height (window height minus title and padding)
            total_height = self.winfo_height() - 100  # Approximate space for title and padding
            
            # Ensure the sum of heights doesn't exceed total available space
            if new_button_height + new_console_height > total_height:
                # Adjust both heights proportionally
                ratio = total_height / (new_button_height + new_console_height)
                new_button_height = int(new_button_height * ratio)
                new_console_height = int(new_console_height * ratio)
            
            # Update both frame heights
            self.button_frame.configure(height=new_button_height)
            self.console_frame.configure(height=new_console_height)
            
            # Update canvas scroll region when button frame is resized
            self.button_canvas.configure(scrollregion=self.button_canvas.bbox("all"))
            
            # Force update of layout
            self.button_frame.update_idletasks()
            self.console_frame.update_idletasks()
            
            # Update the window to ensure smooth resizing
            self.update_idletasks()

    def stop_v_resize(self, event):
        """Stop the vertical resize operation"""
        self.is_v_resizing = False
        # Ensure the resize handle stays visible
        self.v_resize_handle.lift()

graph_width_ct = 1
graph_height_ct = 1
window_width = 720
window_height = 480
button_width = 100
serial_height = 200  # Default serial monitor height
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
if 'button_width' in gui_config:
    button_width = gui_config['button_width']
if 'serial_height' in gui_config:
    serial_height = gui_config['serial_height']
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

f = None  # This will be set when MainPage is created

data = dict()
state = dict()
info = dict()
mutually_inclusive = dict()
mutually_exclusive = dict()

loop_time = 0
tried_to_open = False
newCommand = [0,0,0,0,0,0,0,0,0,0,0,0,0,0]

for i in gui_config['state'].keys():
    state[i] = {
        "lastUpdated": 0,
        "currentState": gui_config['state'][i]['currentState'],
        "commandedState": gui_config['state'][i]['commandedState'],
        "inProgress": gui_config['state'][i]['inProgress'],
        "nominalState": gui_config['state'][i]['nominalState'],
        "commandIndex": gui_config['state'][i]['commandIndex']
        }
    newCommand[state[i]['commandIndex']] = state[i]['commandedState']
    if state[i]['nominalState'] != None:
        mutually_inclusive[i] = gui_config['state'][i]['mutuallyInclusive']
        mutually_exclusive[i] = gui_config['state'][i]['mutuallyExclusive']
    

app = CapstoneGSE()
app.geometry(f"{window_width}x{window_height}")  # Set initial window size
app.minsize(800, 600)  # Minimum window size

# Create animation for each figure with minimal updates
animations = []
for fig in app.frames[MainPage].figures.values():
    ani = animation.FuncAnimation(
        fig, 
        lambda i: None, 
        interval=1000,  # Dummy animation to keep figures alive
        cache_frame_data=False,  # Disable frame caching
        save_count=1  # Minimal save count since we're not actually animating
    )
    animations.append(ani)

app.mainloop()