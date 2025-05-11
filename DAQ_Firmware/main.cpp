#include "mbed.h"
#include <cstdint>
#include <cstdio>
#include <vector>

#include "SDBlockDevice.h"
#include "FATFileSystem.h"

#include "RTD.h"
#include "ADC.h"
#include "LoadCellSensor.h"
#include "SensorEventQueue.h"

#define UART_TX PA_2
#define UART_RX PA_3

#define SD_MOSI PA_7
#define SD_MISO PA_6
#define SD_SCLK PA_5
#define SD_CS   PB_6

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


// ================= SD CARD INIT ==================
    SDBlockDevice _sd(SD_MOSI, SD_MISO, SD_SCLK, SD_CS, 1000000);
    FATFileSystem _fs("sd");

    void deinit_sd(FILE*& file, Mutex& sd_mutex) {
        sd_mutex.lock();

        if (file == nullptr) {
            sd_mutex.unlock();
            return;
        }

        fclose(file);
        file = nullptr;
        _fs.unmount();
        sd_mutex.unlock();
    }

    bool init_sd(FILE*& file, const char* path) {
        int init_err = _sd.init();
        if (init_err) {
            log_nb("SD init failed: %d\n", init_err);
            return false;
        }

        int err = _fs.mount(&_sd);
        if (err) {
            log_nb("No filesystem found. Formatting...\n");
            err = FATFileSystem::format(&_sd);
            if (err) {
                log_nb("Format failed.\n");
                return false;
            }
            _fs.mount(&_sd);
        }

        // clear file, then open in append mode
        FILE *f = fopen(path, "w");
        fclose(f);
        f = fopen(path, "a");
    
        file = f;
        return true;
    }
// =================================================


int main() {
    ser.sigio(serial_isr); // enable serial interrupt

    FILE* sd = nullptr;
    Mutex sd_mutex;

    
    SensorEventQueue queue;   

    // ======== Load Cell Setup ========
        LoadCellSensor lc1("LC1", PB_10, PB_14, 1.892f, 4.55f, 4.55f, 588.399f);
        lc1.set_sd(&sd, &sd_mutex);
        queue.queue(callback(&lc1, &LoadCellSensor::sample_log), 100);
    // =================================

    // =========== RTD Setup ===========
        SPI spi(PB_2, PC_11, PC_10);
        spi.format(8, 1); 

        RTD rtd0("RTD0", &spi, PC_3);
        RTD rtd1("RTD1", &spi, PB_12);
        RTD rtd2("RTD2", &spi, PB_13);
        RTD rtd3("RTD3", &spi, PB_7);
        vector<RTD*> rtds = {&rtd0, &rtd1, &rtd2, &rtd3};

        for (RTD* rtd : rtds) {
            rtd->set_sd(&sd, &sd_mutex);
            queue.queue(callback(rtd, &RTD::sample_log), 90);
        }
    // =================================

    // =========== ADC Setup ===========
        ADCSensor adc0("ADC0", PA_0, 1.5f, 0, 5);
        ADCSensor adc1("ADC1", PA_1, 1.5f, 0, 5);
        ADCSensor adc2("ADC2", PA_4, 1.5f, 0, 5);
        ADCSensor adc3("ADC3", PC_4, 1.5f, 0, 5);
        ADCSensor adc4("ADC4", PC_5, 1.5f, 0, 5);
        ADCSensor adc5("ADC5", PB_0, 1.5f, 0, 5);
        ADCSensor adc6("ADC6", PB_1, 1.5f, 0, 5);
        ADCSensor adc7("ADC7", PC_0, 1.5f, 0, 5);
        ADCSensor adc8("ADC8", PC_1, 1.5f, 0, 5);
        ADCSensor adc9("ADC9", PC_2, 1.5f, 0, 5);
        vector<ADCSensor*> adcs = {&adc0, &adc1, &adc2, &adc3, &adc4, &adc5, &adc6, &adc7, &adc8, &adc9};

        for (ADCSensor* adc : adcs) {
            adc->set_sd(&sd, &sd_mutex);
            queue.queue(callback(adc, &ADCSensor::sample_log), 20);
        }
    // =================================
    
    // ======== Actuation Setup ========
        PinName actuation_pins[12] = {PB_15, PC_6, PC_7, PC_8, PC_9, PA_8, PA_9, PA_10, PA_11, PA_12, PA_15, PC_12};
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

        Thread* cmd_thread = new Thread;

        // keep alive timer
        Timer kl;
        int kl_count = 0;
        kl.start();

        while (true) {
            if (read_flag == true) {
                kl.reset();
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

                        else if (buf[0] == 'D') { // Disk Command
                            if (buf[1] == 'E') {
                                log_nb("Ejecting SD Card\n");
                                deinit_sd(sd, sd_mutex);
                                log_nb("Ejected\n");
                            }
                            else if (buf[1] == 'M') {
                                log_nb("Mounting SD Card\n");
                                log_nb("path: %s\n", &buf[2]);
                                deinit_sd(sd, sd_mutex);
                                if (!init_sd(sd, &buf[2])) {
                                    log_nb("Failed to Mount SD\n");
                                }
                                else {
                                    log_nb("Mounted Succesfully\n");
                                }
                            }
                            else {
                                log_nb("Disk Command Not Found\n");
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

            if (kl.read_ms() > 60000) {
                kl_count ++;
                log_nb("Keep Alive, %d minutes\n", kl_count);
                kl.reset();
            }
            ThisThread::yield();
        }
    // =================================
}
