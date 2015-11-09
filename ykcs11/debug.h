#ifndef DEBUG_H
#define DEBUG_H

#define D(x...) do {                                                           \
    fprintf (stderr, "debug: %s:%d (%s): ", __FILE__, __LINE__, __FUNCTION__); \
    fprintf (stderr, x);                                                       \
    fprintf (stderr, "\n");                                                    \
  } while (0)

#if YKCS11_DBG
#include <stdio.h>
#define DBG(x...) D(x);
#else
#define DBG(x...)
#endif

#if YKCS11_DINOUT
#define DIN D(("In"));
#define DOUT D(("Out"));
#else
#define DIN
#define DOUT
#endif

#endif
