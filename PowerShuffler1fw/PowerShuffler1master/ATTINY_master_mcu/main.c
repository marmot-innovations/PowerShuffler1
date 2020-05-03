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
 * ATTINY_master_mcu::main.c
 *
 * Created: 3/16/2020 4:00:40 PM
 * Author : mike
 * Summary: This master MCU code as-is is designed to run on a 9S 4.2V Li-ion battery stack.
 *          Parameters can be adjusted to run on 2S - 11S battery stacks (electrically limited).
 */
#define F_CPU                 8000000UL // 8 MHz
#define VERIFYTRIGGERTIMEOUT  4      // Max timeout ticks to verify not a glitch
#define READINGTIMEOUT        15625  // Reading timeout tick 62,500 (or about .5s at 31.25khz)
#define MAXTIMEOUT            0xFFFF // Max timeout tick 65,535
#define MAXTIMEOUTCOUNT       5      // Max iterations of max timeout ticks
#define BRIEFBLINKDELAYCOUNT  0xFFFF // Number of cycles to delay between LED on-off
#define SLEEPDELAYCOUNT       30     // Number of cycles to sleep for LED heartbeat ~5min (measured 4min 56sec on a stopwatch)
#define ADCITERATIONS         4      // Number of readings to take an average. Base 2 number saves memory.
#define ADCTHRESHOLDHI        7      // Threshold ADC value for stopping the client MCU / charging (battery delta of 0.124V)
#define ADCTHRESHOLDLOW       4      // Hysteresis threshold of restarting client MCU / charging (battery delta of 0.071V)
#define MAXVOLTAGEADCVALUE    232    // ADC value of maximum output battery voltage (about 4.12V)
#define MINVOLTAGEADCVALUE    163    // ADC value of minimum input battery voltage (about 2.90V)
#define BLINKTOGGLEVALUE      16384  // Toggle LED when count reaches this value. Base 2 number saves memory.
#define CLIENTDEBOUNCEDELAY   0xFFFF // Number of cycles for client MCU power-on debounce delay and restarting main loop
#define POWERDEBOUNCEDELAYMS  3000   // Power-on debounce delay, which should be <8 seconds (watchdog)
#define ADCREADDELAYMS        10     // Delay between ADC readings

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h> // _delay_ms

enum ReadDataState
{
	Read_Idle = 0,
	Read_VerifyTrigger,
	Read_Reading,
	Read_Timeout,
	Read_Error
};

enum ReadDataState g_readDataState = Read_Idle;
unsigned int g_incomingCount = 0;      // 0 is error (i.e. INT0 held lo by client MCU to indicate an error).
unsigned int g_ledBlinkCount = 0;      // counter to randomly toggle the LED each time it hits BLINKTOGGLEVALUE.
unsigned int g_adcThreshold = 0;       // Variable threshold to implement hysteresis
unsigned short g_idleTimeoutCount = 0; // counter to activate idle timeout

void initializeRegisters()
{
	g_adcThreshold = ADCTHRESHOLDHI;

	// Clock frequency
	CCP    = 0xD8; // write the signature to enable CPU changes per datasheet
	CLKPSR = 0x00; // 0000 0000 for prescaler 1

	// Watchdog timer and Sleep Mode
	WDTCSR = 0x61; // 0110 0001 Interrupt Mode and 1024k prescalar (about 8 seconds)
	SMCR   = 0x05; // 0000 0101 Power-Down mode

	// Ports and pins
	// PB0 = ADC0 input battery voltage
	// PB1 = LED output, hi on, lo off (default lo)
	// PB2 = data input, hi idle (default hi)
	// PB3 = client MCU switch, hi off (default hi), lo on, 12V high-voltage reset
	PUEB |= _BV(3); // Enable pull-up resistor to fully float PB3 hi
	DDRB |= _BV(1); // 0000 0010 for STATUS LED output pin PB1

	_delay_ms(POWERDEBOUNCEDELAYMS); // Power-on debounce delay to wait for steady-state
	wdt_reset();

	// Timers
	OCR0A  = MAXTIMEOUT;
	// TCCR0A = 0X00; // 0000 0000 wgm01=0, wgm00=0
	// TCCR0B = 0x00; // 0000 0000 wgm03=0, wgm02=0, disable timer, no clock source

	// ADC
	ADCSRA = 0x83; // 1000 0011 enables ADC and set prescaler 8
	// ADCSRB = 0x00; // Single Conversion Mode
	// ADMUX = 0x00; // Select ADC0 on PB0
	DIDR0 = 0x0E; // 0000 1110 disables buffers on unused ADC pins for power savings

	// Interrupts
	// EICRA  = 0x00; // 0000 0000 to initially enable interrupt on low INT0
	EIMSK  = 0x01; // 0000 0001 to enable INT0 interrupts
	TIMSK0 = 0x03; // 0000 0011 to enable interrupt on timer match OCF0A and TOV
	SREG   = 0x80; // 1000 0000 to globally enable interrupts
}

