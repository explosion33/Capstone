int i;

void setup() {
    Serial.begin(115200);
    Serial.println("Init");
    i = 0;
}

void loop() {
    Serial.println(i);
    i++;
    delay(100);
}