# DAQ Firmware

## Commands

1. Request Sensor Data

    ```
    {}
    ```
    Success:
    ```
    {
    "Sensor1" : [time (ms), value (float), raw (float or uint16_t)],
    "Sensor2" : [time (ms), value (float), raw (float or uint16_t)],
    ...
    "SensorN" : [time (ms), value (float), raw (float or uint16_t)]
    }
    ```

2.  Tare Sensor

    completes an auto tareing of the selected sensor around a set-point. All tareing is done post-gain calculation. the tareing process should take ~1s, during which time data is not available to be logged or requested.
    ```
    // {"<Sensor Name>": <tare value (float)>}
    {"LC1": 0.0}
    ```
    Success:
    ```
    taring: LC1 to 0.000000
    DONE
    ```
    Fail:
    ```
    Sensor does not exist
    DONE
    ```

3. Direct Actuation

    Actuate each solenoid group directly according to the table below, also found on [This Google Sheet](https://docs.google.com/spreadsheets/d/1ON2VdkJxlJqttcMQD-l6U2NdHoalpuC-nYF2xrq4dh4/edit?gid=0#gid=0). Actuation channel is the physical actuation hardware channel each device occupies. Command Channel is the command position (\<C0\> --> \<C7\>).
		
    | Name	            | Abbreviation  | Nominal State    | Actuation Channel (0-15) | Command Channel | Physical Pins       |
    | ----------------------|---------------|------------------|--------------------------|-----------------|---------------------|
    | Helium Bottle Valve   | HBV	    | Closed	       | 6, 7                     | 4               | PB_15               |
    | Oxygen Bottle Valve   | OBV	    | Closed	       | 10, 11                   | 6               | PC_6                |
    | Oxygen Purge Valve    | OPV	    | Closed	       | 12, 13                   | 5               | PC_7                |
    | Fuel Vent Valve	    | FVV	    | Closed	       | 8, 9                     | 3               | PC_8                |
    | Oxygen Vent Valve	    | OVV	    | Closed	       | 14, 15                   | 7               | PC_9                |
    | Oxygen Main Valve	    | OMV	    | Closed	       | 0, 1                     | 1               | PA_9                |
    | Fuel Main Valve	    | FMV	    | Closed	       | 2, 3                     | 0               | PA_11		          |
    | Igniter	            | IGN	    | Off	           | 4, 5                     | 2               | PA_12               |

    ```
    // {S<C0><C1><C2><C3><C4><C5><C6><C7>}
       {S00000000} // all off
       {S11111111} // all on
    ```

    \* Command Channels work with P-TYPE transistors / mofsets, so writing a `'1'` pulls each physical line low

5. Command Sequences

    Have the DAQ perform a pre-loaded command sequence as follows:

    |Command             |Abbreviation                            | Description                                                             |
    |--------------------|----------------------------------------|-------------------------------------------------------------------------|
    | Fire               | FI                                     | Perform Main Fire Sequence                                              |
    | Abort              | AB                                     | Abort any command, close main valves                                    |
    | Pulse Helium       | HP<span style="color: green">%d</span> | Pulse Heium for <span style="color: green">%d</span> ms (defaut 100ms)  |
    | Pulse Fuel         | FP<span style="color: green">%d</span> | Pulse Fuel for <span style="color: green">%d</span> ms (defaut 100ms)   |
    | Pulse Oxygen       | OP<span style="color: green">%d</span> | Pulse Oxygen for <span style="color: green">%d</span> ms (defaut 100ms) |

    ```
    // {CXX}
       {CFI}    // fire
       {CAB}    // abort
       {CHP}    // pulse 100ms
       {CHP500} // pulse 500ms
    ```

6. Mount Disk

    mounts an SD card and opens a file at the given path for sensor logging

    ```
    // {DM/sd/<path>}
       {DM/sd/log.txt}
    ```

7. Eject Disk

    flushes and ejects SD card for safe removal (extremely important, this isn't like windows where ejecting is optionally (usually))

    ```
    {DE}
    ```