void resetAndStartTimer()
{
	TCNT0   = 0;    // reset timer count to 0
	TCCR0B |= 0x04; // start timer w/ prescaler 256
}

void stopTimer()
{
	TCCR0B &= 0xFB; // stop timer
}

void resetReadStateToIdle()
{
	g_idleTimeoutCount = 0;
	g_readDataState = Read_Idle;
	OCR0A = MAXTIMEOUT;
	EICRA = 0x00;  // reset to enable interrupt on low INT0
	EIMSK = 0x01;  // Enable INT0 interrupt
}

void toggleLed() // toggles LED on PB1
{
	PORTB = (PORTB & _BV(1)) ? PORTB & ~_BV(1) : PORTB | _BV(1);
}

void turnOnLed()
{
	PORTB |= _BV(1); // set PB1
}

void turnOffLed()
{
	PORTB &= ~_BV(1); // clear PB1
}

// make LED look like it's blinking when called from inside a busy-loop
void incrementAndBlinkLed()
{
	g_ledBlinkCount++; // start from 1 initially
	if(0 == (g_ledBlinkCount % BLINKTOGGLEVALUE))
		toggleLed();
}

void turnOnClient()
{
	PUEB &= ~_BV(3); // Disable pull-up resistor to save power
	DDRB |= _BV(3);  // DCDC enable output pin PB3
	for(unsigned int j=0; j<CLIENTDEBOUNCEDELAY; j++) // Wait for client power steady-state and delay restarting of main loop.
		incrementAndBlinkLed();
}

void turnOffClient()
{
	PUEB |= _BV(3);  // Enable pull-up resistor to fully float PB3 hi
	DDRB &= ~_BV(3); // Set PB3 to float hi to turn off client.
}

void startADC()
{
	ADCSRA |= 0x40; // 0100 0000 set ADSC to start single conversion mode
}

unsigned short adcInProgress()
{
	return ADCSRA & 0x40; // 0100 0000 returns 0 if adc is done, non-zero otherwise.
}

// 0   = 0.0V, which is battery 0.0V
// 255 = 3.394V, which is battery 4.530V after resistor divider
// Each step is 0.01331V, which is battery 0.01776V per step
unsigned short adcValue()
{
	return ADCL;
}

unsigned short getAdcValueBusyWait()
{
	startADC();
	while(adcInProgress()) // busy waits until ADC conversion is done
		incrementAndBlinkLed();

	return adcValue();
}

unsigned int getAdcValueBusyWaitWithAveraging()
{
	unsigned int adcvalue = 0;
	for(int i=0; i<ADCITERATIONS; i++)
	{
		if(i) _delay_ms(ADCREADDELAYMS); // Skip delaying the first reading, then delay in milliseconds
		adcvalue += (unsigned int)getAdcValueBusyWait();
	}

	return adcvalue / ADCITERATIONS; // average ADC value
}

unsigned short readPinB2()
{
	return PINB & _BV(2); // 0000 0100
}

//void busywaitDelay(unsigned int count) // Not enough memory to implement busy-wait delay and toggle LED subroutine
//{
	//while(count)
	//{
		//incrementAndBlinkLed();
		//count--;
	//}
//}

ISR(INT0_vect)
{
	// if triggering on low level, make sure not a glitch
	// then start timer for Reading, switch to trigger on falling edge and count edges
	// when the timer expires, reset to interrupt triggering on lo
	switch(g_readDataState)
	{
		case Read_Idle:
			stopTimer();
			EIMSK = 0x00; // Disable INT0 interrupt
			g_readDataState = Read_VerifyTrigger; // Set next state
			OCR0A = VERIFYTRIGGERTIMEOUT; // Set timeout
			resetAndStartTimer();
			break;
		case Read_Reading:
			g_incomingCount++;
			break;
		default: // Interrupt has been triggered out-of-state
			break;
	}
}

