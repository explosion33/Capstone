#include "RTD.h"


RTD::RTD(const char* name, SPI* spi, PinName cs) : _cs(cs) {
    this->spi = spi;
    this->name = name;
    
    writeRegister8(0x00, 0xD0);

    t.start();
}

void RTD::sample_log() {
    uint8_t msb = readRegister8(0x1);
    uint8_t lsb = readRegister8(0x2);
    uint8_t fault = readRegister8(0x7);
    int ms = t.read_ms();

    uint16_t raw = (((uint16_t) msb << 8) | lsb) >> 1;
    float temp = temperature(100, 430, raw) + 273.15;

    data_mutex.lock();
    this->_last_time = ms;
    this->_last_value = temp;
    this->_last_raw = raw;
    data_mutex.unlock();

    if (this->sd_mutex == nullptr)
        return;

    this->sd_mutex->lock();
    if (this->sd == nullptr || *this->sd == nullptr) {
        this->sd_mutex->unlock();
        return;
    }

    fprintf(*this->sd, "\"%s\", %f, %d, %d\n", this->name, temp, raw, ms);
    fflush(*this->sd);
    this->sd_mutex->unlock();
}


float RTD::temperature(float RTDnominal, float refResistor, uint16_t rtdVal) {
    // http://www.analog.com/media/en/technical-documentation/application-notes/AN709_0.pdf

    float Z1, Z2, Z3, Z4, Rt, temp;
    Rt = rtdVal;
    Rt /= 32768;
    Rt *= refResistor;

    Z1 = -RTD_A;
    Z2 = RTD_A * RTD_A - (4 * RTD_B);
    Z3 = (4 * RTD_B) / RTDnominal;
    Z4 = 2 * RTD_B;

    temp = Z2 + (Z3 * Rt);
    temp = (sqrt(temp) + Z1) / Z4;

    if (temp >= 0)
        return temp;

    // ugh.
    float rpoly = Rt;

    temp = -242.02f;
    temp += 2.2228f * rpoly;
    rpoly *= Rt; // square
    temp += (float)2.5859e-3 * rpoly;
    rpoly *= Rt; // ^3
    temp -= (float)4.8260e-6 * rpoly;
    rpoly *= Rt; // ^4
    temp -= (float)2.8183e-8 * rpoly;
    rpoly *= Rt; // ^5
    temp += (float)1.5243e-10 * rpoly;

    return temp;
}

void RTD::writeRegister8(uint8_t addr, uint8_t data) {
    this->spi_mutex.lock();
    _cs = 0;                //select chip
    spi->write(addr | 0x80); // make sure top bit is set
    spi->write(data);
    _cs = 1;
    this->spi_mutex.unlock();
}

void RTD::readRegisterN(uint8_t address, uint8_t buffer[], uint8_t n) {
    this->spi_mutex.lock();
    
    address &= 0x7F; // make sure top bit is not set

    _cs = 0;

    spi->write(address);

    for (uint8_t i = 0; i < n; i++)
    {
        buffer[i] = spi->write(0x00);
    }

    _cs = 1;

    this->spi_mutex.unlock();
}
uint8_t RTD::readRegister8(uint8_t addr) {
    uint8_t ret[] = {0};
    readRegisterN(addr, ret, 1);
    return ret[0];
}

void RTD::last_data(float* value, uint16_t* raw, int* ms) {
    this->data_mutex.lock();

    *value = this->_last_value;
    *raw   = this->_last_raw;
    *ms    = this->_last_time;

    this->data_mutex.unlock();
}

void RTD::set_sd(FILE** sd, Mutex* sd_mutex) {
    this->sd = sd;
    this->sd_mutex = sd_mutex;
}