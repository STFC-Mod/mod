/* config.h.  Generated from config.h.in by cmake.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Define this to enable trace code  */
/* #undef DO_RS_TRACE */

/* Define to 1 if you have the <sys/file.h> header file. */
#define HAVE_SYS_FILE_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the <io.h> header file. */
/* #undef HAVE_IO_H */

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the <mcheck.h> header file. */
/* #undef HAVE_MCHECK_H */

/* Define to 1 if you have the <zlib.h> header file. */
/* #undef HAVE_ZLIB_H */

/* Define to 1 if you have the <bzlib.h> header file.  */
/* #undef HAVE_BZLIB_H */

/* Define if your compiler has C99's __func__. */
#define HAVE___FUNC__

/* Define if your compiler has GNU's __FUNCTION__. */
#define HAVE___FUNCTION__

/* Define to 1 if fseeko (and presumably ftello) exists and is declared. */
#define HAVE_FSEEKO 1

/* Define to 1 if fseeko64 (and presumably ftello64) exists and is declared. */
/* #undef HAVE_FSEEKO64 */

/* Define to 1 if _fseeki64 (and presumably _ftelli64) exists and is declared. */
/* #undef HAVE__FSEEKI64 */

/* Define to 1 if fstat64 exists and is declared. */
#define HAVE_FSTAT64 1

/* Define to 1 if _fstati64 exists and is declared. */
/* #undef HAVE__FSTATI64 */

/* Define to 1 if fileno exists and is declared (Posix). */
#define HAVE_FILENO 1

/* Define to 1 if _fileno exists and is declared (ISO C++). */
/* #undef HAVE__FILENO */

/* Name of package */
#define PACKAGE "librsync"

/* The size of `long', as computed by sizeof. */
#define SIZEOF_LONG 8

/* The size of `long long', as computed by sizeof. */
#define SIZEOF_LONG_LONG 8

/* The size of `size_t', as computed by sizeof. */
#define SIZEOF_SIZE_T 8

/* The size of `off_t', as computed by sizeof. */
#define SIZEOF_OFF_T 8

/* The size of `off64_t', as computed by sizeof. */
/* #undef SIZEOF_OFF64_T */

/* The size of `unsigned int', as computed by sizeof. */
#define SIZEOF_UNSIGNED_INT 4

/* The size of `unsigned long', as computed by sizeof. */
#define SIZEOF_UNSIGNED_LONG 8

/* The size of `unsigned short', as computed by sizeof. */
#define SIZEOF_UNSIGNED_SHORT 2

/* Define to 1 if printf supports the size_t "%zu" length field. */
#define HAVE_PRINTF_Z 1

/* FIXME Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* FIXME Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* FIXME Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* FIXME Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* FIXME Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif


/* Version number of package */
#define VERSION "2.3.4"

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
/* #undef WORDS_BIGENDIAN */

/* FIXME Enable large inode numbers on Mac OS X 10.5.  */
#ifndef _DARWIN_USE_64_BIT_INODE
# define _DARWIN_USE_64_BIT_INODE 1
#endif

/* FIXME Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* FIXME Define to 1 to make fseeko visible on some hosts (e.g. glibc 2.2). */
/* #undef _LARGEFILE_SOURCE */

/* FIXME Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* FIXME Define to 1 if on MINIX. */
/* #undef _MINIX */

/* FIXME Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* FIXME Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */
