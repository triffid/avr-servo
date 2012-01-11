#include	<avr/interrupt.h>
#include	<avr/pgmspace.h>

#include	"arduino.h"
#include	"serial.h"
#include	"watchdog.h"
#include	"sersendf.h"

#ifndef	MASK
	#define	MASK(m)	(1 << m)
#endif

#define	DEBUG 0

// 0 8 4 12 2 10 6 14
// 1 9 5 13 3 11 7 15

uint8_t		servo_index;
volatile uint16_t	pulse_width[16];

uint8_t line_index;
char line[64];

uint16_t read_tcnt(void) {
	uint8_t sreg;
	uint16_t r;
	sreg = SREG;
	cli();
	r = TCNT1;
	SREG = sreg;
	return r;
}

void timer_init(void) {
	// reset all pulse widths to 1500us
	for (servo_index = 0; servo_index < 16; servo_index++)
		pulse_width[servo_index] = 1500 US;

	// reset index
	servo_index = 0;

	// set up initial timeout
	OCR1A = pulse_width[servo_index] + (1 US);
	OCR1B = pulse_width[servo_index];

	// set up index output pins
	DDRD |= 0b00111100;
	PORTD = (PORTD & 0b11000011) | (servo_index << 2);

	// set up timer output pin
	SET_OUTPUT(OC1B);
	//SET_OUTPUT(DEBUG_LED);

	// Fast PWM mode: ICR1 as top, de-assert OCR1A pin at reset, assert on match
	TCCR1A = MASK(COM1B1) | MASK(COM1B0) | MASK(WGM11) | MASK(WGM10);
	// 1x prescaler for max speed. at 20MHz and 16 bits this gives us 3.2768ms maximum pulse width
	TCCR1B = MASK(WGM13) | MASK(WGM12) | MASK(CS10);
	// enable interrupts
	TIMSK1 = MASK(TOIE1);
}

/*
	Please Note: this interrupt code depends on the line decoder latching the index.
	to achieve this with a 4514, connect !EN and LATCH together so that the chip only
	accepts a new address while disabled.
*/
ISR(TIMER1_OVF_vect) {
	// update servo index
	servo_index = (servo_index + 1) & 0b00001111;
	// cache timeout since it's volatile
	uint16_t p = pulse_width[servo_index];
	// write to output pins for next servo pulse
	PORTD = (servo_index << 2);
	// update timeouts for next servo pulse (double buffered)
	OCR1A = p + (1 US);
	OCR1B = p;
	// reset watchdog
	wd_reset();
	// toggle led
	//TOGGLE(DEBUG_LED);
	// debug
	//if (DEBUG)
	//	serial_writechar(hexalpha[servo_index]);
}

void main(void) __attribute__ ((noreturn));
void main(void) {
	wd_init();
	serial_init();
	timer_init();

	sei();

	serial_writestr_P(PSTR("Start\n"));

	line_index = 0;

	for (;;) {
		if (serial_rxchars()) {
			uint8_t c = serial_popchar();
			if (c >= 'a' && c <= 'z')
				c &= ~32;
			line[line_index] = c;
			if (c == 13 || c == 10) {
				if (line_index > 0) {
					if (line[0] == 'R') {
						uint8_t j;
						for (j = 0; j < 16; j++) {
							sersendf_P(PSTR("Servo %u: %u\n"), j, pulse_width[j]);
						}
					}
					else if (line[0] == 'S') {
						uint8_t j;
						enum {
							FIND_INDEX,
							FOUND_INDEX,
							FIND_VALUE,
							FOUND_VALUE,
							DONE
						} state = 0;
						uint8_t sindex = 0;
						uint16_t svalue = 0;
						for (j = 1; j < line_index; j++) {
							switch(state) {
								case FIND_INDEX:
									if (line[j] >= '0' && line[j] <= '9') {
										state = FOUND_INDEX;
										sindex = line[j] - '0';
									}
									break;
								case FOUND_INDEX:
									if (line[j] >= '0' && line[j] <= '9') {
										sindex = (sindex * 10) + (line[j] - '0');
									}
									else
										state = FIND_VALUE;
									break;
								case FIND_VALUE:
									if (line[j] >= '0' && line[j] <= '9') {
										state = FOUND_VALUE;
										svalue = line[j] - '0';
									}
									break;
								case FOUND_VALUE:
									if (line[j] >= '0' && line[j] <= '9') {
										svalue = (svalue * 10) + (line[j] -'0');
									}
									else {
										state = DONE;
										j = line_index;
									}
									break;
								case DONE:
									break;
							}
							if (DEBUG)
								sersendf_P(PSTR("[%u %u %u]"), j, line[j], state);
						}
						if (state == FOUND_VALUE || state == DONE) {
							if ((sindex & 0xF0) == 0) {
								if (svalue >= (600 US) && svalue < (2400 US)) {
									//sersendf_P(PSTR("Servo %u changed from %u to %u\n"), sindex, pulse_width[sindex], svalue);
									pulse_width[sindex] = svalue;
								}
								else
									sersendf_P(PSTR("Bad value: %u\n"), svalue);
							}
							else
								sersendf_P(PSTR("Bad Servo Index: %u\n"), sindex);
						}
						else
							sersendf_P(PSTR("Syntax: S <servo> <value>\n"));
					}
				}
				line_index = 0;
			}
			else if (line_index < 64) {
				line_index++;
			}
		}
	}
}
