//SPDX-License-Identifier: GPL-2.0-or-later

/*

    Copyright (C) 2022 Cyril Hrubis <metan@ucw.cz>

 */

#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <utils/gp_utils.h>
#include <widgets/gp_widgets.h>

#define APP_NAME "gptimer"

static gp_widget *timer_time;
static gp_widget *timer_pbar;
static gp_widget *hours;
static gp_widget *mins;
static gp_widget *secs;
static gp_widget *wake;

static struct timespec start_time;
static uint64_t timer_elapsed_ms;
static uint64_t timer_duration_ms;

static timer_t wake_timer;
static int wake_timer_created;

#define HOURS_IN_MS (60 * 60 * 1000)
#define MINS_IN_MS (60 * 1000)
#define SECS_IN_MS (1000)

#define WAKEUP_MARGIN 5

static clockid_t timer_clock;

static void check_posix_timer_support(void)
{
	struct timespec tmp;

	if (!clock_gettime(CLOCK_BOOTTIME_ALARM, &tmp)) {
		timer_clock = CLOCK_BOOTTIME_ALARM;
		GP_DEBUG(1, "Selected CLOCK_BOOTTIME_ALARM");
		return;
	}

	GP_DEBUG(1, "CLOCK_BOOTTIME_ALARM %s", strerror(errno));

	if (wake)
		gp_widget_disable(wake);

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &tmp)) {
		GP_DEBUG(1, "Selected CLOCK_MONOTONIC_RAW");
		timer_clock = CLOCK_MONOTONIC_RAW;
		return;
	}

	GP_DEBUG(1, "CLOCK_MONOTONIC_RAW %s", strerror(errno));

	timer_clock = CLOCK_MONOTONIC;
	GP_DEBUG(1, "Selected CLOCK_MONOTONIC_RAW");
}

static void update_timer(uint64_t elapsed)
{
	uint64_t cur_time = timer_duration_ms - elapsed;

	int hours = cur_time / HOURS_IN_MS;
	int mins = (cur_time % HOURS_IN_MS) / MINS_IN_MS;
	int secs = (cur_time % MINS_IN_MS) / SECS_IN_MS;
	int msecs = (cur_time % SECS_IN_MS) / 100;

	gp_widget_label_printf(timer_time, "%02i:%02i:%02i.%1i", hours, mins, secs, msecs);

	if (timer_pbar) {
		gp_widget_pbar_set_max(timer_pbar, timer_duration_ms);
		gp_widget_pbar_set(timer_pbar, cur_time);
	}
}

static void update_duration(void)
{
	timer_duration_ms = gp_widget_int_val_get(hours) * HOURS_IN_MS +
	                    gp_widget_int_val_get(mins) * MINS_IN_MS +
                            gp_widget_int_val_get(secs) * SECS_IN_MS;

	update_timer(0);
}

static int update_duration_callback(gp_widget_event *ev)
{
	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

	update_duration();

	return 0;
}

static uint64_t timespec_diff_ms(struct timespec *end, struct timespec *start)
{
	uint64_t diff_ms = 1000 * (end->tv_sec - start->tv_sec);

	if (end->tv_nsec < start->tv_nsec)
		diff_ms -= (start->tv_nsec - end->tv_nsec + 500)/1000000;
	else
		diff_ms += (end->tv_nsec - start->tv_nsec + 500)/1000000;

	return diff_ms;
}

static char *alarm_cmdline = "aplay " ALARM_PATH;

static void play_alarm(void)
{
	if (!alarm_cmdline)
		return;

	if (access(ALARM_PATH, F_OK))
		alarm_cmdline = "aplay alarm.wav";

	if (!fork()) {
		if (system(alarm_cmdline))
			GP_WARN("Failed to execute '%s'", alarm_cmdline);
		exit(0);
	}
}

static uint32_t timer_tick_callback(gp_timer *self)
{
	struct timespec cur_time;
	uint64_t elapsed_ms;

	(void) self;

	clock_gettime(timer_clock, &cur_time);

	elapsed_ms = timespec_diff_ms(&cur_time, &start_time) + timer_elapsed_ms;

	if (elapsed_ms >= timer_duration_ms) {
		update_timer(timer_duration_ms);
		play_alarm();
		return GP_TIMER_STOP;
	}

	update_timer(elapsed_ms);

	return self->period;
}

static gp_timer timer_tick = {
	.period = 100,
	.callback = timer_tick_callback,
	.id = "timer",
};