ISR(TIM0_COMPA_vect)
{
	switch(g_readDataState)
	{
		case Read_VerifyTrigger:
			stopTimer();
			if(readPinB2()) // if PB2 has floated hi, signal was a glitch, go back to Idle
			{
				resetReadStateToIdle();
			}
			else            // PB2 still held lo, not a glitch, continue Reading
			{
				g_incomingCount = 0; // set count to 0
				g_readDataState = Read_Reading;
				OCR0A = READINGTIMEOUT; // Set reading timeout value
				EICRA = 0x02; // 0000 0010 enable interrupt on falling edge of INT0
				EIMSK = 0x01; // Enable INT0 interrupt
			}
			resetAndStartTimer();
			break;
		case Read_Reading:
			stopTimer();
			g_readDataState = readPinB2() ? Read_Timeout : Read_Error; // Error if PB2 held lo after timeout
			EIMSK = 0x00; // Disable INT0 interrupt
			break;
		default:
			break;
	}
}

ISR(TIM0_OVF_vect)
{
	wdt_reset();
	if(g_readDataState == Read_Idle && ++g_idleTimeoutCount > MAXTIMEOUTCOUNT)
	{
		stopTimer();
		g_readDataState = Read_Error; // Timeout error
		g_idleTimeoutCount = 0; // clear idle timeout count before continuing
		EIMSK = 0x00; // Disable INT0 interrupt
	}
}

ISR(WDT_vect)
{
	// do nothing and wake up MCU
}

// LED status behavior and description:
//   - Rapid blinking                                 = activity (reading data and/or charging)
//   - Short series of blinks, then off w/heartbeat   = no charging
//   - Two short blinks per sleep time, then off w/hb = input voltage below threshold, conserving power
//   - Solid on w/ inverted hb                        = Error
//   - Off at all times                               = no power
int main(void)
{
	initializeRegisters();

    while (1)
    {
		if(getAdcValueBusyWait() > MINVOLTAGEADCVALUE) // Turn on client MCU if input voltage is above minimum voltage. Otherwise, save power.
		{
			turnOnClient();         // Turn on client MCU
			resetReadStateToIdle(); // Starts listening for input
			resetAndStartTimer();   // Starts timer
			while(g_readDataState != Read_Timeout && g_readDataState != Read_Error) // Busy waits until error or until reading is done
				incrementAndBlinkLed();

			if(g_readDataState == Read_Timeout)
			{
				// Re-acquire ADC reading since there might have been a delay, then determine if charging needs to continue or stop
				if(g_incomingCount < (getAdcValueBusyWaitWithAveraging()+g_adcThreshold) // keep charging if output is lower than input voltage threshold
					&& g_incomingCount < MAXVOLTAGEADCVALUE) // stop charging if output voltage hits an absolute max
				{
					g_adcThreshold = ADCTHRESHOLDHI; // Boost the output voltage to higher threshold
					continue; // skips the rest of the while-loop and start over
				}
				g_adcThreshold = ADCTHRESHOLDLOW; // Reset to a lower voltage threshold for hysteresis
				turnOffLed(); // keep LED off to indicate OK
			}
			else // Error
			{
				turnOnLed(); // keep LED solid on to indicate error
			}
		}
		else
		{
			turnOffLed(); // turn off LED to save power
			for(unsigned short i=0; i<4; i++) // Double-blink LED
			{
				for(unsigned int j=0; j<BRIEFBLINKDELAYCOUNT; j++); // Not enough memory to use _delay_ms
				toggleLed();
			}
		}
		turnOffClient();
		ADCSRA &= 0x7F; // disable ADC
		PRR = 0x03; // turn off ADC and timers to save power

		for(unsigned short i=0; i<SLEEPDELAYCOUNT; i++) // Sleep before starting again. Blip LED for heartbeat.
		{
			wdt_reset();
			sleep_mode();
			toggleLed();
			for(unsigned int j=0; j<BRIEFBLINKDELAYCOUNT; j++); // Not enough memory to use _delay_ms
			toggleLed();
		}

		PRR = 0x00; // restore power
		ADCSRA |= 0x80; // enable ADC
	}
}
