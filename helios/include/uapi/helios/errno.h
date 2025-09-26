/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

enum __ERRNO_VALUES {
	EPERM = 1,    /* Operation not permitted */
	ENOENT = 2,   /* No such file or directory */
	ESRCH = 3,    /* No such process */
	EINTR = 4,    /* Interrupted system call */
	EIO = 5,      /* I/O error */
	ENXIO = 6,    /* No such device or address */
	E2BIG = 7,    /* Argument list too long */
	ENOEXEC = 8,  /* Exec format error */
	EBADF = 9,    /* Bad file number */
	ECHILD = 10,  /* No child processes */
	EAGAIN = 11,  /* Try again */
	ENOMEM = 12,  /* Out of memory */
	EACCES = 13,  /* Permission denied */
	EFAULT = 14,  /* Bad address */
	ENOTBLK = 15, /* Block device required */
	EBUSY = 16,   /* Device or resource busy */
	EEXIST = 17,  /* File exists */
	EXDEV = 18,   /* Cross-device link */
	ENODEV = 19,  /* No such device */
	ENOTDIR = 20, /* Not a directory */
	EISDIR = 21,  /* Is a directory */
	EINVAL = 22,  /* Invalid argument */
	ENFILE = 23,  /* File table overflow */
	EMFILE = 24,  /* Too many open files */
	ENOTTY = 25,  /* Not a typewriter */
	ETXTBSY = 26, /* Text file busy */
	EFBIG = 27,   /* File too large */
	ENOSPC = 28,  /* No space left on device */
	ESPIPE = 29,  /* Illegal seek */
	EROFS = 30,   /* Read-only file system */
	EMLINK = 31,  /* Too many links */
	EPIPE = 32,   /* Broken pipe */
	EDOM = 33,    /* Math argument out of domain of func */
	ERANGE = 34,  /* Math result not representable */

