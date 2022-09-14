//SPDX-License-Identifier: GPL-2.0-or-later

/*

    Copyright (C) 2022 Cyril Hrubis <metan@ucw.cz>

 */

#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <utils/gp_utils.h>
#include <widgets/gp_widgets.h>

#define APP_NAME "gptimer"

static gp_widget *timer_time;
static gp_widget *timer_pbar;
static gp_widget *hours;
static gp_widget *mins;
static gp_widget *secs;

static struct timespec start_time;
static uint64_t timer_elapsed_ms;
static uint64_t timer_duration_ms;

#define HOURS_IN_MS (60 * 60 * 1000)
#define MINS_IN_MS (60 * 1000)
#define SECS_IN_MS (1000)

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

	clock_gettime(CLOCK_MONOTONIC, &cur_time);

	elapsed_ms = timespec_diff_ms(&cur_time, &start_time) + timer_elapsed_ms;

	if (elapsed_ms >= timer_duration_ms) {
		update_timer(timer_duration_ms);
		play_alarm();
		return GP_TIMER_PERIOD_STOP;
	}

	update_timer(elapsed_ms);

	return 0;
}

static gp_timer timer_tick = {
	.period = 100,
	.callback = timer_tick_callback,
	.id = "timer",
};

int start_timer(gp_widget_event *ev)
{
	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

	clock_gettime(CLOCK_MONOTONIC, &start_time);
	gp_widgets_timer_ins(&timer_tick);

	return 0;
}

int stop_timer(gp_widget_event *ev)
{
	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

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

	clock_gettime(CLOCK_MONOTONIC, &cur_time);

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
	                  gp_widget_int_val_get(hours),
	                  gp_widget_int_val_get(mins),
	                  gp_widget_int_val_get(secs));
}

static int app_on_event(gp_widget_event *ev)
{
	if (ev->type != GP_WIDGET_EVENT_FREE)
		return 0;

	save_config();
	return 1;
}

int main(int argc, char *argv[])
{
	gp_htable *uids;
	gp_widget *layout = gp_app_layout_load(APP_NAME, &uids);

	timer_time = gp_widget_by_uid(uids, "timer_time", GP_WIDGET_LABEL);
	timer_pbar = gp_widget_by_uid(uids, "timer_pbar", GP_WIDGET_PROGRESSBAR);

	hours = gp_widget_by_cuid(uids, "hours", GP_WIDGET_CLASS_INT);
	mins = gp_widget_by_cuid(uids, "mins", GP_WIDGET_CLASS_INT);
	secs = gp_widget_by_cuid(uids, "secs", GP_WIDGET_CLASS_INT);

	gp_htable_free(uids);

	gp_widget_on_event_set(hours, update_duration_callback, NULL);
	gp_widget_on_event_set(mins, update_duration_callback, NULL);
	gp_widget_on_event_set(secs, update_duration_callback, NULL);

	load_config();
	update_duration();

	gp_app_on_event_set(app_on_event);

	gp_widgets_main_loop(layout, "gptimer", NULL, argc, argv);

	return 0;
}
