/*
 * This file is part of the PowerShuffler project.
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
 *
 * ATTINY_client_mcu::main.c
 *
 * Created: 3/23/2020 2:09:01 PM
 * Author : mike
 * Summary: This client MCU code as-is is designed to run on any 4.2V single cell Li-ion battery.
 *          Parameters can be adjusted as needed for your application.
 */
#define F_CPU 8000000UL // 8 MHz
#define TRIGGERTIMEOUTCOUNT  256   // minimum iterations to trigger master MCU reading
#define TRANSMITDELAYCOUNT   128   // number of iterations between transmitting edges (1 bit = 1 falling and 1 rising edge)
#define POWERDEBOUNCEDELAYMS 1000  // power-on debounce delay must be less than 4 seconds due to watchdog timeout
#define ADCITERATIONS        3     // Number of readings to take an average
#define ADCREADDELAYMS       10    // Delay between ADC readings
#define BRIEFBLINKDELAYMS    50    // Duration of short LED blink
#define MAXVOLTAGEADCVALUE   237   // ADC value of maximum battery voltage (about 4.20V battery)
#define BLINKTOGGLEVALUE     16384 // Toggle LED when count reaches this value

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

unsigned int g_ledBlinkCount = 0; // counter to randomly toggle the LED each time it hits BLINKTOGGLEVALUE.

void initializeRegisters()
{
	// Clock frequency
	CCP    = 0xD8; // write the signature to enable CPU changes per datasheet
	CLKPSR = 0x00; // 0000 0000 for prescaler 1

	// Watchdog timer and Sleep Mode
	WDTCSR = 0x60; // 0110 0000 Interrupt Mode and 512k prescalar (about 4 seconds)
	SMCR   = 0x05; // 0000 0101 Power-Down mode

	_delay_ms(POWERDEBOUNCEDELAYMS); // power-on debounce. Roughly the amount of time needed for Li-ion charger to turn on and reach steady-state.
	wdt_reset();

	// Ports and pins
	// PB0 = ADC0 output battery voltage
	// PB1 = LED output, hi on, lo off (default lo)
	// PB2 = data output, hi idle (default hi)
	// PB3 = /reset (not used for anything else)
	// PUEB  |= _BV(0); // for debugging, enable pull-up on ADC0
	PUEB  |= _BV(3); // enable pull-up on input PB3 (noise reduction for floating pin)
	PORTB |= _BV(2); // ensure output PB2 is hi (not transmitting / idle)
	DDRB  |= _BV(1); // 0000 0010 for STATUS LED output pin PB1
	DDRB  |= _BV(2); // 0000 0100 for data output pin PB2

	// ADC
	ADCSRA = 0x83; // 1000 0011 enables ADC and set prescaler 8
	// ADCSRB = 0x00; // Single Conversion Mode
	// ADMUX = 0x00; // Select ADC0 on PB0
	DIDR0 = 0x0E; // 0000 1110 disables buffers on unused ADC pins for power savings

	// Interrupts
	SREG = 0x80; // 1000 0000 to globally enable interrupts
}

void toggleLed() // toggles LED on PB1
{
	PORTB = (PORTB & _BV(1)) ? PORTB & ~_BV(1) : PORTB | _BV(1);
}

void turnOnLed()
{
	PORTB |= _BV(1); // 0000 0010 set PB1
}

void turnOffLed()
{
	PORTB &= ~_BV(1); // 1111 1101 clear PB1
}

void incrementAndBlinkLed()
{
	if(!(++g_ledBlinkCount % BLINKTOGGLEVALUE)) // make LED look like it's blinking inside a loop
		toggleLed();
}

void startADC()
{
	ADCSRA |= 0x40; // 0100 0000 set ADSC to start single conversion mode
}

unsigned short adcInProgress()
{
	return ADCSRA & 0x40; // 0100 0000 returns 0 if adc is done, non-zero otherwise.
}

unsigned short adcValue()
{
	// 0   = 0.0V, which is battery 0.0V
	// 255 = 3.406V, which is battery 4.519V after resistor divider
	// Each step is 0.01335V, which is battery 0.01775V per step
	return (unsigned short)ADCL;
}

unsigned short getAdcValueBusyWait()
{
	startADC();
	while(adcInProgress()) // busy waits until ADC conversion is done
	{
		incrementAndBlinkLed();
	}
	return adcValue();
}

void transmitOneTick()
{
	PORTB &= ~_BV(2);
	for(unsigned int i=0; i<TRANSMITDELAYCOUNT; i++)
		incrementAndBlinkLed();
	PORTB |= _BV(2);
	for(unsigned int i=0; i<TRANSMITDELAYCOUNT; i++)
		incrementAndBlinkLed();
}

void triggerRead()
{
	PORTB &= ~_BV(2);
	for(unsigned int i=0; i<TRIGGERTIMEOUTCOUNT; i++) // hold lo for minimum trigger count
		incrementAndBlinkLed();
	PORTB |= _BV(2);
	for(unsigned int i=0; i<TRANSMITDELAYCOUNT; i++) // reset to hi for data output
		incrementAndBlinkLed();
}

void outputDataError() // holds the output lo for duration of power-on, solid LED
{
	PORTB &= ~_BV(2);
	turnOffLed();
	_delay_ms(BRIEFBLINKDELAYMS);
	turnOnLed();
}

void resetAndStart()
{
	wdt_reset();
	turnOffLed();
}

ISR(WDT_vect)
{
	// do nothing and wake up MCU
}

// LED status behavior and description:
//   - Rapid blinking = sending data
//   - Short series of blinks, then off = send data complete
//   - Solid on = Error
//   - Off at all times = Idle / off
int main(void)
{
	initializeRegisters();

    while (1)
    {
		resetAndStart();
		unsigned int adcvalue = 0;
		for(int i=0; i<ADCITERATIONS; i++)
		{
			if(i) _delay_ms(ADCREADDELAYMS); // skips delaying of the first reading
			adcvalue += (unsigned int)getAdcValueBusyWait();
		}
		adcvalue /= ADCITERATIONS; // average ADC value

		if(adcvalue > MAXVOLTAGEADCVALUE || adcvalue == 0) // Over-voltage or grounded ADC input
		{
			 outputDataError(); // Error
		}
		else
		{
			triggerRead(); // trigger the master MCU to start reading
			for(int i=0; i<adcvalue; i++)
			{
				transmitOneTick(); // transmit 1 bit at a time
			}
			turnOnLed();
			_delay_ms(BRIEFBLINKDELAYMS);
			turnOffLed(); // No errors
		}

		ADCSRA &= 0x7F; // disable ADC
		PRR = 0x03; // turn off ADC and timers to save power
		wdt_reset();
		sleep_mode();
		PRR = 0x00; // restore power
		ADCSRA |= 0x80; // enable ADC
    }
}

