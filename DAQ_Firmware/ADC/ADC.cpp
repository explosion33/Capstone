#include "ADC.h"

void ADCSensor::sample_log() {
    this->dataMutex.lock();

    float raw = 0;
    for (int i = 0; i<this->samples; i++) {
        raw += this->adc.read();
    }
    raw /= (float) this->samples;
    int ms = this->t.read_ms();

    float val = raw * 3.3f;     // TODO compare agains internal stm32 reference voltage to determine V_REF;
    val *= this->gain;
    val += this->offset;


    this->raw = raw;
    this->value = val;
    this->time = ms;

    this->dataMutex.unlock();

    if (this->sd_mutex == nullptr)
        return;
        
    this->sd_mutex->lock();
    if (this->sd == nullptr || *this->sd == nullptr) {
        this->sd_mutex->unlock();
        return;
    }

    fprintf(*this->sd, "\"%s\", %f, %f, %d\n", this->name, val, raw, ms);
    fflush(*this->sd);
    this->sd_mutex->unlock();
}

void ADCSensor::set_sd(FILE** sd, Mutex* sd_mutex) {
    this->sd = sd;
    this->sd_mutex = sd_mutex;
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

void ADCSensor::tare(float expected) {
    this->dataMutex.lock();

    float raw = 0;
    for (int i = 0; i<100; i++) {
        raw += this->adc.read();
        ThisThread::sleep_for(10ms);
    }

    raw /= 100.0f;

    float val = raw * 3.3f;
    val *= this->gain;

    this->offset = expected - val;

    this->dataMutex.unlock();
}