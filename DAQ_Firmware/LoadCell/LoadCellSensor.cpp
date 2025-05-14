#include "LoadCellSensor.h"

void LoadCellSensor::sample_log() {
    this->dataMutex.lock();
    int ms = t.read_ms();

    float _raw = -1.0f;
    float _value = -1.0f;

    if (hx711.isReady()) {
        _raw = hx711.read();

        _value = (_raw) / (this->MVV * this->excitation) * this->gain;
        _value += this->offset;
    }

    this->raw = _raw;
    this->time = ms;
    this->value = _value;

    this->dataMutex.unlock();

    if (this->sd_mutex == nullptr)
        return;

    this->sd_mutex->lock();
    if (this->sd == nullptr || *this->sd == nullptr) {
        this->sd_mutex->unlock();
        return;
    }

    fprintf(*this->sd, "\"%s\", %f, %f, %d\n", this->name, _value, _raw, ms);
    fflush(*this->sd);
    this->sd_mutex->unlock();
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

void LoadCellSensor::tare(float expected) {
    this->dataMutex.lock();

    float _raw = 0;

    for (int i = 0; i<100; i++) {
        if (!hx711.isReady()) {
            continue;
        }

        _raw += hx711.read();
    }

    _raw /= 100.0f;

    float _value = (_raw) / (this->MVV * this->excitation) * this->gain;

    this->offset = expected - _value;

    this->dataMutex.unlock();
}

void LoadCellSensor::set_sd(FILE** sd, Mutex* sd_mutex) {
    this->sd = sd;
    this->sd_mutex = sd_mutex;
}