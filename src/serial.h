#ifndef SERIAL_H_
#define SERIAL_H_

#include "systime.h"
#include <sys/ioctl.h>

#include "timef.h"

#ifdef ENABLE_TIMEPPS

    #include <sys/timepps.h>

#endif


//a serial device (serDevT) is an individual serial port, with several status control lines
//a modem status line (serLineT) is an individual status line on a serial port

typedef struct serDevS serDevT;
typedef struct serLineS serLineT;

#define MAX_DEVICE_NAME_LENGTH 128

struct serDevS {
    serDevT* next;

    //full device name (including "/dev/")
    char dev[MAX_DEVICE_NAME_LENGTH];

    #define    SERPORT_MODE_IWAIT      (1)
    #define    SERPORT_MODE_POLL       (2)
    #define    SERPORT_MODE_TIMEPPS    (3)
    #define    SERPORT_MODE_GPIO       (4)
    #define    SERPORT_MODE_GPIO_CHAR  (5)
    int mode;

    /** Which modem status lines to check - some of TIOCM_{RNG|DSR|CD|CTS} */
    int modemlines;

    //-- runtime data

    /** Once opened, the fd for this device */
    int fd;
    #ifdef ENABLE_TIMEPPS
    pps_handle_t ppshandle;
    int ppslastassert;
    int ppslastclear;
    #endif // ENABLE_TIMEPPS

    #ifdef ENABLE_GPIO_CHARDEV
    char* chipname;
    unsigned int pin_number;    // GPIO Pin #2
    struct gpiod_line_event* event;
    struct gpiod_chip* gpiod_chip;
    struct gpiod_line* line;
    #endif

    /** the current and previous modem lines active - some of modemlines */
    int curlines;
    int prevlines;
    time_f eventtime;

};

struct serLineS {
    serLineT* next;

    //one of TIOCM_{RNG|DSR|CD|CTS}
    int line;
    serDevT* dev;

    int curstate;
    time_f eventtime;

};

//int ser_init(void);

serLineT* ser_add_line(char* dev, int line, int mode);

//pass in NULL to get the first dev/line
//pass in dev/line to get next dev/line
//returns NULL at end of dev/line list
serDevT* ser_get_dev(serDevT* prev);

serLineT* ser_get_line(serLineT* prev);

int ser_init_hardware(serDevT* dev);

int ser_wait_for_serial_change(serDevT* dev);

int ser_get_dev_status_lines(serDevT* dev, time_f timef);

int ser_store_dev_status_lines(serDevT* dev, int lines, time_f timef);

int ser_update_lines_for_device(serDevT* dev);

#endif
