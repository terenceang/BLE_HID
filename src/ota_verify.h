/**
 ****************************************************************************************
 * @file ota_verify.h
 *
 * @brief OTA image signature check (ECDSA P-256 over SHA-256, micro-ecc).
 ****************************************************************************************
 */

#ifndef _OTA_VERIFY_H_
#define _OTA_VERIFY_H_

#include <stdint.h>

enum ota_check { OTA_CHECK_NONE, OTA_CHECK_OK, OTA_CHECK_BAD };

/**
 ****************************************************************************************
 * @brief Check the image SUOTA just wrote and marked valid at flash slot @p slot_addr.
 *        A signed image is firmware + "BTSNSIG1" + 64-byte signature (r || s) of
 *        SHA-256(firmware), made by tools/suota.py --sign. If the slot is not a freshly
 *        validated image, nothing happens (OTA_CHECK_NONE). If the signature is wrong or
 *        missing, the slot's valid flag is cleared so the boot ROM keeps the old image.
 *        Blocks for a few seconds (the watchdog is frozen meanwhile). SPI flash must be awake.
 ****************************************************************************************
 */
enum ota_check ota_check_new_image(uint32_t slot_addr);

#endif // _OTA_VERIFY_H_
