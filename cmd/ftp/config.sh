#!/bin/sh

## Global flags
workdir=`pwd`
OS=$1

## Flags to enable/disable an auto determination of some features
# use the sendfile() system call
auto_sendfile=1
# PAM support
auto_pam=1
# use the euid of the current user for a data transfer in active mode
auto_bsdnewbind=1

## Flags to specify the safeness of the usage of some features
# safeness of the usage of sendfile()
safe_sendfile=0
# safeness of the usage of the euid of the current user for a data transfer in
# active mode
safe_newbind=0

## Check the version of FreeBSD and set safe_* flags
check_version_FreeBSD()
{
    RELVER=`uname -r`

    NUMVER=`echo $RELVER|sed "s/-.*//"`
    MAJVER=`echo $NUMVER|sed "s/\..*//"`
    if [ $NUMVER = $MAJVER ] ; then
	MINVER=0
    else
	SUBVER=`echo $NUMVER |sed "s/.*\.//"`
	if [ $NUMVER = "$MAJVER.$SUBVER" ] ; then
	    MINVER=$SUBVER
	    SUBVER=0
	else
	    MINVER=`echo $RELVER|sed "s/\.$SUBVER\-.*//"|sed "s/.*\.//"`
	fi
    fi

    ## conditions
    # >5.x
    if [ $MAJVER -gt 5  ] ; then
	safe_sendfile=1
	safe_newbind=1
    fi
    # >=5.2
    if [ $MAJVER -eq 5 -a $MINVER -ge 2 ] ; then
	safe_sendfile=1
    fi
    # >=5.3
    if [ $MAJVER -eq 5 -a $MINVER -ge 3 ] ; then
	safe_newbind=1
    fi
    # >4.9
    if [ $MAJVER -eq 4 -a \( $MINVER -gt 9 -o $MINVER -eq 9 -a $SUBVER -gt 0 \) ] ; then
	safe_sendfile=1
    fi
    # >4.10 (if such versions will be released)
    if [ $MAJVER -eq 4 -a \( $MINVER -gt 10 -o $MINVER -eq 10 -a $SUBVER -gt 0 \) ] ; then
	safe_newbind=1
    fi
}

## Prepare the source tree
case "$OS" in
FreeBSD)
    $0 clean
    cd $workdir
    ln -s Makefile.FreeBSD Makefile
    echo "BSDTYPE=$OS">>Makefile.inc
    cd $workdir/ftp
    ln -s Makefile.FreeBSD Makefile
    cd $workdir/ftpd
    ln -s Makefile.FreeBSD Makefile
    cd $workdir/port/libbsdport
    ln -s Makefile.FreeBSD Makefile
    cd $workdir/port/libedit
    ln -s Makefile.FreeBSD Makefile
    # make symlinks to headers
    cd $workdir/port
    ln -s ../contrib/libedit/histedit.h histedit.h
    ;;
NetBSD)
    $0 clean
    cd $workdir
    ln -s Makefile.FreeBSD Makefile
    echo "BSDTYPE=$OS">>Makefile.inc
    echo "MKOBJ=no">>Makefile.inc
    echo "MKMANZ=no">>Makefile.inc
    echo "CFLAGS+=-DNETBSD">>Makefile.inc
    cd $workdir/ftp
    ln -s Makefile.FreeBSD Makefile
    cd $workdir/ftpd
    ln -s Makefile.FreeBSD Makefile
    cd $workdir/port/libbsdport
    ln -s Makefile.FreeBSD Makefile
    cd $workdir/port/libedit
    ln -s Makefile.FreeBSD Makefile
    # make symlinks to headers
    cd $workdir/port
    ln -s ../contrib/libedit/histedit.h histedit.h
    # NetBSD's libutil uses the util.h header, unlike FreeBSD, which uses the
    # libutil.h one.
    ln -s /usr/include/util.h libutil.h
    ;;
Linux)
    $0 clean
    cd $workdir
    ln -s Makefile.linux Makefile
    touch Makefile.inc
    cd $workdir/ftp
    ln -s Makefile.linux Makefile
    cd $workdir/ftpd
    ln -s Makefile.linux Makefile
    touch Makefile.inc
    cd $workdir/port/libbsdport
    ln -s Makefile.linux Makefile
    cd $workdir/port/libedit
    ln -s Makefile.linux Makefile
    touch Makefile.inc
    # make symlinks to headers
    cd $workdir/port
    ln -s ../contrib/libbsdport/include/glob.h bsdglob.h
    ln -s ../contrib/libbsdport/include/fts.h bsdfts.h
    ln -s ../contrib/libbsdport/include/stringlist.h stringlist.h
    ln -s ../contrib/libbsdport/libutil/libutil.h libutil.h
    ln -s ../contrib/libedit/histedit.h histedit.h
    ;;
