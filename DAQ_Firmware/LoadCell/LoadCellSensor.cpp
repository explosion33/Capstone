#include "LoadCellSensor.h"

// sample_log | takes an ADC sample, updates internal values, and logs to SD (TODO)
void LoadCellSensor::sample_log() {
    if (!hx711.isReady()) {
        return;
    }

    float _raw = hx711.read();
    float mV = (raw - this->offset);    // read voltage in mV

    float _value = (mV) / (this->MVV * this->excitation) * this->gain;
    
    int ms = t.read_ms();

    this->dataMutex.lock();

    this->raw = _raw;
    this->time = ms;
    this->value = _value;

    this->dataMutex.unlock();

    // TODO: Write to SD Card
}

/* last_data | gets data from the last sample_log call
    *     value (float*) | float to put post gain and offset value into
    *     raw   (float*) | float to put raw adc reading into
    *     ms    (int*)   | int to put reading time in milliseconds into
    */
void LoadCellSensor::last_data(float* value, float* raw, int* ms) {
    this->dataMutex.lock();

    *value = this->value;
    *raw = this->raw;
    *ms = this->time;

    this->dataMutex.unlock();
}

// set_gain | sets the ADC gain
void LoadCellSensor::set_gain(float gain) {
    this->dataMutex.lock();

    this->gain = gain;

    this->dataMutex.unlock();
}

// set offset | sets the ADC offset
void LoadCellSensor::set_offset(float offset) {
    this->dataMutex.lock();

    this->offset = offset;

    this->dataMutex.unlock();
}