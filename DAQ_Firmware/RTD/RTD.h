#include "mbed.h"
#include <cstdint>


#ifndef _RTD_H_
#define _RTD_H_

#define RTD_A 3.9083e-3
#define RTD_B -5.775e-7

class RTD {
    public:
        /* RTD | creates a new RTD object to be used with the MAX31865
         *     name (const char*) | user defined RTD name
         *     spi  (SPI*)        | SPI object
         *     cs   (PinName)     | Chip Select Pin for SPI object
         */
        RTD(const char* name, SPI* spi, PinName cs);

        // sample_log | samples data, and logs it to an SD Card (TODO)
        void sample_log();

        /* last_data | gets data from the last sample_log call
         *     value (float*)    | float to put computed RTD value into
         *     raw   (uint16_t*) | uint16_t to put raw adc reading into
         *     ms    (int*)      | int to put reading time in milliseconds into
         */
        void last_data(float* value, uint16_t* raw, int* ms);

        
        const char* name;
    private:
        DigitalOut _cs;
        SPI* spi;
        Timer t;

        int _last_time;
        float _last_value;
        float _last_raw;
        Mutex data_mutex;
        Mutex spi_mutex;

        float temperature(float RTDnominal, float refResistor, uint16_t rtdVal);
        void writeRegister8(uint8_t addr, uint8_t data);
        void readRegisterN(uint8_t address, uint8_t buffer[], uint8_t n);
        uint8_t readRegister8(uint8_t addr);

};

#endif //_RTD_H_