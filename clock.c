#include	"clock.h"

/** \file
	\brief Do stuff periodically
*/

#include	"timer.h"
#include	"watchdog.h"

/*!	do stuff every 1/4 second

	called from clock_10ms(), do not call directly
*/
void clock_250ms() {
	ifclock(clock_flag_1s) {
	}
}

/*! do stuff every 10 milliseconds

	call from ifclock(CLOCK_FLAG_10MS) in busy loops
*/
void clock_10ms() {
	// reset watchdog
	wd_reset();

	ifclock(clock_flag_250ms) {
		clock_250ms();
	}
}

