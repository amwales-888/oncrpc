/*
 * tls.c - TLS for RPC-with-TLS (RFC 9289), on OpenSSL 3.
 *
 * OpenSSL never touches a socket here. Each session reads the bytes that
 * arrived (fed in by the client or server code) from one memory BIO and
 * leaves the bytes to send in another, which the caller writes with libuv.
 * That keeps everything on the caller's loop and never blocks.
 *
 * Memory: liboncrpc's own allocations use the application's allocator, but
 * OpenSSL allocates internally with its own functions; an application that
 * wants those routed too can call OpenSSL's CRYPTO_set_mem_functions before
 * any other OpenSSL use.
 */
#include "internal.h"

#include <stdio.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

struct oncRPCTlsConfig {
    const oncRPCAllocator *alloc;
    SSL_CTX               *ctx;
    oncRPCTlsRole          role;
    int                    hasCert;
    int                    refs;
};

struct tlsSession {
    const oncRPCAllocator *alloc;
    SSL                   *ssl;
    BIO                   *in;     /* bytes received, for OpenSSL to read */
    BIO                   *out;    /* bytes OpenSSL wants sent */
    char                   err[192];
};

/*
 * The ALPN protocol id that RFC 9289 registers for RPC-with-TLS, in ALPN's
 * wire form (a length byte, then the id). It is written as byte values: the
 * id is a protocol name fixed by the RFC.
 */
static const unsigned char alpn_id[] = { 6, 0x73, 0x75, 0x6e, 0x72, 0x70, 0x63 };

/* ---- Errors -------------------------------------------------------------------- */

/* OpenSSL's reason for the most recent failure, and clear its error queue
 * (which is per thread and shared with anything else using OpenSSL). */
static const char *openssl_reason(void)
{
    unsigned long e = ERR_peek_last_error();
    const char *r = e != 0 ? ERR_reason_error_string(e) : NULL;
    ERR_clear_error();
    return r != NULL ? r : "unknown error";
}

static void set_error(tlsSession *t, const char *what)
{
    long v = SSL_get_verify_result(t->ssl);
    if (v != X509_V_OK)
        snprintf(t->err, sizeof(t->err), "%s: certificate %s", what,
                 X509_verify_cert_error_string(v));
    else
        snprintf(t->err, sizeof(t->err), "%s: %s", what, openssl_reason());
    ERR_clear_error();
}

/* ---- Configurations ---------------------------------------------------------- */

static int select_alpn(SSL *ssl, const unsigned char **out, unsigned char *outlen,
                       const unsigned char *in, unsigned int inlen, void *arg)
{
    unsigned char *chosen = NULL;
    (void)ssl;
    (void)arg;
    if (SSL_select_next_proto(&chosen, outlen, alpn_id, (unsigned int)sizeof(alpn_id), in,
                              inlen) != OPENSSL_NPN_NEGOTIATED)
        return SSL_TLSEXT_ERR_ALERT_FATAL;   /* the client wants another protocol */
    *out = chosen;
    return SSL_TLSEXT_ERR_OK;
}

