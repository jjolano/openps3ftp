#ifndef _PS3_DEFINES_H
#define _PS3_DEFINES_H

// *** sys/stat.h *** //

#define	S_IFSOCK 0140000 /* socket */
#define	S_IFBLK	0060000	/* block special */
#define	S_IFCHR	0020000	/* character special */
#define	S_IFIFO	0010000	/* fifo */

#define	S_ISBLK(m)	(((m)&S_IFMT) == S_IFBLK)
#define	S_ISCHR(m)	(((m)&S_IFMT) == S_IFCHR)
#define	S_ISDIR(m)	(((m)&S_IFMT) == S_IFDIR)
#define	S_ISFIFO(m)	(((m)&S_IFMT) == S_IFIFO)
#define	S_ISREG(m)	(((m)&S_IFMT) == S_IFREG)
#define	S_ISLNK(m)	(((m)&S_IFMT) == S_IFLNK)
#define	S_ISSOCK(m)	(((m)&S_IFMT) == S_IFSOCK)

#define	S_ISVTX 0001000
#define S_ISGID 0002000
#define	S_ISUID	0004000

// *** errno.h *** //

#define EADDRNOTAVAIL EAGAIN
#define EADDRINUSE EBUSY

// ** miscellaneous *** //

//#define MAX(a,b) ((a) > (b) ? (a) : (b))
//#define MIN(a,b) ((a) < (b) ? (a) : (b))
static inline int MAX(int a, int b) { return a > b ? a : b; }
static inline int MIN(int a, int b) { return a < b ? a : b; }

#endif //_PS3_DEFINES_H
