#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <math.h>

#include "fan_config.h"

volatile sig_atomic_t running = 1;

typedef enum {
    FAN_STATE_OFF,
    FAN_STATE_LOW,
    FAN_STATE_MID,
    FAN_STATE_HIGH
} FanState;

static FanState current_state = FAN_STATE_OFF;

void handle_signal(int sig) {
    running = 0;
}

int write_file(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    int r = write(fd, val, strlen(val));
    close(fd);
    return r == strlen(val) ? 0 : -1;
}

void set_duty_cycle(int duty, int *last_duty) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", duty);
    if (write_file(DUTY_PATH, buf) == 0) {
        *last_duty = duty;
        LOG_CUSTOM_INFO("Set duty=%d", duty);
    } else {
        LOG_CUSTOM_ERROR("Failed to set duty=%d", duty);
    }
}

float smooth_temperature(float new_temp, float *smoothed_temp) {
    const float alpha = 0.3;
    if (*smoothed_temp < 1) {
        *smoothed_temp = new_temp;
    } else {
        *smoothed_temp = alpha * new_temp + (1 - alpha) * (*smoothed_temp);
    }
    return *smoothed_temp;
}

int read_temperature(int *temp) {
    FILE *fp = fopen(TEMP_PATH, "r");
    if (!fp) {
        LOG_CUSTOM_ERROR("Temp read failed");
        return -1;
    }

    if (fscanf(fp, "%d", temp) != 1) {
        LOG_CUSTOM_ERROR("Temp parse failed");
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

// 状态转换函数
FanState get_next_state(FanState current, float temp) {
    switch (current) {
        case FAN_STATE_OFF:
            if (temp >= TEMP_OFF_MAX + HYSTERESIS) return FAN_STATE_LOW;
            break;
        case FAN_STATE_LOW:
            if (temp >= TEMP_LOW_MAX + HYSTERESIS) return FAN_STATE_MID;
            if (temp <= TEMP_OFF_MAX - HYSTERESIS) return FAN_STATE_OFF;
            break;
        case FAN_STATE_MID:
            if (temp >= TEMP_MID_MAX + HYSTERESIS) return FAN_STATE_HIGH;
            if (temp <= TEMP_LOW_MAX - HYSTERESIS) return FAN_STATE_LOW;
            break;
        case FAN_STATE_HIGH:
            if (temp <= TEMP_MID_MAX - HYSTERESIS) return FAN_STATE_MID;
            break;
    }
    return current;
}

int get_duty_by_state(FanState state) {
    switch (state) {
        case FAN_STATE_OFF: return DUTY_OFF;
        case FAN_STATE_LOW: return DUTY_LOW;
        case FAN_STATE_MID: return DUTY_MID;
        case FAN_STATE_HIGH: return DUTY_HIGH;
        default: return DUTY_OFF;
    }
}

int adjust_fan_speed(int *last_duty, float *smoothed_temp) {
    int temp_raw = 0;
    if (read_temperature(&temp_raw) != 0) return -1;

    float temp = smooth_temperature((float)temp_raw, smoothed_temp);
    FanState next = get_next_state(current_state, temp);

    if (next != current_state) {
        current_state = next;
        int new_duty = get_duty_by_state(current_state);
        set_duty_cycle(new_duty, last_duty);
        LOG_CUSTOM_INFO("State changed: temp=%.1f°C, state=%d", temp / 1000.0, current_state);
    }

    return 0;
}

int main() {
    openlog("fan-speed", LOG_PID, LOG_USER);
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    int last_duty = -1;
    int initial_temp = 0;
    if (read_temperature(&initial_temp)) {
        LOG_CUSTOM_ERROR("Initial temp read failed");
        return -1;
    }
    float smoothed_temp = (float)initial_temp;

    if (access(PWM_FAN_PATH, F_OK) != 0) {
        if (write_file(EXPORT_PATH, "1") < 0) {
            LOG_CUSTOM_ERROR("Export failed!");
            closelog();
            return -1;
        }
        sleep(1);
    }

    if (write_file(PERIOD_PATH, "10000") < 0 || write_file(POLARITY_PATH, "normal") < 0 || write_file(ENABLE_PATH, "1") < 0) {
        LOG_CUSTOM_ERROR("PWM init failed!");
        closelog();
        return -1;
    }

    set_duty_cycle(DUTY_LOW, &last_duty);
    LOG_CUSTOM_INFO("Service started");

    int tick = 0;
    while (running) {
        sleep(5);
        tick++;

        if (adjust_fan_speed(&last_duty, &smoothed_temp) != 0) continue;

        if (tick >= 180) {
            tick = 0;
            LOG_CUSTOM_INFO("Status: temp=%.1f°C, duty=%d", smoothed_temp / 1000.0, last_duty);
        }
    }

    write_file(ENABLE_PATH, "0");
    LOG_CUSTOM_INFO("Service stopped");
    closelog();
    return 0;
}
