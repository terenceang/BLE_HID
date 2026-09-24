#ifndef _APP_VERSION_H_
#define _APP_VERSION_H_

/// Firmware version (keep in sync with the VERSION file). APP_VERSION is the string
/// ("0.5.4", also in the flash image header), APP_VERSION_BCD the Device Information PnP ID
/// product version in USB bcdDevice form 0xJJMN (0.5.4 = 0x0054; Windows shows REV&0054).
#define APP_VERSION_MAJOR               0
#define APP_VERSION_MINOR               5
#define APP_VERSION_PATCH               4
#define APP_VER_STR_(x)                 #x
#define APP_VER_STR(x)                  APP_VER_STR_(x)
#define APP_VERSION                     APP_VER_STR(APP_VERSION_MAJOR) "." APP_VER_STR(APP_VERSION_MINOR) "." APP_VER_STR(APP_VERSION_PATCH)
#define APP_VERSION_BCD                 ((APP_VERSION_MAJOR << 8) | (APP_VERSION_MINOR << 4) | APP_VERSION_PATCH)

#endif // _APP_VERSION_H_
