#!/bin/sh
#
# CA - wrapper around ca to make it easier to use ... basically ca requires
#      some setup stuff to be done before you can use it and this makes
#      things easier between now and when Eric is convinced to fix it :-)
#
# CA -newca ... will setup the right stuff
# CA -newreq ... will generate a certificate request 
# CA -sign ... will sign the generated request and output 
#
# At the end of that grab newreq.pem and newcert.pem (one has the key 
# and the other the certificate) and cat them together and that is what
# you want/need ... I'll make even this a little cleaner later.
#
#
# 12-Jan-96 tjh    Added more things ... including CA -signcert which
#                  converts a certificate to a request and then signs it.
# 10-Jan-96 eay    Fixed a few more bugs and added the SSLEAY_CONFIG
#		   environment variable so this can be driven from
#		   a script.
# 25-Jul-96 eay    Cleaned up filenames some more.
# 11-Jun-96 eay    Fixed a few filename missmatches.
# 03-May-96 eay    Modified to use 'ssleay cmd' instead of 'cmd'.
# 18-Apr-96 tjh    Original hacking
#
# Tim Hudson
# tjh@cryptsoft.com
#

# 24-Feb-2002 skynick
# Script name has been changed to xCA.sh to prevent confuses with original
# script which may be installed as part of OpenSSL distribution.
#
# CRL maintaining commands were added:
# xCA.sh -revoke <sernum> ... will revoke certificate with serial number
#    <sernum> (all certificates with serial numbers are listed in CA's
#    index.txt file)
# xCA.sh -gencrl ... will generate CRL
#
# 8-Mar-2003 skynick
# new command was added:
# xCA.sh -renewca ... will renew existing CA certificate
#
# Nick Leuta
# skynick@stu.lipetsk.ru
#

# default openssl.cnf file has setup as per the following
# demoCA ... where everything is stored

DAYS="-days 365"
REQ="openssl req $SSLEAY_CONFIG"
CA="openssl ca $SSLEAY_CONFIG"
VERIFY="openssl verify"
X509="openssl x509"

CATOP=./demoCA
CAKEY=./cakey.pem
CACERT=./cacert.pem

for i
do
case $i in
-\?|-h|-help)
    echo "usage: xCA -newcert|-newreq|-newca|-renewca|-sign|-revoke <sernum>|-gencrl|-verify" >&2
    exit 0
    ;;
-newcert) 
    # create a certificate
    $REQ -new -nodes -x509 -keyout newreq.pem -out newreq.pem $DAYS
    RET=$?
    echo "Certificate (and private key) is in newreq.pem"
    ;;
-newreq) 
    # create a certificate request
    $REQ -new -nodes -keyout newreq.pem -out newreq.pem $DAYS
    RET=$?
    echo "Request (and private key) is in newreq.pem"
    ;;
-newca)     
    # if explicitly asked for or it doesn't exist then setup the directory
    # structure that Eric likes to manage things 
    NEW="1"
    if [ "$NEW" -o ! -f ${CATOP}/serial ]; then
	# create the directory hierarchy
	mkdir ${CATOP} 
	mkdir ${CATOP}/certs 
	mkdir ${CATOP}/crl 
	mkdir ${CATOP}/newcerts
	mkdir ${CATOP}/private
	echo "01" > ${CATOP}/serial
	touch ${CATOP}/index.txt
    fi
    if [ ! -f ${CATOP}/private/$CAKEY ]; then
	echo "CA certificate filename (or enter to create)"
	read FILE

	# ask user for existing CA certificate
	if [ "$FILE" ]; then
	    cp $FILE ${CATOP}/private/$CAKEY
	    RET=$?
	else
	    echo "Making CA certificate ..."
	    $REQ -new -nodes -x509 -keyout ${CATOP}/private/$CAKEY \
			   -out ${CATOP}/$CACERT $DAYS
	    RET=$?
	fi
    fi
    ;;
-renewca)     
    echo "Current CA certificate information"
    echo "Note: current and renewed certificates MUST to have exactly the same subject."
    echo
    OLDSUBJECT=`$X509 -noout -in ${CATOP}/$CACERT -subject`
    echo $OLDSUBJECT
    echo
    echo "Renewing CA certificate ..."
    $REQ -new -nodes -x509 -key ${CATOP}/private/$CAKEY \
			   -out ${CATOP}/$CACERT.new $DAYS
    RET=$?
    echo
    echo "Verifying the subject of the renewed certificate ..."
    echo
    NEWSUBJECT=`$X509 -noout -in ${CATOP}/$CACERT.new -subject`
    echo current $OLDSUBJECT
    echo new $NEWSUBJECT
    echo
    if [ "$OLDSUBJECT" = "$NEWSUBJECT" ] ; then
	echo currect and new subjects are the same, certificate will be renewed.
	cp -f ${CATOP}/$CACERT.new ${CATOP}/$CACERT
    else
	echo currect and new subjects are not the same, certificate will not be renewed.
	rm -f ${CATOP}/$CACERT.new
    fi
    ;;
-xsign)
    $CA -policy policy_anything -infiles newreq.pem 
    RET=$?
    ;;
-sign|-signreq) 
    $CA -policy policy_anything -out newcert.pem -infiles newreq.pem
    RET=$?
    cat newcert.pem
    echo "Signed certificate is in newcert.pem"
    ;;
-revoke)
    $CA -policy policy_anything -revoke ${CATOP}/newcerts/$2.pem
    RET=$?
    exit $RET
    ;;
-gencrl)
    $CA -policy policy_anything -gencrl -out ${CATOP}/crl.pem 
    RET=$?
    cat ${CATOP}/crl.pem>crl.pem
    echo "Certificate Revokation List is in crl.pem"
    ;;
-signcert) 
    echo "Cert passphrase will be requested twice - bug?"
    $X509 -x509toreq -in newreq.pem -signkey newreq.pem -out tmp.pem
    $CA -policy policy_anything -out newcert.pem -infiles tmp.pem
    cat newcert.pem
    echo "Signed certificate is in newcert.pem"
    ;;
-verify) 
    shift
    if [ -z "$1" ]; then
	    $VERIFY -CAfile $CATOP/$CACERT newcert.pem
	    RET=$?
    else
	for j
	do
	    $VERIFY -CAfile $CATOP/$CACERT $j
	    if [ $? != 0 ]; then
		    RET=$?
	    fi
	done
    fi
    exit 0
    ;;
*)
    echo "Unknown arg $i";
    exit 1
    ;;
esac
done
exit $RET