	/* POSIX Standard Error Codes (continued) */
	EDEADLK = 35,	   /* Resource deadlock would occur */
	ENAMETOOLONG = 36, /* File name too long */
	ENOLCK = 37,	   /* No record locks available */
	ENOSYS = 38,	   /* Function not implemented */
	ENOTEMPTY = 39,	   /* Directory not empty */
	ELOOP = 40,	   /* Too many symbolic links encountered */
	EWOULDBLOCK =
		41, /* Operation would block (same as EAGAIN on many systems) */
	ENOMSG = 42,	   /* No message of desired type */
	EIDRM = 43,	   /* Identifier removed */
	ECHRNG = 44,	   /* Channel number out of range */
	EL2NSYNC = 45,	   /* Level 2 not synchronized */
	EL3HLT = 46,	   /* Level 3 halted */
	EL3RST = 47,	   /* Level 3 reset */
	ELNRNG = 48,	   /* Link number out of range */
	EUNATCH = 49,	   /* Protocol driver not attached */
	ENOCSI = 50,	   /* No CSI structure available */
	EL2HLT = 51,	   /* Level 2 halted */
	EBADE = 52,	   /* Invalid exchange */
	EBADR = 53,	   /* Invalid request descriptor */
	EXFULL = 54,	   /* Exchange full */
	ENOANO = 55,	   /* No anode */
	EBADRQC = 56,	   /* Invalid request code */
	EBADSLT = 57,	   /* Invalid slot */
	EDEADLOCK = 58,	   /* File locking deadlock error (same as EDEADLK) */
	EBFONT = 59,	   /* Bad font file format */
	ENOSTR = 60,	   /* Device not a stream */
	ENODATA = 61,	   /* No data available */
	ETIME = 62,	   /* Timer expired */
	ENOSR = 63,	   /* Out of streams resources */
	ENONET = 64,	   /* Machine is not on the network */
	ENOPKG = 65,	   /* Package not installed */
	EREMOTE = 66,	   /* Object is remote */
	ENOLINK = 67,	   /* Link has been severed */
	EADV = 68,	   /* Advertise error */
	ESRMNT = 69,	   /* Srmount error */
	ECOMM = 70,	   /* Communication error on send */
	EPROTO = 71,	   /* Protocol error */
	EMULTIHOP = 72,	   /* Multihop attempted */
	EDOTDOT = 73,	   /* RFS specific error */
	EBADMSG = 74,	   /* Not a data message */
	EOVERFLOW = 75,	   /* Value too large for defined data type */
	ENOTUNIQ = 76,	   /* Name not unique on network */
	EBADFD = 77,	   /* File descriptor in bad state */
	EREMCHG = 78,	   /* Remote address changed */
	ELIBACC = 79,	   /* Can not access a needed shared library */
	ELIBBAD = 80,	   /* Accessing a corrupted shared library */
	ELIBSCN = 81,	   /* .lib section in a.out corrupted */
	ELIBMAX = 82,	   /* Attempting to link in too many shared libraries */
	ELIBEXEC = 83,	   /* Cannot exec a shared library directly */
	EILSEQ = 84,	   /* Illegal byte sequence */
	ERESTART = 85,	   /* Interrupted system call should be restarted */
	ESTRPIPE = 86,	   /* Streams pipe error */
	EUSERS = 87,	   /* Too many users */
	ENOTSOCK = 88,	   /* Socket operation on non-socket */
	EDESTADDRREQ = 89, /* Destination address required */
	EMSGSIZE = 90,	   /* Message too long */
	EPROTOTYPE = 91,   /* Protocol wrong type for socket */
	ENOPROTOOPT = 92,  /* Protocol not available */
	EPROTONOSUPPORT = 93, /* Protocol not supported */
	ESOCKTNOSUPPORT = 94, /* Socket type not supported */
	EOPNOTSUPP = 95,    /* Operation not supported on transport endpoint */
	EPFNOSUPPORT = 96,  /* Protocol family not supported */
	EAFNOSUPPORT = 97,  /* Address family not supported by protocol */
	EADDRINUSE = 98,    /* Address already in use */
	EADDRNOTAVAIL = 99, /* Cannot assign requested address */
	ENETDOWN = 100,	    /* Network is down */
	ENETUNREACH = 101,  /* Network is unreachable */
	ENETRESET = 102,    /* Network dropped connection because of reset */
	ECONNABORTED = 103, /* Software caused connection abort */
	ECONNRESET = 104,   /* Connection reset by peer */
	ENOBUFS = 105,	    /* No buffer space available */
	EISCONN = 106,	    /* Transport endpoint is already connected */
	ENOTCONN = 107,	    /* Transport endpoint is not connected */
	ESHUTDOWN = 108,    /* Cannot send after transport endpoint shutdown */
	ETOOMANYREFS = 109, /* Too many references: cannot splice */
	ETIMEDOUT = 110,    /* Connection timed out */
	ECONNREFUSED = 111, /* Connection refused */
	EHOSTDOWN = 112,    /* Host is down */
	EHOSTUNREACH = 113, /* No route to host */
	EALREADY = 114,	    /* Operation already in progress */
	EINPROGRESS = 115,  /* Operation now in progress */
	ESTALE = 116,	    /* Stale file handle */
	EUCLEAN = 117,	    /* Structure needs cleaning */
	ENOTNAM = 118,	    /* Not a XENIX named type file */
	ENAVAIL = 119,	    /* No XENIX semaphores available */
	EISNAM = 120,	    /* Is a named type file */
	EREMOTEIO = 121,    /* Remote I/O error */
	EDQUOT = 122,	    /* Quota exceeded */

	/* Additional POSIX.1-2001 */
	ENOMEDIUM = 123,    /* No medium found */
	EMEDIUMTYPE = 124,  /* Wrong medium type */
	ECANCELED = 125,    /* Operation Canceled */
	ENOKEY = 126,	    /* Required key not available */
	EKEYEXPIRED = 127,  /* Key has expired */
	EKEYREVOKED = 128,  /* Key has been revoked */
	EKEYREJECTED = 129, /* Key was rejected by service */

