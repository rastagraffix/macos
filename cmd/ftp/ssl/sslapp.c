/* sslapp.c	- ssl application code */
/*-
 * Copyright (c) 2002, 2003, 2004, 2005 Nick Leuta
 * All rights reserved.
 *
 * This software is based on code developed by Tim Hudson <tjh@cryptsoft.com>
 * for use in the SSLftp project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef USE_SSL

#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef LINUX
#include <sys/time.h>
#endif

#include <port_base.h>

#include "sslapp.h"

BIO	*bio_err;
SSL	*ssl_con;
SSL_CTX	*ssl_ctx;
X509_STORE	*x509st_CRL = NULL;

int	ssl_debug_flag = 0;
int	ssl_active_flag = 0;
int	ssl_verify_flag = SSL_VERIFY_NONE;
int	ssl_logerr_syslog = 0;	/* Log error information to syslog */
int	ssl_verbose_flag = 0;
char	*ssl_cert_file = NULL;
char	*ssl_key_file = NULL;
char	*ssl_cipher_list = NULL;
char	*ssl_log_file = NULL;
char	*ssl_CA_file = NULL;
char	*ssl_CA_path = NULL;
char	*ssl_CRL_file = NULL;
char	*ssl_CRL_path = NULL;

/* fwd decl */
static void client_info_callback();

int
do_ssleay_init(int server)
{
    char *p;

    /* Make sure we have somewhere we can log errors to */
    if (bio_err == NULL) {
	    if ((bio_err = BIO_new(BIO_s_file())) != NULL) {
		    if (ssl_log_file == NULL) {
			    BIO_set_fp(bio_err, stderr, BIO_NOCLOSE);
		    } else {
			    if (BIO_write_filename(bio_err, ssl_log_file)<=0) {
			    /* not a lot we can do */
			    }
		    }
	    }
    }

    if (ssl_debug_flag)
	    ssl_log_msgn(bio_err, "SSL_DEBUG_FLAG on");

    /*
     * Init things so we will get meaningful error messages rather than
     * numbers
     */
    SSL_load_error_strings();
    SSLeay_add_ssl_algorithms();
    ssl_ctx = (SSL_CTX *)SSL_CTX_new(SSLv23_method());

    /*
     * We may require a temp 512 bit RSA key because of the wonderful way
     * export things work... If so we generate one now!
     */
    if (server) {
	    const char ctx_sid[] = "BSDftpd-ssl";
	    SSL_CTX_set_session_id_context(ssl_ctx, ctx_sid, strlen(ctx_sid));

	    if (SSL_CTX_need_tmp_RSA(ssl_ctx)) {
		    RSA *rsa;
		    BIGNUM *e;

		    if (ssl_debug_flag)
			    ssl_log_msgn(bio_err, "Generating temp (512 bit) RSA key...");

		    e = BN_new();
		    BN_set_word(e, RSA_F4);
		    RSA_generate_key_ex(rsa, 512, e, NULL);
		    if (ssl_debug_flag)
			    ssl_log_msgn(bio_err, "Generation of temp (512 bit) RSA key done");

		    if (!SSL_CTX_set_tmp_rsa(ssl_ctx, rsa)) {
			    ssl_log_msgn(bio_err, "Failed to assign generated temp RSA key!");
		    }
		    RSA_free(rsa);
		    if (ssl_debug_flag)
			    ssl_log_msgn(bio_err, "Assigned temp (512 bit) RSA key");
	    }
    }

    /*
     * Also switch on all the interoperability and bug workarounds so
     * that we will communicate with people that cannot read poorly
     * written specs :-)
     */
    SSL_CTX_set_options(ssl_ctx, SSL_OP_ALL);

    /* The user can set whatever ciphers they want to use */
    if (ssl_cipher_list == NULL) {
	    p = getenv("SSL_CIPHER");
	    if (p != NULL)
		    SSL_CTX_set_cipher_list(ssl_ctx, p);
    } else
	    SSL_CTX_set_cipher_list(ssl_ctx, ssl_cipher_list);

    /*
     * For verbose we use the 0.6.x info callback that I got eric to
     * finally add into the code :-) --tjh
     */
    if (ssl_verbose_flag)
	    SSL_CTX_set_info_callback(ssl_ctx, client_info_callback);

    /* Add in any certificates if you want to here... */
    if (ssl_cert_file) {
	    if (!SSL_CTX_use_certificate_file(ssl_ctx, ssl_cert_file,
		X509_FILETYPE_PEM)) {
		    ssl_log_err(bio_err, "Error loading '%s'", ssl_cert_file);
		    return (0);
	    } else {
		    if (!ssl_key_file)
			    ssl_key_file = ssl_cert_file;
			    if (!SSL_CTX_use_RSAPrivateKey_file(ssl_ctx,
				ssl_key_file, X509_FILETYPE_PEM)) {
				    ssl_log_err(bio_err, "Error loading '%s'",
					ssl_cert_file);
				    return (0);
			    }
	    }
    }

    /*
     * Check if certificate locations specified in command-line or
     * environment. Command-line values will override environment ones.
     */
    if (ssl_CA_file != NULL || ssl_CA_path != NULL ||
	getenv(X509_get_default_cert_file_env()) ||
	getenv(X509_get_default_cert_dir_env())) {
		SSL_CTX_load_verify_locations(ssl_ctx,
		    ssl_CA_file ? ssl_CA_file : getenv(X509_get_default_cert_file_env()),
		    ssl_CA_path ? ssl_CA_path : getenv(X509_get_default_cert_dir_env()));
    } else {
	    /*
	     * Make sure we will find certificates in the standard
	     * location ... Otherwise we don't look anywhere for these
	     * things which is going to make client certificate exchange
	     * rather useless :-)
	     */
	    SSL_CTX_set_default_verify_paths(ssl_ctx);
    }

    /*
     * Check if CRL locations specified in command-line or environment.
     * Command-line values will override environment ones.
     */
    if (ssl_CRL_file != NULL || ssl_CRL_path != NULL ||
	getenv("SSL_CRL_FILE") || getenv("SSL_CRL_DIR")) {
		x509st_CRL = ssl_X509_STORE_create(
		    ssl_CRL_file ? ssl_CRL_file : getenv("SSL_CRL_FILE"),
		    ssl_CRL_path ? ssl_CRL_path : getenv("SSL_CRL_DIR"));
    } else {
	    /*
	     * Set defaults for CRL locations
	     */
	    char ssl_CRL_file_tmp[MAXPATHLEN], ssl_CRL_path_tmp[MAXPATHLEN];
	    snprintf(ssl_CRL_file_tmp, sizeof(ssl_CRL_file_tmp), "%s/crl.pem",
		X509_get_default_cert_area());
	    snprintf(ssl_CRL_path_tmp, sizeof(ssl_CRL_path_tmp), "%s",
		X509_get_default_cert_dir());
	    x509st_CRL = ssl_X509_STORE_create(ssl_CRL_file_tmp,
		ssl_CRL_path_tmp);
    }

    SSL_CTX_set_verify(ssl_ctx, ssl_verify_flag, verify_cb_CRL);

    return (1);
}