oncRPCStat oncRPCTlsConfigCreate(const oncRPCAllocator *alloc, oncRPCTlsRole role,
                                 oncRPCTlsConfig **out)
{
    oncRPCTlsConfig *cfg;

    if (out != NULL)
        *out = NULL;
    if (alloc == NULL || out == NULL || (role != ONCRPC_TLS_CLIENT && role != ONCRPC_TLS_SERVER))
        return ONCRPC_ERR_INVALID;
    cfg = (oncRPCTlsConfig *)oncRPCAlloc(alloc, sizeof(*cfg));
    if (cfg == NULL)
        return ONCRPC_ERR_NOMEM;
    cfg->alloc = alloc;
    cfg->role = role;
    cfg->refs = 1;
    cfg->ctx = SSL_CTX_new(role == ONCRPC_TLS_SERVER ? TLS_server_method() : TLS_client_method());
    if (cfg->ctx == NULL || !SSL_CTX_set_min_proto_version(cfg->ctx, TLS1_3_VERSION)) {
        ERR_clear_error();
        SSL_CTX_free(cfg->ctx);
        oncRPCFree(alloc, cfg);
        return ONCRPC_ERR_TLS;
    }
    if (role == ONCRPC_TLS_SERVER) {
        SSL_CTX_set_alpn_select_cb(cfg->ctx, select_alpn, NULL);
        SSL_CTX_set_verify(cfg->ctx, SSL_VERIFY_NONE, NULL);
        /* Connections are long-lived: no session tickets to resume them. */
        (void)SSL_CTX_set_num_tickets(cfg->ctx, 0);
    } else {
        /* SSL_CTX_set_alpn_protos returns 0 on success. */
        if (SSL_CTX_set_alpn_protos(cfg->ctx, alpn_id, (unsigned int)sizeof(alpn_id)) != 0) {
            ERR_clear_error();
            SSL_CTX_free(cfg->ctx);
            oncRPCFree(alloc, cfg);
            return ONCRPC_ERR_TLS;
        }
        SSL_CTX_set_verify(cfg->ctx, SSL_VERIFY_PEER, NULL);
    }
    *out = cfg;
    return ONCRPC_OK;
}

void oncRPCIntTlsConfigRef(oncRPCTlsConfig *cfg)
{
    if (cfg != NULL)
        cfg->refs++;
}

void oncRPCIntTlsConfigUnref(oncRPCTlsConfig *cfg)
{
    if (cfg == NULL || --cfg->refs > 0)
        return;
    SSL_CTX_free(cfg->ctx);
    oncRPCFree(cfg->alloc, cfg);
}

void oncRPCTlsConfigFree(oncRPCTlsConfig *cfg)
{
    oncRPCIntTlsConfigUnref(cfg);
}

int oncRPCIntTlsConfigIsServer(const oncRPCTlsConfig *cfg)
{
    return cfg != NULL && cfg->role == ONCRPC_TLS_SERVER;
}

int oncRPCIntTlsConfigHasCert(const oncRPCTlsConfig *cfg)
{
    return cfg != NULL && cfg->hasCert;
}

oncRPCStat oncRPCTlsConfigSetCertificate(oncRPCTlsConfig *cfg, const char *certPem,
                                         const char *keyPem)
{
    BIO *b;
    X509 *x;
    EVP_PKEY *key;
    int ok;

    if (cfg == NULL || certPem == NULL || keyPem == NULL)
        return ONCRPC_ERR_INVALID;
    /* The certificate, then any intermediates. */
    b = BIO_new_mem_buf(certPem, -1);
    if (b == NULL)
        return ONCRPC_ERR_NOMEM;
    x = PEM_read_bio_X509(b, NULL, NULL, NULL);
    ok = x != NULL && SSL_CTX_use_certificate(cfg->ctx, x) == 1;
    X509_free(x);
    (void)SSL_CTX_clear_chain_certs(cfg->ctx);
    while (ok && (x = PEM_read_bio_X509(b, NULL, NULL, NULL)) != NULL) {
        /* add0 takes the reference on success */
        if (SSL_CTX_add0_chain_cert(cfg->ctx, x) != 1) {
            X509_free(x);
            ok = 0;
        }
    }
    BIO_free(b);
    ERR_clear_error();   /* the end of the PEM text reads as an error */
    if (!ok)
        return ONCRPC_ERR_TLS;

    b = BIO_new_mem_buf(keyPem, -1);
    if (b == NULL)
        return ONCRPC_ERR_NOMEM;
    key = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);
    BIO_free(b);
    ok = key != NULL && SSL_CTX_use_PrivateKey(cfg->ctx, key) == 1 &&
         SSL_CTX_check_private_key(cfg->ctx) == 1;
    EVP_PKEY_free(key);
    ERR_clear_error();
    if (!ok)
        return ONCRPC_ERR_TLS;
    cfg->hasCert = 1;
    return ONCRPC_OK;
}

