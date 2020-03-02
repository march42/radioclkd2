/*
 * Copyright (c) 2002 Jon Atkins http://www.jonatkins.com/
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "systime.h"
#include <sys/ioctl.h>
#include <stdio.h>
#include <signal.h>
#include <sys/errno.h>

#ifdef ENABLE_GPIO

    #include <poll.h>

#endif // ENABLE_GPIO

#ifdef ENABLE_GPIO_CHARDEV

    #include <gpiod.h>

#endif // ENABLE_GPIO_CHARDEV

#ifdef ENABLE_TIMEPPS

    #include <sys/timepps.h>
    #include <stdint.h>

#endif // ENABLE_TIMEPPS

#include "logger.h"

#include "serial.h"
#include "memory.h"

static serDevT* serDevHead;
static serLineT* serLineHead;

/*int ser_init(void) {
    serDevHead = NULL;
    serLineHead = NULL;

    return 0;
}*/

serLineT* ser_add_line(char* dev, unsigned int line, int mode) {
    char fulldev[MAX_DEVICE_NAME_LENGTH];
    serDevT* serdev;
    serLineT* serline;

    //allow for either full paths or /dev relative paths...
    if (dev[0] == '/') {
        if (strlen(dev) >= MAX_DEVICE_NAME_LENGTH) {
            loggerf(LOGGER_NOTE, "ser_add_line(): dev too long\n");
            return NULL;
        }
        strcpy(fulldev, dev);
    } else {
        strcpy(fulldev, "/dev/");
        if (strlen(dev) + strlen(fulldev) >= MAX_DEVICE_NAME_LENGTH) {
            loggerf(LOGGER_NOTE, "ser_add_line(): dev too long\n");
            return NULL;
        }
        strcat(fulldev, dev);
    }

    //make sure only one line bit is set...
    if (line & (line - 1)) {
        loggerf(LOGGER_NOTE, "ser_add_line(): more than one line bit set\n");
        return NULL;
    }

    //try and find an existing device in the list...
    if (serDevHead == NULL) {
        serDevHead = safe_mallocz(sizeof(serDevT));
    }

    serdev = serDevHead;
    while (serdev != NULL) {
        if (strcmp(serdev->dev, fulldev) == 0) {
            break;
        }
        serdev = serdev->next;
    }
    if (serdev == NULL) {
        //no existing device - create a new one..
        serdev = safe_mallocz(sizeof(serDevT));
        serdev->next = NULL; // There isn't a next one in the list if serDevHead is NULL
        serDevHead = serdev;

        strcpy(serdev->dev, fulldev);
        serdev->mode = mode;
        serdev->modemlines = 0;
        serdev->fd = -1;
        if (serdev->mode == SERPORT_MODE_GPIO_CHAR) {
            char* tmp = malloc(strlen(serdev->dev) + 1);
            strcpy(tmp, serdev->dev);
            char* subs = strtok(tmp, "/");
            while (subs != NULL) {
                if (subs[0] == 'd' &&
                    subs[1] == 'e' &&
                    subs[2] == 'v') {
                    // This is the /dev prefix, ignore
                } else if (subs[0] == 'g' &&
                           subs[1] == 'p' &&
                           subs[2] == 'i' &&
                           subs[3] == 'o' &&
                           subs[4] == 'c' &&
                           subs[5] == 'h') {
                    // This is the gpiochip part
                    serdev->chipname = safe_mallocz(strlen(subs) + 1);
                    strcpy(serdev->chipname, subs);
                    loggerf(LOGGER_INFO, "Using chip \"%s\"\n", subs);
                } else if (isdigit(subs[0])) {
                    serdev->pin_number = atoi(subs);
                    loggerf(LOGGER_INFO, "Using pin \"%s\"\n", subs);
                } else {
                    loggerf(LOGGER_INFO, "Can't parse this format\n");
                    return NULL;
                }
                subs = strtok(NULL, "/");
            }

            if (serdev->chipname == NULL) {
                loggerf(LOGGER_INFO, "Gpiochip was not provided or parsed from the path\n");
                return NULL;
            }
        }
    }

    //make sure we're using it in the same mode...
    if (serdev->mode != mode) {
        loggerf(LOGGER_NOTE, "ser_add_line(): cannot add line for same device with different mode\n");
        return NULL;
    }


    //make sure we're not already monitoring this line...
    if (serdev->modemlines & line) {
        loggerf(LOGGER_NOTE, "ser_add_line(): cannot add modem status line more than once\n");
        return NULL;
    }

    //ok - we've got a valid device/line/mode combo...
    serdev->modemlines |= line;

    //create a new line entry...
    serline = safe_mallocz(sizeof(serLineT));
    serline->next = serLineHead;
    serLineHead = serline;

    serline->dev = serdev;
    serline->line = line;

    return serline;

}

