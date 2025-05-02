#include "MAX31865.h"

MAX31865::MAX31865(SPI &_spi, PinName _ncs)
    : spi(_spi), ncs(_ncs, 1), sensorPresent(false)
{
}

bool MAX31865::begin(max31865_numwires_t wires)
{
    setWires(wires);
    enableBias(true);
    autoConvert(true);
    clearFault();

    return true;
}

uint8_t MAX31865::readFault(void)
{
    return readRegister8(MAX31865_FAULTSTAT_REG);
}

void MAX31865::clearFault(void)
{
    volatile uint8_t t = readRegister8(MAX31865_CONFIG_REG);
    t &= ~0x2C;
    t |= MAX31865_CONFIG_FAULTSTAT;
    writeRegister8(MAX31865_CONFIG_REG, t);
}

void MAX31865::enableBias(bool b)
{
    volatile uint8_t t = readRegister8(MAX31865_CONFIG_REG);
    if (b)
    {
        t |= MAX31865_CONFIG_BIAS; // enable bias
    }
    else
    {
        t &= ~MAX31865_CONFIG_BIAS; // disable bias
    }
    writeRegister8(MAX31865_CONFIG_REG, t);
}

void MAX31865::autoConvert(bool b)
{
    uint8_t t = readRegister8(MAX31865_CONFIG_REG);
    if (b)
    {
        t |= MAX31865_CONFIG_MODEAUTO; // enable autoconvert
    }
    else
    {
        t &= ~MAX31865_CONFIG_MODEAUTO; // disable autoconvert
    }
    writeRegister8(MAX31865_CONFIG_REG, t);
}

void MAX31865::setWires(max31865_numwires_t wires)
{
    uint8_t t = readRegister8(MAX31865_CONFIG_REG);
    if (wires == MAX31865_3WIRE)
    {
        t |= MAX31865_CONFIG_3WIRE;
    }
    else
    {
        // 2 or 4 wire
        t &= ~MAX31865_CONFIG_3WIRE;
    }
    writeRegister8(MAX31865_CONFIG_REG, t);
}

float MAX31865::temperature(float RTDnominal, float refResistor, uint16_t rtdVal)
{
    // http://www.analog.com/media/en/technical-documentation/application-notes/AN709_0.pdf

    float Z1, Z2, Z3, Z4, Rt, temp;

    if (rtdVal == 0)
    {
        Rt = readRTD();

        if (!sensorPresent)
        {
            return 0.0f;
        }
    }
    else
    {
        Rt = rtdVal;
    }
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

uint16_t MAX31865::readRTD(void)
{
    uint16_t rtd = readRegister16(MAX31865_RTDMSB_REG);
    sensorPresent = readFault() == 0;
    clearFault();

    rtd >>= 1;

    return rtd;
}

/**********************************************/

uint8_t MAX31865::readRegister8(uint8_t addr)
{
    uint8_t ret[] = {0};
    readRegisterN(addr, ret, 1);
    return ret[0];
}

uint16_t MAX31865::readRegister16(uint8_t addr)
{
    uint8_t buffer[2] = {0, 0};
    readRegisterN(addr, buffer, 2);

    uint16_t ret = buffer[0];
    ret <<= 8;
    ret |= buffer[1];

    return ret;
}

void MAX31865::writeRegister8(uint8_t addr, uint8_t data)
{
    ncs = 0;                //select chip
    spi.write(addr | 0x80); // make sure top bit is set
    spi.write(data);
    ncs = 1;
}

void MAX31865::readRegisterN(uint8_t address, uint8_t buffer[], uint8_t n)
{
    address &= 0x7F; // make sure top bit is not set

    ncs = 0;

    spi.write(address);

    for (uint8_t i = 0; i < n; i++)
    {
        buffer[i] = spi.write(0x00);
    }

    ncs = 1;
}