static void
client_info_callback(SSL *s, int where, int ret)
{
    if (where == SSL_CB_CONNECT_LOOP) {
	ssl_log_msgn(bio_err, "SSL_connect:%s %s",
	    SSL_state_string(s), SSL_state_string_long(s));
    } else if (where == SSL_CB_CONNECT_EXIT) {
	if (ret == 0) {
	    ssl_log_msgn(bio_err, "SSL_connect:failed in %s %s",
		SSL_state_string(s), SSL_state_string_long(s));
	} else if (ret < 0) {
	    ssl_log_msgn(bio_err, "SSL_connect:error in %s %s",
		SSL_state_string(s), SSL_state_string_long(s));
	}
    }
}

/*
 * Certificate Revocation List (CRL) Storage Support
 *
 * CRL support is based on the similar CRL support developed by
 * Ralf S. Engelschall <rse@engelschall.com> for use in the
 * mod_ssl project (http://www.modssl.org/).
 */

/* Initialize X509_STORE structure (which holds the tables etc for verification
 * stuff).
 * arguments:
 *	cpFile - a file of CRLs in PEM format
 *	cpPath - a directory containing CRLs in PEM format and its hashes.
 * return:
 *	the pointer to X509_STORE structure or NULL if failed.
 * notes:
 *	This function is very similar to X509_STORE_load_locations(),
 *	the only principal difference is that return values of 
 *	X509_LOOKUP_*() calls aren't checked.
 */
X509_STORE *
ssl_X509_STORE_create(char *cpFile, char *cpPath)
{
    X509_STORE *pStore;
    X509_LOOKUP *pLookup;

    if (cpFile == NULL && cpPath == NULL)
        return NULL;
    if ((pStore = X509_STORE_new()) == NULL)
        return NULL;
    if (cpFile != NULL) {
        if ((pLookup = X509_STORE_add_lookup(pStore, X509_LOOKUP_file())) == NULL) {
            X509_STORE_free(pStore);
            return NULL;
        }
        X509_LOOKUP_load_file(pLookup, cpFile, X509_FILETYPE_PEM);
    }
    if (cpPath != NULL) {
        if ((pLookup = X509_STORE_add_lookup(pStore, X509_LOOKUP_hash_dir())) == NULL) {
            X509_STORE_free(pStore);
            return NULL;
        }
        X509_LOOKUP_add_dir(pLookup, cpPath, X509_FILETYPE_PEM);
    }
    return pStore;
}

