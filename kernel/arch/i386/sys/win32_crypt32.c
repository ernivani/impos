#include <kernel/win32_types.h>
#include <string.h>
#include <stdio.h>

/* ── Crypt32 — Certificate Store Emulation ───────────────────── */

#define CERT_STORE_HANDLE 0xCE570001

/* Placeholder DER-encoded self-signed root CA stub (minimal) */
static const BYTE placeholder_cert_der[] = {
    0x30, 0x82, 0x01, 0x00,  /* SEQUENCE, ~256 bytes (placeholder) */
    0x30, 0x82, 0x00, 0xA0,  /* SEQUENCE (tbsCertificate) */
    0x02, 0x01, 0x01,         /* INTEGER 1 (version) */
    0x02, 0x01, 0x01,         /* INTEGER 1 (serial) */
    0x30, 0x0D,               /* SEQUENCE (signature alg) */
    0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B,
    0x05, 0x00,               /* sha256WithRSAEncryption */
};

/* Static CERT_CONTEXT returned by find/enum */
static BYTE cert_ctx_encoded[sizeof(placeholder_cert_der)];
static int cert_ctx_initialized = 0;

typedef struct {
    DWORD  dwCertEncodingType;
    BYTE  *pbCertEncoded;
    DWORD  cbCertEncoded;
    void  *pCertInfo;
    HANDLE hCertStore;
} CERT_CONTEXT;

static CERT_CONTEXT static_cert_ctx;

static void ensure_cert_ctx(void) {
    if (cert_ctx_initialized) return;
    cert_ctx_initialized = 1;
    memcpy(cert_ctx_encoded, placeholder_cert_der, sizeof(placeholder_cert_der));
    static_cert_ctx.dwCertEncodingType = 1;  /* X509_ASN_ENCODING */
    static_cert_ctx.pbCertEncoded = cert_ctx_encoded;
    static_cert_ctx.cbCertEncoded = sizeof(placeholder_cert_der);
    static_cert_ctx.pCertInfo = NULL;
    static_cert_ctx.hCertStore = CERT_STORE_HANDLE;
}

/* ── CertOpenStore ───────────────────────────────────────────── */

static HANDLE WINAPI shim_CertOpenStore(
    DWORD dwStoreProvider, DWORD dwEncodingType,
    HANDLE hCryptProv, DWORD dwFlags, const void *pvPara)
{
    (void)dwStoreProvider; (void)dwEncodingType;
    (void)hCryptProv; (void)dwFlags; (void)pvPara;
    ensure_cert_ctx();
    return CERT_STORE_HANDLE;
}

/* ── CertOpenSystemStoreA ────────────────────────────────────── */

static HANDLE WINAPI shim_CertOpenSystemStoreA(
    HANDLE hProv, const char *szSubsystemProtocol)
{
    (void)hProv; (void)szSubsystemProtocol;
    ensure_cert_ctx();
    return CERT_STORE_HANDLE;
}

/* ── CertCloseStore ──────────────────────────────────────────── */

static BOOL WINAPI shim_CertCloseStore(HANDLE hCertStore, DWORD dwFlags) {
    (void)hCertStore; (void)dwFlags;
    return TRUE;
}

/* ── CertFindCertificateInStore ──────────────────────────────── */

static void * WINAPI shim_CertFindCertificateInStore(
    HANDLE hCertStore, DWORD dwCertEncodingType, DWORD dwFindFlags,
    DWORD dwFindType, const void *pvFindPara,
    void *pPrevCertContext)
{
    (void)hCertStore; (void)dwCertEncodingType; (void)dwFindFlags;
    (void)dwFindType; (void)pvFindPara;
    ensure_cert_ctx();

    /* Return static context on first call, NULL on subsequent */
    if (pPrevCertContext == NULL)
        return &static_cert_ctx;
    return NULL;
}

/* ── CertFreeCertificateContext ──────────────────────────────── */

static BOOL WINAPI shim_CertFreeCertificateContext(void *pCertContext) {
    (void)pCertContext;
    return TRUE;
}

/* ── CertEnumCertificatesInStore ─────────────────────────────── */

static void * WINAPI shim_CertEnumCertificatesInStore(
    HANDLE hCertStore, void *pPrevCertContext)
{
    (void)hCertStore;
    ensure_cert_ctx();

    /* Yield the one placeholder cert, then NULL */
    if (pPrevCertContext == NULL)
        return &static_cert_ctx;
    return NULL;
}

/* ── CertGetCertificateChain (stub) ─────────────────────────── */

static BOOL WINAPI shim_CertGetCertificateChain(
    HANDLE hChainEngine, void *pCertContext,
    void *pTime, HANDLE hAdditionalStore,
    void *pChainPara, DWORD dwFlags,
    void *pvReserved, void **ppChainContext)
{
    (void)hChainEngine; (void)pCertContext; (void)pTime;
    (void)hAdditionalStore; (void)pChainPara; (void)dwFlags;
    (void)pvReserved;
    if (ppChainContext) *ppChainContext = NULL;
    return FALSE;
}

/* ── CertVerifyCertificateChainPolicy (stub) ─────────────────── */

static BOOL WINAPI shim_CertVerifyCertificateChainPolicy(
    DWORD pszPolicyOID, void *pChainContext,
    void *pPolicyPara, void *pPolicyStatus)
{
    (void)pszPolicyOID; (void)pChainContext;
    (void)pPolicyPara; (void)pPolicyStatus;
    return TRUE;  /* optimistic — accept all */
}

/* ── Export Table ─────────────────────────────────────────────── */

static const win32_export_entry_t crypt32_exports[] = {
    { "CertCloseStore",                   (void *)shim_CertCloseStore },
    { "CertEnumCertificatesInStore",      (void *)shim_CertEnumCertificatesInStore },
    { "CertFindCertificateInStore",       (void *)shim_CertFindCertificateInStore },
    { "CertFreeCertificateContext",       (void *)shim_CertFreeCertificateContext },
    { "CertGetCertificateChain",          (void *)shim_CertGetCertificateChain },
    { "CertOpenStore",                    (void *)shim_CertOpenStore },
    { "CertOpenSystemStoreA",             (void *)shim_CertOpenSystemStoreA },
    { "CertVerifyCertificateChainPolicy", (void *)shim_CertVerifyCertificateChainPolicy },
};

const win32_dll_shim_t win32_crypt32 = {
    .dll_name = "crypt32.dll",
    .exports = crypt32_exports,
    .num_exports = sizeof(crypt32_exports) / sizeof(crypt32_exports[0]),
};
