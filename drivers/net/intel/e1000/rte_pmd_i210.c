/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 rikus@tsn.systems
 */

#include <eal_export.h>
#include <ethdev_driver.h>
#include "base/e1000_api.h"
#include "e1000_ethdev.h"
#include "rte_pmd_i210.h"

/* Add missing i210 defines*/
#define E1000_AUXSTMPL0 0x0B65C /* Auxiliary Timestamp Register 0 Low  - RO */
#define E1000_AUXSTMPH0 0x0B660 /* Auxiliary Timestamp Register 0 High - RO */
#define E1000_AUXSTMPL1 0x0B664 /* Auxiliary Timestamp Register 1 Low  - RO */
#define E1000_AUXSTMPH1 0x0B668 /* Auxiliary Timestamp Register 1 High - RO */

#define E1000_TRGTTIML(n) (0x0B644 + (((n) & 1) * 8))
#define E1000_TRGTTIMH(n) (0x0B648 + (((n) & 1) * 8))
#define E1000_AUXSTMPL(n) (0x0B65C + (((n) & 1) * 8))
#define E1000_AUXSTMPH(n) (0x0B660 + (((n) & 1) * 8))

/* TSAUXC Configuration Bits 8.15.13 */
#define TSAUXC_EN_TT(n)		(1 << (n & 1))
#define TSAUXC_EN_TS1  (1 << 10) /* Enable hardware timestamp 0. */
#define TSAUXC_AUTT0   (1 << 9)  /* Auxiliary timestamp taken */
#define TSAUXC_EN_TS1  (1 << 10) /* Enable hardware timestamp 1. */
#define TSAUXC_AUTT1   (1 << 11) /* Auxiliary timestamp taken */
#define TSAUXC_PLSG	(1 << 17) /* Target Time 0 generate level/pulse */

/* TSSDP Configuration Bits 8.15.25 */
#define AUXx_TS_SDP_EN(x) (1 << (2 + (((x) & 1) * 3)))	   /* Enable auxiliary time stamp trigger x. */
#define AUXx_SEL_SDPn(x, n) (((n) & 3u) << (3 * ((x) & 1))) /* Assign SDPn to auxiliary time stamp x. */
#define AUXx_SEL_SDP_CLR(x) (~(AUXx_SEL_SDPn(x, 3)))
#define AUXx_SEL_SDPnr(x, n, r) r = ((r & AUXx_SEL_SDP_CLR(x)) | AUXx_SEL_SDPn(x, n))
#define TS_SDPn_EN(n) (1u << (8u + (((n) & 3u) * 3u)))  /* SDPn is assigned to Tsync. */
#define TS_SDPn_SEL_pos(n) (6u + (((n) & 3u) * 3u))
#define TS_SDPn_SEL_TTx(n, x) ( ((x) & 1u)	   << TS_SDPn_SEL_pos(n))  /* Target time x = (0/1) is output on SDPn. */
#define TS_SDPn_SEL_FCx(n, x) ((((x) & 1u) | 2u) << TS_SDPn_SEL_pos(n))  /* Freq clock  x = (0/1) is output on SDPn. */
#define TS_SDPn_SEL_CLR(n) (~(3u << TS_SDPn_SEL_pos(n)))  /* Clear register bits */
#define TS_SDPn_SEL_TTxr(n, x, r) r = ((r & TS_SDPn_SEL_CLR(n)) | TS_SDPn_SEL_TTx(n, x))
#define TS_SDPn_SEL_FCxr(n, x, r) r = ((r & TS_SDPn_SEL_CLR(n)) | TS_SDPn_SEL_FCx(n, x))

#define E1000_CTRL_SDP0_DATA	 0x00040000
#define E1000_CTRL_SDP1_DATA	 0x00080000
#define E1000_CTRL_EXT_SDP2_DATA 0x00000040 /* SW Definable Pin 2 data */
#define E1000_CTRL_EXT_SDP2_DIR  0x00000400 /* Direction of SDP2 0=in 1=out */

