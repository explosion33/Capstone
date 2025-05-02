#include "mbed.h"
#include "MAX31865.h"
#include "HX711.h"
#include <cstdint>
#include <cstdio>
#include "MutexData.h"
#include "EventQueue.h"

#define LOAD_MV_V 1.892f
#define LOAD_V_E  4.55f
#define LOAD_V_REF 4.55f
#define LOAD_CAP 588.399 //60kg (Newtons)

#define UART_TX PA_2
#define UART_RX PA_3

BufferedSerial ser(PA_2, PA_3, 115200);
SPI spi(PA_7, PA_6, PA_5);
DigitalOut cs(PB_6, 1);

Timer t;

void printf_nb(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char buf[64] = {0};
    size_t n = vsnprintf (buf, 64, format, args);

    va_end(args);

    ser.write(buf, n);
}

void writeRegister8(uint8_t addr, uint8_t data) {
    cs = 0;                //select chip
    spi.write(addr | 0x80); // make sure top bit is set
    spi.write(data);
    cs = 1;
}

void readRegisterN(uint8_t address, uint8_t buffer[], uint8_t n) {
    address &= 0x7F; // make sure top bit is not set

    cs = 0;

    spi.write(address);

    for (uint8_t i = 0; i < n; i++)
    {
        buffer[i] = spi.write(0x00);
    }

    cs = 1;
}

uint8_t readRegister8(uint8_t addr) {
    uint8_t ret[] = {0};
    readRegisterN(addr, ret, 1);
    return ret[0];
}

float temperature(float RTDnominal, float refResistor, uint16_t rtdVal) {
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



// main() runs in its own thread in the OS

int rtd() {

    //SPI spi(PA_7, PA_6, PA_5);
    spi.format(8, 1);
    MAX31865 rtd(spi, PB_6);

    //rtd.begin(MAX31865_3WIRE);

    rtd.writeRegister8(0x00, 0xD0);

    while (true) {
        //uint8_t pres = rtd.isSensorPresent();
        //uint16_t raw = rtd.readRTD();
        //uint8_t fault = rtd.readFault();

        uint8_t reg = rtd.readRegister8(0x00);

        uint8_t msb = rtd.readRegister8(0x1);
        uint8_t lsb = rtd.readRegister8(0x2);
        uint16_t raw = (((uint16_t) msb << 8) | lsb) >> 1;
        uint8_t fault = rtd.readRegister8(0x7);
        float temp = rtd.temperature(100, 430, raw);
        float temp_f = (temp * 9.0 / 5.0) + 32.0;

        printf_nb("config: 0x%02X\n", reg);
        printf_nb("msb   : %d\n", msb);
        printf_nb("lsb   : %d\n", lsb);
        printf_nb("raw   : %d\n", raw);
        printf_nb("fault : 0x%02X\n", fault);
        printf_nb("temp  : %f °C | %f °F\n", temp, temp_f);

        //printf_nb("pres: %d | fault: %d | raw: %d\n", pres, fault, raw);
        ThisThread::sleep_for(500ms);
    }
}


void sample_rtd() {
    uint8_t reg = readRegister8(0x00);

    uint8_t msb = readRegister8(0x1);
    uint8_t lsb = readRegister8(0x2);
    uint8_t fault = readRegister8(0x7);
    int ms = t.read_ms();

    uint16_t raw = (((uint16_t) msb << 8) | lsb) >> 1;
    float temp = temperature(100, 430, raw) + 273.15;
    //float temp_f = (temp * 9.0 / 5.0) + 32.0;

    if (fault != 0) {
        printf_nb("rtd fault : 0x%02X\n", fault);
    }
    printf_nb("(%d ms) temp = %f °K (%d)\n", ms, temp, raw);
    

    //printf_nb("config: 0x%02X\n", reg);
    //printf_nb("msb   : %d\n", msb);
    //printf_nb("lsb   : %d\n", lsb);
    //printf_nb("raw   : %d\n", raw);
    //printf_nb("fault : 0x%02X\n", fault);
    //printf_nb("temp  : %f °C | %f °F\n", temp, temp_f);
}


HX711 hx711(LOAD_V_REF, PA_0, PA_1, 64);
volatile float tare;
volatile float gain;

void tare_cell() {
    int i = 0;
    ThisThread::sleep_for(1s);
    while (true) {
        if (hx711.isReady()) {
            tare += hx711.read();
            i += 1;
            ThisThread::sleep_for(25ms);
        }

        if (i == 40) {
            tare /= 40.0;
            break;
        }
    }
}

void sample_load_cell() {
    if (!hx711.isReady()) {
        return;
    }

    float raw = hx711.read();
    float mV = (raw - tare) * gain;    // read voltage in mV

    float force = (mV) / (LOAD_MV_V * LOAD_V_E) * LOAD_CAP;
    
    int ms = t.read_ms();

    printf_nb("(%d ms) force = %f N (%f, %f)\n", ms, force, raw, tare);
}


int main2() {
    Ticker load_ticker;

    tare = 0.0f;
    gain = 7.33f;

    printf_nb("program start\n");

    int i = 0;
    ThisThread::sleep_for(1s);
    while (true) {
        if (hx711.isReady()) {
            tare += hx711.read();
            i += 1;
            ThisThread::sleep_for(25ms);
        }

        if (i == 40) {
            tare /= 40.0;
            break;
        }
    }

    t.start();
    load_ticker.attach(sample_load_cell, 13ms);

    while (true) {
        ThisThread::sleep_for(5ms);
    }
}



MyEventQueue queue;

volatile bool queue_load = false;

void spawn() {
    while (true) {
        if (queue_load == false) {
            queue_load = false;
            bool post = queue.post(sample_load_cell, 5);
        }
        ThisThread::sleep_for(13ms);

    }
}

int main() {
    //rtd();
    // RTD Setup
    spi.format(8, 1);
    writeRegister8(0x00, 0xD0);

    // Load Cell Setup
    tare = 0.0f;
    gain = 7.33f;

    tare_cell();

    t.start();

    Thread th;
    //th.start(spawn);

    Ticker load_ticker;
    Ticker rtd_ticker;
    load_ticker.attach(callback([&] { queue.post(sample_load_cell, 5); }), 14ms);
    rtd_ticker.attach(callback([&] { queue.post(sample_rtd, 5); }), 90ms);

    queue.run();
}

// sample function -> samples load cell stores variable in mutex and updates bool in mutex to indicate new_data

// main function
// ticker adds sigio to event to eventqueue
// ticker runs every 13ms, ticker adds sample function to event queue, then adds log function to event queue
// 