serDevT* ser_get_dev(serDevT* prev) {
    if (prev == NULL) {
        if (serDevHead == NULL) {
            serDevHead = safe_mallocz(sizeof(serDevT));
        }
        return serDevHead;
    }

    return prev->next;
}

serLineT* ser_get_line(serLineT* prev) {
    if (prev == NULL) {
        return serLineHead;
    }

    return prev->next;
}

int ser_open_dev(serDevT* dev) {
    #ifdef ENABLE_TIMEPPS
    pps_params_t ppsparams;
    int ppsmode;
    #endif

    if (dev->mode != SERPORT_MODE_GPIO_CHAR) {
        dev->fd = open(dev->dev, O_RDONLY | O_NOCTTY);
    }

    switch (dev->mode) {
        #ifdef ENABLE_TIMEPPS
        case SERPORT_MODE_TIMEPPS: {
            if (time_pps_create(dev->fd, &dev->ppshandle) == -1) {
                return -1;
            }

            if (time_pps_getparams(dev->ppshandle, &ppsparams) == -1) {
                return -1;
            }

            ppsparams.mode |= PPS_TSFMT_TSPEC | PPS_CAPTUREBOTH;

            if (time_pps_setparams(dev->ppshandle, &ppsparams) == -1) {
                return -1;
            }

            if (time_pps_getcap(dev->ppshandle, &ppsmode) == -1) {
                return -1;
            }

            //NOTE: these should probably be error cases, but the code is still experimental and
            //the PPS support I've used also seems to have problems
            if (!(ppsmode | PPS_CAPTUREASSERT)) {
                loggerf(LOGGER_NOTE, "Warning: PPS_CAPTUREASSERT not supported\n");
            }
            if (!(ppsmode | PPS_CAPTURECLEAR)) {
                loggerf(LOGGER_NOTE, "Warning: PPS_CAPTURECLEAR not supported\n");
            }

            //!!! no point using this - too many problems
            //1. linux doesn't report PPS_CANWAIT, but can
            //2. FreeBSD does report PPS_CANWAIT, but can't
            //(unless its me reading the docs backwards?)
            //		if ( ! (ppsmode | PPS_CANWAIT) )
            //			loggerf ( LOGGER_NOTE, "Warning: PPS_CANWAIT not supported (linux lies)\n" );
            break;
        }
            #endif
            #if defined(ENABLE_GPIO_CHARDEV)
        case SERPORT_MODE_GPIO_CHAR: {
            dev->gpiod_chip = gpiod_chip_open_by_name(dev->chipname);
            if (!dev->gpiod_chip) {
                loggerf(LOGGER_INFO,"Opening chip failed\n");
                return -1;
            }

            dev->gpiod_line = gpiod_chip_get_line(dev->gpiod_chip, dev->pin_number);
            if (!dev->gpiod_line) {
                loggerf(LOGGER_INFO,"Get line failed\n");
                return -1;
            }

            if (gpiod_line_request_both_edges_events_flags(dev->gpiod_line, "Consumer", GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW) < 0) {
                loggerf(LOGGER_INFO,"Request event notification failed\n");
                return -1;
            }

            dev->fd = 1; // So that dumb checks will succeed, nothing should be using this _really_ // TODO: Improve
            break;
        }
            #endif
    }

    return dev->fd;
}

int ser_init_hardware(serDevT* dev) {
    if (dev->fd < 0) {
        ser_open_dev(dev);
    }

    if (dev->fd < 0) {
        return -1;
    }

    //add code to power on the device (set DTR high - maybe other options..?)

    //wait for the device to power up...
    sleep(3);
    return 0;
}


