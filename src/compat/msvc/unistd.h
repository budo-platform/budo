#ifndef BUDO_COMPAT_UNISTD_H
#define BUDO_COMPAT_UNISTD_H

#include <direct.h>
#include <io.h>
#include <process.h>

#ifndef F_OK
#define F_OK 0
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef R_OK
#define R_OK 4
#endif

#endif