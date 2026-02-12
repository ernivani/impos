#include <kernel/win32_types.h>
#include <kernel/crypto.h>
#include <string.h>
#include <stdio.h>

/* ── BCrypt Algorithm & Hash Handle Tables ───────────────────── */

#define MAX_ALG_HANDLES  8
#define MAX_HASH_HANDLES 16

#define ALG_TYPE_NONE   0
#define ALG_TYPE_SHA256 1
#define ALG_TYPE_SHA1   2
#define ALG_TYPE_AES    3
#define ALG_TYPE_RNG    4

/* NTSTATUS codes */
#define STATUS_SUCCESS           ((LONG)0x00000000)
#define STATUS_NOT_FOUND         ((LONG)0xC0000225)
#define STATUS_INVALID_HANDLE    ((LONG)0xC0000008)
#define STATUS_INVALID_PARAMETER ((LONG)0xC000000D)
#define STATUS_BUFFER_TOO_SMALL  ((LONG)0xC0000023)
#define STATUS_NOT_SUPPORTED     ((LONG)0xC00000BB)

#define ALG_HANDLE_BASE  0xBCA10000
#define HASH_HANDLE_BASE 0xBCB20000

typedef struct {
    int in_use;
    int alg_type;
} bcrypt_alg_slot_t;

typedef struct {
    int         in_use;
    int         alg_type;
    sha256_ctx_t sha256;
} bcrypt_hash_slot_t;

static bcrypt_alg_slot_t  alg_slots[MAX_ALG_HANDLES];
static bcrypt_hash_slot_t hash_slots[MAX_HASH_HANDLES];

/* ── Helpers ─────────────────────────────────────────────────── */

static int alg_from_handle(HANDLE h) {
    uint32_t idx = h - ALG_HANDLE_BASE;
    if (idx >= MAX_ALG_HANDLES || !alg_slots[idx].in_use) return -1;
    return (int)idx;
}

static int hash_from_handle(HANDLE h) {
    uint32_t idx = h - HASH_HANDLE_BASE;
    if (idx >= MAX_HASH_HANDLES || !hash_slots[idx].in_use) return -1;
    return (int)idx;
}

/* Case-insensitive wide string compare (limited to ASCII) */
static int wstr_icmp(const WCHAR *a, const char *b) {
    while (*a && *b) {
        WCHAR ca = *a, cb = (WCHAR)(unsigned char)*b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return (int)ca - (int)cb;
        a++; b++;
    }
    return (int)*a - (int)(unsigned char)*b;
}

/* ── BCryptOpenAlgorithmProvider ─────────────────────────────── */

