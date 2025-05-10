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

// ==================== Logging ====================
    BufferedSerial ser(PA_2, PA_3, 115200);
    void printf_nb(const char* format, ...) {
        va_list args;
        va_start(args, format);
        
        char buf[64] = {0};
        size_t n = vsnprintf (buf, 64, format, args);

        va_end(args);

        ser.write(buf, n);
    }

    void log_nb(const char* format, ...) {
        va_list args;
        va_start(args, format);
        
        char buf[64] = {0};
        size_t n = vsnprintf (buf, 64, format, args);

        va_end(args);

        ser.write("log: ", 5);
        ser.write(buf, n);
    }
// =================================================

// serial rx interrupt
volatile bool read_flag = false;
void serial_isr() {
    read_flag = true;
}

vector<DigitalOut> solenoids;
// =============== Command Sequences ===============
    void cmd_fire() {
        Timer cmd_timer;
        cmd_timer.start();
        log_nb("(%d ms) Firing\n", cmd_timer.read_ms());
        
        Timer i_timer;
        Timer fire_timer;
        i_timer.start();
        
        // fire igniter
        solenoids[9].write(0);
        solenoids[10].write(0);
        solenoids[11].write(0);
        log_nb("(%d ms) igniter on\n", cmd_timer.read_ms());

        // open FMV
        solenoids[8].write(0);
        log_nb("(%d ms) FMV open\n", cmd_timer.read_ms());
        
        ThisThread::sleep_for(65ms);

        // open OMV
        solenoids[6].write(0);
        solenoids[7].write(0);
        log_nb("(%d ms) OMV open\n", cmd_timer.read_ms());

        fire_timer.start();

        while (true) {
            // igniter off after 2s
            if (i_timer.read_ms() >= 2000) {
                i_timer.stop();
                i_timer.reset();

                solenoids[9].write(1);
                solenoids[10].write(1);
                solenoids[11].write(1);
                log_nb("(%d ms) igniter off\n", cmd_timer.read_ms());
            }

            // open OPV 15s after OMV opens
            if (fire_timer.read_ms() >= 15000) {
                break;
            }
            ThisThread::yield();
        }

        // close OBV 15s after OMV opens
        solenoids[1].write(1);
        log_nb("(%d ms) OBV closed\n", cmd_timer.read_ms());

        // open OPV 15s after OMV opens
        solenoids[2].write(0);
        log_nb("(%d ms) OPV open\n", cmd_timer.read_ms());

        ThisThread::sleep_for(30s);

        // close OPV and HBV
        solenoids[2].write(1);
        solenoids[0].write(1);
        log_nb("(%d ms) OPV and HBV closed\n", cmd_timer.read_ms());

        ThisThread::sleep_for(30s);

        // close OMV and FMV
        solenoids[6].write(1);
        solenoids[7].write(1);
        solenoids[8].write(1);
        log_nb("(%d ms) OMV and FMV closed\n", cmd_timer.read_ms());

        log_nb("(%d ms) Done Firing\n", cmd_timer.read_ms());
    }

    void cmd_abort() {
        Timer cmd_timer;
        cmd_timer.start();

        log_nb("(%d ms) Aborting\n", cmd_timer.read_ms());

        solenoids[6].write(1);
        solenoids[7].write(1);
        solenoids[8].write(1);

        log_nb("(%d ms) OMV and FMV closed\n", cmd_timer.read_ms());
        log_nb("(%d ms) Done Aborting\n", cmd_timer.read_ms());
    }

    void cmd_pulse_fuel(int pulse_ms) {
        Timer cmd_timer;
        cmd_timer.start();

        log_nb("(%d ms) Pulsing Fuel\n", cmd_timer.read_ms());
        
        solenoids[8].write(0);
        log_nb("(%d ms) FMV Open\n", cmd_timer.read_ms());

        ThisThread::sleep_for(pulse_ms);
        
        solenoids[8].write(1);
        log_nb("(%d ms) FMV Closed\n", cmd_timer.read_ms());

        log_nb("(%d ms) Done Pulsing Fuel\n", cmd_timer.read_ms());
    }

    void cmd_pulse_helium(int pulse_ms) {
        Timer cmd_timer;
        cmd_timer.start();

        log_nb("(%d ms) Pulsing Helium\n", cmd_timer.read_ms());
        
        solenoids[0].write(0);
        log_nb("(%d ms) HBV Open\n", cmd_timer.read_ms());

        ThisThread::sleep_for(pulse_ms);
        
        solenoids[0].write(1);
        log_nb("(%d ms) HBV Closed\n", cmd_timer.read_ms());

        log_nb("(%d ms) Done Pulsing Helium\n", cmd_timer.read_ms());
    }

    void cmd_pulse_ox(int pulse_ms) {
        Timer cmd_timer;
        cmd_timer.start();

        log_nb("(%d ms) Pulsing Oxygen\n", cmd_timer.read_ms());
        
        solenoids[6].write(0);
        solenoids[7].write(0);
        log_nb("(%d ms) OMV Open\n", cmd_timer.read_ms());

        ThisThread::sleep_for(pulse_ms);
        
        solenoids[6].write(1);
        solenoids[7].write(1);
        log_nb("(%d ms) OMV Closed\n", cmd_timer.read_ms());

        log_nb("(%d ms) Done Pulsing Oxygen\n", cmd_timer.read_ms());
    }
