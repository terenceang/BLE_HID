#ifndef _APP_VERSION_H_
#define _APP_VERSION_H_

/// Firmware version - the single source (bump APP_VERSION_* only). APP_VERSION is the string
/// ("x.y.z": flash image header, DIS Firmware Revision, tools' version tag), APP_VERSION_BCD the
/// Device Information PnP ID product version in USB bcdDevice form 0xJJMN (e.g. 1.2.3 = 0x0123,
/// shown by Windows as REV&0123).
#define APP_VERSION_MAJOR               0
#define APP_VERSION_MINOR               5
#define APP_VERSION_PATCH               5
#define APP_VER_STR_(x)                 #x
#define APP_VER_STR(x)                  APP_VER_STR_(x)
#define APP_VERSION                     APP_VER_STR(APP_VERSION_MAJOR) "." APP_VER_STR(APP_VERSION_MINOR) "." APP_VER_STR(APP_VERSION_PATCH)
#define APP_VERSION_BCD                 ((APP_VERSION_MAJOR << 8) | (APP_VERSION_MINOR << 4) | APP_VERSION_PATCH)

#endif // _APP_VERSION_H_