	/* Additional error codes */
	EOWNERDEAD = 130,      /* Owner died */
	ENOTRECOVERABLE = 131, /* State not recoverable */
	ERFKILL = 132,	       /* Operation not possible due to RF-kill */
	EHWPOISON = 133,       /* Memory page has hardware error */

	/* Some systems define these additional codes */
	ENOTSUP = 134,	   /* Not supported (may be same as EOPNOTSUPP) */
	ENOSHARE = 135,	   /* No such host or network path */
	ECASECLASH = 136,  /* Filename exists with different case */
	EILSEQ_2 = 137,	   /* Illegal byte sequence (alternative) */
	EOVERFLOW_2 = 138, /* Value too large for data type (alternative) */

	/* Network File System (NFS) specific */
	EREMOTE_2 = 139, /* Too many levels of remote in path */
	ENOATTR = 140,	 /* No such attribute */

	/* Extended attributes */
	ENODATA_2 = 141, /* No message available on STREAM */
	ENOSR_2 = 142,	 /* No STREAM resources */
	ENOSTR_2 = 143,	 /* Not a STREAM */
	ETIME_2 = 144,	 /* STREAM ioctl timeout */

	/* Additional Linux-specific codes */
	ERESTARTSYS = 512,    /* Restart system call */
	ERESTARTNOINTR = 513, /* Restart if no interrupt */
	ERESTARTNOHAND = 514, /* Restart if no handler */
	ENOIOCTLCMD = 515,    /* No ioctl command */
	ERESTART_RESTARTBLOCK =
		516,	      /* Restart by calling sys_restart_syscall */
	EPROBE_DEFER = 517,   /* Driver requests probe retry */
	EOPENSTALE = 518,     /* Open found a stale dentry */
	ENOPARAM = 519,	      /* Parameter not supported */
	EBADHANDLE = 521,     /* Illegal NFS file handle */
	ENOTSYNC = 522,	      /* Update synchronization mismatch */
	EBADCOOKIE = 523,     /* Cookie is stale */
	ENOTSUPP = 524,	      /* Operation is not supported */
	ETOOSMALL = 525,      /* Buffer or request is too small */
	ESERVERFAULT = 526,   /* An untranslatable error occurred */
	EBADTYPE = 527,	      /* Type not supported by server */
	EJUKEBOX =
		528, /* Request initiated, but will not complete before timeout */
	EIOCBQUEUED = 529,     /* iocb queued, will get completion event */
	ERECALLCONFLICT = 530, /* Conflict with recalled state */
};

