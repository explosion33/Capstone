#define IND_timeSent      0  // 4 bytes
#define IND_timeConnected 1  // 4 bytes
#define IND_HBPT          0  // 4 bytes
#define IND_FTPT          1  // 4 bytes
#define IND_OBPT          2  // 4 bytes
#define IND_OVPT          3  // 4 bytes
#define IND_FMPT          4  // 4 bytes
#define IND_OMPT          5  // 4 bytes
#define IND_FRMPT         6  // 4 bytes
#define IND_FRMRTD        7  // 4 bytes
#define IND_FMRTD         8  // 4 bytes
#define IND_HBTT          9  // 4 bytes
#define IND_OBTT          10 // 4 bytes
#define IND_LC            11 // 4 bytes
#define IND_RRTD1         12
#define IND_RRTD2         13
#define IND_FIRE          0  // 3 bits
#define IND_ABORT         1  // 3 bits
#define IND_SW_ARM        2  // 3 bits
#define IND_HW_ARM        3  // 1 bit
#define IND_HBV           4  // 2 bits
#define IND_FVV           5  // 2 bits
#define IND_OBV           6  // 2 bits
#define IND_OPV           7  // 2 bits
#define IND_OVV           8  // 2 bits
#define IND_OMV           9  // 2 bits
#define IND_FMV           10 // 2 bits
#define IND_IGNITER       11 // 2 bits
#define IND_PULSE_OX      12 // 2 bits
#define IND_PULSE_FUEL    13 // 2 bits
#define OFFSET_inProgress 0
#define OFFSET_commanded  1
#define OFFSET_state      2
#define COUNT_COMMANDS    14
#define COUNT_SENSORS     14
#define loopTime          500

const String command_keys[] = {
    "FIRE", "ABORT",   "SW ARM",         "HW ARM",          "HBV", "FVV", "OBV", "OPV", "OVV", "OMV",
    "FMV",  "IGNITER", "PULSE OX 100ms", "PULSE FUEL 100ms"};
const String sensor_keys[] = {"HBPT",   "FTPT",  "OBPT", "OVPT", "FMPT", "OMPT",  "FRMPT",
                              "FRMRTD", "FMRTD", "HBTT", "OBTT", "LC",   "RRTD1", "RRTD2"};

const String trueValue = "true";
const String falseValue = "false";
float sensor_data[COUNT_SENSORS];
uint8_t command_state[COUNT_COMMANDS];
long connection_time;
long initial_connection_time = 0; // Add variable to store initial connection time
int currentTime;
int loopIteration = 0;
long lastLoopTimeTaken;
long lastLoopStartTime;

void setup() {
    Serial.begin(115200);
    while (!Serial)
        ;
    for (int i = 0; i < COUNT_SENSORS; i++) {
        sensor_data[i] = 0.0;
    }
    for (int i = 0; i < COUNT_COMMANDS; i++) {
        writeCommand(i, false, false, false);
    }
    connection_time = millis();
    if (initial_connection_time == 0) { // Only set initial connection time once
        initial_connection_time = connection_time;
    }

    lastLoopStartTime = millis();
    Serial.println(connection_time);
}

void loop() {
    if (!Serial) {
        Serial.begin(115200);
        while (!Serial)
            ;
        connection_time = millis(); // Update current connection time
    }
    currentTime = millis() - connection_time;
    while (currentTime < (loopIteration * loopTime)) {
        refreshSensors();
        receiveCommands();
        refreshCommands();
        currentTime = millis() - connection_time;
    }
    loopIteration++;
    lastLoopTimeTaken = currentTime - lastLoopStartTime;
    lastLoopStartTime = currentTime;
    Serial.print("{");
    Serial.print("\"LoopTime\":");
    Serial.print(lastLoopTimeTaken);
    Serial.print(",");
    Serial.print("\"timeSent\":");
    Serial.print(millis() - initial_connection_time); // Use time relative to initial connection
    Serial.print(",");
    for (int i = 0; i < COUNT_SENSORS; i++) {
        Serial.print("\"");
        Serial.print(sensor_keys[i]);
        Serial.print("\":");
        Serial.print(sensor_data[i]);
        Serial.print(",");
    }
    for (int i = 0; i < COUNT_COMMANDS; i++) {
        Serial.print("\"");
        Serial.print(command_keys[i]);
        Serial.print("\":[");
        if ((command_state[i] >> 2) & 0x01) {
            Serial.print(trueValue);
        } else {
            Serial.print(falseValue);
        }
        Serial.print(",");
        if ((command_state[i] >> 1) & 0x01) {
            Serial.print(trueValue);
        } else {
            Serial.print(falseValue);
        }
        Serial.print(",");
        if ((command_state[i] >> 0) & 0x01) {
            Serial.print(trueValue);
        } else {
            Serial.print(falseValue);
        }
        if (i < COUNT_COMMANDS - 1) {
            Serial.print("],");
        } else {
            Serial.print("]}\n");
        }
    }
}

void refreshSensors() {
    for (int i = 0; i < COUNT_SENSORS; i++) {
        sensor_data[i] = float(random(0, 10000)) / 7;
    }
}

void refreshCommands() {
    for (int i = 0; i < COUNT_COMMANDS; i++) {
        bool currentState = getCommandValue(i, OFFSET_state);
        bool commandedState = getCommandValue(i, OFFSET_commanded);
        bool inProgress = getCommandValue(i, OFFSET_inProgress);

        if (inProgress) {
            // If command was in progress, complete it by updating state to match commanded state
            writeCommand(i, commandedState, commandedState, false);
            // Force an immediate state update
            Serial.flush();
        }
    }
}

void writeCommand(uint8_t index, bool state, bool commanded, bool inProgress) {
    // Clear all bits first
    command_state[index] = 0;

    // Set bits based on parameters
    if (inProgress) {
        command_state[index] |= (1 << OFFSET_inProgress);
    }
    if (commanded) {
        command_state[index] |= (1 << OFFSET_commanded);
    }
    if (state) {
        command_state[index] |= (1 << OFFSET_state);
    }

    // Force an immediate state update
    Serial.flush();
}

bool getCommandValue(uint8_t index, uint8_t valueType) { return ((command_state[index] >> valueType) & 0x01); }

void receiveCommands() {
    if (Serial.available() == 0) {
        return;
    }
    String newCommands = Serial.readStringUntil('\n');
    Serial.print("{\"received\":\"");
    Serial.print(newCommands);
    Serial.println("\"}");
    int startIndex = 0;
    int valueIndex = 0;

    while (valueIndex < COUNT_COMMANDS) {
        int commaIndex = newCommands.indexOf(',', startIndex);

        String token;
        if (commaIndex == -1) {
            // Last value (no comma at the end)
            token = newCommands.substring(startIndex);
        } else {
            token = newCommands.substring(startIndex, commaIndex);
        }

        token.trim(); // Clean up any spaces
        bool newCommand = (token == "1" || token == "true");

        // Get current state values
        bool currentState = getCommandValue(valueIndex, OFFSET_state);
        bool commandedState = getCommandValue(valueIndex, OFFSET_commanded);

        // Update command state
        if (newCommand != commandedState) {
            // When commanded state changes, keep current state but mark as in progress
            writeCommand(valueIndex, currentState, newCommand, true);
        }

        valueIndex++;
        if (commaIndex == -1)
            break; // No more commas

        startIndex = commaIndex + 1;
    }
}