static LONG WINAPI shim_BCryptOpenAlgorithmProvider(
    HANDLE *phAlgorithm, const WCHAR *pszAlgId,
    const WCHAR *pszImplementation, DWORD dwFlags)
{
    (void)pszImplementation; (void)dwFlags;

    if (!phAlgorithm || !pszAlgId) return STATUS_INVALID_PARAMETER;

    int alg_type = ALG_TYPE_NONE;
    if (wstr_icmp(pszAlgId, "SHA256") == 0)      alg_type = ALG_TYPE_SHA256;
    else if (wstr_icmp(pszAlgId, "SHA1") == 0)    alg_type = ALG_TYPE_SHA1;
    else if (wstr_icmp(pszAlgId, "AES") == 0)     alg_type = ALG_TYPE_AES;
    else if (wstr_icmp(pszAlgId, "RNG") == 0)     alg_type = ALG_TYPE_RNG;
    else return STATUS_NOT_FOUND;

    for (int i = 0; i < MAX_ALG_HANDLES; i++) {
        if (!alg_slots[i].in_use) {
            alg_slots[i].in_use = 1;
            alg_slots[i].alg_type = alg_type;
            *phAlgorithm = (HANDLE)(ALG_HANDLE_BASE + i);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INVALID_HANDLE;
}

/* ── BCryptCloseAlgorithmProvider ────────────────────────────── */

static LONG WINAPI shim_BCryptCloseAlgorithmProvider(
    HANDLE hAlgorithm, DWORD dwFlags)
{
    (void)dwFlags;
    int idx = alg_from_handle(hAlgorithm);
    if (idx < 0) return STATUS_INVALID_HANDLE;
    alg_slots[idx].in_use = 0;
    return STATUS_SUCCESS;
}

/* ── BCryptGenRandom ─────────────────────────────────────────── */

static LONG WINAPI shim_BCryptGenRandom(
    HANDLE hAlgorithm, BYTE *pbBuffer, DWORD cbBuffer, DWORD dwFlags)
{
    (void)hAlgorithm; (void)dwFlags;
    if (!pbBuffer || cbBuffer == 0) return STATUS_INVALID_PARAMETER;
    prng_random(pbBuffer, cbBuffer);
    return STATUS_SUCCESS;
}

/* ── BCryptGetProperty ───────────────────────────────────────── */

static LONG WINAPI shim_BCryptGetProperty(
    HANDLE hObject, const WCHAR *pszProperty,
    BYTE *pbOutput, DWORD cbOutput, DWORD *pcbResult, DWORD dwFlags)
{
    (void)dwFlags;
    if (!pszProperty || !pcbResult) return STATUS_INVALID_PARAMETER;

    /* Check if it's an algorithm handle */
    int idx = alg_from_handle(hObject);
    if (idx < 0) return STATUS_INVALID_HANDLE;

    /* "HashDigestLength" */
    if (wstr_icmp(pszProperty, "HashDigestLength") == 0) {
        DWORD digest_len = 0;
        if (alg_slots[idx].alg_type == ALG_TYPE_SHA256) digest_len = 32;
        else if (alg_slots[idx].alg_type == ALG_TYPE_SHA1) digest_len = 20;
        else return STATUS_NOT_SUPPORTED;

        *pcbResult = sizeof(DWORD);
        if (pbOutput && cbOutput >= sizeof(DWORD))
            *(DWORD *)pbOutput = digest_len;
        else if (pbOutput)
            return STATUS_BUFFER_TOO_SMALL;
        return STATUS_SUCCESS;
    }

    /* "ObjectLength" — return size of hash state */
    if (wstr_icmp(pszProperty, "ObjectLength") == 0) {
        DWORD obj_len = 0;
        if (alg_slots[idx].alg_type == ALG_TYPE_SHA256) obj_len = sizeof(sha256_ctx_t);
        else if (alg_slots[idx].alg_type == ALG_TYPE_SHA1) obj_len = 96;
        else return STATUS_NOT_SUPPORTED;

        *pcbResult = sizeof(DWORD);
        if (pbOutput && cbOutput >= sizeof(DWORD))
            *(DWORD *)pbOutput = obj_len;
        else if (pbOutput)
            return STATUS_BUFFER_TOO_SMALL;
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

/* ── BCryptCreateHash ────────────────────────────────────────── */

static LONG WINAPI shim_BCryptCreateHash(
    HANDLE hAlgorithm, HANDLE *phHash,
    BYTE *pbHashObject, DWORD cbHashObject,
    BYTE *pbSecret, DWORD cbSecret, DWORD dwFlags)
{
    (void)pbHashObject; (void)cbHashObject;
    (void)pbSecret; (void)cbSecret; (void)dwFlags;

    if (!phHash) return STATUS_INVALID_PARAMETER;

    int ai = alg_from_handle(hAlgorithm);
    if (ai < 0) return STATUS_INVALID_HANDLE;

    if (alg_slots[ai].alg_type != ALG_TYPE_SHA256)
        return STATUS_NOT_SUPPORTED;

    for (int i = 0; i < MAX_HASH_HANDLES; i++) {
        if (!hash_slots[i].in_use) {
            hash_slots[i].in_use = 1;
            hash_slots[i].alg_type = alg_slots[ai].alg_type;
            sha256_init(&hash_slots[i].sha256);
            *phHash = (HANDLE)(HASH_HANDLE_BASE + i);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INVALID_HANDLE;
}

/* ── BCryptHashData ──────────────────────────────────────────── */

static LONG WINAPI shim_BCryptHashData(
    HANDLE hHash, const BYTE *pbInput, DWORD cbInput, DWORD dwFlags)
{
    (void)dwFlags;
    int idx = hash_from_handle(hHash);
    if (idx < 0) return STATUS_INVALID_HANDLE;
    if (!pbInput && cbInput > 0) return STATUS_INVALID_PARAMETER;

    if (hash_slots[idx].alg_type == ALG_TYPE_SHA256) {
        sha256_update(&hash_slots[idx].sha256, pbInput, cbInput);
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

/* ── BCryptFinishHash ────────────────────────────────────────── */

static LONG WINAPI shim_BCryptFinishHash(
    HANDLE hHash, BYTE *pbOutput, DWORD cbOutput, DWORD dwFlags)
{
    (void)dwFlags;
    int idx = hash_from_handle(hHash);
    if (idx < 0) return STATUS_INVALID_HANDLE;

    if (hash_slots[idx].alg_type == ALG_TYPE_SHA256) {
        if (!pbOutput || cbOutput < SHA256_DIGEST_SIZE)
            return STATUS_BUFFER_TOO_SMALL;
        sha256_final(&hash_slots[idx].sha256, pbOutput);
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

/* ── BCryptDestroyHash ───────────────────────────────────────── */

static LONG WINAPI shim_BCryptDestroyHash(HANDLE hHash) {
    int idx = hash_from_handle(hHash);
    if (idx < 0) return STATUS_INVALID_HANDLE;
    hash_slots[idx].in_use = 0;
    return STATUS_SUCCESS;
}

/* ── BCryptHash (one-shot) ───────────────────────────────────── */

static LONG WINAPI shim_BCryptHash(
    HANDLE hAlgorithm, BYTE *pbSecret, DWORD cbSecret,
    BYTE *pbInput, DWORD cbInput,
    BYTE *pbOutput, DWORD cbOutput)
{
    (void)pbSecret; (void)cbSecret;

    int ai = alg_from_handle(hAlgorithm);
    if (ai < 0) return STATUS_INVALID_HANDLE;

    if (alg_slots[ai].alg_type == ALG_TYPE_SHA256) {
        if (!pbOutput || cbOutput < SHA256_DIGEST_SIZE)
            return STATUS_BUFFER_TOO_SMALL;
        sha256(pbInput, cbInput, pbOutput);
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

/* ── Stubs ───────────────────────────────────────────────────── */

static LONG WINAPI shim_BCryptDeriveKeyPBKDF2(
    HANDLE hPrf, BYTE *pbPassword, DWORD cbPassword,
    BYTE *pbSalt, DWORD cbSalt, uint64_t cIterations,
    BYTE *pbDerivedKey, DWORD cbDerivedKey, DWORD dwFlags)
{
    (void)hPrf; (void)pbPassword; (void)cbPassword;
    (void)pbSalt; (void)cbSalt; (void)cIterations;
    (void)pbDerivedKey; (void)cbDerivedKey; (void)dwFlags;
    return STATUS_NOT_SUPPORTED;
}

static LONG WINAPI shim_BCryptEncrypt(
    HANDLE hKey, BYTE *pbInput, DWORD cbInput,
    void *pPaddingInfo, BYTE *pbIV, DWORD cbIV,
    BYTE *pbOutput, DWORD cbOutput, DWORD *pcbResult, DWORD dwFlags)
{
    (void)hKey; (void)pbInput; (void)cbInput; (void)pPaddingInfo;
    (void)pbIV; (void)cbIV; (void)pbOutput; (void)cbOutput;
    (void)pcbResult; (void)dwFlags;
    return STATUS_NOT_SUPPORTED;
}

static LONG WINAPI shim_BCryptDecrypt(
    HANDLE hKey, BYTE *pbInput, DWORD cbInput,
    void *pPaddingInfo, BYTE *pbIV, DWORD cbIV,
    BYTE *pbOutput, DWORD cbOutput, DWORD *pcbResult, DWORD dwFlags)
{
    (void)hKey; (void)pbInput; (void)cbInput; (void)pPaddingInfo;
    (void)pbIV; (void)cbIV; (void)pbOutput; (void)cbOutput;
    (void)pcbResult; (void)dwFlags;
    return STATUS_NOT_SUPPORTED;
}

/* ── Export Table ─────────────────────────────────────────────── */

static const win32_export_entry_t bcrypt_exports[] = {
    { "BCryptCloseAlgorithmProvider", (void *)shim_BCryptCloseAlgorithmProvider },
    { "BCryptCreateHash",            (void *)shim_BCryptCreateHash },
    { "BCryptDecrypt",               (void *)shim_BCryptDecrypt },
    { "BCryptDeriveKeyPBKDF2",       (void *)shim_BCryptDeriveKeyPBKDF2 },
    { "BCryptDestroyHash",           (void *)shim_BCryptDestroyHash },
    { "BCryptEncrypt",               (void *)shim_BCryptEncrypt },
    { "BCryptFinishHash",            (void *)shim_BCryptFinishHash },
    { "BCryptGenRandom",             (void *)shim_BCryptGenRandom },
    { "BCryptGetProperty",           (void *)shim_BCryptGetProperty },
    { "BCryptHash",                  (void *)shim_BCryptHash },
    { "BCryptHashData",              (void *)shim_BCryptHashData },
    { "BCryptOpenAlgorithmProvider",  (void *)shim_BCryptOpenAlgorithmProvider },
};

const win32_dll_shim_t win32_bcrypt = {
    .dll_name = "bcrypt.dll",
    .exports = bcrypt_exports,
    .num_exports = sizeof(bcrypt_exports) / sizeof(bcrypt_exports[0]),
};
