#include "FlowMeter.h"
#include <cstdint>


void FlowMeterSensor::sample_log() {
    this->dataMutex.lock();

    this->raw = pulse_count.exchange(0, std::memory_order_relaxed);
    int ms = this->t.read_ms();

    this->value = ((float) this->raw / (((float) (ms - last_time)) / 1000.0)) * this->gain;

    this->last_time = ms;
    this->time = ms;

    this->dataMutex.unlock();

    if (this->sd_mutex == nullptr)
        return;

    this->sd_mutex->lock();
    if (this->sd == nullptr || *this->sd == nullptr) {
        this->sd_mutex->unlock();
        return;
    }

    fprintf(*this->sd, "\"%s\", %f, %d, %d\n", this->name, this->value, this->raw, ms);
    fflush(*this->sd);
    this->sd_mutex->unlock();
}


/*
void FlowMeterSensor::sample_log() {
    this->dataMutex.lock();

    this->raw = pulse_count.load(std::memory_order_relaxed);
    int ms = this->t.read_ms();

    this->value = (float) raw;

    this->last_time = ms;
    this->time = ms;

    this->dataMutex.unlock();
}
*/

void FlowMeterSensor::set_sd(FILE** sd, Mutex* sd_mutex) {
    this->sd = sd;
    this->sd_mutex = sd_mutex;
}

void FlowMeterSensor::last_data(float* value, uint32_t* raw, int* ms) {
    this->dataMutex.lock();

    *value = this->value;
    *raw = this->raw;
    *ms = this->time;
    
    this->dataMutex.unlock();
}


void FlowMeterSensor::set_gain(float gain) {
    this->dataMutex.lock();

    this->gain = gain;
    
    this->dataMutex.unlock();
}

void FlowMeterSensor::tick() {
    pulse_count.fetch_add(1, std::memory_order_relaxed);
}