// =================================================

int main() {
    ser.sigio(serial_isr); // enable serial interrupt

    // SPI setup for RTDs
    SPI spi(PA_7, PA_6, PA_5);
    spi.format(8, 1); 
    
    SensorEventQueue queue;   

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
    
    // ======== Actuation Setup ========
        PinName actuation_pins[12] = {PC_3, PC_2, PB_9, PB_8, PC_13, PB_7, PA_15, PA_14, PA_13, PC_12, PC_10, PC_11};
        // declared in global
        // vector<DigitalOut> solenoids;
        for (PinName pin: actuation_pins) {
            DigitalOut p(pin);
            p.write(1);
            solenoids.push_back(p);
        }
    // =================================


    Thread t;
    t.start(callback(&queue, &SensorEventQueue::run));

    // =========== GUI Comms ===========
    int i = 0;
    char buf[256] = {0};

    Timer h;
    h.start();

    Thread* cmd_thread = new Thread;

    while (true) {
        if (read_flag == true) {
            while (ser.readable()) {
                char c = 0;
                ser.read(&c, 1);

                if (c == '{') {
                    i = 0;
                }
                else if (c == '}') { // packet completed
                    buf[i] = 0;
                    log_nb("recieved: %s\n", buf);

                    char name[50] = {0};
                    float tare;
                    int res = sscanf(buf, "\"%[^\"]\": %f", name, &tare);
                    
                    if (res == 2) { // tare command
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

                    else if (buf[0] == 'S') { // Solenoid Direct Command
                        cmd_thread->terminate();
                        delete cmd_thread;
                        cmd_thread = new Thread;
                        
                        log_nb("%s\n", &buf[1]);

                        log_nb("%d\n", buf[4] == '0');

                        solenoids[0].write(buf[0+1] == '0');
                        solenoids[1].write(buf[1+1] == '0');
                        solenoids[2].write(buf[2+1] == '0');
                        solenoids[3].write(buf[3+1] == '0');
                        solenoids[4].write(buf[4+1] == '0');
                        solenoids[5].write(buf[4+1] == '0');
                        solenoids[6].write(buf[5+1] == '0');
                        solenoids[7].write(buf[5+1] == '0');
                        solenoids[8].write(buf[6+1] == '0');
                        solenoids[9].write(buf[7+1] == '0');
                        solenoids[10].write(buf[7+1] == '0');
                        solenoids[11].write(buf[7+1] == '0');
                    }
                    else if (buf[0] == 'C') { // Command Sequence
                        if (buf[1] == 'F' && buf[2] == 'I') {
                            cmd_thread->terminate();
                            delete cmd_thread;
                            cmd_thread = new Thread;
                            cmd_thread->start(cmd_fire);
                        }
                        else if (buf[1] == 'A' && buf[2] == 'B') {
                            cmd_thread->terminate();
                            delete cmd_thread;
                            cmd_thread = new Thread;
                            cmd_thread->start(cmd_abort);
                        }
                        else if (buf[1] == 'F' && buf[2] == 'P') {
                            cmd_thread->terminate();
                            delete cmd_thread;
                            cmd_thread = new Thread;

                            int pulse_ms = 100;
                            sscanf(&buf[3], "%d", &pulse_ms);

                            cmd_thread->start([pulse_ms]() {cmd_pulse_fuel(pulse_ms);});
                        }
                        else if (buf[1] == 'H' && buf[2] == 'P') {
                            cmd_thread->terminate();
                            delete cmd_thread;
                            cmd_thread = new Thread;
                            
                            int pulse_ms = 100;
                            sscanf(&buf[3], "%d", &pulse_ms);

                            cmd_thread->start([pulse_ms]() {cmd_pulse_helium(pulse_ms);});
                        }
                        else if (buf[1] == 'O' && buf[2] == 'P') {
                            cmd_thread->terminate();
                            delete cmd_thread;
                            cmd_thread = new Thread;
                            
                            int pulse_ms = 100;
                            sscanf(&buf[3], "%d", &pulse_ms);

                            cmd_thread->start([pulse_ms]() {cmd_pulse_ox(pulse_ms);});
                        }
                        else {
                            printf_nb("Invalid Command\n");
                        }
                    }

                    else { // No Command Found, Log Data
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