/* This function is a wrapper around X509_STORE_get_by_subject().
 * Return values are the same, arguments nType, pName and pObj are the same
 * too, pStore is used for initialization of X509_STORE_CTX structure
 * which is used while validating a single certificate.
 */
int
ssl_X509_STORE_lookup(X509_STORE *pStore, int nType,
			X509_NAME *pName, X509_OBJECT *pObj)
{
    X509_STORE_CTX *pStoreCtx;
    int rc;

    pStoreCtx = X509_STORE_CTX_new();
    X509_STORE_CTX_init(pStoreCtx, pStore, NULL, NULL);
    rc = X509_STORE_get_by_subject(pStoreCtx, nType, pName, pObj);
    X509_STORE_CTX_free(pStoreCtx);
    return rc;
}

/* Certificate verify callback function which performs CRL-based revocation
 * checks. See SSL_CTX_set_verify() documentation for more information.
 * Reports next verification errors (see verify(1) for description):
 *	X509_V_ERR_CRL_SIGNATURE_FAILURE
 *	X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD
 *	X509_V_ERR_CRL_HAS_EXPIRED
 *	X509_V_ERR_CERT_REVOKED
 *
 * Comments written by Ralf S. Engelschall <rse@engelschall.com>.
 */
int
verify_cb_CRL(int ok, X509_STORE_CTX *ctx)
{
    X509_OBJECT *obj;
    X509_NAME *subject;
    X509_NAME *issuer;
    X509 *xs;
    X509_CRL *crl;
    X509_REVOKED *revoked;
    int i, n, rc;

    /*
     * Unless a revocation store for CRLs was created we
     * cannot do any CRL-based verification, of course.
     */
    if (!x509st_CRL)
        return ok;

    /*
     * Determine certificate ingredients in advance
     */
    xs      = X509_STORE_CTX_get_current_cert(ctx);
    subject = X509_get_subject_name(xs);
    issuer  = X509_get_issuer_name(xs);

    /*
     * OpenSSL provides the general mechanism to deal with CRLs but does not
     * use them automatically when verifying certificates, so we do it
     * explicitly here. We will check the CRL for the currently checked
     * certificate, if there is such a CRL in the store.
     *
     * We come through this procedure for each certificate in the certificate
     * chain, starting with the root-CA's certificate. At each step we've to
     * both verify the signature on the CRL (to make sure it's a valid CRL)
     * and it's revocation list (to make sure the current certificate isn't
     * revoked).  But because to check the signature on the CRL we need the
     * public key of the issuing CA certificate (which was already processed
     * one round before), we've a little problem. But we can both solve it and
     * at the same time optimize the processing by using the following
     * verification scheme (idea and code snippets borrowed from the GLOBUS
     * project):
     *
     * 1. We'll check the signature of a CRL in each step when we find a CRL
     *    through the _subject_ name of the current certificate. This CRL
     *    itself will be needed the first time in the next round, of course.
     *    But we do the signature processing one round before this where the
     *    public key of the CA is available.
     *
     * 2. We'll check the revocation list of a CRL in each step when
     *    we find a CRL through the _issuer_ name of the current certificate.
     *    This CRLs signature was then already verified one round before.
     *
     * This verification scheme allows a CA to revoke its own certificate as
     * well, of course.
     */

    /*
     * Try to retrieve a CRL corresponding to the _subject_ of
     * the current certificate in order to verify it's integrity.
     */
    obj = X509_OBJECT_new();
    rc = ssl_X509_STORE_lookup(x509st_CRL, X509_LU_CRL, subject, obj);
    crl = X509_OBJECT_get0_X509_CRL(obj);
    if (rc > 0 && crl != NULL) {
        /*
         * Verify the signature on this CRL
         */
        if (X509_CRL_verify(crl, X509_get_pubkey(xs)) <= 0) {
            X509_STORE_CTX_set_error(ctx, X509_V_ERR_CRL_SIGNATURE_FAILURE);
            X509_OBJECT_free(obj);
            return 0;
        }

        /*
         * Check date of CRL to make sure it's not expired
         */
        i = X509_cmp_current_time(X509_CRL_get_nextUpdate(crl));
        if (i == 0) {
            X509_STORE_CTX_set_error(ctx, X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD);
            X509_OBJECT_free(obj);
            return 0;
        }
        if (i < 0) {
            X509_STORE_CTX_set_error(ctx, X509_V_ERR_CRL_HAS_EXPIRED);
            X509_OBJECT_free(obj);
            return 0;
        }
        X509_OBJECT_free(obj);
    }

    /*
     * Try to retrieve a CRL corresponding to the _issuer_ of
     * the current certificate in order to check for revocation.
     */
    obj = X509_OBJECT_new();
    rc = ssl_X509_STORE_lookup(x509st_CRL, X509_LU_CRL, issuer, obj);
    crl = X509_OBJECT_get0_X509_CRL(obj);
    if (rc > 0 && crl != NULL) {
        /*
         * Check if the current certificate is revoked by this CRL
         */
        n = sk_X509_REVOKED_num(X509_CRL_get_REVOKED(crl));
        for (i = 0; i < n; i++) {
            revoked = sk_X509_REVOKED_value(X509_CRL_get_REVOKED(crl), i);
            if (ASN1_INTEGER_cmp(X509_REVOKED_get0_serialNumber(revoked),
              X509_get_serialNumber(xs)) == 0) {
                X509_STORE_CTX_set_error(ctx, X509_V_ERR_CERT_REVOKED);
                X509_OBJECT_free(obj);
                return 0;
            }
        }
        X509_OBJECT_free(obj);
    }
    return ok;
}

