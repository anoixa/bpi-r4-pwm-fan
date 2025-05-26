#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

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
        LOG_CUSTOM_INFO("Set duty cycle to %d", duty);
    } else {
        LOG_CUSTOM_ERROR("Failed to set duty cycle to %d", duty);
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
        LOG_CUSTOM_ERROR("Failed to read temperature file");
        return -1;
    }
    if (fscanf(fp, "%d", temp) != 1) {
        LOG_CUSTOM_ERROR("Failed to parse temperature value");
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

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

int adjust_fan_speed(int *last_duty, float *smoothed_temp, float *last_temp) {
    int temp_raw = 0;
    if (read_temperature(&temp_raw) != 0) return -1;

    float temp = smooth_temperature((float)temp_raw, smoothed_temp);
    float slope = (*smoothed_temp - *last_temp) / 5.0f;
    *last_temp = *smoothed_temp;

    FanState next_state = current_state;

    if (temp < TEMP_OFF_MAX) {
        next_state = FAN_STATE_OFF;
    } else if (slope > 500) {
        LOG_CUSTOM_INFO("Temperature rising rapidly, boosting to high speed");
        next_state = FAN_STATE_HIGH;
    } else {
        next_state = get_next_state(current_state, temp);
    }

    if (next_state != current_state) {
        current_state = next_state;
        int new_duty = get_duty_by_state(current_state);
        set_duty_cycle(new_duty, last_duty);
        LOG_CUSTOM_INFO("State changed: temp=%.1f°C, state=%d", temp / 1000.0, current_state);
    }

    return 0;
}

int init_pwm() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", PWM_PERIOD);

    // 导出PWM
    if (access(PWM_FAN_PATH, F_OK) != 0) {
        if (write_file(EXPORT_PATH, "1") < 0) {
            LOG_CUSTOM_ERROR("Failed to export PWM interface");
            return -1;
        }
        sleep(1);
    }

    // 设置周期
    if (write_file(PERIOD_PATH, buf) < 0) {
        LOG_CUSTOM_ERROR("Failed to set PWM period");
        return -1;
    }

    // 设置极性
    if (write_file(POLARITY_PATH, PWM_POLARITY) < 0) {
        LOG_CUSTOM_ERROR("Failed to set PWM polarity");
        return -1;
    }

    // 启用PWM
    if (write_file(ENABLE_PATH, "1") < 0) {
        LOG_CUSTOM_ERROR("Failed to enable PWM output");
        return -1;
    }

    return 0;
}

void cleanup_pwm() {
    write_file(ENABLE_PATH, "0");
    LOG_CUSTOM_INFO("PWM output disabled");
}

int main() {
    openlog("fan-speed", LOG_PID, LOG_USER);
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    int last_duty = -1;
    int initial_temp = 0;
    if (read_temperature(&initial_temp)) {
        LOG_CUSTOM_ERROR("Failed to read initial temperature");
        closelog();
        return -1;
    }

    float smoothed_temp = (float)initial_temp;
    float last_temp = smoothed_temp;

    if (init_pwm() != 0) {
        closelog();
        return -1;
    }

    set_duty_cycle(DUTY_LOW, &last_duty);
    LOG_CUSTOM_INFO("Fan control service started");

    int tick = 0;
    while (running) {
        sleep(5);
        tick++;

        if (adjust_fan_speed(&last_duty, &smoothed_temp, &last_temp) != 0) continue;

        if (tick >= 180) {
            tick = 0;
            LOG_CUSTOM_INFO("Status: temp=%.1f°C, duty=%d", smoothed_temp / 1000.0, last_duty);
        }
    }

    cleanup_pwm();
    LOG_CUSTOM_INFO("Fan control service stopped");
    closelog();
    return 0;
}
