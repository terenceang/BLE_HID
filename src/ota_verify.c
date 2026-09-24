/**
 ****************************************************************************************
 * @file ota_verify.c
 *
 * @brief OTA image signature check: SHA-256 of the firmware in flash, ECDSA P-256 verify
 *        against ota_pubkey (src/config/ota_pubkey.h, from tools/suota.py --keygen).
 *        The verify itself is micro-ecc in the DA14585 ROM (used by the BLE stack for LE
 *        Secure Connections; addresses in sdk/common_project_files/misc/da14585_symbols.txt).
 ****************************************************************************************
 */

#include <string.h>
#include "spi_flash.h"
#include "arch_wdg.h"
#include "ota_verify.h"
#include "ota_pubkey.h"
#include "app_suotar.h"                // image_header_t, IMAGE_HEADER_SIGNATURE*, STATUS_VALID_IMAGE, CODE_OFFSET

// micro-ecc ROM API (uECC.h v1.x). The ROM build takes keys, hashes and signatures big-endian
// (checked on the chip against a Python-made signature: 1.4 s, ~1.3 KB of stack)
typedef const struct uECC_Curve_t *uECC_Curve;
uECC_Curve uECC_secp256r1(void);
int uECC_verify(const uint8_t *public_key, const uint8_t *message_hash, unsigned hash_size,
                const uint8_t *signature, uECC_Curve curve);

static const uint8_t sig_magic[8] = { 'B', 'T', 'S', 'N', 'S', 'I', 'G', '1' };
#define SIG_TRAILER         (sizeof(sig_magic) + 64)

/*
 * SHA-256 (FIPS 180-4), just enough for one streamed message
 ****************************************************************************************
 */

typedef struct { uint32_t h[8]; uint8_t buf[64]; uint32_t len; } sha256_t;

static const uint32_t K[64] =
{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n)   (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const uint8_t p[64])
{
    uint32_t w[64], a, b, c, d, e, f, g, k, t1, t2;

    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++)
        w[i] = (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7]
             + (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];

    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; k = h[7];
    for (int i = 0; i < 64; i++)
    {
        t1 = k + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

static void sha256_init(sha256_t *s)
{
    static const uint32_t iv[8] =
        { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, iv, sizeof(iv));
    s->len = 0;
}

static void sha256_update(sha256_t *s, const uint8_t *p, uint32_t n)
{
    while (n--)
    {
        s->buf[s->len++ % 64] = *p++;
        if (s->len % 64 == 0)
            sha256_block(s->h, s->buf);
    }
}

static void sha256_final(sha256_t *s, uint8_t out[32])
{
    uint32_t bits = s->len * 8;
    uint8_t pad = 0x80, zero = 0, lenbe[8] = { 0, 0, 0, 0, bits >> 24, bits >> 16, bits >> 8, bits };

    sha256_update(s, &pad, 1);
    while (s->len % 64 != 56)
        sha256_update(s, &zero, 1);
    sha256_update(s, lenbe, 8);
    for (int i = 0; i < 8; i++)
    {
        out[4 * i] = s->h[i] >> 24; out[4 * i + 1] = s->h[i] >> 16;
        out[4 * i + 2] = s->h[i] >> 8; out[4 * i + 3] = s->h[i];
    }
}

/*
 * Image check
 ****************************************************************************************
 */

/// SHA-256 of @p len bytes of SPI flash from @p addr
static void flash_sha256(uint32_t addr, uint32_t len, uint8_t out[32])
{
    sha256_t s;
    uint8_t buf[256];
    uint32_t n;

    sha256_init(&s);
    while (len)
    {
        uint32_t chunk = len > sizeof(buf) ? sizeof(buf) : len;
        spi_flash_read_data(buf, addr, chunk, &n);
        sha256_update(&s, buf, chunk);
        addr += chunk;
        len  -= chunk;
        wdg_reload(WATCHDOG_DEFAULT_PERIOD);
    }
    sha256_final(&s, out);
}

/// ECDSA P-256 verify (all inputs big-endian, as tools/suota.py writes them) with the ROM
static int ecdsa_verify(const uint8_t pub[64], const uint8_t hash[32], const uint8_t sig[64])
{
    int ok;

    wdg_freeze();                       // P-256 verify takes a while on a 16 MHz Cortex-M0
    ok = uECC_verify(pub, hash, 32, sig, uECC_secp256r1());
    wdg_resume();
    return ok;
}

enum ota_check ota_check_new_image(uint32_t slot_addr)
{
    image_header_t hdr;
    uint8_t trailer[SIG_TRAILER], hash[32];
    uint32_t n, fw_len;
    int ok;

    spi_flash_read_data((uint8_t *)&hdr, slot_addr, sizeof(hdr), &n);
    if (hdr.signature[0] != IMAGE_HEADER_SIGNATURE1 || hdr.signature[1] != IMAGE_HEADER_SIGNATURE2 ||
        hdr.validflag != STATUS_VALID_IMAGE)
    {
        return OTA_CHECK_NONE;          // SUOTA did not complete/validate this slot
    }

    ok = 0;
    if (hdr.code_size > SIG_TRAILER && hdr.code_size < 0x20000)
    {
        fw_len = hdr.code_size - SIG_TRAILER;
        spi_flash_read_data(trailer, slot_addr + CODE_OFFSET + fw_len, SIG_TRAILER, &n);
        if (!memcmp(trailer, sig_magic, sizeof(sig_magic)))
        {
            flash_sha256(slot_addr + CODE_OFFSET, fw_len, hash);
            ok = ecdsa_verify(ota_pubkey, hash, &trailer[sizeof(sig_magic)]);
        }
    }
    if (ok)
    {
        return OTA_CHECK_OK;
    }

    // Unsigned or wrong key: clear the valid flag (0xAA -> 0x00 needs no erase) so the boot
    // ROM ignores this slot and keeps booting the old image
    uint8_t invalid = 0x00;
    spi_flash_write_data(&invalid, slot_addr + 2, 1, &n);
    return OTA_CHECK_BAD;
}