static void get_rdd(uint8_t pin_num, uint32_t *reg, uint32_t *dir, uint32_t *data)
{
	switch(pin_num) {
	default:
	case 0:
		*reg = E1000_CTRL; /* 8.2.1 */
		*dir = E1000_CTRL_SDP0_DIR;
		*data= E1000_CTRL_SDP0_DATA;
		break;
	case 1:
		*reg = E1000_CTRL;
		*dir = E1000_CTRL_SDP1_DIR;
		*data= E1000_CTRL_SDP1_DATA;
		break;
	case 2:
		*reg = E1000_CTRL_EXT; /* 8.2.3 */
		*dir = E1000_CTRL_EXT_SDP2_DIR;
		*data= E1000_CTRL_EXT_SDP2_DATA;
		break;
	case 3:
		*reg = E1000_CTRL_EXT;
		*dir = E1000_CTRL_EXT_SDP3_DIR;
		*data= E1000_CTRL_EXT_SDP3_DATA;
		break;
	}
}

static void sdp_set_function(struct e1000_hw *hw, uint8_t pin_num,
			enum i210_sdp_function f, uint32_t *tsauxc, uint32_t *tssdp)
{
	uint32_t reg, dir, data, ctrl;
	get_rdd(pin_num, &reg, &dir, &data);
	ctrl = E1000_READ_REG(hw, reg);
	if (f & 1) {
		ctrl |= dir; /* output */
	} else {
		ctrl &= ~dir; /* input */
	}
	if (f & 2) {
		ctrl |= data;
	} else {
		ctrl &= ~data;
	}
	E1000_WRITE_REG(hw, reg, ctrl);

	switch (f & 0xE0) {
	case 0x20: /* 7.8.3.3.1: Level Change Generation */
		*tssdp |= TS_SDPn_EN(pin_num);
		TS_SDPn_SEL_TTxr(pin_num, f >> 4, *tssdp);
		*tsauxc &= ~TSAUXC_PLSG;
		break;

	case 0x80: /* 7.8.3.3.2 Pulse generation */
		*tssdp |= TS_SDPn_EN(pin_num);
		TS_SDPn_SEL_TTxr(pin_num, 0, *tssdp);
		*tsauxc |= TSAUXC_PLSG;
		break;

	case 0x40: /* 7.8.3.3.3 Synchronized Output Clock */
		E1000_WRITE_REG(hw, E1000_TRGTTIML(f >> 4), 0);
		E1000_WRITE_REG(hw, E1000_TRGTTIMH(f >> 4), 0);
		*tssdp |= TS_SDPn_EN(pin_num);
		TS_SDPn_SEL_FCxr(pin_num, f >> 4, *tssdp);
		*tsauxc &= ~TSAUXC_PLSG;
		break;

	case 0x60: /* 7.8.3.4 Time Stamp Events */
		AUXx_SEL_SDPnr(f >> 4, pin_num, *tssdp);
		*tssdp |= AUXx_TS_SDP_EN(f >> 4);
		*tsauxc |= f & 0x10 ? TSAUXC_EN_TS1 : TSAUXC_EN_TS0;
		break;
	}
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_set_functions, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_set_functions(uint16_t port,
			enum i210_sdp_function sdp0, enum i210_sdp_function sdp1,
			enum i210_sdp_function sdp2, enum i210_sdp_function sdp3)
{
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}

	uint32_t tsauxc, tssdp = 0;
	tsauxc = E1000_READ_REG(hw, E1000_TSAUXC);
    tsauxc &= ~(TSAUXC_EN_TT0 | TSAUXC_EN_CLK0 | TSAUXC_EN_TS0 |
                TSAUXC_EN_TT1 | TSAUXC_EN_CLK1 | TSAUXC_EN_TS1);

	//printf("tssdp = %08x tsauxc = %08x pre\n", tssdp, tsauxc);
	sdp_set_function(hw, 0, sdp0, &tsauxc, &tssdp);
	sdp_set_function(hw, 1, sdp1, &tsauxc, &tssdp);
	sdp_set_function(hw, 2, sdp2, &tsauxc, &tssdp);
	sdp_set_function(hw, 3, sdp3, &tsauxc, &tssdp);
	//printf("tssdp = %08x tsauxc = %08x post\n", tssdp, tsauxc);

	E1000_WRITE_REG(hw, E1000_TSSDP, tssdp); /* 8.15.25 */
	E1000_WRITE_REG(hw, E1000_TSAUXC, tsauxc); /* 8.15.13 */
	E1000_WRITE_FLUSH(hw);
	return 0;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_disable_function, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_disable_function(uint16_t port, enum i210_sdp_function f)
{
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}

