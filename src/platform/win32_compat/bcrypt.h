#ifndef XBOXRECOMP_BCRYPT_COMPAT_H
#define XBOXRECOMP_BCRYPT_COMPAT_H

#ifdef _WIN32
#include_next <bcrypt.h>
#else
typedef void *BCRYPT_ALG_HANDLE;
typedef void *BCRYPT_HASH_HANDLE;
#endif

#endif
