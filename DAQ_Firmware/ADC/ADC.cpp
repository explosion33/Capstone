#include "ADC.h"

void ADCSensor::sample_log() {
    float raw = 0;
    for (int i = 0; i<this->samples; i++) {
        raw += this->adc.read();
    }
    raw /= (float) this->samples;
    int ms = this->t.read_ms();

    float val = raw * 3.3f;     // TODO compare agains internal stm32 reference voltage to determine V_REF;
    val *= this->gain;
    val += this->offset;

    this->dataMutex.lock();

    this->raw = raw;
    this->value = val;
    this->time = ms;

    this->dataMutex.unlock();

    // TODO write to SD card
}

void ADCSensor::last_data(float* value, float* raw, int* ms) {
    this->dataMutex.lock();

    *value = this->value;
    *raw = this->raw;
    *ms = this->time;
    
    this->dataMutex.unlock();
}

void ADCSensor::set_gain(float gain) {
    this->dataMutex.lock();

    this->gain = gain;
    
    this->dataMutex.unlock();
}

void ADCSensor::set_offset(float offset) {
    this->dataMutex.lock();

    this->offset = offset;
    
    this->dataMutex.unlock();
}