#ifndef SE_INCL_SOCKETCOMPAT_H
#define SE_INCL_SOCKETCOMPAT_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

// Sockets for the network code (included after StdH.h by the network headers that hold sockets
// and by the sources that use them directly).
// On Windows Winsock comes from StdH.h (winsock2.h) and this header adds nothing but socklen_t.
// Elsewhere the code keeps its Winsock spelling (SOCKET, closesocket, WSAGetLastError,
// WSAE* codes...) over BSD sockets. What really differs is handled where the sockets are used:
// select() takes the highest descriptor + 1 (Winsock ignores it), a non-blocking connect()
// reports EINPROGRESS and a failed one is reported as writable with SO_ERROR set (Winsock: the
// except set), and the option levels are checked (TcpIpConnection.cpp).

#ifdef PLATFORM_WIN32

// the size argument of getsockname/getpeername/accept/recvfrom (int in Winsock; ws2tcpip.h
// declares the same typedef)
typedef int socklen_t;

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

typedef int SOCKET;
#define INVALID_SOCKET  (-1)
#define SOCKET_ERROR    (-1)

typedef struct hostent     HOSTENT;
typedef struct sockaddr    SOCKADDR;
typedef struct sockaddr   *LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;

#define closesocket(s)     close(s)
#define WSAGetLastError()  (errno)

// No library to start: WSAStartup only succeeds. Winsock never raises signals, but a send() on a
// connection the peer closed raises SIGPIPE here, which ends the process. The game only sends
// after a WSAStartup (Winsock needs one), so the signal is ignored from there on and send() just
// fails (EPIPE), as it fails with an error code on Windows.
typedef struct WSAData { WORD wVersion; WORD wHighVersion; } WSADATA;
inline int WSAStartup(WORD wVersionRequested, WSADATA *pwsaData)
{
  signal(SIGPIPE, SIG_IGN);
  if (pwsaData != NULL) {
    pwsaData->wVersion = pwsaData->wHighVersion = wVersionRequested;
  }
  return 0;
}
inline int WSACleanup(void) { return 0; }

// only FIONBIO (non-blocking mode) is used; the argument is a ULONG or an unsigned long
template<class Type>
inline int ioctlsocket(SOCKET s, unsigned long ulCommand, Type *pArgument)
{
  if (ulCommand == (unsigned long)FIONBIO) {
    const int iFlags = fcntl(s, F_GETFL, 0);
    if (iFlags == -1) {
      return SOCKET_ERROR;
    }
    const int iNewFlags = (*pArgument != 0) ? (iFlags | O_NONBLOCK) : (iFlags & ~O_NONBLOCK);
    return (fcntl(s, F_SETFL, iNewFlags) == -1) ? SOCKET_ERROR : 0;
  }
  int iArgument = (int)*pArgument;
  const int iResult = ioctl(s, ulCommand, &iArgument);
  *pArgument = (Type)iArgument;
  return iResult;
}

// Winsock error codes as their errno counterparts (the ones that only exist in Winsock keep
// their Windows values, which errno never takes)
#define WSAEINTR            EINTR
#define WSAEBADF            EBADF
#define WSAEACCES           EACCES
#define WSAEFAULT           EFAULT
#define WSAEINVAL           EINVAL
#define WSAEMFILE           EMFILE
#define WSAEWOULDBLOCK      EWOULDBLOCK
#define WSAEINPROGRESS      EINPROGRESS
#define WSAEALREADY         EALREADY
#define WSAENOTSOCK         ENOTSOCK
#define WSAEDESTADDRREQ     EDESTADDRREQ
#define WSAEMSGSIZE         EMSGSIZE
#define WSAEPROTOTYPE       EPROTOTYPE
#define WSAENOPROTOOPT      ENOPROTOOPT
#define WSAEPROTONOSUPPORT  EPROTONOSUPPORT
#define WSAESOCKTNOSUPPORT  ESOCKTNOSUPPORT
#define WSAEOPNOTSUPP       EOPNOTSUPP
#define WSAEPFNOSUPPORT     EPFNOSUPPORT
#define WSAEAFNOSUPPORT     EAFNOSUPPORT
#define WSAEADDRINUSE       EADDRINUSE
#define WSAEADDRNOTAVAIL    EADDRNOTAVAIL
#define WSAENETDOWN         ENETDOWN
#define WSAENETUNREACH      ENETUNREACH
#define WSAENETRESET        ENETRESET
#define WSAECONNABORTED     ECONNABORTED
#define WSAECONNRESET       ECONNRESET
#define WSAENOBUFS          ENOBUFS
#define WSAEISCONN          EISCONN
#define WSAENOTCONN         ENOTCONN
#define WSAESHUTDOWN        ESHUTDOWN
#define WSAETOOMANYREFS     ETOOMANYREFS
#define WSAETIMEDOUT        ETIMEDOUT
#define WSAECONNREFUSED     ECONNREFUSED
#define WSAELOOP            ELOOP
#define WSAENAMETOOLONG     ENAMETOOLONG
#define WSAEHOSTDOWN        EHOSTDOWN
#define WSAEHOSTUNREACH     EHOSTUNREACH
#define WSASYSNOTREADY      10091
#define WSAVERNOTSUPPORTED  10092
#define WSANOTINITIALISED   10093
#define WSAEDISCON          10101
#define WSAHOST_NOT_FOUND   11001
#define WSATRY_AGAIN        11002
#define WSANO_RECOVERY      11003
#define WSANO_DATA          11004

#endif  // PLATFORM_WIN32

#endif  /* include-once check. */
