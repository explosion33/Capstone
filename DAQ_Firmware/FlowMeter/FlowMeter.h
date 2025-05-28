/* Flow Meter Sensor
 * Ethan Armstrong
 * warmst@uw.edu
 *
 * A library to read and measure flow rate using a frequency modulated
 * flow rate sensor, compatible with a SensorEventQueue object
 */

#include "mbed.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

#ifndef _FLOW_METER_H_
#define _FLOW_METER_H_

/* FlowMeterSensor | creates a PWM flow meter object, with functionality compatibly with SensorEventQueue
 *    name    (const char*) | user defined sensor name
 *    adc     (PinName)     | Pin Number for ADC
 *    gain    (float)       | gain to apply to raw adc value
 */
class FlowMeterSensor {
    public:
        FlowMeterSensor(const char* name, PinName in, float gain = 1) :
            in(in) {
                this->name = name;
                this->gain = gain;
                t.start();
                this->in.rise(callback(this, &FlowMeterSensor::tick));
            }

        void set_sd(FILE** sd, Mutex* sd_mutex);

        // sample_log | takes an ADC sample, updates internal values, and logs to SD
        void sample_log();

        /* last_data | gets data from the last sample_log call
         *     value (float*) | float to put post gain and offset value into
         *     raw   (float*) | float to put raw adc reading into
         *     ms    (int*)   | int to put reading time in milliseconds into
         */
        void last_data(float* value, uint32_t* raw, int* ms);
        
        // set_gain | sets the ADC gain
        void set_gain(float gain);

        const char* name;
    private:
        Timer t;
        InterruptIn in;
        float gain;

        uint32_t raw;
        float value;
        int time;
        int last_time;
        Mutex dataMutex;

        FILE** sd;
        Mutex* sd_mutex;

        std::atomic<uint32_t> pulse_count{0};

        // interrupt callbck for ticking counter
        void tick();

};

#endif //_ADC_H_