/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 rikus@tsn.systems
 */

#include <eal_export.h>
#include <ethdev_driver.h>
#include "base/e1000_api.h"
#include "e1000_ethdev.h"
#include "rte_pmd_i210.h"

/* Add missing i210 defines*/
#define E1000_AUXSTMPL0        0x0B65C /* Auxiliary Timestamp Register 0 Low  - RO */
#define E1000_AUXSTMPH0        0x0B660 /* Auxiliary Timestamp Register 0 High - RO */
#define E1000_AUXSTMPL1        0x0B664 /* Auxiliary Timestamp Register 1 Low  - RO */
#define E1000_AUXSTMPH1        0x0B668 /* Auxiliary Timestamp Register 1 High - RO */

#define E1000_TRGTTIML(n) (0x0B644 + ((n) * 8))
#define E1000_TRGTTIMH(n) (0x0B648 + ((n) * 8))
#define E1000_AUXSTMPL(n) (0x0B65C + ((n) * 8))
#define E1000_AUXSTMPH(n) (0x0B660 + ((n) * 8))

/* TSAUXC Configuration Bits 8.15.13 */
#define TSAUXC_EN_TT(n)        (1 << (n & 1))
#define TSAUXC_EN_TS1  (1 << 10) /* Enable hardware timestamp 0. */
#define TSAUXC_AUTT0   (1 << 9)  /* Auxiliary timestamp taken */
#define TSAUXC_EN_TS1  (1 << 10) /* Enable hardware timestamp 1. */
#define TSAUXC_AUTT1   (1 << 11) /* Auxiliary timestamp taken */
#define TSAUXC_PLSG    (1 << 17) /* Target Time 0 generate level/pulse */

/* TSSDP Configuration Bits 8.15.25 */
#define AUXx_TS_SDP_EN(x) (1 << (2 + (((x) & 1) * 3)))       /* Enable auxiliary time stamp trigger x. */
#define AUXx_SEL_SDPn(x, n) (((n) & 3u) << (3 * ((x) & 1))) /* Assign SDPn to auxiliary time stamp x. */
#define AUXx_SEL_SDP_CLR(x) (~(AUXx_SEL_SDPn(x, 3)))
#define AUXx_SEL_SDPnr(x, n, r) r = ((r & AUXx_SEL_SDP_CLR(x)) | AUXx_SEL_SDPn(x, n))
#define TS_SDPn_EN(n) (1u << (8u + (((n) & 3u) * 3u)))  /* SDPn is assigned to Tsync. */
#define TS_SDPn_SEL_pos(n) (6u + (((n) & 3u) * 3u))
#define TS_SDPn_SEL_TTx(n, x) ( ((x) & 1u)       << TS_SDPn_SEL_pos(n))  /* Target time x = (0/1) is output on SDPn. */
#define TS_SDPn_SEL_FCx(n, x) ((((x) & 1u) | 2u) << TS_SDPn_SEL_pos(n))  /* Freq clock  x = (0/1) is output on SDPn. */
#define TS_SDPn_SEL_CLR(n) (~(3u << TS_SDPn_SEL_pos(n)))  /* Clear register bits */
#define TS_SDPn_SEL_TTxr(n, x, r) r = ((r & TS_SDPn_SEL_CLR(n)) | TS_SDPn_SEL_TTx(n, x))
#define TS_SDPn_SEL_FCxr(n, x, r) r = ((r & TS_SDPn_SEL_CLR(n)) | TS_SDPn_SEL_TTx(n, x))

#define E1000_CTRL_SDP0_DATA     0x00040000
#define E1000_CTRL_SDP1_DATA     0x00080000
#define E1000_CTRL_EXT_SDP2_DATA 0x00000040 /* SW Definable Pin 2 data */
#define E1000_CTRL_EXT_SDP2_DIR  0x00000400 /* Direction of SDP2 0=in 1=out */

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_setup, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_setup(uint16_t port,
			uint8_t pin_num, bool output, bool pin_value)
{
	uint32_t ctrl, reg, dir, data;
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
    struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}

	switch(pin_num) {
	case 0:
		reg = E1000_CTRL; /* 8.2.1 */
		dir = E1000_CTRL_SDP0_DIR;
		data= E1000_CTRL_SDP0_DATA;
		break;
	case 1:
		reg = E1000_CTRL;
		dir = E1000_CTRL_SDP1_DIR;
		data= E1000_CTRL_SDP1_DATA;
		break;
	case 2:
		reg = E1000_CTRL_EXT; /* 8.2.3 */
		dir = E1000_CTRL_EXT_SDP2_DIR;
		data= E1000_CTRL_EXT_SDP2_DATA;
		break;
	case 3:
		reg = E1000_CTRL_EXT;
		dir = E1000_CTRL_EXT_SDP3_DIR;
		data= E1000_CTRL_EXT_SDP3_DATA;
		break;
	default:
		return -EINVAL;
	}

	ctrl = E1000_READ_REG(hw, reg);
	if (output) {
		ctrl |= dir;
		if (pin_value) {
			ctrl |= data;
		} else {
			ctrl &= ~data;
		}
	} else {
		ctrl &= ~dir;
	}
	E1000_WRITE_REG(hw, reg, ctrl);

	E1000_WRITE_FLUSH(hw);
	return 0;
}

