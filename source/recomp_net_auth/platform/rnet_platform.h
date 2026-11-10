/* The one platform call rnet_auth.c makes, for the launcher build (MIT, see ../LICENSE).
 * The full recomp-net platform layer is not needed here. */
#ifndef RNET_PLATFORM_H
#define RNET_PLATFORM_H
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
static inline void rnet_os_startup(void){static int started;WSADATA wsa;if(!started&&!WSAStartup(MAKEWORD(2,2),&wsa))started=1;}
#endif
