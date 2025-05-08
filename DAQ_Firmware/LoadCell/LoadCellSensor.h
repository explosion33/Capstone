#include "mbed.h"
#include "HX711.h"
#include <cstdint>

#ifndef _LOAD_CELL_SENSOR_H_
#define _LOAD_CELL_SENSOR_H_

class LoadCellSensor {
    public:
        /* ADCSensor | creates a built in ADC object, with functionality compatibly with SensorEventQueue
         *    name    (const char*) | user defined sensor name
         *    adc     (PinName)     | Pin Number for ADC
         *    gain    (float)       | gain to apply to raw adc value (pre offset)
         *    offset  (float)       | offset to apply to raw adc value (post-gain)
         *    samples (int)         | number of samples to take per measurement
         */
        LoadCellSensor(const char* name, PinName clk, PinName dout, float MVV, float excitation, float vref, float gain=1, float offset=0) :
            hx711(vref, clk, dout, 64) {
                this->name = name;
                this->MVV = MVV;
                this->excitation = excitation;
                this->gain = gain;
                this->offset = offset;

                t.start();
            }

        // sample_log | takes an ADC sample, updates internal values, and logs to SD (TODO)
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
        HX711 hx711;
        Timer t;

        float MVV;
        float excitation;
        float gain;
        float offset;

        float value;
        float raw;
        int time;
        Mutex dataMutex;

};


#endif // _LOAD_CELL_SENSOR_H_