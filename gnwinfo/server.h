#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

void gnwinfo_server_init(void);
void gnwinfo_server_fini(void);
void gnwinfo_server_broadcast(const char* message);

#ifdef __cplusplus
}
#endif
