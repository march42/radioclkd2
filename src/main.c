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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#if defined(ENABLE_SCHED)

    #include <sched.h>

#endif

#if defined(ENABLE_MLOCKALL)

    #include <sys/mman.h>
    #include <stdint.h>

#endif

#include "settings.h"
#include "logger.h"
#include "clock.h"
#include "serial.h"
#include "memory.h"

#if !defined(HAVE_STRCASECMP)
    #if defined(HAVE_STRICMP)
        #define strcasecmp(a,b) stricmp((a),(b))
    #else // !HAVE_STRICMP
        #define strcasecmp(a,b) strcmpi((a),(b))
    #endif // !HAVE_STRICMP
#endif // !HAVE_STRCASECMP

typedef struct {
    char* name;
    serLineT* serline;
    clkInfoT* clock;
} serClockT;

#define    MAX_CLOCKS        (16)
serClockT clocklist[MAX_CLOCKS];

void start_clocks(serDevT* serdev);

void set_real_time(void) {
    #ifdef ENABLE_SCHED
    struct sched_param schedp;
    #endif

    #ifdef ENABLE_SCHED
    /** Set realtime scheduling priority */
    memset(&schedp, 0, sizeof(schedp));
    schedp.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sched_setscheduler(0, SCHED_FIFO, &schedp) != 0) {
        loggerf(LOGGER_INFO, "Error, unable to set real time scheduling");
    }
    #else
    nice ( -20 );
    #endif

    #ifdef ENABLE_MLOCKALL
    /** Lock all memory pages */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        loggerf(LOGGER_INFO, "Error, unable to lock memory pages");
    }
    #endif

}

void set_daemon(void) {
    int pid;

    pid = fork();
    if (pid < 0) {
        loggerf(LOGGER_NOTE, "fork() failed\n");
        exit(1);
    }
    if (pid > 0) {
        // Parent
        exit(0);
    } else {
        // Child
        loggerf(LOGGER_INFO, "Entering daemon mode\n");
        if (setsid() < 0) {
            loggerf(LOGGER_NOTE, "setsid() failed\n");
        }

    }
}

void usage(void) {
    printf("Usage: radioclkd2 [ -s poll|"
           #ifdef ENABLE_TIOCMIWAIT
           "iwait|"
           #endif // ENABLE_TIOCMIWAIT
           #ifdef ENABLE_TIMEPPS
           "timepps|"
           #endif // ENABLE_TIMEPPS
           #ifdef ENABLE_GPIO
           "gpio|"
           #endif // ENABLE_GPIO
           #ifdef ENABLE_GPIO_CHARDEV
           "gpio_char"
           #endif // ENABLE_GPIO_CHARDEV
           " ] [ -t dcf77|msf|wwvb ] [ -d ] [ -v ] tty[:[-]line[:fudgeoffs]] ...\n"
           "   -s poll: poll the serial port 1000 times/sec (poor)\n"
           #ifdef ENABLE_TIOCMIWAIT
           "   -s iwait: wait for serial port interrupts (ok)\n"
           #else // !ENABLE_TIOCMIWAIT
           "  (iwait support is not available in this build)\n"
           #endif // !ENABLE_TIOCMIWAIT
           #ifdef ENABLE_TIMEPPS
           "   -s timepps: use the timepps interface (good)\n"
           #else // !ENABLE_TIMEPPS
           "  (timepps support is not available in this build)\n"
           #endif // !ENABLE_TIMEPPS
           #ifdef ENABLE_GPIO
           "   -s gpio: use /sys/class/gpio/gpioX/value for tty\n"
           "         setup \"edges\" to \"both\", uses poll() for GPIO pin interrupts\n"
           "         GPIO pulses are simulating DCD, so use :DCD and :-DCD for polarity\n"
           #else // !ENABLE_GPIO
           "  (GPIO support is not available in this build)\n"
           #endif // !ENABLE_GPIO
           #ifdef ENABLE_GPIO_CHARDEV
           "   -s gpio_char: use /dev/gpiochipX/Y for specifying the pin\n"
           "         setup \"edges\" to \"both\", uses poll() for GPIO pin interrupts\n"
           #else // !ENABLE_GPIO_CHARDEV
           "  (chardev GPIO support is not available in this build)\n"
           #endif // !ENABLE_GPIO_CHARDEV
           "   -t dcf77: 77.5KHz Germany/Europe DCF77 Radio Station (default)\n"
           "   -t msf: UK 60KHz MSF Radio Station\n"
           "   -t wwvb: US 60KHz WWVB Fort Collins Radio Station\n"
           "   -d: debug mode. runs in the foreground and print pulses\n"
           "   -v: verbose mode.\n"
           "   tty: serial port for clock\n"
           "   line: one of DCD, CTS, DSR or RNG - default is DCD\n"
           "   (-, if specified, treat signal as inverted)\n"
           "   fudgeoffs: fudge time, in seconds\n");

    exit(1);
}

