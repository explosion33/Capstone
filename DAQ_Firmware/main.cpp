#include "mbed.h"
#include <cstdint>
#include <cstdio>
#include <vector>

#include "RTD.h"
#include "ADC.h"
#include "LoadCellSensor.h"
#include "SensorEventQueue.h"

#define UART_TX PA_2
#define UART_RX PA_3


BufferedSerial ser(PA_2, PA_3, 115200);
void printf_nb(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char buf[64] = {0};
    size_t n = vsnprintf (buf, 64, format, args);

    va_end(args);

    ser.write(buf, n);
}

/*
#include "HX711.h"

#define LOAD_MV_V 1.892f
#define LOAD_V_E  4.55f
#define LOAD_V_REF 4.55f
#define LOAD_CAP 588.399 //60kg (Newtons)

HX711 hx711(LOAD_V_REF, PA_0, PA_1, 64);
volatile float tare;
volatile float gain;

void tare_cell() {
    int i = 0;
    ThisThread::sleep_for(1s);
    while (true) {
        if (hx711.isReady()) {
            tare += hx711.read();
            i += 1;
            ThisThread::sleep_for(25ms);
        }

        if (i == 40) {
            tare /= 40.0;
            break;
        }
    }
}

void sample_load_cell() {
    if (!hx711.isReady()) {
        return;
    }

    float raw = hx711.read();
    float mV = (raw - tare) * gain;    // read voltage in mV

    float force = (mV) / (LOAD_MV_V * LOAD_V_E) * LOAD_CAP;
    
    int ms = t.read_ms();

    printf_nb("(%d ms) force = %f N (%f, %f)\n", ms, force, raw, tare);
}
*/


// serial data available interrupt
volatile bool read_flag = false;
void serial_isr() {
    read_flag = true;
}


int main() {
    ser.sigio(serial_isr); // enable serial interrupt

    SPI spi(PA_7, PA_6, PA_5);
    spi.format(8, 1); 
    
    SensorEventQueue queue;   

    // Load Cell Setup
    //tare = 0.0f;
    //gain = 7.33f;

    //tare_cell();

    //Thread th;
    //th.start(spawn);

    //Ticker load_ticker;
    //Ticker rtd_ticker;

    // ======== Load Cell Setup ========
    LoadCellSensor lc1("LC1", PA_0, PA_1, 1.892f, 4.55f, 4.55f, 588.399f);
    queue.queue(callback(&lc1, &LoadCellSensor::sample_log), 100);
    // =================================

    // =========== RTD Setup ===========
    RTD rtd1("RTD1", &spi, PB_6);
    vector<RTD*> rtds = {&rtd1};

    for (RTD* rtd : rtds) {
        queue.queue(callback(rtd, &RTD::sample_log), 90);
    }
    // =================================

    // =========== ADC Setup ===========
    ADCSensor adc1("ADC1", PC_0, 1.5f, 0, 5);
    vector<ADCSensor*> adcs = {&adc1};

    for (ADCSensor* adc : adcs) {
        queue.queue(callback(adc, &ADCSensor::sample_log), 20);
    }
    // =================================
    

    Thread t;
    t.start(callback(&queue, &SensorEventQueue::run));

    // =========== GUI Comms ===========
    int i = 0;
    char buf[256] = {0};

    Timer h;
    h.start();

    while (true) {
        if (read_flag == true) {
            while (ser.readable()) {
                char c = 0;
                ser.read(&c, 1);

                if (c == '{') {
                    i = 0;
                }
                else if (c == '}') {
                    buf[i] = 0;
                    printf_nb("=========================================================\n");
                    printf_nb("%s\n", buf);
                    printf_nb("=========================================================\n");

                    char name[50] = {0};
                    float tare;
                    int res = sscanf(buf, "\"%[^\"]\": %f", name, &tare);
                    if (res == 2) {
                        if (strcmp(lc1.name, name) == 0) {
                            printf_nb("taring: %s to %f\n", name, tare);
                            lc1.tare(tare);
                        }
                        else {
                            bool found = false;
                            for (ADCSensor* adc : adcs) {
                                if (strcmp(adc->name, name) == 0) {
                                    found = true;
                                    printf_nb("taring: %s to %f\n", name, tare);
                                    adc->tare(tare);
                                    break;
                                }
                            }

                            if (!found) {
                                printf_nb("Sensor does not exist\n");
                            }
                        }

                        printf_nb("DONE\n");

                    }
                    else {
                        printf_nb("{\n");
                        for (RTD* rtd : rtds) {
                            int time;
                            float value;
                            uint16_t raw;
                            rtd->last_data(&value, &raw, &time);
                            printf_nb("\"%s\" : [%d, %f, %d],\n", rtd->name, time, value, raw);                     
                        }
                        for (ADCSensor* adc : adcs) {
                            int time;
                            float value;
                            float raw;
                            adc->last_data(&value, &raw, &time);
                            printf_nb("\"%s\" : [%d, %f, %f],\n", adc->name, time, value, raw);                     
                        }

                        int time;
                        float value;
                        float raw;
                        lc1.last_data(&value, &raw, &time);
                        printf_nb("\"%s\" : [%d, %f, %f]\n", lc1.name, time, value, raw);

                        printf_nb("}\n");
                    }

                    i = 0;
                    
                }
                else {
                    buf[i] = c;
                    i ++;
                }
            }

            read_flag = false;
        }
        ThisThread::yield();
    }
    // =================================
}