static inline const char* __get_error_string(int errnum)
{
	if (errnum == 0) return "Success";

	enum __ERRNO_VALUES err = (enum __ERRNO_VALUES)errnum;
	switch (err) {
	// Most frequent errors (ENOENT, EACCES, etc.)
	case ENOENT: return "No such file or directory";
	case EACCES: return "Permission denied";
	case EAGAIN: return "Resource temporarily unavailable";

	case EPERM:	      return "Operation not permitted";
	case ESRCH:	      return "No such process";
	case EINTR:	      return "Interrupted system call";
	case EIO:	      return "Input/output error";
	case ENXIO:	      return "No such device or address";
	case E2BIG:	      return "Argument list too long";
	case ENOEXEC:	      return "Exec format error";
	case EBADF:	      return "Bad file descriptor";
	case ECHILD:	      return "No child processes";
	case ENOMEM:	      return "Cannot allocate memory";
	case EFAULT:	      return "Bad address";
	case ENOTBLK:	      return "Block device required";
	case EBUSY:	      return "Device or resource busy";
	case EEXIST:	      return "File exists";
	case EXDEV:	      return "Invalid cross-device link";
	case ENODEV:	      return "No such device";
	case ENOTDIR:	      return "Not a directory";
	case EISDIR:	      return "Is a directory";
	case EINVAL:	      return "Invalid argument";
	case ENFILE:	      return "Too many open files in system";
	case EMFILE:	      return "Too many open files";
	case ENOTTY:	      return "Inappropriate ioctl for device";
	case ETXTBSY:	      return "Text file busy";
	case EFBIG:	      return "File too large";
	case ENOSPC:	      return "No space left on device";
	case ESPIPE:	      return "Illegal seek";
	case EROFS:	      return "Read-only file system";
	case EMLINK:	      return "Too many links";
	case EPIPE:	      return "Broken pipe";
	case EDOM:	      return "Numerical argument out of domain";
	case ERANGE:	      return "Numerical result out of range";
	case EDEADLK:	      return "Resource deadlock avoided";
	case ENAMETOOLONG:    return "File name too long";
	case ENOLCK:	      return "No locks available";
	case ENOSYS:	      return "Function not implemented";
	case ENOTEMPTY:	      return "Directory not empty";
	case ELOOP:	      return "Too many levels of symbolic links";
	case EWOULDBLOCK:     return "Resource temporarily unavailable";
	case ENOMSG:	      return "No message of desired type";
	case EIDRM:	      return "Identifier removed";
	case ECHRNG:	      return "Channel number out of range";
	case EL2NSYNC:	      return "Level 2 not synchronized";
	case EL3HLT:	      return "Level 3 halted";
	case EL3RST:	      return "Level 3 reset";
	case ELNRNG:	      return "Link number out of range";
	case EUNATCH:	      return "Protocol driver not attached";
	case ENOCSI:	      return "No CSI structure available";
	case EL2HLT:	      return "Level 2 halted";
	case EBADE:	      return "Invalid exchange";
	case EBADR:	      return "Invalid request descriptor";
	case EXFULL:	      return "Exchange full";
	case ENOANO:	      return "No anode";
	case EBADRQC:	      return "Invalid request code";
	case EBADSLT:	      return "Invalid slot";
	case EDEADLOCK:	      return "Resource deadlock avoided";
	case EBFONT:	      return "Bad font file format";
	case ENOSTR:	      return "Device not a stream";
	case ENODATA:	      return "No data available";
	case ETIME:	      return "Timer expired";
	case ENOSR:	      return "Out of streams resources";
	case ENONET:	      return "Machine is not on the network";
	case ENOPKG:	      return "Package not installed";
	case EREMOTE:	      return "Object is remote";
	case ENOLINK:	      return "Link has been severed";
	case EADV:	      return "Advertise error";
	case ESRMNT:	      return "Srmount error";
	case ECOMM:	      return "Communication error on send";
	case EPROTO:	      return "Protocol error";
	case EMULTIHOP:	      return "Multihop attempted";
	case EDOTDOT:	      return "RFS specific error";
	case EBADMSG:	      return "Bad message";
	case EOVERFLOW:	      return "Value too large for defined data type";
	case ENOTUNIQ:	      return "Name not unique on network";
	case EBADFD:	      return "File descriptor in bad state";
	case EREMCHG:	      return "Remote address changed";
	case ELIBACC:	      return "Can not access a needed shared library";
	case ELIBBAD:	      return "Accessing a corrupted shared library";
	case ELIBSCN:	      return ".lib section in a.out corrupted";
	case ELIBMAX:	      return "Attempting to link in too many shared libraries";
	case ELIBEXEC:	      return "Cannot exec a shared library directly";
	case EILSEQ:	      return "Invalid or incomplete multibyte or wide character";
	case ERESTART:	      return "Interrupted system call should be restarted";
	case ESTRPIPE:	      return "Streams pipe error";
	case EUSERS:	      return "Too many users";
	case ENOTSOCK:	      return "Socket operation on non-socket";
	case EDESTADDRREQ:    return "Destination address required";
	case EMSGSIZE:	      return "Message too long";
	case EPROTOTYPE:      return "Protocol wrong type for socket";
	case ENOPROTOOPT:     return "Protocol not available";
	case EPROTONOSUPPORT: return "Protocol not supported";
	case ESOCKTNOSUPPORT: return "Socket type not supported";
	case EOPNOTSUPP:      return "Operation not supported";
	case EPFNOSUPPORT:    return "Protocol family not supported";
	case EAFNOSUPPORT:    return "Address family not supported by protocol";
	case EADDRINUSE:      return "Address already in use";
	case EADDRNOTAVAIL:   return "Cannot assign requested address";
	case ENETDOWN:	      return "Network is down";
	case ENETUNREACH:     return "Network is unreachable";
	case ENETRESET:	      return "Network dropped connection on reset";
	case ECONNABORTED:    return "Software caused connection abort";
	case ECONNRESET:      return "Connection reset by peer";
	case ENOBUFS:	      return "No buffer space available";
	case EISCONN:	      return "Transport endpoint is already connected";
	case ENOTCONN:	      return "Transport endpoint is not connected";
	case ESHUTDOWN:	      return "Cannot send after transport endpoint shutdown";
	case ETOOMANYREFS:    return "Too many references: cannot splice";
	case ETIMEDOUT:	      return "Connection timed out";
	case ECONNREFUSED:    return "Connection refused";
	case EHOSTDOWN:	      return "Host is down";
	case EHOSTUNREACH:    return "No route to host";
	case EALREADY:	      return "Operation already in progress";
	case EINPROGRESS:     return "Operation now in progress";
	case ESTALE:	      return "Stale file handle";
	case EUCLEAN:	      return "Structure needs cleaning";
	case ENOTNAM:	      return "Not a XENIX named type file";
	case ENAVAIL:	      return "No XENIX semaphores available";
	case EISNAM:	      return "Is a named type file";
	case EREMOTEIO:	      return "Remote I/O error";
	case EDQUOT:	      return "Disk quota exceeded";
	case ENOMEDIUM:	      return "No medium found";
	case EMEDIUMTYPE:     return "Wrong medium type";
	case ECANCELED:	      return "Operation canceled";
	case ENOKEY:	      return "Required key not available";
	case EKEYEXPIRED:     return "Key has expired";
	case EKEYREVOKED:     return "Key has been revoked";
	case EKEYREJECTED:    return "Key was rejected by service";
	case EOWNERDEAD:      return "Owner died";
	case ENOTRECOVERABLE: return "State not recoverable";
	case ERFKILL:	      return "Operation not possible due to RF-kill";
	case EHWPOISON:	      return "Memory page has hardware error";
	case ENOTSUP:	      return "Operation not supported";

	case ENOSHARE:	     return "No such host or network path";
	case ECASECLASH:     return "Filename exists with different case";
	case EILSEQ_2:	     return "Illegal byte sequence";
	case EOVERFLOW_2:    return "Value too large for data type (alternative)";
	case EREMOTE_2:	     return "Too many levels of remote in path";
	case ENOATTR:	     return "No such attribute";
	case ENODATA_2:	     return "No message available on STREAM";
	case ENOSR_2:	     return "No STREAM resources";
	case ENOSTR_2:	     return "Not a STREAM";
	case ETIME_2:	     return "STREAM ioctl timeout";
	case ERESTARTSYS:    return "Restart system call";
	case ERESTARTNOINTR: return "Restart if no interrupt";
	case ERESTARTNOHAND: return "Restart if no handler";
	case ENOIOCTLCMD:    return "No ioctl command";
	case ERESTART_RESTARTBLOCK:
		return "Restart by calling sys_restart_syscall";
	case EPROBE_DEFER: return "Driver requests probe retry";
	case EOPENSTALE:   return "Open found a stale dentry";
	case ENOPARAM:	   return "Parameter not supported";
	case EBADHANDLE:   return "Illegal NFS file handle";
	case ENOTSYNC:	   return "Update synchronization mismatch";
	case EBADCOOKIE:   return "Cookie is stale";
	case ENOTSUPP:	   return "Operation is not supported";
	case ETOOSMALL:	   return "Buffer or request is too small";
	case ESERVERFAULT: return "An untranslatable error occurred";
	case EBADTYPE:	   return "Type not supported by server";
	case EJUKEBOX:
		return "Request initiated, but will not complete before timeout";
	case EIOCBQUEUED:     return "iocb queued, will get completion event";
	case ERECALLCONFLICT: return "Conflict with recalled state";
	}

	return "Unknown error";
}