oncRPCStat oncRPCTlsConfigAddTrusted(oncRPCTlsConfig *cfg, const char *pem)
{
    X509_STORE *store;
    BIO *b;
    X509 *x;
    int n = 0, ok = 1;

    if (cfg == NULL || pem == NULL)
        return ONCRPC_ERR_INVALID;
    store = SSL_CTX_get_cert_store(cfg->ctx);
    b = BIO_new_mem_buf(pem, -1);
    if (b == NULL)
        return ONCRPC_ERR_NOMEM;
    while (ok && (x = PEM_read_bio_X509(b, NULL, NULL, NULL)) != NULL) {
        ok = X509_STORE_add_cert(store, x) == 1;
        X509_free(x);
        n++;
    }
    BIO_free(b);
    ERR_clear_error();
    return ok && n > 0 ? ONCRPC_OK : ONCRPC_ERR_TLS;
}

oncRPCStat oncRPCTlsConfigUseSystemTrust(oncRPCTlsConfig *cfg)
{
    int ok;
    if (cfg == NULL)
        return ONCRPC_ERR_INVALID;
    ok = SSL_CTX_set_default_verify_paths(cfg->ctx) == 1;
    ERR_clear_error();
    return ok ? ONCRPC_OK : ONCRPC_ERR_TLS;
}

oncRPCStat oncRPCTlsConfigSetVerify(oncRPCTlsConfig *cfg, oncRPCTlsVerify mode)
{
    int flags;
    if (cfg == NULL)
        return ONCRPC_ERR_INVALID;
    switch (mode) {
    case ONCRPC_TLS_VERIFY_NONE:
        flags = SSL_VERIFY_NONE;
        break;
    case ONCRPC_TLS_VERIFY_PEER:
        flags = SSL_VERIFY_PEER;
        break;
    case ONCRPC_TLS_VERIFY_REQUIRE:
        flags = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        break;
    default:
        return ONCRPC_ERR_INVALID;
    }
    SSL_CTX_set_verify(cfg->ctx, flags, NULL);
    return ONCRPC_OK;
}

/* ---- Sessions ------------------------------------------------------------------- */

/* Whether name is an IPv4 or IPv6 address rather than a DNS name. */
static int is_ip(const char *name)
{
    struct sockaddr_in a4;
    struct sockaddr_in6 a6;
    return uv_ip4_addr(name, 0, &a4) == 0 || uv_ip6_addr(name, 0, &a6) == 0;
}

tlsSession *oncRPCIntTlsStart(oncRPCTlsConfig *cfg, const char *serverName)
{
    tlsSession *t;

    if (cfg == NULL)
        return NULL;
    t = (tlsSession *)oncRPCAlloc(cfg->alloc, sizeof(*t));
    if (t == NULL)
        return NULL;
    t->alloc = cfg->alloc;
    t->ssl = SSL_new(cfg->ctx);
    t->in = BIO_new(BIO_s_mem());
    t->out = BIO_new(BIO_s_mem());
    if (t->ssl == NULL || t->in == NULL || t->out == NULL)
        goto fail;
    /* An empty input BIO means "wait for more", not end of file. */
    BIO_set_mem_eof_return(t->in, -1);
    SSL_set_bio(t->ssl, t->in, t->out);   /* the SSL now owns both */
    if (cfg->role == ONCRPC_TLS_SERVER) {
        SSL_set_accept_state(t->ssl);
    } else {
        SSL_set_connect_state(t->ssl);
        if (serverName != NULL && serverName[0] != '\0') {
            if (is_ip(serverName)) {
                if (X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(t->ssl), serverName) != 1)
                    goto fail_owned;
            } else if (SSL_set1_host(t->ssl, serverName) != 1 ||
                       SSL_set_tlsext_host_name(t->ssl, serverName) != 1) {
                goto fail_owned;
            }
        }
    }
    return t;

