/**
 ****************************************************************************************
 * @file selflash.c
 *
 * @brief Self-install of the running image into external SPI flash (DA14585/86).
 *
 * The boot ROM copies a SUOTA-format image (64-byte header + body) from the slot named
 * by the product header (flash 0x38000) to RAM 0x07FC0000. The running image is that
 * same RAM range, so we CRC it and, if flash differs, write it as the newest slot 0.
 ****************************************************************************************
 */

#include "rwip_config.h"

#if defined (CFG_SPI_FLASH_ENABLE)

#include <string.h>
#include "spi_flash.h"
#include "arch_wdg.h"
#include "arch_console.h"
#include "user_periph_setup.h"        // SPI_FLASH_DEV_SIZE
#include "user_config.h"               // APP_VERSION
#include "selflash.h"

// SUOTA image layout (same as app_suotar.h, which is only usable with BLE_SUOTA_RECEIVER)
#define PRODUCT_HEADER_POSITION     0x38000
#define PRODUCT_HEADER_SIGNATURE1   0x70
#define PRODUCT_HEADER_SIGNATURE2   0x52
#define IMAGE_HEADER_SIGNATURE1     0x70
#define IMAGE_HEADER_SIGNATURE2     0x51
#define CODE_OFFSET                 64
#define STATUS_VALID_IMAGE          0xAA

typedef struct { uint8_t signature[2]; uint8_t version[2]; uint32_t offset1; uint32_t offset2; } sf_product_hdr_t;

typedef struct
{
    uint8_t  signature[2];
    uint8_t  validflag;      // STATUS_VALID_IMAGE once the image is complete
    uint8_t  imageid;        // higher = newer
    uint32_t code_size;
    uint32_t CRC;
    uint8_t  version[16];
    uint32_t timestamp;
    uint8_t  encryption;
    uint8_t  reserved[31];
} sf_image_hdr_t;

#define IMG_RAM_BASE        ((const uint8_t *)0x07FC0000)
#define IMG_RAM_END         ((uint32_t)&Image$$ER_ZI$$Base)   // bin ends where ZI starts
#define SECTOR              (SPI_FLASH_SECTOR_SIZE)

// Sectors we must never touch: SDK bond DB (0x1E000) + saved CCC (CCC_FLASH_ADDR, user_periph_setup.h), product header area
#define KEEP_OUT_1_START    (CCC_FLASH_ADDR)
#define KEEP_OUT_1_END      (0x1F000)
#define KEEP_OUT_2_START    (0x38000)
#define KEEP_OUT_2_END      (0x3B000)

extern uint32_t Image$$ER_ZI$$Base;

/// Keil_5/flash.sh writes SELFLASH_MAGIC here (address from the .map) after loading the image.
/// Uninitialised RAM: a ROM boot from flash (power-on, OTA reboot, deep-sleep wake) leaves
/// garbage, so only a RAM load installs - never an image the ROM or SUOTA already put in flash.
/// (Needed for OTA: the RAM CRC differs from the pristine image once .data has changed, so
/// without this the first boot after an OTA update would rewrite slot 0.)
#define SELFLASH_MAGIC      0x5E1FF1A5u
volatile uint32_t selflash_magic __attribute__((section(".bss.retention_mem_area_uninit"), used));