/*
 * Compare two X509 certificates.
 * return:
 *	 1 - certificates are not NULL and equal
 *	 0 - certificates are not NULL and differ
 *	-1 - both certificates are NULL
 *	-2 - x509_cert1 is NULL, x509_cert2 is not NULL
 *	-3 - x509_cert1 in not NULL, x509_cert2 is NULL
 */
int
ssl_X509_cmp(X509 *x509_cert1, X509 *x509_cert2)
{
    /* X509_cmp() will crash if any of its args are NULL
     */
    if (x509_cert1 != NULL) {
	if (x509_cert2 == NULL) {
	    return -3; /* x509_cert1 in not NULL, x509_cert2 is NULL */
	} else {
	    if (X509_cmp(x509_cert1, x509_cert2)) {
		return 0; /* certificates are differ */
	    } else {
		return 1; /* certificates are equal */
	    }
	}
    } else {
	if (x509_cert2 == NULL) {
	    return -1; /* both certificates are NULL */
	} else {
	    return -2; /* x509_cert1 is NULL, x509_cert2 is not NULL */
	}
    }
}

/*
 * Log an error and a debug information.
 */

/*
 * Log the message.
 */
void
ssl_log_msg(BIO *bio, const char *fmt, ...)
{
    va_list ap;
    char *outputbuf;

    va_start(ap, fmt);
    vasprintf(&outputbuf, fmt, ap);
    va_end(ap);
    if (outputbuf == NULL) {
	    BIO_printf(bio, "\r\nRan out of memory.\r\n");
	    BIO_flush(bio);
	    return;
    }

    BIO_printf(bio, "%s", outputbuf);
    BIO_flush(bio);

    free(outputbuf);
}

/*
 * Log the message prepended and appended by the newline.
 */
void
ssl_log_msgn(BIO *bio, const char *fmt, ...)
{
    va_list ap;
    char *outputbuf;

    va_start(ap, fmt);
    vasprintf(&outputbuf, fmt, ap);
    va_end(ap);
    if (outputbuf == NULL) {
	    BIO_printf(bio, "\r\nRan out of memory.\r\n");
	    BIO_flush(bio);
	    return;
    }

    BIO_printf(bio, "\r\n%s\r\n", outputbuf);
    BIO_flush(bio);

    free(outputbuf);
}

/*
 * Common code for both ssl_log_vwarn() and ssl_log_vwarn_debug().
 */
void
ssl_log_vwarn_common(BIO *bio, int debug_flag, const char *fmt, va_list ap)
{
    char *tmp, *outputbuf;

    vasprintf(&tmp, fmt, ap);
    if (tmp == NULL) {
	    BIO_printf(bio, "\r\nRan out of memory.\r\n");
	    BIO_flush(bio);
	    if (ssl_logerr_syslog)
		    syslog(LOG_ERR, "Ran out of memory.");
	    return;
    }

    asprintf(&outputbuf, "%s: %s", tmp,
	    debug_flag ? ERR_error_string(ERR_get_error(), NULL) :
	    ERR_reason_error_string(ERR_get_error()));
    free(tmp);
    if (outputbuf == NULL) {
	    BIO_printf(bio, "\r\nRan out of memory.\r\n");
	    BIO_flush(bio);
	    if (ssl_logerr_syslog)
		    syslog(LOG_ERR, "Ran out of memory.");
	    return;
    }

    BIO_printf(bio, "%s\r\n", outputbuf);
    BIO_flush(bio);
    if (ssl_logerr_syslog)
	    syslog(LOG_WARNING, "%s", outputbuf);
    free(outputbuf);
}

/*
 * Log the message appended by the reason of the last error code from the SSL
 * error queue and removes that code from the queue.
 */
void
ssl_log_vwarn(BIO *bio, const char *fmt, va_list ap)
{
    ssl_log_vwarn_common(bio, 0, fmt, ap);
}

