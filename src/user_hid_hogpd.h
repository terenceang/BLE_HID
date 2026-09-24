/**
 ****************************************************************************************
 *
 * @file user_hid_hogpd.h
 *
 * @brief HID application layer (HOGPD profile glue) - BLE HID keyboard device.
 *
 * The SDK has no application module for the HOGPD profile, so this file provides
 * the database creation / connection enable functions (registered in the
 * user_prf_funcs table of user_callback_config.h) and the handlers for the
 * messages the HOGPD task sends to the application. They are dispatched from
 * user_catch_rest_hndl() in user_hid.c.
 *
 * Copyright (C) 2015-2023 Renesas Electronics Corporation and/or its affiliates.
 * All rights reserved. Confidential Information.
 *
 ****************************************************************************************
 */

#ifndef _USER_HID_HOGPD_H_
#define _USER_HID_HOGPD_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include <stdint.h>
#include <stdbool.h>
#include "rwip_config.h"
#include "rwprf_config.h"
#include "ke_task.h"
#include "hogpd.h"
#include "bass_task.h"

/*
 * DEFINES
 ****************************************************************************************
 */

/*
 * GLOBAL VARIABLE DECLARATIONS
 ****************************************************************************************
 */

/// HID Report Map: SNES gamepad report descriptor (Report ID 1)
extern const uint8_t hid_report_map[];

/// Number of SNES buttons: the single source for the Report Map (button count + padding), the
/// input report length and pack_report()'s mask. snes_map[] (user_hid_hogpd.c) lists them in
/// shift-register order, so it must have this many entries.
#define SNES_BUTTON_COUNT               (12)

/// Input report length: the button bitmap rounded up to whole bytes (uint16_t bitmap: max 16 buttons)
#define HID_REPORT_LEN                  ((SNES_BUTTON_COUNT + 7) / 8)

/// SNES button bit positions
#define SNES_BUTTON_A                   (1 << 0)
#define SNES_BUTTON_B                   (1 << 1)
#define SNES_BUTTON_SELECT              (1 << 2)
#define SNES_BUTTON_START               (1 << 3)
#define SNES_BUTTON_DPAD_UP             (1 << 4)
#define SNES_BUTTON_DPAD_DOWN           (1 << 5)
#define SNES_BUTTON_DPAD_LEFT           (1 << 6)
#define SNES_BUTTON_DPAD_RIGHT          (1 << 7)
#define SNES_BUTTON_X                   (1 << 8)
#define SNES_BUTTON_Y                   (1 << 9)
#define SNES_BUTTON_L                   (1 << 10)
#define SNES_BUTTON_R                   (1 << 11)


/// Index of the active connection (-1 when disconnected). Defined in user_hid.c.
extern int app_connection_idx;

/*
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @brief Create the HID service database. Called by the SDK during the database
 *        initialization phase (entry in the user_prf_funcs table).
 ****************************************************************************************
 */
void app_hogpd_create_db(void);

/**
 ****************************************************************************************
 * @brief Enable the HOGPD profile for a new connection. Called by the SDK right
 *        after a connection has been established.
 * @param[in] conidx Connection index
 ****************************************************************************************
 */
void app_hogpd_enable(uint8_t conidx);

/**
 ****************************************************************************************
 * @brief Send a Report-protocol input report to the HID host (notification).
 * @param[in] conidx Connection index
 * @param[in] report Report data (without the Report ID)
 * @param[in] length Report length in bytes (max HOGPD_REPORT_MAX_LEN)
 * @return GAP_ERR_NO_ERROR on success, error code otherwise
 ****************************************************************************************
 */
uint8_t app_hogpd_send_input_report(uint8_t conidx, const uint8_t *report, uint16_t length);

/**
 ****************************************************************************************
 * @brief Send a SNES gamepad input report: 2-byte button bitmap.
 * @param[in] conidx  Connection index
 * @param[in] buttons Button bitmask (OR of SNES_BUTTON_* defines)
 ****************************************************************************************
 */
void app_hogpd_send_gamepad_report(uint8_t conidx, uint16_t buttons);

/// Read the SNES pad once (OR of SNES_BUTTON_*)
uint16_t user_hid_read_pad(void);

/// Stop the pad polling timer (call on disconnect)
void user_hid_poll_stop(void);

/// (Re)start the pad polling timer if the host has notifications enabled and hasn't suspended
/// (call on connect or on HOGPD_NTF_CFG_IND)
void user_hid_poll_resume(void);

/// HID Control Point Suspend (true) / Exit Suspend (false): stops/resumes pad polling.
/// Remembered until Exit Suspend or the next connection.
void user_hid_set_suspended(bool suspended);

/// Reboot into OTA mode (L + R + Start + Select held 3 s; defined in user_hid.c)
void user_hid_request_ota(void);

/// A button changed: restart the connected-idle timeout (defined in user_hid.c)
void user_hid_activity(void);

/// A report notification failed (HOGPD_REPORT_UPD_RSP error): resend the pad state on the next poll
void user_hid_report_failed(void);

/**
 ****************************************************************************************
 * @brief Handle HOGPD_NTF_CFG_IND: the HID host enabled/disabled notifications.
 ****************************************************************************************
 */
void user_hid_ntf_cfg_ind_handler(struct hogpd_ntf_cfg_ind const *param);

/**
 ****************************************************************************************
 * @brief Handle HOGPD_REPORT_REQ_IND: the HID host read or wrote a report.
 *        Replies with HOGPD_REPORT_CFM (mandatory, otherwise the host stalls).
 ****************************************************************************************
 */
void user_hid_report_req_ind_handler(struct hogpd_report_req_ind const *param,
                                     ke_task_id_t const dest_id,
                                     ke_task_id_t const src_id);

/**
 ****************************************************************************************
 * @brief Handle HOGPD_PROTO_MODE_REQ_IND: the HID host changed the protocol mode.
 *        Replies with HOGPD_PROTO_MODE_CFM (mandatory).
 ****************************************************************************************
 */
void user_hid_proto_mode_req_ind_handler(struct hogpd_proto_mode_req_ind const *param,
                                         ke_task_id_t const dest_id,
                                         ke_task_id_t const src_id);

/// Battery Service (level from VBAT, as HMCLOCK): profile db creation, per-connection enable, stop on disconnect
void user_batt_create_db(void);
void user_batt_enable(uint8_t conidx);
void user_batt_ntf_cfg_ind_handler(struct bass_batt_level_ntf_cfg_ind const *param);
void user_batt_stop(void);

#endif // _USER_HID_HOGPD_H_
