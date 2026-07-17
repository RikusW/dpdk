/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
//#include <sys/queue.h>

#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_debug.h>

#include <rte_pmd_i210.h>

/* Initialization of Environment Abstraction Layer (EAL). 8< */
int
main(int argc, char **argv)
{
	int ret;
	unsigned lcore_id;

	if (rte_eal_init(argc, argv) < 0) {
		rte_panic("Cannot init EAL\n");
	}

	if (rte_pmd_i210_sdp_setup(0, 0, 0, 0) < 0) {
		printf("rte_pmd_i210_sdp_setup failed\n");
	}

	/* clean up the EAL */
	rte_eal_cleanup();

	return 0;
}
