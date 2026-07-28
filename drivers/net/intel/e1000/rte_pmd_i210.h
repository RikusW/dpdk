/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 rikus@tsn.systems
 */

#ifndef _RTE_PMD_I210_H_
#define _RTE_PMD_I210_H_

#include <stdint.h>

/* bit0 = output, bit1 = value, bit4 = 0/1, bits5+6 = function */
enum i210_sdp_function
{
	i210_sdp_input    = 0x00, //function 0 gpio
	i210_sdp_outputl  = 0x01, //output 0
	i210_sdp_outputh  = 0x03, //output 1
	//The functions below can be assigned to one pin only
	i210_sdp_event0   = 0x21, //function 1
	i210_sdp_event1   = 0x31,
	i210_sdp_clock0   = 0x41, //function 2 clockx and eventx use the same hardware
	i210_sdp_clock1   = 0x51,
	i210_sdp_capture0 = 0x60, //function 3 independent from event hardware
	i210_sdp_capture1 = 0x70,
	i210_sdp_pulse    = 0x81, //function 4 uses event0 and event1 hardware
};

__rte_experimental
int rte_pmd_i210_sdp_set_functions(uint16_t port,
			enum i210_sdp_function sdp0, enum i210_sdp_function sdp1,
			enum i210_sdp_function sdp2, enum i210_sdp_function sdp3);

/* Disable event, clock, or capture function */
/* The toggle, clock, pulse or timestamp functions below will enable it */
__rte_experimental
int rte_pmd_i210_sdp_disable_function(uint16_t port, enum i210_sdp_function f);

/* pin_num = 0-3 */
__rte_experimental
int rte_pmd_i210_sdp_get(uint16_t port, uint8_t pin_num, bool *pin_value);

/* pin_num = 0-3 */
__rte_experimental
int rte_pmd_i210_sdp_set(uint16_t port, uint8_t pin_num, bool pin_value);

__rte_experimental
int rte_pmd_i210_get_system_time(uint16_t port, struct timespec *ts);

/* eventx = 0-1 */
/* ts needs to be at least 50us in future */
__rte_experimental
int rte_pmd_i210_sdp_toggle(uint16_t port, uint8_t eventx, struct timespec *ts);

/* eventx = 0-1 */
/* minimum delay of 50us*/
__rte_experimental
int rte_pmd_i210_sdp_toggle_delay(uint16_t port, uint8_t eventx, uint32_t us);

/* uses both event0 and event1 */
/* ts needs to be at least 50us in future */
/* len in ns */
__rte_experimental
int rte_pmd_i210_sdp_pulse(uint16_t port, struct timespec *ts, uint32_t len);

/* These cannot have a phase offset */
#define I210_PPS1 500000000
#define I210_PPS2 250000000
#define I210_PPS4 125000000

/* clockx = 0-1 */
/* ns_period 8ns - 70ms, 125ms, 250ms, 500ms */
/* API for setting phase offset when period <= 70ms not implemented yet */
__rte_experimental
int rte_pmd_i210_sdp_set_clock(uint16_t port, uint8_t clockx, uint32_t ns_period);

/* clockx = 0-1 */
/* ns_offset should be < (ns_period * 2) */
__rte_experimental
int rte_pmd_i210_sdp_set_clock_phase(uint16_t port, uint8_t clockx, uint32_t ns_offset);

/* capturex = 0-1 */
__rte_experimental
int rte_pmd_i210_sdp_get_timestamp(uint16_t port, uint8_t capturex, struct timespec *ts);

#endif /* _RTE_PMD_I210_H_ */