void
ssl_log_warn(BIO *bio, const char *fmt, ...)
{
    va_list ap;

    va_start(ap,fmt);
    ssl_log_vwarn(bio, fmt, ap);
    va_end(ap);
}

/*
 * Log the message appended by the human-readable string that represents the
 * last error code from the SSL error queue and removes it.
 */
void
ssl_log_vwarn_debug(BIO *bio, const char *fmt, va_list ap)
{
    ssl_log_vwarn_common(bio, 1, fmt, ap);
}

void
ssl_log_warn_debug(BIO *bio, const char *fmt, ...)
{
    va_list ap;

    va_start(ap,fmt);
    ssl_log_vwarn_debug(bio, fmt, ap);
    va_end(ap);
}

/*
 * Log the message with the verbosity level depending on the SSL debug state.
 */
void
ssl_log_err(BIO *bio, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);

    if (ssl_debug_flag) {
	    ssl_log_vwarn_debug(bio, fmt, ap);
    } else {
	    ssl_log_vwarn(bio, fmt, ap);
    }

    va_end(ap);
}

/*
 * Support for the X.509 client authentication.
 */

/*
 * Get the text value by the object type.
 * arguments:
 *	obj_type - object type name, may be provided in "short" (SN; for
 *	           example "CN") or "long" (LN; for example "commonName")
 *	           form.
 * x509_name, str and len means the same as correspondent arguments of
 * X509_NAME_get_text_by_NID().
 *	str - buffer for returning the string value.
 *	len - size of the buffer.
 * return:
 *	>=0 - actual length of the returned string str.
 *	-1  - object not found.
 *	-2  - unknown object type in obj_type.
 * notes:
 *	This function is a wrapper around X509_NAME_get_text_by_NID().
 */
int
ssl_X509_NAME_get_text_by_name(X509_NAME *x509_name, char *obj_type,
    char *str, int len)
{
    int nid;
    
    if ((nid=OBJ_txt2nid(obj_type)) == NID_undef) {
	return -2;
    }
    return (X509_NAME_get_text_by_NID(x509_name, nid, str, len));
}

/*
 * Get the login name from the certificate.
 * arguments:
 *	cert     - X.509 certificate.
 *	obj_type - object type name, may be provided in "short" (SN; for
 *	           example "CN") or "long" (LN; for example "commonName")
 *	           form.
 *	username - buffer for returning the string value.
 *	len      - size of the buffer.
 * return:
 *	1   - if no errors occurred.
 *	<=0 - if any error(s) occurred.
 * notes:
 *	in future the list of return values <=0 may be extended, currently
 *	only 0 is supported.
 */
int
x509_get_value(X509 *cert, char *obj_type, char *username, int len)
{
    X509_NAME *x509_subject_name;
    int ret = -1;

    x509_subject_name = X509_get_subject_name(cert);
    if (x509_subject_name != NULL)
	ret = ssl_X509_NAME_get_text_by_name(x509_subject_name, obj_type,
					    username, len);

    if (ret > 0)
	return 1;
    else
	return 0;
}

/*
 * Get the login name by the e-mail address from the certificate.
 * arguments:
 *	cert     - X.509 certificate.
 *	obj_type - object type name, may be provided in "short" (SN; for
 *	           example "CN") or "long" (LN; for example "commonName")
 *	           form.
 *	domain   - domain name that must be after the `@' symbol in the e-mail
 *		   address; ignored if NULL.
 *	username - buffer for returning the string value.
 *	len      - size of the buffer.
 * return:
 *	1   - if login name extracted successfully.
 *	<=0 - if any error(s) occurred.
 * notes:
 *	E-mail address is expected in form: "username@domain.name". If domain
 *	is not NULL, it will be verified against "domain.name", after that
 *	"username" will be returned as the login name.
 *
 *	In future the list of return values <=0 may be extended, currently
 *	only 0 is supported.
 */
int
ssl_get_login_by_email(X509 *cert, char *obj_type, char *domain,
			char *username, int len)
{
    char email[512], *cp_login, *cp_domain;

    /* Extract the field from the cert */
    if (x509_get_value(cert, obj_type, email, sizeof(email)) > 0) {
	/* Process e-mail */
	cp_login = email;
	cp_domain = strchr(email, '@');
	if (!cp_domain) {
	    /* Invalid e-mail address */
	    return 0;
	}
	*cp_domain++ = '\0';

	/* If domain is specified, it must be compared with the cert's one */
	if (domain != NULL) {
	    if (strcasecmp(cp_domain, domain) != 0) {
		/* Cert's e-mail domain doesn't match the expected one */
		return 0;
	    }
	}

	strncpy(username, cp_login, len);
	return 1;
    }

    /* Error extracting a field from the cert */
    if (ssl_debug_flag)
	ssl_log_msgn(bio_err,
	    "ssl_get_login_by_email(): error extracting field: %s", obj_type);
    return 0;
}