static uint32_t crc32_buf(uint32_t crc, const uint8_t *p, uint32_t n)
{
    while (n--)
    {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static uint32_t flash_crc(uint32_t addr, uint32_t size)
{
    uint8_t buf[256];
    uint32_t crc = 0xFFFFFFFFu, n;

    while (size)
    {
        uint32_t chunk = size > sizeof(buf) ? sizeof(buf) : size;
        spi_flash_read_data(buf, addr, chunk, &n);
        crc = crc32_buf(crc, buf, chunk);
        addr += chunk;
        size -= chunk;
        wdg_reload(WATCHDOG_DEFAULT_PERIOD);
    }
    return ~crc;
}

static bool overlaps(uint32_t s, uint32_t e, uint32_t ks, uint32_t ke)
{
    return s < ke && e > ks;
}

static bool hdr_valid(const sf_image_hdr_t *h)
{
    return h->signature[0] == IMAGE_HEADER_SIGNATURE1 &&
           h->signature[1] == IMAGE_HEADER_SIGNATURE2 &&
           h->validflag == STATUS_VALID_IMAGE;
}

/// Image id order used by the boot ROM and SUOTA (app_suotar.c app_find_old_img/app_get_image_id):
/// higher is newer, except 0 follows 0xFF
static bool id_newer(uint8_t a, uint8_t b)
{
    if (a == 0x00 && b == 0xFF) return true;
    if (a == 0xFF && b == 0x00) return false;
    return a > b;
}

void selflash_run(void)
{
    sf_product_hdr_t ph;
    sf_image_hdr_t   h0, h1, nh;
    uint32_t n, off0, off1, size, crc, end;
    uint8_t id0 = 0, id1 = 0;

    if (selflash_magic != SELFLASH_MAGIC)
    {
        return;         // booted from flash: the image is already installed
    }
    selflash_magic = 0;

    size = IMG_RAM_END - (uint32_t)IMG_RAM_BASE;

    spi_flash_release_from_power_down();
    spi_flash_configure_memory_protection(SPI_FLASH_MEM_PROT_NONE);

    spi_flash_read_data((uint8_t *)&ph, PRODUCT_HEADER_POSITION, sizeof(ph), &n);
    if (ph.signature[0] != PRODUCT_HEADER_SIGNATURE1 || ph.signature[1] != PRODUCT_HEADER_SIGNATURE2)
    {
        printk("selflash: no product header, skipped\n");
        goto done;
    }

    // Slot addresses come from the product header (they differ between units)
    off0 = ph.offset1;
    off1 = ph.offset2;
    end  = off0 + CODE_OFFSET + size;
    if ((off0 % SECTOR) || (off1 % SECTOR) || end > SPI_FLASH_DEV_SIZE ||
        overlaps(off0, end, KEEP_OUT_1_START, KEEP_OUT_1_END) ||
        overlaps(off0, end, KEEP_OUT_2_START, KEEP_OUT_2_END))
    {
        printk("selflash: slot %x unsuitable, skipped\n", off0);
        goto done;
    }

    spi_flash_read_data((uint8_t *)&h0, off0, sizeof(h0), &n);
    spi_flash_read_data((uint8_t *)&h1, off1, sizeof(h1), &n);

    crc = ~crc32_buf(0xFFFFFFFFu, IMG_RAM_BASE, size);

    // Up to date: slot 0 holds this image AND is what the ROM boots. After an OTA update the
    // other slot can hold a newer id; then slot 0 matching isn't enough - reinstall above it.
    if (hdr_valid(&h0) && h0.code_size == size && h0.CRC == crc &&
        !memcmp(h0.version, APP_VERSION, sizeof(APP_VERSION)) &&
        (!hdr_valid(&h1) || id_newer(h0.imageid, h1.imageid)) &&    // equal ids: SUOTA ranks slot 1 newer
        flash_crc(off0 + CODE_OFFSET, size) == crc)
    {
        printk("selflash: up to date\n");
        goto done;
    }

    printk("selflash: installing %d bytes at %x\n", size, off0);

    memset(&nh, 0xFF, sizeof(nh));
    nh.signature[0] = IMAGE_HEADER_SIGNATURE1;
    nh.signature[1] = IMAGE_HEADER_SIGNATURE2;
    nh.validflag    = 0xFF;                       // set to 0xAA only after the body verifies
    // One above the newest valid slot, in the ROM's order (0xFF -> 0), so this image boots next
    if (hdr_valid(&h0)) id0 = h0.imageid;
    if (hdr_valid(&h1)) id1 = h1.imageid;
    if (hdr_valid(&h0) && hdr_valid(&h1))
        id0 = id_newer(id1, id0) ? id1 : id0;
    else if (hdr_valid(&h1))
        id0 = id1;
    nh.imageid      = (id0 == 0xFF) ? 0x00 : id0 + 1;
    nh.code_size    = size;
    nh.CRC          = crc;
    memset(nh.version, 0, sizeof(nh.version));
    memcpy(nh.version, APP_VERSION, sizeof(APP_VERSION));
    nh.timestamp    = 0;
    nh.encryption   = 0;
    memset(nh.reserved, 0, sizeof(nh.reserved));

    // Erase, then header (not yet valid), body, verify, finally the valid flag
    for (uint32_t a = off0; a < end; a += SECTOR)
    {
        spi_flash_block_erase(a, SPI_FLASH_OP_SE);
        wdg_reload(WATCHDOG_DEFAULT_PERIOD);
    }
    spi_flash_write_data((uint8_t *)&nh, off0, sizeof(nh), &n);
    spi_flash_write_data((uint8_t *)IMG_RAM_BASE, off0 + CODE_OFFSET, size, &n);

    if (flash_crc(off0 + CODE_OFFSET, size) == crc)
    {
        uint8_t ok = STATUS_VALID_IMAGE;
        spi_flash_write_data(&ok, off0 + 2, 1, &n);
        printk("selflash: done\n");
    }
    else
    {
        printk("selflash: verify FAILED, image left invalid\n");
    }

done:
    spi_flash_power_down();
}

#else
#include "selflash.h"
void selflash_run(void) {}
#endif