//static jmp_buf alrmjump;

#ifdef ENABLE_TIOCMIWAIT
static void sigalrm(int _ignore) {
    //empty func - just used so that ioctl() aborts on an alarm()
}
#endif

#if defined(ENABLE_GPIO_CHARDEV)
struct timespec timeout_length = {10, 0};
#endif

int ser_wait_for_serial_change(serDevT* dev) {
    struct timeval tv;
    time_f timef;
    int i, ret;
    #ifdef ENABLE_TIMEPPS
    struct timespec timeout;
    pps_info_t ppsinfo;
    int ppslines;
    #endif
    #ifdef ENABLE_GPIO
    /** Array of polled file pointers */
    struct pollfd pollfds[1];
    #endif

    if (dev->modemlines == 0) {
        return -1;
    }

    switch (dev->mode) {
        case SERPORT_MODE_POLL: {
            for (i = 0; i < 10 * 1000; i++) {
                gettimeofday(&tv, NULL);
                timeval2time_f (&tv, timef);
                ret = ser_get_dev_status_lines(dev, timef);
                if (ret < 0) {
                    return -1;
                }
                if (ret == 1) {
                    return 0;
                }

                usleep(1000);
            }

            //timeout
            return -1;
        }
            #ifdef ENABLE_GPIO
        case SERPORT_MODE_GPIO: {
            pollfds[0].fd = dev->fd;
            pollfds[0].events = POLLERR;

            i = poll(pollfds, 1, 10000); /* timeout 10 seconds */
            if (i != 1 && !(pollfds[0].revents & POLLERR)) {
                return -1;
            }

            gettimeofday(&tv, NULL);
            timeval2time_f (&tv, timef);

            if (ser_get_dev_status_lines(dev, timef) < 0) {
                return -1;
            }

            return 0;
        }
            #endif // ENABLE_GPIO
            #ifdef ENABLE_GPIO_CHARDEV
        case SERPORT_MODE_GPIO_CHAR: {
            // Register interrupt
            pollfds[0].fd = dev->fd;
            pollfds[0].events = POLLERR;
            if (dev->gpiod_line == NULL) {
                loggerf(LOGGER_NOTE, "GPIOD: No line to wait for event\n");
                return -1;
            }

            uint_fast8_t return_code = gpiod_line_event_wait(dev->gpiod_line, &timeout_length);
            if (return_code < 0) {
                loggerf(LOGGER_NOTE, "GPIOD: Event notification failed (err: %d)\n", return_code);
                return -1;
            } else if (return_code == 0) {
                loggerf(LOGGER_NOTE, "GPIOD: Timeout (err: 0)\n");
                return -1;
            }

            // The timef is incorrect right now, it must be read from the received event itself
            if (ser_get_dev_status_lines(dev, timef) < 0) {
                return -1;
            }

            return 0;
        }
            #endif // ENABLE_GPIO_CHARDEV
            #ifdef ENABLE_TIOCMIWAIT
        case SERPORT_MODE_IWAIT: {
            signal(SIGALRM, sigalrm);
            alarm(10);

            if (ioctl(dev->fd, TIOCMIWAIT, dev->modemlines) != 0) {
                return -1;
            }
            gettimeofday(&tv, NULL);
            timeval2time_f (&tv, timef);

            signal(SIGALRM, SIG_DFL);
            alarm(0);

            if (ser_get_dev_status_lines(dev, timef) < 0) {
                return -1;
            }

            return 0;
        }
            #endif // ENABLE_TIOCMIWAIT
            #ifdef ENABLE_TIMEPPS
        case SERPORT_MODE_TIMEPPS: {
            timeout.tv_sec = 0;
            timeout.tv_nsec = 0;

            for (i = 0; i < 10 * 100; i++) {
                if (time_pps_fetch(dev->ppshandle, PPS_TSFMT_TSPEC, &ppsinfo, &timeout) == -1) {
                    loggerf(LOGGER_NOTE, "ppsfetch failed: %d\n", errno);
                    return -1;
                }

                if (ppsinfo.assert_sequence != dev->ppslastassert) {
                    timespec2time_f(&ppsinfo.assert_timestamp, timef);
                    ppslines = TIOCM_CD;    //NOTE: assuming that pps support is on the DCD line
                    dev->ppslastassert = ppsinfo.assert_sequence;

                    if (ser_store_dev_status_lines(dev, ppslines, timef) < 0) {
                        return -1;
                    }
                    return 0;
                } else if (ppsinfo.clear_sequence != dev->ppslastclear) {
                    timespec2time_f(&ppsinfo.clear_timestamp, timef);
                    ppslines = 0;
                    dev->ppslastclear = ppsinfo.clear_sequence;

                    if (ser_store_dev_status_lines(dev, ppslines, timef) < 0) {
                        return -1;
                    }
                    return 0;
                }
                usleep(10000);
            }
            return -1;
        }
            #endif // ENABLE_TIMEPPS
    }

    loggerf(LOGGER_NOTE, "Error: ser_wait_for_serial_change(): mode not supported\n");

    //unknown serial port mode !!
    return -1;
}

