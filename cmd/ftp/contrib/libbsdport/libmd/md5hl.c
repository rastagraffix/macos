/* md5hl.c    - OpenSSL-compatible support for MD5 checksums */
/* This file is based on mdXhl.c from FreeBSD 5.0 libmd library. */

/* mdXhl.c * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 */

#if 0
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/lib/libmd/mdXhl.c,v 1.18 2002/09/08 15:10:04 phk Exp $");
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "md5hl.h"

char *
MD5End(MD5_CTX *ctx, char *buf)
{
	int i;
	unsigned char digest[MD5_DIGEST_LENGTH];
	static const char hex[]="0123456789abcdef";

	if (!buf)
		buf = malloc(2*MD5_DIGEST_LENGTH + 1);
	if (!buf)
		return 0;
	MD5_Final(digest, ctx);
	for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
		buf[i+i] = hex[digest[i] >> 4];
		buf[i+i+1] = hex[digest[i] & 0x0f];
	}
	buf[i+i] = '\0';
	return buf;
}

char *
MD5File(const char *filename, char *buf)
{
	return (MD5FileChunk(filename, buf, 0, 0));
}

char *
MD5FileChunk(const char *filename, char *buf, off_t ofs, off_t len)
{
	unsigned char buffer[BUFSIZ];
	MD5_CTX ctx;
	struct stat stbuf;
	int f, i, e;
	off_t n;

	MD5_Init(&ctx);
	f = open(filename, O_RDONLY);
	if (f < 0)
		return 0;
	if (fstat(f, &stbuf) < 0)
		return 0;
	if (ofs > stbuf.st_size)
		ofs = stbuf.st_size;
	if ((len == 0) || (len > stbuf.st_size - ofs))
		len = stbuf.st_size - ofs;
	if (lseek(f, ofs, SEEK_SET) < 0)
		return 0;
	n = len;
	i = 0;
	while (n > 0) {
		if (n > sizeof(buffer))
			i = read(f, buffer, sizeof(buffer));
		else
			i = read(f, buffer, n);
		if (i < 0) 
			break;
		MD5_Update(&ctx, buffer, i);
		n -= i;
	} 
	e = errno;
	close(f);
	errno = e;
	if (i < 0)
		return 0;
	return (MD5End(&ctx, buf));
}

char *
MD5Data (const unsigned char *data, unsigned int len, char *buf)
{
	MD5_CTX ctx;

	MD5_Init(&ctx);
	MD5_Update(&ctx,data,len);
	return (MD5End(&ctx, buf));
}
