#ifndef _SELFLASH_H_
#define _SELFLASH_H_

/**
 * Install the running RAM image into external SPI flash (primary boot slot from the
 * product header at 0x38000) so it boots after a power cycle. No-op when the flash
 * copy is already identical, when the product header is invalid, or without
 * CFG_SPI_FLASH_ENABLE. Blocking: takes a few seconds the first time.
 */
void selflash_run(void);

#endif