int ser_get_dev_status_lines(serDevT* dev, time_f timef) {
    int lines;

    #ifdef ENABLE_GPIO
    char buf[8];
    if (dev->mode == SERPORT_MODE_GPIO) {
        if (lseek(dev->fd, SEEK_SET, 0) == -1) {
            return -1;
        }
        if (read(dev->fd, buf, sizeof(buf)) <= 0) {
            return -1;
        }

        if (buf[0] == '1') {
            lines = TIOCM_CD;
        } else {
            lines = 0;
        }

        return ser_store_dev_status_lines(dev, lines, timef);
    }
    #endif // ENABLE_GPIO
    #if defined(ENABLE_GPIO_CHARDEV)
        #if !defined(ENABLE_GPIO) // When GPIO is not enabled we still need the buffer
    char buf[8];
        #endif // !defined(ENABLE_GPIO)
    if (dev->mode == SERPORT_MODE_GPIO_CHAR) {
        // Read the GPIO pin
        uint_fast8_t return_code = gpiod_line_event_read(dev->gpiod_line, dev->event);
        loggerf(LOGGER_NOTE, "Get event notification on line #%u\n", dev->pin_number);
        if (return_code < 0) {
            loggerf(LOGGER_NOTE, "Read last event notification failed\n");
            return -1;
        }

        if (dev->event == NULL) {
            loggerf(LOGGER_INFO, "Event was NULL\n");
            return -1;
        }

        if (dev->event->event_type == GPIOD_LINE_EVENT_FALLING_EDGE) {
            lines = 0;
        } else if (dev->event->event_type == GPIOD_LINE_EVENT_RISING_EDGE) {
            lines = TIOCM_CD;
        }

        timespec_struct_to_time_f(dev->event->ts, timef);
        return ser_store_dev_status_lines(dev, lines, timef);
    }
    #endif // defined(ENABLE_GPIO_CHARDEV)

    if (ioctl(dev->fd, TIOCMGET, &lines) != 0) {
        return -1;
    }

    return ser_store_dev_status_lines(dev, lines, timef);
}

int ser_store_dev_status_lines(serDevT* dev, int lines, time_f timef) {
    time_f diff = timef - dev->eventtime;
    if (diff < 0.05) {
        loggerf(LOGGER_DEBUG, "ser_store_dev_status_lines: pulse too short %f, filtering!\n", diff);
        return 0;
    }
    if (lines != dev->curlines) {
        dev->prevlines = dev->curlines;
        dev->curlines = lines;
        dev->eventtime = timef;

        return 1;
    }

    return 0;
}

int ser_update_lines_for_device(serDevT* dev) {
    serLineT* line;

    for (line = ser_get_line(NULL); line != NULL; line = ser_get_line(line)) {
        //skip lines not on this device...
        if (line->dev != dev) {
            continue;
        }

        if ((dev->curlines & line->line) != (dev->prevlines & line->line)) {
            line->curstate = dev->curlines & line->line;
            line->eventtime = dev->eventtime;
        }
    }

    return 0;
}