clean)
    cd $workdir
    rm -f Makefile
    rm -f Makefile.inc
    rm -f $workdir/ftpd/Makefile
    rm -f $workdir/ftpd/Makefile.inc
    rm -f $workdir/ftp/Makefile
    rm -f $workdir/port/libbsdport/Makefile
    rm -f $workdir/port/libedit/Makefile
    rm -f $workdir/port/libedit/Makefile.inc
    rm -f $workdir/port/bsdglob.h
    rm -f $workdir/port/bsdfts.h
    rm -f $workdir/port/stringlist.h
    rm -f $workdir/port/libutil.h
    rm -f $workdir/port/histedit.h
    ;;
_conv_gcc29x)
    flist="contrib/ls/ls.c contrib/ls/print.c ftp/cmds.c ftp/ftp.c ftp/util.c \
	  ftpd/ftpcmd.y ftpd/ftpd.c"
    ext="gcc3"
    for fname in ${flist} ; do
	mv $fname $fname.$ext
	cat $fname.$ext | sed s/"#include <stdint.h>"/""/g | \
	    sed s/"%jd"/"%lld"/g | sed s/"%\*jd"/"%\*lld"/g | \
	    sed s/"%5jd"/"%5lld"/g | sed s/"intmax_t"/"long long"/g > $fname
    done
    echo "The original versions of modified source files are saved with the \".$ext\""
    echo "extensions."
    exit 0
    ;;
"")
    echo "usage:"
    echo "  config.sh OS [OSFeature, [OSFeature, ...]]"
    echo "    \"OS\" is a name of the target operating system, \"OSFeature\" can be"
    echo "    used to enable or disable the support for some specific features."
    echo "    See the INSTALL file for more information."
    echo "  config.sh clean"
    echo "    The cleanup of the source tree configuration."
    echo "  config.sh _conv_gcc29x"
    echo "    The conversion of the source code to the gcc 2.9x compatible state."
    exit 1
    ;;
*)
    echo "OS \"$OS\" not supported"
    exit 1
    ;;
esac

## Proccess command-line options
shift
for _switch; do
	case "${_switch}" in
	sendfile)
	    echo "CFLAGS+=-DUSE_SENDFILE">>$workdir/ftpd/Makefile.inc
	    auto_sendfile=0
	    shift
	    ;;
	nosendfile)
	    auto_sendfile=0
	    shift
	    ;;
	bsdnewbind)
	    auto_bsdnewbind=0
	    shift
	    ;;
	nobsdnewbind)
	    echo "CFLAGS+=-DBSDORIG_BIND">>$workdir/ftpd/Makefile.inc
	    auto_bsdnewbind=0
	    shift
	    ;;
	NOPAM)
	    auto_pam=0
	    shift
	    ;;
	Kerberos)
	    echo "CFLAGS+= -I/usr/kerberos/include">>$workdir/Makefile.inc
	    shift
	    ;;
	noncursesdir)
	    echo "CFLAGS+= -DNONCURSESDIR">>$workdir/port/libedit/Makefile.inc
	    shift
	    ;;
	*)
	    shift
	    ;;
	esac
done

## Autodetermination of some features
case "$OS" in
FreeBSD)
    check_version_FreeBSD
    if [ $safe_sendfile -eq 1 -a $auto_sendfile -eq 1 ] ; then
	echo "CFLAGS+=-DUSE_SENDFILE">>$workdir/ftpd/Makefile.inc
    fi
    if [ $safe_newbind -eq 0 -a $auto_bsdnewbind -eq 1 ] ; then
	echo "CFLAGS+=-DBSDORIG_BIND">>$workdir/ftpd/Makefile.inc
    fi
    ;;
NetBSD)
    if [ $safe_newbind -eq 0 -a $auto_bsdnewbind -eq 1 ] ; then
	echo "CFLAGS+=-DBSDORIG_BIND">>$workdir/ftpd/Makefile.inc
    fi
    if [ $auto_pam -eq 1 ] ; then
	echo "NO_PAM=1">>$workdir/ftpd/Makefile.inc
    fi
    ;;
Linux)
    if [ $auto_pam -eq 1 ] ; then
	echo "CFLAGS+= -DUSE_PAM">>$workdir/ftpd/Makefile.inc
	echo "LIBS+= -lpam">>$workdir/ftpd/Makefile.inc
    fi
    ;;
esac
