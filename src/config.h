#ifndef CONFIG_H_
#define CONFIG_H_

#if defined(HAVE_DECL_TIOCMIWAIT) && defined(HAVE_ALARM)
// ioctl(serialfd,TIOCMIWAIT,..) waits for a serial interrupt
// alarm() is used as a timeout on the above
    #define ENABLE_TIOCMIWAIT
#endif // defined(HAVE_DECL_TIOCMIWAIT) && defined(HAVE_ALARM)

#if defined(HAVE_SYS_TIMEPPS_H)
// if <sys/timepps.h> is available, enable pps code
    #define ENABLE_TIMEPPS
#endif // defined(HAVE_SYS_TIMEPPS_H)

#if defined(HAVE_MLOCKALL) && defined(HAVE_SYS_MMAN_H)
    #define ENABLE_MLOCKALL
#endif // defined(HAVE_MLOCKALL) && defined(HAVE_SYS_MMAN_H)

#if defined(HAVE_SCHED_H) && defined(HAVE_SCHED_GET_PRIORITY_LEVEL) && defined(HAVE_SCHED_SETSCHEDULER)
    #define ENABLE_SCHED
#endif // defined(HAVE_SCHED_H) && defined(HAVE_SCHED_GET_PRIORITY_LEVEL) && defined(HAVE_SCHED_SETSCHEDULER)

#endif // CONFIG_H_
