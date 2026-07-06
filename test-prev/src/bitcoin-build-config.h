// Copyright (c) 2023-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_CONFIG_H
#define BITCOIN_CONFIG_H

/* Version Build */
#define CLIENT_VERSION_BUILD 0

/* Version is release */
#define CLIENT_VERSION_IS_RELEASE true

/* Major version */
#define CLIENT_VERSION_MAJOR 29

/* Minor version */
#define CLIENT_VERSION_MINOR 3

/* Copyright holder(s) before %s replacement */
#define COPYRIGHT_HOLDERS "The %s developers"

/* Copyright holder(s) */
#define COPYRIGHT_HOLDERS_FINAL "The Bitcoin Knots developers"

/* Replacement for %s in copyright holders string */
#define COPYRIGHT_HOLDERS_SUBSTITUTION "Bitcoin Knots"

/* Copyright year */
#define COPYRIGHT_YEAR 2026

/* #undef ENABLE_POWER8 */

/* Define this symbol to build code that uses ARMv8 SHA-NI intrinsics */
/* #undef ENABLE_ARM_SHANI */

/* Define this symbol to build code that uses AVX2 intrinsics */
/* #undef ENABLE_AVX2 */

/* Define if external signer support is enabled */
#define ENABLE_EXTERNAL_SIGNER 1

#define ENABLE_TOR_SUBPROCESS 1

/* Define this symbol to build code that uses SSE4.1 intrinsics */
/* #undef ENABLE_SSE41 */

/* Define to 1 to enable tracepoints for Userspace, Statically Defined Tracing
   */
/* #undef ENABLE_TRACING */

/* Define this symbol if the consensus lib has been built */
/* #undef HAVE_CONSENSUS_LIB */

/* Define to 1 to enable wallet functions. */
#define ENABLE_WALLET 1

/* Define this symbol to build code that uses x86 SHA-NI intrinsics */
/* #undef ENABLE_X86_SHANI */

/* Define to 1 if you have the declaration of `fork', and to 0 if you don't.
   */
#define HAVE_DECL_FORK 1

/* Define to 1 if you have the declaration of `freeifaddrs', and to 0 if you
   don't. */
#define HAVE_DECL_FREEIFADDRS 1

/* Define to 1 if you have the declaration of `getifaddrs', and to 0 if you
   don't. */
#define HAVE_DECL_GETIFADDRS 1

/* Define to 1 if you have the declaration of `pipe2', and to 0 if you don't.
   */
#define HAVE_DECL_PIPE2 1

/* Define to 1 if you have the declaration of `setsid', and to 0 if you don't.
   */
#define HAVE_DECL_SETSID 1

/* Define to 1 if fdatasync is available. */
#define HAVE_FDATASYNC 1

/* Define this symbol if the BSD getentropy system call is available with
   sys/random.h */
#define HAVE_GETENTROPY_RAND 1

/* Define this symbol if the Linux getrandom function call is available */
#define HAVE_GETRANDOM 1

/* Define this symbol if you have malloc_info */
#define HAVE_MALLOC_INFO 1

/* Define this symbol if you have mallopt with M_ARENA_MAX */
#define HAVE_MALLOPT_ARENA_MAX 1

/* Define to 1 if O_CLOEXEC flag is available. */
#define HAVE_O_CLOEXEC 1

/* Define this symbol if you have posix_fallocate */
#define HAVE_POSIX_FALLOCATE 1

#define HAVE_IOPOLICY 0

#define HAVE_IOPRIO_SYSCALL 1

#define HAVE_WINDOWS_IOPRIO 0

/* Define this symbol if platform supports unix domain sockets */
#define HAVE_SOCKADDR_UN 1

/* Define to 1 if close_range works with just unistd.h included. */
#define HAVE_CLOSE_RANGE_GENERIC 1

/* Define to 1 if close_range works only with Linux-specific includes. */
#define HAVE_CLOSE_RANGE_LINUX 0

/* Define this symbol to build code that uses getauxval */
#define HAVE_STRONG_GETAUXVAL 1

#define HAVE_LINUX_SYSINFO 1

/* Define this symbol if the BSD sysctl() is available */
/* #undef HAVE_SYSCTL */

/* Define this symbol if the BSD sysctl(KERN_ARND) is available */
/* #undef HAVE_SYSCTL_ARND */

/* Define to 1 if std::system or ::wsystem is available. */
#define HAVE_SYSTEM 1

/* Define to 1 if you have the <sys/prctl.h> header file. */
#define HAVE_SYS_PRCTL_H 1

/* Define to 1 if you have the <sys/resources.h> header file. */
/* #undef HAVE_SYS_RESOURCES_H */

/* Define to 1 if you have the <sys/vmmeter.h> header file. */
/* #undef HAVE_SYS_VMMETER_H */

/* Define to 1 if you have the <vm/vm_param.h> header file. */
/* #undef HAVE_VM_VM_PARAM_H */

/* Define to the address where bug reports for this package should be sent. */
#define CLIENT_BUGREPORT "https://github.com/bitcoinknots/bitcoin/issues"

/* Define to the full name of this package. */
#define CLIENT_NAME "Bitcoin Knots"

/* Define to the home page for this package. */
#define CLIENT_URL "https://bitcoinknots.org/"

/* Define to the version of this package. */
#define CLIENT_VERSION_STRING "29.3.0.knots20260508"

/* Define to 1 if strerror_r returns char *. */
#define STRERROR_R_CHAR_P 1

/* Define if BDB support should be compiled in */
/* #undef USE_BDB */

/* Define if dbus support should be compiled in */
/* #undef USE_DBUS */

/* Define if Windows taskbar progress support should be compiled in */
/* #undef BITCOIN_QT_WIN_TASKBAR */

/* Define if QR support should be compiled in */
/* #undef USE_QRCODE */

/* Define if sqlite support should be compiled in */
#define USE_SQLITE 1

/* Define if building embedded LevelDB */
#define EMBEDDED_LEVELDB 1

#define RDTS_CONSENT RDTSConsentFlag::IMPLICIT

#endif //BITCOIN_CONFIG_H
