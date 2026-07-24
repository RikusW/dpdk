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

static bool checkarg(int argc, char *argv[], const char *p, uint8_t cnt)
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

static void addts(struct timespec *ts, uint32_t d)
{
	if (d < 50) {
		puts("Warning use at least 50us to account for setup time");
	}
	uint64_t t = d;
	t *= 1000; //use us
	t += ts->tv_nsec;
	ts->tv_sec += t / 1000000000;
	ts->tv_nsec = t % 1000000000;
}

struct str_to_func
{
	const char *str;
	enum i210_sdp_function func;
};

static const struct str_to_func s2f[10] =
{
	{ "input",   i210_sdp_input },
	{ "outputl", i210_sdp_outputl },
	{ "outputh", i210_sdp_outputh },
	{ "event0",  i210_sdp_event0 },
	{ "event1",  i210_sdp_event1 },
	{ "clock0",  i210_sdp_clock0 },
	{ "clock1",  i210_sdp_clock1 },
	{ "capture0",i210_sdp_capture0 },
	{ "capture1",i210_sdp_capture1 },
	{ "pulse"   ,i210_sdp_pulse },
};

static enum i210_sdp_function str2func(const char *s)
{
	int i;

	for (i = 0; i < 10; i++) {
		if (!strcmp(s2f[i].str, s)) {
			return s2f[i].func;
		}
	}
	printf("Unknown function string %s\n", s);
	return i210_sdp_input;
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
		if (checkarg(argc, argv, "--setup", 5)) {
			enum i210_sdp_function sdp0 = str2func(argv[1]);
			enum i210_sdp_function sdp1 = str2func(argv[2]);
			enum i210_sdp_function sdp2 = str2func(argv[3]);
			enum i210_sdp_function sdp3 = str2func(argv[4]);

			if (rte_pmd_i210_sdp_set_functions(port, sdp0, sdp1, sdp2, sdp3) < 0) {
				puts("rte_pmd_i210_sdp_set_functions failed");
				return 1;
			}
		} else
		if (checkarg(argc, argv, "--set", 3)) {
			uint8_t pin = atoi(argv[1]);
			bool value  = atoi(argv[2]);

			if (rte_pmd_i210_sdp_set(port, pin, value) < 0) {
				puts("rte_pmd_i210_sdp_set failed");
			}
		} else
		if (checkarg(argc, argv, "--get", 2)) {
			uint8_t pin = atoi(argv[1]);

			bool value;
			if (rte_pmd_i210_sdp_get(port, pin, &value) < 0) {
				puts("rte_pmd_i210_sdp_get failed");
			}
			printf("Pin %hhu value = %hhu\n", pin, value);
		} else
		if (checkarg(argc, argv, "--toggle", 3)) {
			uint8_t eventx = atoi(argv[1]) & 1;
			int delay = atoi(argv[2]); //us

			if (rte_pmd_i210_sdp_toggle_delay(port, eventx, delay) < 0) {
				puts("rte_pmd_i210_sdp_toggle failed");
			}
		} else
		if (checkarg(argc, argv, "--toggle2", 3)) {
			int delay0 = atoi(argv[1]);
			int delay1 = atoi(argv[2]);

			struct timespec ts0, ts1;
			if (rte_pmd_i210_get_system_time(port, &ts0) < 0) {
				puts("rte_pmd_i210_get_system_time failed");
			}
			ts1 = ts0;
			addts(&ts0, delay0);
			if (rte_pmd_i210_sdp_toggle(port, 0, &ts0) < 0) {
				puts("rte_pmd_i210_sdp_toggle failed");
			}
			addts(&ts1, delay1);
			if (rte_pmd_i210_sdp_toggle(port, 1, &ts1) < 0) {
				puts("rte_pmd_i210_sdp_toggle failed");
			}
			printf("Toggle0 at %lu.%09lu\n", ts0.tv_sec, ts0.tv_nsec);
			printf("Toggle1 at %lu.%09lu\n", ts1.tv_sec, ts1.tv_nsec);
		} else
		if (checkarg(argc, argv, "--pulse", 2)) {
			int len = atoi(argv[1]); //ns

			struct timespec ts;
			if (rte_pmd_i210_get_system_time(port, &ts) < 0) {
				puts("rte_pmd_i210_get_system_time failed");
			}
			addts(&ts, 50);
			if (rte_pmd_i210_sdp_pulse(port, &ts, len) < 0) {
				puts("rte_pmd_i210_sdp_pulse failed");
			}
		} else
		if (checkarg(argc, argv, "--clock", 3)) {
			uint8_t clockx = atoi(argv[1]) & 1;
			int period = atoi(argv[2]); //ns

			if (rte_pmd_i210_sdp_set_clock(port, clockx, period) < 0) {
				puts("rte_pmd_i210_sdp_clock failed");
			}
		} else
		if (checkarg(argc, argv, "--timestamp", 3)) {
			uint8_t capturex = atoi(argv[1]) & 1;
			int i, cnt = atoi(argv[2]); //ms to sleep while retrying

			int ret = 0;
			for (i = 0; i <= cnt; i++)  {
				struct timespec ts;
				ret = rte_pmd_i210_sdp_get_timestamp(port, capturex, &ts);
				if (ret == 0) {
					printf("Level change on capture %hhu at %li.%09li\n", capturex, ts.tv_sec, ts.tv_nsec);
					break;
				}
				if (ret != -EAGAIN) {
					puts("rte_pmd_i210_sdp_read_timestamp failed");
					i = cnt;
					break;
				}
				usleep(1000);
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
		} else {
			printf("Unknown option %s\n", argv[0]);
		}

		do {
			argc--;
			argv++;
			if (argc <= 0) {
				goto exit;
			}
		} while (!(argv[0][0] == '-' && argv[0][1] == '-'));
	}
exit:
	if (rte_eth_timesync_disable(port) < 0) {
		puts("rte_eth_timesync_disable failed");
	}
	rte_eal_cleanup();

	return 0;
}
