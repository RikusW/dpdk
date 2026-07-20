/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 rikus@tsn.systems
 */

#ifndef _RTE_PMD_I210_H_
#define _RTE_PMD_I210_H_

#include <stdint.h>

/*
 * i210 SDP
 *
 * pin_num = 0-3
 * aux_register_set = 0-1
 * target_register_set = 0-1
 * len = ns pulse width
 */

__rte_experimental
int rte_pmd_i210_sdp_get(uint16_t port, uint8_t pin_num, bool *pin_value);

__rte_experimental
int rte_pmd_i210_sdp_setup(uint16_t port,
            uint8_t pin_num, bool output, bool pin_value);

__rte_experimental
int rte_pmd_i210_sdp_toggle(uint16_t port,
            uint8_t pin_num, uint8_t target_register_set, struct timespec *ts);

__rte_experimental
int rte_pmd_i210_sdp_pulse(uint16_t port,
            uint8_t pin_num, struct timespec *ts, uint32_t len);

__rte_experimental
int rte_pmd_i210_sdp_setup_timestamping(uint16_t port,
            uint8_t pin_num, uint8_t aux_timestamp_set, bool enable);

__rte_experimental
int rte_pmd_i210_sdp_read_timestamp(uint16_t port,
            uint8_t aux_timestamp_set, struct timespec *ts);

__rte_experimental
int rte_pmd_i210_get_system_time(uint16_t port, struct timespec *ts);

#endif /* _RTE_PMD_I210_H_ */