int main(int argc, char** argv) {
    int serialmode;
    int shmunit;
    int clocktype = CLOCKTYPE_DCF77;
    char* arg;
    char* parm;
    serDevT* devfirst;
    serDevT* devnext;

    logger_set_file(stderr, LOGGER_DEBUG);
    logger_syslog(1, LOGGER_INFO);

    loggerf(LOGGER_INFO, "radioclkd2 version %s\n", CMAKE_PROJECT_VERSION);

    #if defined(ENABLE_TIMEPPS)
    serialmode = SERPORT_MODE_TIMEPPS;
    #elif defined(ENABLE_TIOCMIWAIT)
    serialmode = SERPORT_MODE_IWAIT;
    #else // !defined(ENABLE_TIOCMIWAIT)
    serialmode = SERPORT_MODE_POLL;
    #endif // !defined(ENABLE_TIOCMIWAIT)

    shmunit = 0;

    if (argc < 2) {
        loggerf(LOGGER_INFO, "Not enough arguments given!\n");
        usage();
    }

    //skip the program name
    argc--;
    argv++;

    while (argc > 0) {
        arg = argv[0];

        if (arg[0] == '-') {
            switch (arg[1]) {
                case 's': {
                    if (strlen(arg) > 2) {
                        parm = arg + 2;
                    } else {
                        argc--;
                        argv++;
                        parm = argv[0];
                    }

                    if (strcasecmp(parm, "poll") == 0) {
                        serialmode = SERPORT_MODE_POLL;
                    }
                            #ifdef ENABLE_TIMEPPS
                    else if (strcasecmp(parm, "timepps") == 0) {
                        serialmode = SERPORT_MODE_TIMEPPS;
                    }
                            #endif // ENABLE_TIMEPPS
                            #ifdef ENABLE_TIOCMIWAIT
                    else if (strcasecmp(parm, "iwait") == 0) {
                        serialmode = SERPORT_MODE_IWAIT;
                    }
                            #endif // ENABLE_TIOCMIWAIT
                            #ifdef ENABLE_GPIO
                    else if (strcasecmp(parm, "gpio") == 0) {
                        serialmode = SERPORT_MODE_GPIO;
                    }
                            #endif // ENABLE_GPIO
                            #ifdef ENABLE_GPIO_CHARDEV
                    else if (strcasecmp(parm, "gpio_char") == 0) {
                        serialmode = SERPORT_MODE_GPIO_CHAR;
                    }
                            #endif // ENABLE_GPIO_CHARDEV
                    else {
                        loggerf(LOGGER_INFO, "Not a valid input type!\n");
                        usage();
                    }
                    break;
                }
                case 't': {
                    if (strlen(arg) > 2) {
                        parm = arg + 2;
                    } else {
                        argc--;
                        argv++;
                        parm = argv[0];
                    }

                    if (strcasecmp(parm, "dcf77") == 0) {
                        clocktype = CLOCKTYPE_DCF77;
                    } else if (strcasecmp(parm, "msf") == 0) {
                        clocktype = CLOCKTYPE_MSF;
                    } else if (strcasecmp(parm, "wwvb") == 0) {
                        clocktype = CLOCKTYPE_WWVB;
                    } else {
                        loggerf(LOGGER_INFO, "Not a valid clock type!\n");
                        usage();
                    }
                    break;
                }
                case 'd': {
                    debugLevel++;
                    break;
                }
                case 'v': {
                    verboseLevel++;
                    break;
                }
                default: {
                    loggerf(LOGGER_INFO, "An invalid flag was given!\n");
                    usage();
                    break;
                }
            }
        } else {
            //arg = "tty[:[-]line]"
            char* fudgestr;
            serLineT* serline;
            clkInfoT* clock;

            int negate = 0;
            time_f fudgeoffset = 0.0;
            unsigned int line = TIOCM_CD;

            char* dev = safe_xstrcpy(arg, -1);
            char* linestr = strchr(dev, ':');
            if (linestr != NULL) {

                //tty:
                *linestr = 0;    //terminate dev at the colon
                linestr++;        //and move past it...

                fudgestr = strchr(linestr, ':');

                if (fudgestr != NULL) {
                    *fudgestr = 0;
                    fudgestr++;

                    fudgeoffset = atof(fudgestr);
                }

                if (*linestr == '-') {
                    negate = !negate;
                    linestr++;
                }

                if (strcasecmp(linestr, "cd") == 0 || strcasecmp(linestr, "dcd") == 0) {
                    line = TIOCM_CD;
                } else if (strcasecmp(linestr, "cts") == 0) {
                    line = TIOCM_CTS;
                } else if (strcasecmp(linestr, "dsr") == 0) {
                    line = TIOCM_DSR;
                } else if (strcasecmp(linestr, "rng") == 0) {
                    line = TIOCM_RNG;
                } else {
                    line = TIOCM_CD;
                    loggerf(LOGGER_NOTE, "Error: unknown serial port line '%s' - using DCD line instead\n", linestr);
                }

            }


            //right - we've got the serial port details - store them...

            serline = ser_add_line(dev, line, serialmode);
            if (serline == NULL) {
                loggerf(LOGGER_NOTE, "Error: failed to attach to serial line '%s'\n", arg);
            }

            clock = clk_create(negate, shmunit, fudgeoffset, clocktype);
            if (clock == NULL) {
                loggerf(LOGGER_NOTE, "Error: failed to create clock for serial line '%s'\n", arg);
            }

            if (clock != NULL && serline != NULL) {
                clocklist[shmunit].name = safe_xstrcpy(arg, -1);
                clocklist[shmunit].serline = serline;
                clocklist[shmunit].clock = clock;

                loggerf(LOGGER_INFO, "Added clock unit %d on line '%s'\n", shmunit, arg);
                shmunit++;
            }
        }

        argc--;
        argv++;
    }

    if (!debugLevel) {
        //non-debug mode - close stderr logging, fork, and set realtime priority

        logger_set_file(NULL, 0);
        switch (verboseLevel) {
            case 0: {
                logger_syslog(1, LOGGER_INFO);
                break;
            }
            case 1:
            default: {
                logger_syslog(1, LOGGER_DEBUG);
                break;
            }
        }
        set_daemon();
        set_real_time();
    } else {
        switch (verboseLevel) {
            case 0: {
                logger_set_file(stderr, LOGGER_DEBUG);
                break;
            }
            case 1:
            default: {
                logger_set_file(stderr, LOGGER_TRACE);
                break;
            }
        }
        logger_syslog(0, 0);
    }

    //right - we're ready to start...
    //now, due to the fact that we can only watch one serial port at a time, we'll have
    //to fork off for all but the first serial port
    devfirst = ser_get_dev(NULL);

    devnext = devfirst;

    //we don't want to do this if we're debugging clocks...
    if (!debugLevel) {
        while ((devnext = ser_get_dev(devnext)) != NULL) {
            int pid;

            pid = fork();

            if (pid < 0) {
                loggerf(LOGGER_INFO, "Fork() failed for other devices\n");
                //as we have forked above, this will kill all processes in the current process group (ie, just us)
                kill(0, SIGTERM);
            } else if (pid == 0) {
                //child
                start_clocks(devnext);

                loggerf(LOGGER_INFO, "Child process terminated\n");
                exit(0);
            }
            //else parent - continue on
        }
    } else {
        if (ser_get_dev(devnext) != NULL) {
            loggerf(LOGGER_INFO, "Additional serial lines ignored in debug mode\n");
        }
    }

    start_clocks(devfirst);

    loggerf(LOGGER_INFO, "Parent process has been terminated\n");
    exit(1);
}

void start_clocks(serDevT* serdev) {
    serLineT* serline;
    if (serdev == NULL || strlen(serdev->dev) <= 0) {
        loggerf(LOGGER_INFO, "The clock input device doesn't exist\n");
        return;
    }

    loggerf(LOGGER_INFO, "Pid %d for device %s\n", getpid(), serdev->dev);

    if (ser_init_hardware(serdev) < 0) {
        loggerf(LOGGER_INFO, "Error initialising serial device\n");
        return;
    }

    while (1) {
        int return_code = ser_wait_for_serial_change(serdev);
        if (return_code == 0) {
            loggerf(LOGGER_DEBUG, "No serial line change\n");
            continue;
        } else if (return_code < 0) {
            loggerf(LOGGER_DEBUG, "Error waiting for change\n");
            return;
        }

        ser_update_lines_for_device(serdev);

        serline = NULL;
        while ((serline = ser_get_line(serline)) != NULL) {
            for (uint_fast8_t c = 0; c < MAX_CLOCKS; c++) {
                if ((serline->dev == serdev) && (clocklist[c].serline == serline)) {
                    clk_process_status_change(clocklist[c].clock, serline->curstate, serline->eventtime);
                }
            }
        }
    }
}
