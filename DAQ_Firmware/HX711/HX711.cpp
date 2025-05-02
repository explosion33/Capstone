/*
 * Copyright (c) 2020 Zoltan Hudak <hudakz@outlook.com>
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mbed.h"
#include "HX711.h"

/**
 * Class for communication with the HX711 24-Bit Analog-to-Digital converter (ADC)
 * (Developed for Weigh Scales by AVIA Semiconductor but can be used generally.)
 * This library is based on the library at https://github.com/bogde/HX711
 */

/**
 * @brief   Sets gain
 * @note
 * @param   gain Selected gain (128, 64 or 32)
 * @retval
 */
void HX711::setGain(uint8_t gain)
{
    _gain = gain;

    switch (gain) {
        case 128:   // channel A, gain factor 128
            _mode = 1;
            break;

        case 64:    // channel A, gain factor 64
            _mode = 3;
            break;

        case 32:    // channel B, gain factor 32
            _mode = 2;
            break;
    }

    _sck.write(0);
}

/**
 * @brief   Obtains digital value of measured analog input voltage.
 * @note    It's a 24-bit two's complement integer.
 * @retval  Digital value of measured analog input voltage.
 */
uint32_t HX711::readInt()
{
    uint32_t    value = 0;
    for (uint8_t i = 0; i < 24; ++i) {
        _sck.write(1);
        value = value << 1;
        value |= _dout.read();
        _sck.write(0);
    }

    // set the channel and the gain factor for the next reading using the clock pin
    for (unsigned int i = 0; i < _mode; i++) {
        _sck.write(1);
        _sck.write(0);
    }

    return value;
}

/**
 * @brief   Converts a 32-bit signed fixed point integer to floating point value
 * @note    The 32-bit unsigned integer argument represnts actually a 24-bit signed fixed point value:
 *          two’s complement and the LSB of the 24-bit binary number represents 1/8,388,608th of a unit).
 * @param   32-bit unsigned integer
 * @retval  Floating point value ranging from -1 to +1
 */
float HX711::_toFloat(uint32_t dword)
{
    if (dword & 0x800000)
        return(-float((~dword + 1) & 0xFFFFFF) / 8388608.0f);
    else
        return(float(dword) / 8388607.0f);
}

/**
 * @brief   Converts normalised value to voltage.
 * @note
 * @param   val     Normalised measured value (ranging from -1.0f to + 1.0f)
 * @retval  Input   voltage in Volts [V]
 */
float HX711::toVolt(float val)
{
    return((val * _avdd) / _gain / 2);
}

/**
 * @brief   Obtains HX711's input voltage in mV.
 * @note
 * @retval  Measured voltage in mV.
 */
float HX711::read()
{
    return(toVolt(_toFloat(readInt())) * 1000 - _drift);
}
