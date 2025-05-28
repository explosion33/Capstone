/* DAQ Firmware
 * Ethan Armstrong
 * warmst@uw.edu
 *
 * Reads data from RTDs PTs TTs and a load cell.
 * Controls 8 actuator channels.
 * Logs all data over serial terminal
 * while accepting serial commands for actuation,
 * tareing, and command sequences
 */
#include "mbed.h"
#include <cstdint>
#include <cstdio>
#include <vector>

#include "SDBlockDevice.h"
#include "FATFileSystem.h"

#include "RTD.h"
#include "ADC.h"
#include "LoadCellSensor.h"
#include "FlowMeter.h"
#include "SensorEventQueue.h"

#define UART_TX PA_2
#define UART_RX PA_3

#define SD_MOSI PA_7
#define SD_MISO PA_6
#define SD_SCLK PA_5
#define SD_CS   PB_6

#define FMV_CHANNEL 0
#define OMV_CHANNEL 1
#define IGN_CHANNEL 2
#define FVV_CHANNEL 3
#define HBV_CHANNEL 4
#define OPV_CHANNEL 5
#define OBV_CHANNEL 6
#define OVV_CHANNEL 7

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
    void cmd_fire(uint32_t fire_time_ms = 15000, uint32_t valve_delay_ms = 480) {
        const uint32_t igniter_time = 2800;

        Timer cmd_timer;
        cmd_timer.start();
        log_nb("(%d ms) Firing\n", cmd_timer.read_ms());
        
        if (fire_time_ms < igniter_time + 200) {
            log_nb("(%d ms) Error Firing, fire_time is less than igniter time\n", cmd_timer.read_ms());
            return;
        }
        if (valve_delay_ms > igniter_time) {
            log_nb("(%d ms) Error Firing, valve delay is less than igniter time\n", cmd_timer.read_ms());
            return;
        }

        Timer i_timer;
        Timer fire_timer;
        
        // open FMV
        solenoids[FMV_CHANNEL].write(1);
        log_nb("(%d ms) FMV open\n", cmd_timer.read_ms());
        
        ThisThread::sleep_for(valve_delay_ms);

        // fire igniter
        i_timer.start();
        solenoids[IGN_CHANNEL].write(1);
        log_nb("(%d ms) igniter on\n", cmd_timer.read_ms());

        // open OMV
        solenoids[OMV_CHANNEL].write(1);
        log_nb("(%d ms) OMV open\n", cmd_timer.read_ms());

        fire_timer.start();

        while (true) {
            // igniter off after 2s
            if (i_timer.read_ms() >= igniter_time) {
                i_timer.stop();
                i_timer.reset();

                solenoids[IGN_CHANNEL].write(0);
                log_nb("(%d ms) igniter off\n", cmd_timer.read_ms());
            }

            // open OPV 15s after OMV opens
            if (fire_timer.read_ms() >= fire_time_ms) {
                break;
            }
            ThisThread::yield();
        }

        // close OBV 15s after OMV opens
        solenoids[OBV_CHANNEL].write(0);
        log_nb("(%d ms) OBV closed\n", cmd_timer.read_ms());

        // open OPV 15s after OMV opens
        solenoids[OPV_CHANNEL].write(1);
        log_nb("(%d ms) OPV open\n", cmd_timer.read_ms());

        ThisThread::sleep_for(30s);

        // close  HBV
        solenoids[HBV_CHANNEL].write(0);
        log_nb("(%d ms) OPV and HBV closed\n", cmd_timer.read_ms());

        ThisThread::sleep_for(30s);

        // close OPV, OMV and FMV
        solenoids[OPV_CHANNEL].write(0);
        solenoids[OMV_CHANNEL].write(0);
        solenoids[FMV_CHANNEL].write(0);
        log_nb("(%d ms) OPV, OMV and FMV closed\n", cmd_timer.read_ms());

        log_nb("(%d ms) Done Firing\n", cmd_timer.read_ms());
    }

    void cmd_abort() {
        Timer cmd_timer;
        cmd_timer.start();

        log_nb("(%d ms) Aborting\n", cmd_timer.read_ms());

        solenoids[OMV_CHANNEL].write(0);
        solenoids[FMV_CHANNEL].write(0);
        solenoids[HBV_CHANNEL].write(0);
        solenoids[OBV_CHANNEL].write(0);
        solenoids[IGN_CHANNEL].write(0);

        log_nb("(%d ms) OMV, FMV, HBV, OBV and IGN closed\n", cmd_timer.read_ms());
        log_nb("(%d ms) Done Aborting\n", cmd_timer.read_ms());
    }

    void cmd_pulse_fuel(int pulse_ms) {
        Timer cmd_timer;
        cmd_timer.start();

        log_nb("(%d ms) Pulsing Fuel\n", cmd_timer.read_ms());
        
        solenoids[FMV_CHANNEL].write(1);
        log_nb("(%d ms) FMV Open\n", cmd_timer.read_ms());

        ThisThread::sleep_for(pulse_ms);

        solenoids[FMV_CHANNEL].write(0);
        log_nb("(%d ms) FMV Closed\n", cmd_timer.read_ms());

        log_nb("(%d ms) Done Pulsing Fuel\n", cmd_timer.read_ms());
    }

    void cmd_pulse_helium(int pulse_ms) {
        Timer cmd_timer;
        cmd_timer.start();

        log_nb("(%d ms) Pulsing Helium\n", cmd_timer.read_ms());
        
        solenoids[HBV_CHANNEL].write(1);
        log_nb("(%d ms) HBV Open\n", cmd_timer.read_ms());

        ThisThread::sleep_for(pulse_ms);
        
        solenoids[HBV_CHANNEL].write(0);
        log_nb("(%d ms) HBV Closed\n", cmd_timer.read_ms());

        log_nb("(%d ms) Done Pulsing Helium\n", cmd_timer.read_ms());
    }

    void cmd_pulse_ox(int pulse_ms) {
        Timer cmd_timer;
        cmd_timer.start();

        log_nb("(%d ms) Pulsing Oxygen\n", cmd_timer.read_ms());
        
        solenoids[OMV_CHANNEL].write(1);
        log_nb("(%d ms) OMV Open\n", cmd_timer.read_ms());

        ThisThread::sleep_for(pulse_ms);
        
        solenoids[OMV_CHANNEL].write(0);
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

#define OX_VOLUME 0.04413f
#define HE_VOLUME 0.04413f 
#define OX_R      297.0f
#define HE_R      2077.0f
#define HE_PT     4
#define HE_TT     0
#define OX_PT     2
#define OX_TT     3

float mass_flow(float dpres, float dtemp, float volume, float R, float dt) {
    return (dpres * volume) / (R * dtemp * dt);
}

int main() {
    ser.sigio(serial_isr); // enable serial interrupt

    FILE* sd = nullptr;
    Mutex sd_mutex;

    
    SensorEventQueue queue;   
    // ======== Flowmeter Setup ========
        FlowMeterSensor fm1("FM1", PC_13, 0.324f);
        fm1.set_sd(&sd, &sd_mutex);
        queue.queue(callback(&fm1, &FlowMeterSensor::sample_log), 100);
    //  =================================

    // ======== Load Cell Setup ========
        LoadCellSensor lc1("LC1", PC_9, PB_8, 1.892f, 4.55f, 4.55f, 588.399f);
        lc1.set_sd(&sd, &sd_mutex);
        queue.queue(callback(&lc1, &LoadCellSensor::sample_log), 100);
    //  =================================

    // =========== RTD Setup ===========
        //vector<RTD*> rtds;
        SPI spi(PB_2, PC_11, PC_10);
        spi.format(8, 1); 

        RTD rtd0("RTD0", &spi, PC_5);
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
        ADCSensor adc0("HBTT" , PC_1, 1.5f, 0, 5);
        ADCSensor adc1("FTPT" , PC_2, (1.5f / 4.0f) * 500, -63.5, 5);
        ADCSensor adc2("OBPT" , PC_3, (1.5f / 4.0f) * 5000, -625, 20);
        ADCSensor adc3("OBTT" , PC_5, (1.5f / 4.0f) * 160, -63, 5);
        ADCSensor adc4("HBPT" , PC_0, (1.5f / 4.0f) * 5000, -625, 20);
        ADCSensor adc5("OVPT" , PB_0, (1.5f / 4.0f) * 500, -62.5, 5);
        ADCSensor adc6("OMPT" , PA_0, (1.5f / 4.0f) * 500, -62.5, 5);
        ADCSensor adc7("PCPT" , PA_1, (1.5f / 4.0f) * 500, -62.5, 5);
        ADCSensor adc8("FRMPT", PC_4, (1.5f / 4.0f) * 500, -62.5, 5);

        vector<ADCSensor*> adcs = {&adc0, &adc1, &adc2, &adc3, &adc4, &adc5, &adc6, &adc7, &adc8};

        for (ADCSensor* adc : adcs) {
            adc->set_sd(&sd, &sd_mutex);
            queue.queue(callback(adc, &ADCSensor::sample_log), 20);
        }
    // =================================
    
    // ======== Actuation Setup ========
        PinName actuation_pins[8] = {PB_1, PB_15, PB_14, PA_8, PB_10, PB_4, PB_5, PB_3};
        // declared in global
        // vector<DigitalOut> solenoids;
        for (PinName pin: actuation_pins) {
            DigitalOut p(pin);
            p.write(0);
            solenoids.push_back(p);
        }
    // =================================


    Thread t;
    //t.set_priority(osPriorityBelowNormal);
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
            // why tf does the if statement fix everything
            if (read_flag == false && ser.readable()) {
                log_nb("UH OH\n");
            }

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

                            solenoids[0].write(buf[0+1] == '1');
                            solenoids[1].write(buf[1+1] == '1');
                            solenoids[2].write(buf[2+1] == '1');
                            solenoids[3].write(buf[3+1] == '1');
                            solenoids[4].write(buf[4+1] == '1');
                            solenoids[5].write(buf[5+1] == '1');
                            solenoids[6].write(buf[6+1] == '1');
                            solenoids[7].write(buf[7+1] == '1');
                        }

                        else if (buf[0] == 'C') { // Command Sequence
                            if (buf[1] == 'F' && buf[2] == 'I') {
                                cmd_thread->terminate();
                                delete cmd_thread;
                                cmd_thread = new Thread;
                                uint32_t fire_time = 15000;
                                uint32_t valve_delay = 480;

                                uint32_t ftemp;
                                uint32_t vtemp;
                                int res = sscanf(&buf[3], "%d,%d", &ftemp, &vtemp);
                                if (res >= 1)
                                    fire_time = ftemp;
                                if (res >= 2)
                                    valve_delay = vtemp;

                                cmd_thread->start([fire_time, valve_delay]() {cmd_fire(fire_time, valve_delay);});
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
                            printf_nb("{");
                            for (RTD* rtd : rtds) {
                                int time;
                                float value;
                                uint16_t raw;
                                rtd->last_data(&value, &raw, &time);
                                printf_nb("\"%s\" : [%d, %f, %d], ", rtd->name, time, value, raw);                     
                            }
                            for (ADCSensor* adc : adcs) {
                                int time;
                                float value;
                                float raw;
                                adc->last_data(&value, &raw, &time);
                                printf_nb("\"%s\" : [%d, %f, %f], ", adc->name, time, value, raw);                     
                            }

                            float he_dpres;
                            float he_temp;
                            int he_dt;

                            float ox_dpres;
                            float ox_temp;
                            int ox_dt;

                            int he_time;
                            int ox_time;

                            adcs[HE_TT]->last_data(&he_temp, &he_dpres, &he_time);
                            adcs[OX_TT]->last_data(&ox_temp, &he_dpres, &ox_time);

                            adcs[HE_PT]->deltas(&he_dpres, &he_dt);
                            //adcs[HE_TT]->deltas(&he_dtemp, &he_dt);
                            
                            adcs[OX_PT]->deltas(&ox_dpres, &ox_dt);
                            //adcs[OX_TT]->deltas(&ox_dtemp, &ox_dt);

                            he_dpres *= 6895.0f; // PSI to Pa
                            ox_dpres *= 6895.0f; // PSI to Pa

                            he_temp += 273.15; // C to K
                            ox_temp += 273.15; // C to K

                            float he_dt_s = he_dt / 1000.0f; // ms to seconds
                            float ox_dt_s = he_dt / 1000.0f; // ms to seconds

                            float he_mfr = mass_flow(he_dpres, he_temp, HE_VOLUME, HE_R, he_dt);
                            float ox_mfr = mass_flow(ox_dpres, ox_temp, OX_VOLUME, OX_R, ox_dt);
                            //printf_nb("\"HE MFR\" : [%d, %f], ", he_time, he_mfr);
                            printf_nb("\"OX MFR\" : [%d, %f], ", ox_time, ox_mfr);

                            float fm_value;
                            uint32_t fm_raw;
                            int fm_ms;
                            fm1.last_data(&fm_value, &fm_raw, &fm_ms);
                            printf_nb("\"F MFR\" : [%d, %f], ", fm_ms, fm_value);

                            int time;
                            float value;
                            float raw;
                            lc1.last_data(&value, &raw, &time);

                            printf_nb("\"%s\" : [%d, %f, %f], ", lc1.name, time, value, raw);
                            printf_nb("\"actuators\" : [%d, %d, %d, %d, %d, %d, %d, %d]",
                                solenoids[0].read() == 1, solenoids[1].read() == 1, solenoids[2].read() == 1,
                                solenoids[3].read() == 1, solenoids[4].read() == 1, solenoids[5].read() == 1,
                                solenoids[6].read() == 1, solenoids[7].read() == 1
                            );

                            printf_nb("}\n");

                            //log_nb("mfr calc: %f, %f, %f\n", ox_dpres, ox_temp, ox_dt_s);
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

            if (kl.read_ms() > 10000) {
                kl_count ++;
                log_nb("Keep Alive, %d minutes\n", kl_count);
                kl.reset();
            }
            ThisThread::yield();
        }
    // =================================
}
