/* ADC Sensor
 * Ethan Armstrong
 * warmst@uw.edu
 *
 * A library to abstract the built in AnalogIn to be compatible
 * with a SensorEventQueue object
 */

#include "mbed.h"
#include "HX711.h"
#include <cstdint>

#ifndef _ADC_H_
#define _ADC_H_

class ADCSensor {
    public:
        /* ADCSensor | creates a built in ADC object, with functionality compatibly with SensorEventQueue
         *    name    (const char*) | user defined sensor name
         *    adc     (PinName)     | Pin Number for ADC
         *    gain    (float)       | gain to apply to raw adc value (pre offset)
         *    offset  (float)       | offset to apply to raw adc value (post-gain)
         *    samples (int)         | number of samples to take per measurement
         */
        ADCSensor(const char* name, PinName adc, float gain, float offset, int samples=1) :
            adc(adc) {
                this->name = name;
                this->gain = gain;
                this->offset = offset;
                this->samples = samples;
                t.start();
            }

        void set_sd(FILE** sd, Mutex* sd_mutex);

        // sample_log | takes an ADC sample, updates internal values, and logs to SD
        void sample_log();

        /* last_data | gets data from the last sample_log call
         *     value (float*) | float to put post gain and offset value into
         *     raw   (float*) | float to put raw adc reading into
         *     ms    (int*)   | int to put reading time in milliseconds into
         */
        void last_data(float* value, float* raw, int* ms);

        // set_gain | sets the ADC gain
        void set_gain(float gain);
        
        // set offset | sets the ADC offset
        void set_offset(float offset);

        void tare(float expected);

        const char* name;
    private:
        Timer t;
        AnalogIn adc;
        float gain;
        float offset;
        int samples;

        float raw;
        float value;
        int time;
        Mutex dataMutex;

        FILE** sd;
        Mutex* sd_mutex;

};

#endif //_ADC_H_