/*
 * Check if the client's certificate is presented in the specified file.
 * arguments:
 *	cert     - client's X.509 certificate.
 *	filename - name of the file that contains a set of certificates.
 * return:
 *	1 - if file contains the provided certificate.
 *	0 - otherwise, or if any error(s) occurred.
 */
int
ssl_check_cert_file(X509 *cert, char *filename)
{
    int ret = 0;
    FILE *fp;
    X509 *file_cert;
    struct stat stbuf;

    if (!cert || !filename) {
	if (ssl_debug_flag)
	    ssl_log_msgn(bio_err,
		"ssl_check_cert_file(): at least one argument in NULL");
	return 0;
    }

    if (lstat(filename, &stbuf) < 0 || !S_ISREG(stbuf.st_mode)) {
	if (ssl_debug_flag)
	    ssl_log_msgn(bio_err,
		"ssl_check_cert_file(): '%s' is not a plain file or is a symlink",
		filename);
    	return 0;
    }

    if (!(fp = fopen(filename, "r"))) {
	if (ssl_debug_flag)
	    ssl_log_msgn(bio_err,
		"ssl_check_cert_file(): can't open '%s' for reading", filename);
    	return 0;
    }

    while ((file_cert = PEM_read_X509(fp, NULL, NULL, NULL))) {
	if (ssl_X509_cmp(cert, file_cert) == 1)
	    ret = 1;
	X509_free(file_cert);
	if (ret)
	    break;
    }

    fclose(fp);
    return ret;
}

/* Check the client's login name and the certificate with help of an external
 * program.
 * arguments:
 *	cert     - client's X.509 certificate.
 *	name     - non-NULL string that contains the login name provided by the
 *                 client; if it is a zero-length string (i.e. the first symbol
 *                 is '\0'), it will be used as a buffer for returning the
 *                 login name.
 *	len      - size of the buffer; ignored if name has non-zero length.
 *	progname - full path to the external authentication program.
 * return:
 *	1 - if the external authenticator allows access to the requested
 *	    account.
 *	0 - otherwise, or if any error(s) occurred.
 */
int
ssl_check_ext_prog(X509 *cert, char *name, int len, char *progname)
{
    int ret = 0, err;
    int p_in[2], p_out[2]; /* pipes for child's stdin and stdout */
    FILE *f_in = NULL;     /* stream for child's stdin */
    pid_t child_pid = -1;
    int oldmask, status;
    char buf[BUFSIZ], *cp_rcode, *cp_name, *str;
    fd_set rfds;
    struct timeval tv;

    if (!cert || !name || !progname) {
	if (ssl_debug_flag)
	    ssl_log_msgn(bio_err,
		"ssl_check_ext_prog(): at least one argument is NULL");
	return 0;
    }

    if (pipe(p_in)) {
    	ret = 0;
	goto end;
    }
    if (pipe(p_out)) {
    	ret = 0;
	goto end;
    }

    /*
     * Fork
     */
    child_pid = fork();
    /* Is it the parent or the child process? */
    switch (child_pid) {
    case -1: /* fork() failed and it is still to be the parent process */
	ret = 0;
	goto end;
    case  0: /* In the child */
	/* Map stdin/stdout/stderr to the pipe to the parent */
        dup2(p_in[0], 0);  /* stdin */
        dup2(p_out[1], 1); /* stdout */
	dup2(p_out[1], 2); /* stderr */

	/* Close descriptors of pipes */
        close(p_in[0]);
        close(p_in[1]);
        close(p_out[1]);
        close(p_out[0]);

	/* Exec */
        execl(progname, progname, NULL);
	/* Exec failed */
        _exit(1);
    }

    /*
     * In the parent process
     */

    /* Those ends of pipes must not be used by the parent */
    close(p_in[0]);
    close(p_out[1]);

    /* Get the control over child's stdin */
    if (!(f_in = fdopen(p_in[1], "w"))) {
	ret = 0;
	goto end;
    }

    /* Send tokens to the auth program */
    fprintf(f_in, "%s\r\n", name);
    PEM_write_X509(f_in, cert);
    fflush(f_in);

    /* Wait for the result of the authentication from child's stdout */
    FD_ZERO(&rfds);
    FD_SET(p_out[0], &rfds);

    tv.tv_sec = 20; /* auth timeout */
    tv.tv_usec = 0;

    err = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
    if (err < 1) { /* timeout expired */
	kill(child_pid, SIGTERM); /* terminate the ext. program */
	ret = 0;
	if (ssl_debug_flag)
	    ssl_log_msgn(bio_err,
		"No response from the external authentication program");
	goto end;
    }

    /* Read the result of the authentication */
    err = read(p_out[0], buf, sizeof(buf) - 1);
    if (err <= 7) { /* error reading the reply of the authenticator, or an
		     * incorrect reply (min length == 7) */
	ret = 0;
	if (ssl_debug_flag)
	    ssl_log_msgn(bio_err,
		"Invalid response from the external authentication program");
	goto end;
    }
    buf[err] = '\0';

    cp_rcode = buf;
    str = strchr(buf, '\n');
    if (str) {
	*str++ = '\0';
	cp_name = str;
    }
    else {
	ret = 0;
	goto end;
    }
    /* Wipe cr/lf symbols */
    str = strchr(cp_rcode, '\r');
    if (str)
	*str = '\0';
    str = strchr(cp_name, '\r');
    if (str)
	*str = '\0';
    str = strchr(cp_name, '\n');
    if (str)
	*str = '\0';

    /* Process the result of the authentication */
    if (cp_rcode[0] == '1') { /* 1xx - access allowed by the program */
	if (strlen(name) > 0) {
	    /* Sanity checking */
	    ret = (strcmp(name, cp_name) == 0) ? 1 : 0;
	} else {
	    /* Return the login name extracted from the certificate */
	    strncpy(name, cp_name, len);
	    ret = 1;
	}
    } else {
	ret = 0;
    }

end:
    /* Close streams */
    if (f_in!=NULL)
	fclose(f_in);

    /* Close used ends of pipes */
    close(p_in[1]);
    close(p_out[0]);
    /* Close other ends of pipes, normally it isn't needed */
    close(p_in[0]);
    close(p_out[1]);

    /* Wait for child's termination */
    if (child_pid > 0) {
	oldmask = sigblock(sigmask(SIGINT)|sigmask(SIGQUIT)|sigmask(SIGHUP));
	while (waitpid(child_pid, &status, 0) < 0 && errno == EINTR)
	    continue;
	(void)sigsetmask(oldmask);
    }

    return ret;
}

