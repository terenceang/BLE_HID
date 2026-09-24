/**
 ****************************************************************************************
 *
 * @file user_profiles_config.h
 *
 * @brief Configuration file for the profiles used in the application.
 *
 * Copyright (C) 2015-2023 Renesas Electronics Corporation and/or its affiliates.
 * All rights reserved. Confidential Information.
 *
 ****************************************************************************************
 */

#ifndef _USER_PROFILES_CONFIG_H_
#define _USER_PROFILES_CONFIG_H_

/**
 ****************************************************************************************
 * @defgroup APP_CONFIG
 * @ingroup APP
 * @brief  Application configuration file
 *
 * This file contains the configuration of the profiles used by the application.
 *
 * @{
 ****************************************************************************************
 */

/*
 * DEFINITIONS
 ****************************************************************************************
 */

/***************************************************************************************/
/* Used BLE profiles (used by "rwprf_config.h").                                       */
/***************************************************************************************/

/// HID over GATT Profile - Device role (HOGPD)
#define CFG_PRF_HOGPD
#define CFG_PRF_BASS
/// Device Information Service: required by HOGP; exposes manufacturer, model, firmware version
#define CFG_PRF_DISS
/// SUOTA receiver (over-the-air update). Locked (SRV_PERM_DISABLE) unless the pad was powered
/// on with Start + Select held; then it needs an encrypted link (see user_app_init()).
#define CFG_PRF_SUOTAR

/// The SDK app_bass module is compiled but unused (user_batt_* in user_hid_hogpd.c replaces it,
/// the SDK one is CR2032-only). These only satisfy its build; there is no battery LED.
#define USE_BAT_LEVEL_ALERT             (0)
#define GPIO_BAT_LED_PORT               GPIO_PORT_0
#define GPIO_BAT_LED_PIN                GPIO_PIN_0
#define APP_BASS_POLL_INTERVAL          (6000)

/***************************************************************************************/
/* Profile application configuration section                                           */
/***************************************************************************************/

/*
 ****************************************************************************************
 * HOGPD application profile configuration
 ****************************************************************************************
 */

/// Number of HID service instances exposed by the device (1 = a single HID gamepad)
#define APP_HOGPD_HIDS_NB               (1)

/// Service features: Report protocol mode only (no boot protocol for gamepad)
#define APP_HOGPD_SVC_FEATURES          (HOGPD_CFG_PROTO_MODE)

/// Number of Report characteristics in the HID service (a single Report-protocol input report)
#define APP_HOGPD_REPORT_NB             (1)

/// Report characteristics configuration: Report #0 is an input report (notifiable)
#define APP_HOGPD_REPORT_CHAR_CFG       { HOGPD_CFG_REPORT_IN, 0, 0, 0, 0 }

/// Report IDs assigned to the reports (must match the Report Map)
#define APP_HOGPD_REPORT_IDS            { 1, 0, 0, 0, 0 }

/// bcdHID release of the gamepad (1.01)
#define APP_HOGPD_BCD_HID               (0x0101)

/// HID Information flags: normally connectable + remote wake capable
#define APP_HOGPD_HID_FLAGS             (HIDS_NORM_CONNECTABLE | HIDS_REMOTE_WAKE_CAPABLE)

/*
 ****************************************************************************************
 * DISS application profile configuration
 ****************************************************************************************
 */

#include "app_version.h"                // APP_VERSION, APP_VERSION_BCD

#define APP_DIS_FEATURES                (DIS_MANUFACTURER_NAME_CHAR_SUP | \
                                         DIS_MODEL_NB_STR_CHAR_SUP | \
                                         DIS_FIRM_REV_STR_CHAR_SUP | \
                                         DIS_PNP_ID_CHAR_SUP)

#define APP_DIS_MANUFACTURER_NAME       ("motionfxdesign")
#define APP_DIS_MANUFACTURER_NAME_LEN   (sizeof(APP_DIS_MANUFACTURER_NAME) - 1)

#define APP_DIS_MODEL_NB_STR            ("BT-SNES")
#define APP_DIS_MODEL_NB_STR_LEN        (sizeof(APP_DIS_MODEL_NB_STR) - 1)

#define APP_DIS_FIRM_REV_STR            APP_VERSION
#define APP_DIS_FIRM_REV_STR_LEN        (sizeof(APP_DIS_FIRM_REV_STR) - 1)

/// PnP ID: vendor ID source 0x02 (USB-IF), VID 0xFFFF / PID 0x0001 (unassigned placeholder -
/// not a registered vendor), product version APP_VERSION_BCD. All little-endian.
/// A real array defined in user_hid_hogpd.c: app_diss_task.c keeps the pointer past its block
#if !defined (ARM_SCATTERFILE)             // the linker scatter file preprocesses this header too
extern const unsigned char app_dis_pnp_id[7];
#endif
#define APP_DIS_PNP_ID                  (app_dis_pnp_id)
#define APP_DIS_PNP_ID_LEN              (7)

/// Unused characteristics (not in APP_DIS_FEATURES); app_diss.c still needs the macros
#define APP_DIS_SYSTEM_ID               ("\x00\x00\x00\x00\x00\x00\x00\x00")
#define APP_DIS_SYSTEM_ID_LEN           (8)
#define APP_DIS_SERIAL_NB_STR           ("")
#define APP_DIS_SERIAL_NB_STR_LEN       (0)
#define APP_DIS_HARD_REV_STR            ("")
#define APP_DIS_HARD_REV_STR_LEN        (0)
#define APP_DIS_SW_REV_STR              APP_VERSION
#define APP_DIS_SW_REV_STR_LEN          (sizeof(APP_DIS_SW_REV_STR) - 1)
#define APP_DIS_IEEE                    ("")
#define APP_DIS_IEEE_LEN                (0)

/// @} APP_CONFIG

#endif // _USER_PROFILES_CONFIG_H_