fail:
    BIO_free(t->in);
    BIO_free(t->out);
fail_owned:
    SSL_free(t->ssl);
    ERR_clear_error();
    oncRPCFree(cfg->alloc, t);
    return NULL;
}

void oncRPCIntTlsFree(tlsSession *t)
{
    if (t == NULL)
        return;
    SSL_free(t->ssl);   /* frees the BIOs too */
    ERR_clear_error();
    oncRPCFree(t->alloc, t);
}

int oncRPCIntTlsFeed(tlsSession *t, const uint8_t *data, size_t len)
{
    size_t done;
    if (len == 0)
        return 0;
    if (BIO_write_ex(t->in, data, len, &done) != 1 || done != len) {
        snprintf(t->err, sizeof(t->err), "out of memory");
        ERR_clear_error();
        return -1;
    }
    return 0;
}

int oncRPCIntTlsHandshake(tlsSession *t)
{
    int r = SSL_do_handshake(t->ssl);
    if (r == 1)
        return 1;
    switch (SSL_get_error(t->ssl, r)) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
        return 0;
    default:
        set_error(t, "TLS handshake failed");
        return -1;
    }
}

int oncRPCIntTlsRead(tlsSession *t, uint8_t *buf, size_t cap, size_t *got)
{
    *got = 0;
    if (cap == 0)
        return 0;
    if (SSL_read_ex(t->ssl, buf, cap, got) == 1)
        return 1;
    switch (SSL_get_error(t->ssl, 0)) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
        ERR_clear_error();
        return 0;
    case SSL_ERROR_ZERO_RETURN:
        snprintf(t->err, sizeof(t->err), "the peer closed TLS");
        ERR_clear_error();
        return -1;
    default:
        set_error(t, "TLS failed");
        return -1;
    }
}

int oncRPCIntTlsWrite(tlsSession *t, const uint8_t *data, size_t len)
{
    size_t done;
    if (len == 0)
        return 0;
    if (SSL_write_ex(t->ssl, data, len, &done) != 1 || done != len) {
        set_error(t, "TLS write failed");
        return -1;
    }
    return 0;
}

int oncRPCIntTlsOutput(tlsSession *t, const oncRPCAllocator *alloc, uint8_t **buf, size_t *len)
{
    size_t n = BIO_ctrl_pending(t->out), got = 0;

    *buf = NULL;
    *len = 0;
    if (n == 0)
        return 0;
    *buf = (uint8_t *)oncRPCRealloc(alloc, NULL, n);
    if (*buf == NULL)
        return -1;
    if (BIO_read_ex(t->out, *buf, n, &got) != 1 || got != n) {
        oncRPCFree(alloc, *buf);
        *buf = NULL;
        ERR_clear_error();
        return -1;
    }
    *len = n;
    return 1;
}

const char *oncRPCIntTlsError(const tlsSession *t)
{
    return t != NULL && t->err[0] != '\0' ? t->err : "TLS failed";
}

int oncRPCIntTlsPeerSubject(tlsSession *t, char *buf, size_t len)
{
    X509 *x;
    BIO *b;
    size_t got = 0;
    int ok = 0;

    if (len == 0)
        return -1;
    buf[0] = '\0';
    x = SSL_get0_peer_certificate(t->ssl);
    if (x == NULL || SSL_get_verify_result(t->ssl) != X509_V_OK)
        return -1;
    b = BIO_new(BIO_s_mem());
    if (b != NULL && X509_NAME_print_ex(b, X509_get_subject_name(x), 0, XN_FLAG_RFC2253) >= 0 &&
        BIO_read_ex(b, buf, len - 1, &got) == 1) {
        buf[got] = '\0';
        ok = 1;
    }
    BIO_free(b);
    ERR_clear_error();
    return ok ? 0 : -1;
}