static void start_wake_alarm(void)
{
	signal(SIGALRM, SIG_IGN);

	if (timer_create(timer_clock, NULL, &wake_timer)) {
		gp_dialog_msg_printf_run(GP_DIALOG_MSG_ERR,
		                         "Failed to create wake alarm",
		                         "%s", strerror(errno));
		return;
	}

	wake_timer_created = 1;

	struct itimerspec tmr = {};

	tmr.it_value.tv_sec = (timer_duration_ms - timer_elapsed_ms)/SECS_IN_MS;
	if (tmr.it_value.tv_sec > WAKEUP_MARGIN)
		tmr.it_value.tv_sec -= WAKEUP_MARGIN;

	timer_settime(wake_timer, 0, &tmr, NULL);
}

static void stop_wake_alarm(void)
{
	if (!wake_timer_created)
		return;

	timer_delete(&wake_timer);

	wake_timer_created = 0;
}

int start_timer(gp_widget_event *ev)
{
	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

	clock_gettime(timer_clock, &start_time);
	gp_widgets_timer_ins(&timer_tick);

	if (wake && gp_widget_bool_get(wake))
		start_wake_alarm();

	return 0;
}

int stop_timer(gp_widget_event *ev)
{
	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

	stop_wake_alarm();

	timer_elapsed_ms = 0;

	update_timer(0);

	gp_widgets_timer_rem(&timer_tick);

	return 0;
}

int pause_timer(gp_widget_event *ev)
{
	struct timespec cur_time;

	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

	stop_wake_alarm();

	clock_gettime(timer_clock, &cur_time);

	timer_elapsed_ms += timespec_diff_ms(&cur_time, &start_time);

	gp_widgets_timer_rem(&timer_tick);

	return 0;
}

static void load_config(void)
{
	int val_hrs, val_mins, val_secs;

	if (gp_app_cfg_scanf(APP_NAME, "timeout.txt", "%i:%i:%i",
	                     &val_hrs, &val_mins, &val_secs) != 3)
		return;

	if (hours)
		gp_widget_int_val_set(hours, val_hrs);

	if (mins)
		gp_widget_int_val_set(mins, val_mins);

	if (secs)
		gp_widget_int_val_set(secs, val_secs);
}

static void save_config(void)
{
	gp_app_cfg_printf(APP_NAME, "timeout.txt", "%02i:%02i:%02i\n",
	                  (int)gp_widget_int_val_get(hours),
	                  (int)gp_widget_int_val_get(mins),
	                  (int)gp_widget_int_val_get(secs));
}

static int app_on_event(gp_widget_event *ev)
{
	if (ev->type != GP_WIDGET_EVENT_FREE)
		return 0;

	save_config();
	return 1;
}

gp_app_info app_info = {
	.name = "gptimer",
	.desc = "A simple timer app",
	.version = "1.0",
	.license = "GPL-2.0-or-later",
	.url = "http://github.com/gfxprim/gptimer",
	.authors = (gp_app_info_author []) {
		{.name = "Cyril Hrubis", .email = "metan@ucw.cz", .years = "2022"},
		{}
	}
};

int main(int argc, char *argv[])
{
	gp_htable *uids;
	gp_widget *layout;

	layout = gp_app_layout_load(APP_NAME, &uids);

	timer_time = gp_widget_by_uid(uids, "timer_time", GP_WIDGET_LABEL);
	timer_pbar = gp_widget_by_uid(uids, "timer_pbar", GP_WIDGET_PROGRESSBAR);

	hours = gp_widget_by_cuid(uids, "hours", GP_WIDGET_CLASS_INT);
	mins = gp_widget_by_cuid(uids, "mins", GP_WIDGET_CLASS_INT);
	secs = gp_widget_by_cuid(uids, "secs", GP_WIDGET_CLASS_INT);
	wake = gp_widget_by_cuid(uids, "wake", GP_WIDGET_CLASS_BOOL);

	gp_htable_free(uids);

	gp_widget_on_event_set(hours, update_duration_callback, NULL);
	gp_widget_on_event_set(mins, update_duration_callback, NULL);
	gp_widget_on_event_set(secs, update_duration_callback, NULL);

	check_posix_timer_support();
	load_config();
	update_duration();

	gp_app_on_event_set(app_on_event);

	gp_widgets_main_loop(layout, NULL, argc, argv);

	return 0;
}
