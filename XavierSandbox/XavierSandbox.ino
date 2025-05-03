byte i[] = {0, 1, 2, 3, 4};
String keys[] = {"data0", "data1", "data2", "data3", "data4"};
byte length_of_i;
void setup() {
    Serial.begin(115200);
    Serial.println("Init");
    length_of_i = sizeof(i) / sizeof(i[0]);
}

void loop() {
    Serial.print("[");
    for (int k = 0; k < length_of_i; k++) {
        if (i[k] == 101) {
            i[k] = 0;
        }
        Serial.print("\"" + keys[k] + "\"");
        Serial.print(":");
        Serial.print(i[k]);
        if (k < length_of_i - 1) {
            Serial.print(",");
        } else {
            Serial.println("]");
        }
        i[k]++;
    }
    delay(100);
}