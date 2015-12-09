/*
 * ATTiny13LEDDisplayDriver.c
 *
 * Created: 2015-01-11 19:10:57
 *  Author: amr
 */ 

#include <stdlib.h>

#include <avr/io.h>

#include <util/delay_basic.h>

#include <avr/interrupt.h>
#include <avr/sleep.h>

#include <string.h>

#include <math.h>

static uint8_t ctc_toggle_cycles_to_refresh_display;
static uint16_t ctc_toggle_cycles_to_refresh_display_sequence;

// configuring interrupts, sleeping-mode, and ports.
void __config_interrupts_sleeping_mode_and_ports (void) \
__attribute__ ((naked)) __attribute__ ((section (".init1")));
void __config_interrupts_sleeping_mode_and_ports (void) {
	// This value is specified based on the number of cycles
	// that are required to drive the LED display.
	ctc_toggle_cycles_to_refresh_display = 37;
	
	// GIMSK configurations:
	// only INT0 is enabled, all PCINT0-5 are disabled.
	GIMSK = _BV(INT0);
	
	// MCUCR configuration:
	// 1. pull-up resistors are enabled,
	// 2. sleeping is enabled,
	// 3. sleeping mode is set to idle,
	// 4. INT0 interrupt to be generated on rising edge.
	MCUCR = _BV(SE) | _BV(ISC01) | _BV(ISC00);
	
	// port-B configurations:
	// 1. PB0, PB2, PB3 and PB4 are output,
	DDRB = _BV(PB0) | _BV(PB2) | _BV(PB3) | _BV(PB4);
	// 2. Initiate PB0 to 0:
	// disconnected OC0A and not toggling it because double buffering is disabled on CTC,
	// and the OC0A is not synchronized with the CTC.
	PORTB &= ~_BV(PB0);
	// 3. initiate PB2 to high to disable data to M8522HR led display.
	PORTB |= _BV(PB2);
}

// configuring the timer-counter.
void __config_timer_counter (void) \
__attribute__ ((naked)) __attribute__ ((section (".init3")));
void __config_timer_counter (void) {
	// do we need to stop the clock at this stage?
	TCCR0B = 0x00;
	
	// wave generation mode is set to CTC:
	// disconnected OC0A and not toggling it because double buffering is disabled on CTC,
	// and the OC0A is not synchronized with the CTC.
	TCCR0A = _BV(WGM01);
	
	// only handle the OCR0A compare-match interrupt.
	TIMSK0 = _BV(OCIE0A);
	
	// setting the CTC top and initializing the counter.
	OCR0A = 0xFF;
	TCNT0 = 0x00;
	
	// finally, for testing we use a clock pre-scaler of 256 
	// while for running we use a clock pre-scaler of 8.
	// -----------------------------------------------------------------------
	// Note that the resulting frequency of manually toggling the clock pin PB0 
	// is below the calculated one because the handler of the timer interrupt disables/enables
	// interrupts while doing the calculations to update the count frequency or speed
	// and to drive the LED display. That value can be practically measured using a logic analyzer.
	ctc_toggle_cycles_to_refresh_display_sequence = 282; // measured using a logic analyzer as explained in the comment above.
	TCCR0B = _BV(CS01);	
}

// configuring the disabled modules.
void __config_disabled_modules (void) \
__attribute__ ((naked)) __attribute__ ((section (".init5")));
void __config_disabled_modules(void) {
	// disabling ADC:
	ADCSRA = 0x00;
	ADCSRB = 0x00;
	
	// disabling analog comparator:
	ACSR = 0x00;
	
	// disabling not required digital input buffers:
	DIDR0 = 0x1E;
}

static uint16_t frequency_counter = 0;
ISR (INT0_vect) {
	cli();
	// toggle the LED to indicate the frequency.
	PORTB ^= _BV(PB4);
	// update the frequency.
	frequency_counter++;
	sei();
}

static uint16_t speed = 0;
void frequencyToSpeed() {
	if (++ctc_toggle_cycles_to_refresh_display_sequence == 283) {
		// reset the number of cycles to refresh the display sequence.
		ctc_toggle_cycles_to_refresh_display_sequence = 0;
		
		// TODO write a true speed calculation.	
		// TODO remove test code.
		speed = frequency_counter;
		
		// reset the frequency counter.
		frequency_counter = 0;
		
		// max speed to display on a 3-digits LED display.
		if (speed > 999) speed = 999;
	}
}

// the index is a digit and the value is LED segments, including D.P.
// *A*
// F*B
// *G*
// E*C
// *D*
static const uint8_t digits_LED_segments[] = {
	0b00111111,
	0b00000110,
	0b01011011,
	0b01001111,
	0b01100110,
	0b01101101,
	0b01111101,
	0b00000111,
	0b01111111,
	0b01100111
};

static uint8_t speed_digits[3];
static uint8_t current_digit_index;
static uint8_t current_segment_index;
static uint16_t speed_buffer = 0;
void updateDisplaySequence() {	
	
	// copy the speed value, not affect updating it.
	speed_buffer = speed;
	
	// get the digits.
	speed_digits[0] = speed_digits[1] = speed_digits[2] = 0;
	while (speed_buffer >= 100) {
		speed_buffer-=100;
		speed_digits[0]++;
	}
	while (speed_buffer >= 10) {
		speed_buffer-=10;
		speed_digits[1]++;
	}
	speed_digits[2] = speed_buffer;
	
	// reset the indices.
	current_digit_index = 2;
	current_segment_index = 7;
}

ISR (TIM0_COMPA_vect) {	
	cli();
	PORTB ^= _BV(PB0); // Manually toggle the clock, read comments about OC0A above.
	// Note: enable toggling the indication LED with the time-counter interrupt to 
	// measure the actual frequency. Read the comments on the the clock configuration above.
	// PORTB ^= _BV(PB4); // blink the LED as an activity indication.
	// The logic applies only when the OC0A is low.
	if (!(PORTB & _BV(PB0))) {
		// if a second passed, transform the counted frequency into speed.
		frequencyToSpeed();
		// update the LED display.
		if (++ctc_toggle_cycles_to_refresh_display == 38) {
			// a new display refresh round the display.
			ctc_toggle_cycles_to_refresh_display = 0;
			
			// update the display sequence with with current speed.
			updateDisplaySequence();
			
			// enable data.
			PORTB &= ~_BV(PB2);
			
			// wait for at least 100ns.
			_delay_loop_1(1);
			
			// set the start bit.
			PORTB |= _BV(PB3);
		} else if (ctc_toggle_cycles_to_refresh_display == 37) {
			// done with updating the display.
			// disable data.
			PORTB |= _BV(PB2);
		} else {
			PORTB &= ~_BV(PB3);
			if (ctc_toggle_cycles_to_refresh_display < 25) {
				// prepare for the next digit to display.
				if ((++current_segment_index) > 7) {
					current_segment_index = 0;
					if (++current_digit_index > 2) {
						current_digit_index = 0;
					}
				}
				// display the next segment.
				// lookup its value in the digit coding map using its index.
				if (digits_LED_segments[speed_digits[current_digit_index]] & _BV(current_segment_index))
				PORTB |= _BV(PB3);
			}
		}
	}
	sei();
}

int main(void) {
	while(1) {
		// ensure interrupts are enabled before sleeping.
		sei();
		sleep_cpu();
	}
}