/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) rikus@tsn.systems
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>

#include <rte_eal.h>
#include <rte_debug.h>
#include <rte_ethdev.h>
#include <rte_pmd_i210.h>

bool checkarg(int argc, char *argv[], const char *p, uint8_t cnt)
{
	int i;
	if (argc < cnt || argc == 0) {
		return false;
	}
	for (i = 1; i < cnt; i++) {
		if (argv[i][0] == '-' && argv[i][1] == '-') {
			return false;
		}
	}
	if (strcmp(argv[0], p)) {
		return false;
	}
	return true;
}

int main(int argc, char *argv[])
{
	uint8_t port = 0;
	int ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		rte_panic("Cannot init EAL\n");
	}
	if (rte_eth_timesync_enable(port) < 0) {
		puts("rte_eth_timesync_enable failed");
	}

	/* arguments after -- */
	argc -= ret;
	argv += ret;

	while (argc > 0) {
		if (checkarg(argc, argv, "--port", 2)) {
			port = atoi(argv[1]);
		} else
		if (checkarg(argc, argv, "--setpin", 4)) {
			uint8_t pin = atoi(argv[1]);
			bool output = atoi(argv[2]);
			bool value  = atoi(argv[3]);

			if (rte_pmd_i210_sdp_setup(port, pin, output, value) < 0) {
				puts("rte_pmd_i210_sdp_setup failed");
			}
		} else
		if (checkarg(argc, argv, "--getpin", 2)) {
			uint8_t pin = atoi(argv[1]);

			bool value;
			if (rte_pmd_i210_sdp_get(port, pin, &value) < 0) {
				puts("rte_pmd_i210_sdp_get failed");
			}
			printf("Pin %hhu value = %hhu\n", pin, value);
		} else
		if (checkarg(argc, argv, "--toggle", 3)) {
			uint8_t pin = atoi(argv[1]);
			uint8_t delay = atoi(argv[2]);

			struct timespec ts;
			if (rte_pmd_i210_get_system_time(port, &ts) < 0) {
				puts("rte_pmd_i210_get_system_time failed");
			}
			ts.tv_sec += delay;
			if (rte_pmd_i210_sdp_toggle(port, pin, 0, &ts) < 0) {
				puts("rte_pmd_i210_sdp_toggle failed");
			}
		} else
		if (checkarg(argc, argv, "--pulse", 4)) {
			uint8_t pin = atoi(argv[1]);
			int delay = atoi(argv[2]);
			int len = atoi(argv[3]);

			struct timespec ts;
			if (rte_pmd_i210_get_system_time(port, &ts) < 0) {
				puts("rte_pmd_i210_get_system_time failed");
			}
			ts.tv_sec += delay;
			if (rte_pmd_i210_sdp_pulse(port, pin, &ts, len) < 0) {
				puts("rte_pmd_i210_sdp_toggle failed");
			}
		} else
		if (checkarg(argc, argv, "--timestamp", 3)) {
			uint8_t pin = atoi(argv[1]);
			int i, cnt = atoi(argv[2]);

			int ret = -EAGAIN;
			rte_pmd_i210_sdp_setup_timestamping(port, pin, 0, true);
			for (i = 0; i <= cnt; i++)
			while (ret == -EAGAIN) {
				struct timespec ts;
				int ret = rte_pmd_i210_sdp_read_timestamp(port, 0, &ts);
				if (ret == 0) {
					printf("Level change on pin %hhu at %li.%09li\n", pin, ts.tv_sec, ts.tv_nsec);
				} else
				if (ret != -EAGAIN) {
					puts("rte_pmd_i210_sdp_read_timestamp failed");
					i = cnt;
					break;
				}
			}
		} else
		if (!strcmp(argv[0], "--systime")) {
			struct timespec ts;
			if (rte_pmd_i210_get_system_time(port, &ts) < 0) {
				puts("rte_pmd_i210_get_system_time failed");
			} else {
				printf("Systime = %li.%09li\n", ts.tv_sec, ts.tv_nsec);
			}
		} else
		if (checkarg(argc, argv, "--delay", 2)) {
			int delay = atoi(argv[1]);
			usleep(delay);
		}

		do {
			argc--;
			argv++;
			if (argc <= 0) {
				break;
			}
		} while (!(argv[0][0] == '-' && argv[0][1] == '-'));
	}

	if (rte_eth_timesync_disable(port) < 0) {
		puts("rte_eth_timesync_disable failed");
	}
	rte_eal_cleanup();

	return 0;
}
