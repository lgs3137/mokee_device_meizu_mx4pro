/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2014 The  Linux Foundation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


// #define LOG_NDEBUG 0

#include <cutils/log.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <hardware/lights.h>

/******************************************************************************/

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

char const *const LCD_FILE
        = "/sys/class/backlight/lm3697_bl/brightness";
char const *const LCD_FILE_MAX
        = "/sys/class/backlight/lm3697_bl/max_brightness";
char const *const LED_FILE
        = "/sys/class/leds/mx7-led/brightness";

/**
 * device methods
 */

void init_globals(void)
{
    // init the mutex
    pthread_mutex_init(&g_lock, NULL);
}

static int
write_int(char const* path, int value)
{
    int fd;
    static int already_warned = 0;

    fd = open(path, O_RDWR);
    if (fd >= 0) {
        char buffer[20];
        int bytes = sprintf(buffer, "%d\n", value);
        ssize_t amt = write(fd, buffer, (size_t)bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            ALOGE("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}

static int
is_lit(struct light_state_t const* state)
{
    return state->color & 0x00ffffff;
}

static int
rgb_to_brightness(struct light_state_t const* state)
{
    int color = state->color & 0x00ffffff;
    return ((77*((color>>16)&0x00ff))
            + (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
}

static int
get_max_brightness_lcd() {
    char value[6];
    int fd, len, max_brightness;

    if ((fd = open(LCD_FILE_MAX, O_RDONLY)) < 0) {
        ALOGE("[%s]: Could not open max brightness file %s: %s", __FUNCTION__,
                     LCD_FILE_MAX, strerror(errno));
        ALOGE("[%s]: Assume max brightness 255", __FUNCTION__);
        return 255;
    }

    if ((len = read(fd, value, sizeof(value))) <= 1) {
        ALOGE("[%s]: Could not read max brightness file %s: %s", __FUNCTION__,
                     LCD_FILE_MAX, strerror(errno));
        ALOGE("[%s]: Assume max brightness 255", __FUNCTION__);
        close(fd);
        return 255;
    }

    max_brightness = strtol(value, NULL, 10);
    close(fd);

    return (unsigned int) max_brightness;
}

static int
set_light_backlight(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    int brightness = rgb_to_brightness(state);
    int max_brightness = get_max_brightness_lcd();
    if(!dev) {
        return -1;
    }
    pthread_mutex_lock(&g_lock);
    err = write_int(LCD_FILE, max_brightness * brightness / 255);
    pthread_mutex_unlock(&g_lock);
    return err;
}

static int set_light_notifications(struct light_device_t* dev,
			struct light_state_t const* state)
{
    int brightness = rgb_to_brightness(state);
    int v = 0;
    int ret = 0;
    pthread_mutex_lock(&g_lock);

    if (brightness+state->color == 0 || brightness > 100) {
        if (state->color & 0x00ffffff)
	    v = 767;
        } else
            v = 256;

    ret = write_int(LED_FILE, v);
    pthread_mutex_unlock(&g_lock);
    return ret;
}

/** Close the lights device */
static int
close_lights(struct light_device_t *dev)
{
    if (dev) {
        free(dev);
    }
    return 0;
}


/******************************************************************************/

/**
 * module methods
 */

/** Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t* module, char const* name,
        struct hw_device_t** device)
{
    int (*set_light)(struct light_device_t* dev,
            struct light_state_t const* state);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name))
        set_light = set_light_backlight;
    else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name))
        set_light = set_light_notifications;
    else
        return -EINVAL;

    pthread_once(&g_init, init_globals);

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));
    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->common.close = (int (*)(struct hw_device_t*))close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t*)dev;
    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

/*
 * The lights Module
 */
struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "M76 lights module",
    .author = "Tatsuyuki Ishi",
    .methods = &lights_module_methods,
};
