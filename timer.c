#include	"timer.h"

/** \file
	\brief Timer management - step pulse clock and system clock

	Teacup uses timer1 to generate both step pulse clock and system clock.

	We achieve this by using the output compare registers to generate the two clocks while the timer free-runs.

	Teacup has tried numerous timer management methods, and this is the best so far.
*/

#include	<avr/interrupt.h>

#include	"arduino.h"
#include	"config.h"

/// how often we overflow and update our clock; with F_CPU=16MHz, max is < 4.096ms (TICK_TIME = 65535)
#define		TICK_TIME			2 MS
/// convert back to ms from cpu ticks so our system clock runs properly if you change TICK_TIME
#define		TICK_TIME_MS	(TICK_TIME / (F_CPU / 1000))

/// time until next step, as output compare register is too small for long step times
uint32_t	next_step_time;

/// every time our clock fires, we increment this so we know when 10ms has elapsed
uint8_t						clock_counter_10ms = 0;
/// keep track of when 250ms has elapsed
uint8_t						clock_counter_250ms = 0;
/// keep track of when 1s has elapsed
uint8_t						clock_counter_1s = 0;

/// flags to tell main loop when above have elapsed
volatile uint8_t	clock_flag_10ms = 0;
volatile uint8_t	clock_flag_250ms = 0;
volatile uint8_t	clock_flag_1s = 0;

volatile uint8_t	timer1_compa_deferred_enable = 0;

/// comparator B is the system clock, happens every TICK_TIME
ISR(TIMER1_COMPB_vect) {
	// set output compare register to the next clock tick
	OCR1B = (OCR1B + TICK_TIME) & 0xFFFF;

	/*
	clock stuff
	*/
	clock_counter_10ms += TICK_TIME_MS;
	if (clock_counter_10ms >= 10) {
		clock_counter_10ms -= 10;
		clock_flag_10ms = 1;

		clock_counter_250ms += 1;
		if (clock_counter_250ms >= 25) {
			clock_counter_250ms -= 25;
			clock_flag_250ms = 1;

			clock_counter_1s += 1;
			if (clock_counter_1s >= 4) {
				clock_counter_1s -= 4;
				clock_flag_1s = 1;
			}
		}
	}
}

#ifdef	HOST

/// comparator A is the step timer. It has higher priority then B.
ISR(TIMER1_COMPA_vect) {
	// Check if this is a real step, or just a next_step_time "overflow"
	if (next_step_time < 65536) {
		// step!
		WRITE(SCK, 1);

		// disable this interrupt. if we set a new timeout, it will be re-enabled when appropriate
		TIMSK1 &= ~MASK(OCIE1A);
		timer1_compa_deferred_enable = 0;
		
		// stepper tick
		queue_step();

		// led off
		WRITE(SCK, 0);
		
		// Enable the timer1_compa interrupt, if needed, 
		// but only do it after disabling global interrupts.
		// This will cause push any possible timer1a interrupt
		// to the far side of the return, protecting the 
		// stack from recursively clobbering memory.
		
		cli();
		CLI_SEI_BUG_MEMORY_BARRIER();
		
		if (timer1_compa_deferred_enable) {
			TIMSK1 |= MASK(OCIE1A);
		}
		return;
	}

	next_step_time -= 65536;

	// similar algorithm as described in setTimer below.
	if (next_step_time < 65536) {
		OCR1A = (OCR1A + next_step_time) & 0xFFFF;
	} else if(next_step_time < 75536){
		OCR1A = (OCR1A - 10000) & 0xFFFF;
		next_step_time += 10000;
	}
	// leave OCR1A as it was
}
#endif /* ifdef HOST */

/// initialise timer and enable system clock interrupt.
/// step interrupt is enabled later when we start using it
void timer_init()
{
	// no outputs
	TCCR1A = 0;
	// Normal Mode
	TCCR1B = MASK(CS10);
	// set up "clock" comparator for first tick
	OCR1B = TICK_TIME & 0xFFFF;
	// enable interrupt
	TIMSK1 = MASK(OCIE1B);
}

#ifdef	HOST
/// specify how long until the step timer should fire
void setTimer(uint32_t delay)
{
	// save interrupt flag
	uint8_t sreg = SREG;
	uint16_t step_start = 0;
	
	// disable interrupts
	cli();
	CLI_SEI_BUG_MEMORY_BARRIER();
	
	// re-enable clock interrupt in case we're recovering from emergency stop
	TIMSK1 |= MASK(OCIE1B);

	if (delay > 0) {

		// if the delay is too small use a minimum delay so that there is time
		// to set everything up before the timer expires.

		if (delay < 17 )
			delay = 17;
		
		// Assume all steps belong to one move. Within one move the delay is
		// from one step to the next one, which should be more or less the same
		// as from one step interrupt to the next one. The last step interrupt happend
		// at OCR1A, so start delay from there.
		step_start = OCR1A;
		if (next_step_time == 0) {
			// new move, take current time as start value
			step_start = TCNT1;
		}

		next_step_time = delay;
		if (next_step_time < 65536) {
			// set the comparator directly to the next real step
			OCR1A = (next_step_time + step_start) & 0xFFFF;
		}
		else if (next_step_time < 75536) {
			// Next comparator interrupt would have to trigger another
			// interrupt within a short time (possibly within 1 cycle).
			// Avoid the impossible by firing the interrupt earlier.
			OCR1A = (step_start - 10000) & 0xFFFF;
			next_step_time += 10000;
		}
		else {
			OCR1A = step_start;
		}

		// Defer the enabling of the timer1_CompA interrupts.
		
		timer1_compa_deferred_enable = 1;
	} else {
		// flag: move has ended
		next_step_time = 0;
		TIMSK1 &= ~MASK(OCIE1A);
		timer1_compa_deferred_enable = 0;
	}

	// restore interrupt flag
	MEMORY_BARRIER();
	SREG = sreg;
}

/// stop timers - emergency stop
void timer_stop() {
	// disable all interrupts
	TIMSK1 = 0;
}
#endif /* ifdef HOST */
