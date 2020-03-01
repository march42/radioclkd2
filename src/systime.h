#ifndef SYSTIME_H_
#define SYSTIME_H_

#include "config.h"

#if defined(TIME_WITH_SYS_TIME)
    #include <sys/time.h>
    #include <time.h>
#else // !defined(TIME_WITH_SYS_TIME)
    #if defined(HAVE_SYS_TIME_H)
        #include <sys/time.h>
    #else // !defined(HAVE_SYS_TIME)
        #include <time.h>
    #endif // !defined(HAVE_SYS_TIME)
#endif // !defined(TIME_WITH_SYS_TIME)

#endif // SYSTIME_H_