/* 7.8.3.3.1: Level Change Generation */
RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_toggle, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_toggle(uint16_t port,
			uint8_t pin_num, uint8_t target_register_set, struct timespec *ts)
{
	uint32_t tssdp, tsauxc;
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
    struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (pin_num > 3 || target_register_set > 1) {
		return -EINVAL;
	}

	rte_pmd_i210_sdp_setup(port, pin_num, 1, 0); /* Set pin to output */

	/* Time of level change */
	E1000_WRITE_REG(hw, E1000_TRGTTIML(target_register_set), ts->tv_nsec);
	E1000_WRITE_REG(hw, E1000_TRGTTIMH(target_register_set), ts->tv_sec);

	/* Assign the chosen target timer onto the hardware pin via TSSDP */
	tssdp = E1000_READ_REG(hw, E1000_TSSDP);
	tssdp |= TS_SDPn_EN(pin_num);
	TS_SDPn_SEL_TTxr(pin_num, target_register_set, tssdp);
	E1000_WRITE_REG(hw, E1000_TSSDP, tssdp); /* 8.15.25 */

	tsauxc = E1000_READ_REG(hw, E1000_TSAUXC);
	tsauxc |= TSAUXC_EN_TT(target_register_set);
	tsauxc &= ~TSAUXC_PLSG;
	E1000_WRITE_REG(hw, E1000_TSAUXC, tsauxc); /* 8.15.13 */

	E1000_WRITE_FLUSH(hw);
	return 0;
}

/* 7.8.3.3.2: Pulse Generation */
RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_pulse, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_pulse(uint16_t port,
			uint8_t pin_num, struct timespec *ts, uint32_t len)
{
	uint32_t tssdp, tsauxc;
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
    struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (pin_num > 3) {
		return -EINVAL;
	}

	rte_pmd_i210_sdp_setup(port, pin_num, 1, 0); /* Set pin to output */

	/* Start of pulse */
	E1000_WRITE_REG(hw, E1000_TRGTTIML0, (uint32_t)ts->tv_nsec);
	E1000_WRITE_REG(hw, E1000_TRGTTIMH0, (uint32_t)ts->tv_sec);

	/* End of pulse */
	ts->tv_nsec += len;
	ts->tv_sec += ts->tv_nsec / 1000000000;
	ts->tv_nsec = ts->tv_nsec % 1000000000;
	E1000_WRITE_REG(hw, E1000_TRGTTIML1, (uint32_t)ts->tv_nsec);
	E1000_WRITE_REG(hw, E1000_TRGTTIMH1, (uint32_t)ts->tv_sec);

	/* Assign the chosen target timer onto the hardware pin via TSSDP */
	tssdp = E1000_READ_REG(hw, E1000_TSSDP);
	tssdp |= TS_SDPn_EN(pin_num);
	TS_SDPn_SEL_TTxr(pin_num, 0, tssdp);
	E1000_WRITE_REG(hw, E1000_TSSDP, tssdp); /* 8.15.25 */

	tsauxc = E1000_READ_REG(hw, E1000_TSAUXC);
	tsauxc |= TSAUXC_EN_TT0;
	tsauxc |= TSAUXC_PLSG;
	E1000_WRITE_REG(hw, E1000_TSAUXC, tsauxc); /* 8.15.13 */

	E1000_WRITE_FLUSH(hw);
	return 0;
}

/* 7.8.3.4: Time Stamp Input Event Activation */
RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_setup_timestamping, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_setup_timestamping(uint16_t port,
			uint8_t pin_num, uint8_t aux_timestamp_set, uint8_t enable)
{
	uint32_t tssdp, tsauxc;
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
    struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (pin_num > 3 || aux_timestamp_set > 1) {
		return -EINVAL;
	}

	rte_pmd_i210_sdp_setup(port, pin_num, 0, 0); /* Set pin to input */

	/* Configure TSSDP pin mapping */
	tssdp = E1000_READ_REG(hw, E1000_TSSDP);
	tsauxc = E1000_READ_REG(hw, E1000_TSAUXC);
	AUXx_SEL_SDPnr(aux_timestamp_set, pin_num, tssdp);
	if (enable) {
		/* Enable hardware timestamping */
		tssdp |= AUXx_TS_SDP_EN(aux_timestamp_set);
		tsauxc |= aux_timestamp_set == 0 ? TSAUXC_EN_TS0 : TSAUXC_EN_TS1;
	} else {
		/* Disable hardware timestamping */
		tssdp &= ~(AUXx_TS_SDP_EN(aux_timestamp_set));
		tsauxc &= ~(aux_timestamp_set == 0 ? TSAUXC_EN_TS0 : TSAUXC_EN_TS1);
	}
	E1000_WRITE_REG(hw, E1000_TSSDP, tssdp); /* 8.15.25 */
	E1000_WRITE_REG(hw, E1000_TSAUXC, tsauxc); /* 8.15.13 */

	E1000_WRITE_FLUSH(hw);
	return 0;
}

/* Retrieve Latched Timestamp Data */
RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_pmd_i210_sdp_read_timestamp, 25.11)
__rte_experimental
int rte_pmd_i210_sdp_read_timestamp(uint16_t port,
			uint8_t aux_timestamp_set, struct timespec *ts)
{
	uint32_t tsauxc;
	RTE_ETH_VALID_PORTID_OR_ERR_RET(port, -ENODEV);
    struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct e1000_hw *hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	if (hw->mac.type != e1000_i210) {
		return -ENOTSUP;
	}
	if (!ts) {
		return -EINVAL;
	}

	/* Check if hardware has latched an event via checking TSAUXC status */
	tsauxc = E1000_READ_REG(hw, E1000_TSAUXC); /* 8.15.13 */
	uint32_t event_bit = (aux_timestamp_set == 0) ? TSAUXC_AUTT0 : TSAUXC_AUTT1;
	if (tsauxc & event_bit) {
		return -EAGAIN; /* No raw edge trigger captured yet */
	}

	ts->tv_nsec = E1000_READ_REG(hw, E1000_AUXSTMPL(aux_timestamp_set));
	ts->tv_sec  = E1000_READ_REG(hw, E1000_AUXSTMPH(aux_timestamp_set));
	return 0;
}