/* Do the X.509 user authentication.
 * arguments:
 *	service - name of the service which requests the auth.
 *	cert - an X.509 certificate.
 *	name - the login name provided by the client.
 * return:
 *	1   - the X.509 certificate is authorized to use the name.
 *	<=0 - otherwise, or if the peer doesn't provide the cert (for any
 *	      reason, include an unencrypted connection), or if any error(s)
 *	      occurred.
 *	 0  - the X.509 certificate is NOT authorized to use the name.
 *	-5  - the certificate is not provided.
 *	-6  - the service name is not provided.
 *	-7  - error opening _PATH_X509_AUTH
 */
int
ssl_x509_auth(char *service, X509 *cert, char *name)
{
    FILE *user_fp;
    char buf[_X509_AUTH_MAXSTRLEN];
    int ret = 1; /* 0 - all ok, !=0 - something wrong */

    char username[MAXLOGNAME];
    char *ssl_auth_name = NULL;

    /* Sanity checking */
    if (service == NULL) {
	/* The service name isn't provided */
	if (ssl_debug_flag)
	    ssl_log_msgn(bio_err, "ssl_x509_auth(): service name is NULL");
	return -6;
    }

    if (cert != NULL) {
	/* Get the certificate subject */
	ssl_auth_name = (char*)ONELINE_NAME(X509_get_subject_name(cert));
    } else {
	/* Can't authenticate because no peer certificate provided */
	if (ssl_debug_flag)
	    ssl_log_msgn(bio_err, "ssl_x509_auth(): certificate is NULL");
	return -5;
    }

    user_fp = fopen(_PATH_X509_AUTH, "r");
    if (!user_fp) {
	if (ssl_debug_flag)
	    ssl_log_msgn(bio_err,
		"ssl_x509_auth(): can't open '%s' for reading", _PATH_X509_AUTH);
	return -7;
    }

    while (fgets(buf, sizeof(buf), user_fp)) {
	char *cp_service, *cp_action, *cp_userlist, *cp_subject;
	char *n;
	int action_flag; /* deny (0) | allow (1) */

	/* Allow comments in the file */
	if (buf[0]=='#')
	    continue;

	/* Wipe cr/lf symbols */
	n = strchr(buf, '\n');
	if (n)
	    *n = '\0';
	n = strchr(buf, '\r');
	if (n)
	    *n = '\0';

	/*
	 * Process fields
	 */
	/* Point to the service name */
	cp_service = buf;

	/* Point to "action", NUL-terminate "service" */
	cp_action = strchr(cp_service, ':');
	if (!cp_action)
	    continue;
	*cp_action++ = '\0';

	/* Check if the service name matches with the requested one */
	if (strlen(cp_service) == 0)
	    continue;
	else {
	    if (strcmp(cp_service, service))
		continue;
	}

	/* Point to "userlist", NUL-terminate "action" */
	cp_userlist = strchr(cp_action, ':');
	if (!cp_userlist)
	    continue;
	*cp_userlist++ = '\0';

	/* Process the action, set the action flag */
	switch (strlen(cp_action) == 0) {
	case 1:
	    continue;
	case 0:
	    if(!strcmp(cp_action, "deny")) {
		action_flag = 0;
		break;
	    }
	    if(!strcmp(cp_action, "allow")) {
		action_flag = 1;
		break;
	    }
	default:
	    ssl_log_msgn(bio_err, "%s: invalid action: %s", _PATH_X509_AUTH,
		cp_action);
	    continue;
	}

	/* Point to the subject template, NUL-terminate "userlist" */
	cp_subject = strchr(cp_userlist, ':');
	if (!cp_subject)
	    continue;
	*cp_subject++ = '\0';

	/*
	 * Process the userlist
	 */
	n = cp_userlist;
	while (n) {
	    /* Get next template for allowed login name from the list */
	    cp_userlist = strchr(n, ',');
	    if (cp_userlist)
		*cp_userlist++ = '\0';

	    /* Process the template and prepare the username */
	    switch (n[0]) {
	    case '/': /* username is a field of provided subject */
		if (n[1] == '/') {
	    	    /* The field contains the e-mail address */
	    	    char *domain, field[BUFSIZ];

		    domain = strchr(n + 2, '/');
		    if (domain)
			*domain++ = '\0';
		    strncpy(field, n + 2, sizeof(field));

		    if (ssl_get_login_by_email(cert, field, domain, username,
			    sizeof(username)) <= 0) {
			n = cp_userlist;
			continue;
		    }
		} else {
	    	    /* The field contains the username itself */
		    if (x509_get_value(cert, n + 1, username, 
			    sizeof(username)) <= 0) {
			n = cp_userlist;
			continue;
		    }
		}
		break;
	    case '*': /* Username is issued by the client */
		strncpy(username, name, sizeof(username));
		break;
	    default: /* The username itself */
		strncpy(username, n, sizeof(username));
	    }

	    /* Compare the provided login name against the one from userlist */
	    if (!strcmp(name, username)) {
		ret = 0;
		break;
	    }

	    n = cp_userlist;
	}

	/* Compare the certificate against the allowed one */
	if (ret == 0) {
	    if (cp_subject[0] == '-') {
		/* The allowed certificate is a complex expression */
		switch (cp_subject[1]) {
		case 'r': { /* subject is defined as a reqular expression */
		    regex_t preg;
		    cp_subject = cp_subject + 2;

    		    regcomp(&preg, cp_subject, REG_EXTENDED|REG_NOSUB);
    		    ret = regexec(&preg, ssl_auth_name, 0, 0, 0);
    		    regfree(&preg);
		    break;
		}
		case 'f': { /* certificate itself in a file */
		    struct passwd *pwd;
		    char fnbuf[MAXPATHLEN + 1];
		    cp_subject = cp_subject + 2;

		    /* Do '~' expansion */
		    if (cp_subject[0] == '~') {
			char *cp;

			cp = cp_subject + 1;
			if (!(pwd = getpwnam(name))) {
			    if (ssl_debug_flag)
				ssl_log_msgn(bio_err,
				    "ssl_x509_auth(): no such user: %s", name);
			    break;
			}
			snprintf(fnbuf, sizeof(buf), "%s/%s", pwd->pw_dir, cp);
		    } else {
		        snprintf(fnbuf, sizeof(buf), cp_subject);
		    }

		    ret = !ssl_check_cert_file(cert, fnbuf);
		    break;
		}
		case 'p': { /* send the cert to the external program */
		    cp_subject = cp_subject + 2;

		    ret = !ssl_check_ext_prog(cert, name, strlen(name),
					    cp_subject);
		    break;
		}
		default:
		    if (ssl_debug_flag)
			ssl_log_msgn(bio_err,
			    "ssl_x509_auth(): incorrect directive: -%c",
			    cp_subject[1]);
		    ret = -1;
		}
	    } else {
    		/* The pre-configured subject */
    		ret = strcmp(ssl_auth_name, cp_subject);
	    }
	}
	if (ret == 0) { 
	    /* Match is found */
	    fclose(user_fp);
	    switch (action_flag) {
	    case 0: /* deny */
		return 0;
	    case 1: /* allow */
		return 1;
	    default:
		/* Programming error */
		ssl_log_msgn(bio_err,
"ssl_x509_auth(): internal: unhandled action flag '%d' (action '%s')",
		    action_flag, cp_action);
	    }
	}
    }

    /* No match is found */
    fclose(user_fp);
    return 0;
}

#else /* !USE_SSL */

/* Something here to stop warnings if we build without TLS/SSL support */
static int
dummy_func()
{
    return 0;
}

#endif /* USE_SSL */