	uint32_t tsauxc = E1000_READ_REG(hw, E1000_TSAUXC);
	switch (f) {
	case i210_sdp_event0:
		tsauxc &= ~TSAUXC_EN_TT0;
		break;
	case i210_sdp_event1:
		tsauxc &= ~TSAUXC_EN_TT1;
		break;
	case i210_sdp_clock0:
		tsauxc &= ~TSAUXC_EN_CLK0;
		break;
	case i210_sdp_clock1:
		tsauxc &= ~TSAUXC_EN_CLK1;
		break;
	case i210_sdp_capture0:
		tsauxc &= ~TSAUXC_EN_TS0;
		break;
	case i210_sdp_capture1:
		tsauxc &= ~TSAUXC_EN_TS1;
		break;
	case i210_sdp_pulse:
		tsauxc &= ~(TSAUXC_EN_TT0 | TSAUXC_EN_TT1 | TSAUXC_PLSG);
		break;
	default:
		return -EINVAL;
	}

	E1000_WRITE_REG(hw, E1000_TSAUXC, tsauxc); /* 8.15.13 */
	E1000_WRITE_FLUSH(hw);
	return 0;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_get, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_get(uint16_t port, uint8_t pin_num, bool *pin_value)
{
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (pin_num > 3 || !pin_value) {
		return -EINVAL;
	}

	uint32_t reg, dir, data;
	get_rdd(pin_num, &reg, &dir, &data);
	*pin_value = (E1000_READ_REG(hw, reg) & data) != 0;
	return 0;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_set, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_set(uint16_t port, uint8_t pin_num, bool pin_value)
{
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (pin_num > 3) {
		return -EINVAL;
	}

	uint32_t reg, dir, data, ctrl;
	get_rdd(pin_num, &reg, &dir, &data);
	ctrl = E1000_READ_REG(hw, reg);
	if (pin_value) {
		ctrl |= data;
	} else {
		ctrl &= ~data;
	}
	E1000_WRITE_REG(hw, reg, ctrl);
	E1000_WRITE_FLUSH(hw);
	return 0;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_get_system_time, 25.11)
__rte_experimental
int rte_pmd_i210_get_system_time(uint16_t port, struct timespec *ts)
{
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (!ts) {
		return -EINVAL;
	}

	ts->tv_nsec = E1000_READ_REG(hw, E1000_SYSTIMR);
	ts->tv_nsec = E1000_READ_REG(hw, E1000_SYSTIML);
	ts->tv_sec  = E1000_READ_REG(hw, E1000_SYSTIMH);
	return 0;
}

/* 7.8.3.3.1: Level Change Generation */
RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_toggle, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_toggle(uint16_t port, uint8_t eventx, struct timespec *ts)
{
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (!ts || eventx > 1) {
		return -EINVAL;
	}

	/* Time of level change */
	E1000_WRITE_REG(hw, E1000_TRGTTIML(eventx), ts->tv_nsec);
	E1000_WRITE_REG(hw, E1000_TRGTTIMH(eventx), ts->tv_sec);

	uint32_t tsauxc = E1000_READ_REG(hw, E1000_TSAUXC);
	if (!(tsauxc & (eventx ? TSAUXC_EN_TT1 : TSAUXC_EN_TT0))) {
		//puts("enabling event");
		tsauxc |= eventx ? TSAUXC_EN_TT1 : TSAUXC_EN_TT0;
		E1000_WRITE_REG(hw, E1000_TSAUXC, tsauxc); /* 8.15.13 */
		E1000_WRITE_FLUSH(hw);
	}
	return 0;
}

/* 7.8.3.3.1: Level Change Generation */
RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_toggle_delay, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_toggle_delay(uint16_t port, uint8_t eventx, uint32_t us)
{
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (eventx > 1) {
		return -EINVAL;
	}

	uint32_t nsec, sec;
	nsec = E1000_READ_REG(hw, E1000_SYSTIMR);
	nsec = E1000_READ_REG(hw, E1000_SYSTIML);
	sec  = E1000_READ_REG(hw, E1000_SYSTIMH);

	nsec += us * 1000;
	sec  += nsec / 1000000000;
	nsec  = nsec % 1000000000;

	/* Time of level change */
	E1000_WRITE_REG(hw, E1000_TRGTTIML(eventx), nsec);
	E1000_WRITE_REG(hw, E1000_TRGTTIMH(eventx), sec);

	uint32_t tsauxc = E1000_READ_REG(hw, E1000_TSAUXC);
	if (!(tsauxc & (eventx ? TSAUXC_EN_TT1 : TSAUXC_EN_TT0))) {
		//puts("enabling event");
		tsauxc |= eventx ? TSAUXC_EN_TT1 : TSAUXC_EN_TT0;
		E1000_WRITE_REG(hw, E1000_TSAUXC, tsauxc); /* 8.15.13 */
		E1000_WRITE_FLUSH(hw);
	}
	return 0;
}

#if 1
/* 7.8.3.3.2: Pulse Generation */
RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_pulse, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_pulse(uint16_t port, struct timespec *ts, uint32_t len)
{
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (!ts || len < 8) {
		return -EINVAL;
	}

	/* Start of pulse */
	E1000_WRITE_REG(hw, E1000_TRGTTIML0, (uint32_t)ts->tv_nsec);
	E1000_WRITE_REG(hw, E1000_TRGTTIMH0, (uint32_t)ts->tv_sec);

	/* End of pulse */
	uint64_t t = ((uint64_t)len) + ts->tv_nsec;
	ts->tv_sec += t / 1000000000;
	ts->tv_nsec = t % 1000000000;
	E1000_WRITE_REG(hw, E1000_TRGTTIML1, (uint32_t)ts->tv_nsec);
	E1000_WRITE_REG(hw, E1000_TRGTTIMH1, (uint32_t)ts->tv_sec);

	uint32_t tsauxc = E1000_READ_REG(hw, E1000_TSAUXC);
	if ((tsauxc & (TSAUXC_EN_TT0 | TSAUXC_EN_TT1 | TSAUXC_PLSG))
				!= (TSAUXC_EN_TT0 | TSAUXC_EN_TT1 | TSAUXC_PLSG)) {
		puts("enabling pulse");
		tsauxc |= TSAUXC_EN_TT0 | TSAUXC_EN_TT1 | TSAUXC_PLSG;
		E1000_WRITE_REG(hw, E1000_TSAUXC, tsauxc); /* 8.15.13 */
		E1000_WRITE_FLUSH(hw);
	}
	return 0;
}
#endif

/*7.8.3.3.3 Synchronized Output Clock on SDP Pins */
RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_set_clock, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_set_clock(uint16_t port, uint8_t clockx, uint32_t ns_period)
{
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (clockx > 1) {
		return -EINVAL;
	}

	E1000_WRITE_REG(hw, clockx ? E1000_FREQOUT1 : E1000_FREQOUT0, ns_period);

	uint32_t tsauxc;
	tsauxc = E1000_READ_REG(hw, E1000_TSAUXC);
	tsauxc |= clockx ? TSAUXC_EN_CLK1 : TSAUXC_EN_CLK0;
	E1000_WRITE_REG(hw, E1000_TSAUXC, tsauxc); /* 8.15.13 */

	E1000_WRITE_FLUSH(hw);
	return 0;
}

/* Retrieve Latched Timestamp */
RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_get_timestamp, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_get_timestamp(uint16_t port, uint8_t capturex, struct timespec *ts)
{
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (!ts || capturex > 1) {
		return -EINVAL;
	}

	/* Check if hardware has latched an event via checking TSAUXC status */
	uint32_t tsauxc = E1000_READ_REG(hw, E1000_TSAUXC); /* 8.15.13 */
	uint32_t enable_bit= capturex ? TSAUXC_EN_TS1 : TSAUXC_EN_TS0;
	if (!(tsauxc & enable_bit)) {
		puts("enabling timestamp");
		tsauxc |= enable_bit;
		E1000_WRITE_REG(hw, E1000_TSAUXC, tsauxc); /* 8.15.13 */
		E1000_WRITE_FLUSH(hw);
	}
	uint32_t event_bit = capturex ? TSAUXC_AUTT1 : TSAUXC_AUTT0;
	if (!(tsauxc & event_bit)) {
		return -EAGAIN; /* No raw edge trigger captured yet */
	}

	ts->tv_nsec = E1000_READ_REG(hw, E1000_AUXSTMPL(capturex));
	ts->tv_sec  = E1000_READ_REG(hw, E1000_AUXSTMPH(capturex));
	return 0;
}
