#ifdef LINUX /* Linux port */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif

#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>

#include "bsdport.h"

void
strmode(mode_t mode, char *bp)
{
	switch (mode&S_IFMT) {
	case S_IFSOCK: bp[0]='s'; break;
	case S_IFLNK: bp[0]='l'; break;
	case S_IFREG: bp[0]='-'; break;
	case S_IFBLK: bp[0]='b'; break;
	case S_IFDIR: bp[0]='d'; break;
	case S_IFCHR: bp[0]='c'; break;
	case S_IFIFO: bp[0]='p'; break;
	default: bp[0]='?'; break;
	}
	if (mode&S_IRUSR) bp[1]='r';
	else bp[1]='-';
	if (mode&S_IWUSR) bp[2]='w';
	else bp[2]='-';
	if (mode&S_IXUSR) {
		if (mode&S_ISUID) bp[3]='s';
		else bp[3]='x';
	} else {
		if (mode&S_ISUID) bp[3]='S';
		else bp[3]='-';
	}
	if (mode&S_IRGRP) bp[4]='r';
	else bp[4]='-';
	if (mode&S_IWGRP) bp[5]='w';
	else bp[5]='-';
	if (mode&S_IXGRP) {
		if (mode&S_ISGID) bp[6]='s';
		else bp[6]='x';
	} else {
		if (mode&S_ISGID) bp[6]='S';
		else bp[6]='-';
	}
	if (mode&S_IROTH) bp[7]='r';
	else bp[7]='-';
	if (mode&S_IWOTH) bp[8]='w';
	else bp[8]='-';
	if (mode&S_IXOTH) {
		if (mode&S_ISVTX) bp[9]='t';
		else bp[9]='x';
	} else {
		if (mode&S_ISVTX) bp[9]='T';
		else bp[9]='-';
	}
	bp[10]=' ';
	bp[11]=0;
}

char *
user_from_uid(uid_t uid, int ignored)
{
	struct passwd *pw;
	static char buf[16];

	pw = getpwuid(uid);
	if ( pw )
		return pw->pw_name;
	else {
		snprintf(buf, sizeof(buf), "%u", uid);
		return buf;
	}
}

char *
group_from_gid(gid_t gid, int ignored)
{
	struct group *gr;
	static char buf[16];

	gr = getgrgid(gid);
	if ( gr )
		return gr->gr_name;
	else {
		snprintf(buf, sizeof(buf), "%u", gid);
		return buf;
	}
}

/*
 * Parts of SUN math library
 */

/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

/* ilogb(double x)
 * return the binary exponent of non-zero x
 * ilogb(0) = 0x80000001
 * ilogb(inf/NaN) = 0x7fffffff (no signal is raised)
 */
int ilogb(double x)
{
	int32_t hx,lx,ix;

	EXTRACT_WORDS(hx,lx,x);
	hx &= 0x7fffffff;
	if(hx<0x00100000) {
	    if((hx|lx)==0)
		return 0x80000001;	/* ilogb(0) = 0x80000001 */
	    else			/* subnormal x */
		if(hx==0) {
		    for (ix = -1043; lx>0; lx<<=1) ix -=1;
		} else {
		    for (ix = -1022,hx<<=11; hx>0; hx<<=1) ix -=1;
		}
	    return ix;
	}
	else if (hx<0x7ff00000) return (hx>>20)-1023;
	else return 0x7fffffff;
}

#endif /* Linux port */
