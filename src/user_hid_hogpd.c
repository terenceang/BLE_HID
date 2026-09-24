/**
 ****************************************************************************************
 *
 * @file user_hid_hogpd.c
 *
 * @brief HID application layer (HOGPD profile glue) - BLE SNES gamepad device.
 *
 * Implements:
 *  - HID service database (single gamepad instance)
 *  - Profile enable on connection
 *  - Report Map (SNES gamepad: 12 buttons, Report ID 1)
 *  - Read/write handling of reports requested by the HID host
 *  - SNES pad polling (LATCH/CLOCK/DATA GPIO) reported as input reports
 *
 * Copyright (C) 2015-2023 Renesas Electronics Corporation and/or its affiliates.
 * All rights reserved. Confidential Information.
 *
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @addtogroup APP
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "rwip_config.h"
#include "prf.h"
#include "prf_utils.h"
#include "attm.h"                    // ATT_ERR_* status codes
#include "co_error.h"                // GAP_ERR_* status codes
#include "hogpd.h"
#include "hogpd_task.h"
#include "bass.h"
#include "bass_task.h"
#include "adc.h"
#include "gpio.h"
#include "user_periph_setup.h"
#include "app_easy_timer.h"
#if defined (CFG_SPI_FLASH_ENABLE)
#include "spi_flash.h"
#endif
#include "app_prf_perm_types.h"
#include "user_profiles_config.h"
#include "user_hid.h"
#include "user_hid_hogpd.h"

/*
 * GLOBAL VARIABLE DEFINITIONS
 ****************************************************************************************
 */

/// Standard USB HID gamepad Report Map (Report ID 1) - SNES keypad emulation.
/// Button bitmap (12 bits), bit order: A, B, Select, Start, D-pad Up/Down/Left/Right, X, Y, L, R (see SNES_BUTTON_*)
const uint8_t hid_report_map[] =
{
    0x05, 0x01,     /* Usage Page (Generic Desktop)      */
    0x09, 0x05,     /* Usage (Game Pad)                  */
    0xA1, 0x01,     /* Collection (Application)          */
    0x85, 0x01,     /*   Report ID (1)                   */

    /* --- SNES_BUTTON_COUNT face/system buttons (2 bytes, 16 bits, only SNES_BUTTON_COUNT used) --- */
    0x05, 0x09,     /*   Usage Page (Button)             */
    0x19, 0x01,     /*   Usage Minimum (1)               */
    0x29, SNES_BUTTON_COUNT, /*   Usage Maximum          */
    0x15, 0x00,     /*   Logical Minimum (0)             */
    0x25, 0x01,     /*   Logical Maximum (1)             */
    0x75, 0x01,     /*   Report Size (1)                 */
    0x95, SNES_BUTTON_COUNT, /*   Report Count           */
    0x81, 0x02,     /*   Input (Data, Var, Abs)          */
    /* padding bits up to a whole byte */
    0x75, 0x01,     /*   Report Size (1)                 */
    0x95, (HID_REPORT_LEN * 8 - SNES_BUTTON_COUNT), /* Report Count */
    0x81, 0x01,     /*   Input (Const, Arr, Abs)         */

    0xC0            /* End Collection                    */
};

/// Found in the .bin by tools/suota.py to fill the OTA image header version
const char app_version_tag[] __attribute__((used)) = "BLE_HID_VERSION=" APP_VERSION;

/// Device Information PnP ID (see APP_DIS_PNP_ID in user_profiles_config.h)
const unsigned char app_dis_pnp_id[7] =
{
    0x02, 0xFF, 0xFF, 0x01, 0x00, APP_VERSION_BCD & 0xFF, APP_VERSION_BCD >> 8
};

/*
 * DEFINES
 ****************************************************************************************
 */

// The bitmap is a uint16_t, and a zero-count padding item would be an invalid Report Map entry
_Static_assert(SNES_BUTTON_COUNT <= 16 && SNES_BUTTON_COUNT % 8 != 0, "SNES_BUTTON_COUNT: 1..16, not a multiple of 8");


/// Notification (CCC) configs of the HID and Battery services. Also the last values written by
/// the host, restored on reconnect (hosts don't rewrite them for bonded peers).
// ponytail: single peer; persisted in its own flash sector below the bond DB (0x1E000), per-peer if needed
static uint16_t ntf_cfg __SECTION_ZERO("retention_mem_area0");
static uint16_t batt_ntf_cfg __SECTION_ZERO("retention_mem_area0");

#if defined (CFG_SPI_FLASH_ENABLE)
#define CCC_FLASH_MAGIC                 (0xC0C0)

struct ccc_flash { uint16_t magic; uint16_t ntf_cfg; uint16_t batt_ntf_cfg; };

static void ccc_flash_load(void)
{
    struct ccc_flash rec;
    uint32_t n;

    spi_flash_release_from_power_down();
    spi_flash_read_data((uint8_t *)&rec, CCC_FLASH_ADDR, sizeof(rec), &n);
    spi_flash_power_down();

    if (rec.magic != CCC_FLASH_MAGIC)
    {
        return;
    }
    ntf_cfg = rec.ntf_cfg;
    // Records written before v0.2.0 end after ntf_cfg; the erased flash reads 0xFFFF
    batt_ntf_cfg = (rec.batt_ntf_cfg == 0xFFFF) ? 0 : rec.batt_ntf_cfg;
}

static void ccc_flash_save(void)
{
    struct ccc_flash rec = { CCC_FLASH_MAGIC, ntf_cfg, batt_ntf_cfg };
    uint32_t n;

    spi_flash_release_from_power_down();
    spi_flash_configure_memory_protection(SPI_FLASH_MEM_PROT_NONE);
    if (spi_flash_block_erase(CCC_FLASH_ADDR, SPI_FLASH_OP_SE) == SPI_FLASH_ERR_OK)
    {
        spi_flash_write_data((uint8_t *)&rec, CCC_FLASH_ADDR, sizeof(rec), &n);
    }
    spi_flash_power_down();
}
#endif

/// SNES controller state (updated by the application, sent as input reports)
static uint16_t snes_buttons __SECTION_ZERO("retention_mem_area0");

/// Pack a button bitmap into the input report (little-endian; padding bits above SNES_BUTTON_COUNT are 0)
static void pack_report(uint8_t report[HID_REPORT_LEN], uint16_t buttons)
{
    buttons &= (uint16_t)((1UL << SNES_BUTTON_COUNT) - 1);
    for (int i = 0; i < HID_REPORT_LEN; i++)
    {
        report[i] = (uint8_t)(buttons >> (8 * i));
    }
}

/// SNES pad shift-register bit n (B, Y, Select, Start, Up, Down, Left, Right, A, X, L, R) -> report bit
static const uint16_t snes_map[] =
{
    SNES_BUTTON_B, SNES_BUTTON_Y, SNES_BUTTON_SELECT, SNES_BUTTON_START,
    SNES_BUTTON_DPAD_UP, SNES_BUTTON_DPAD_DOWN, SNES_BUTTON_DPAD_LEFT, SNES_BUTTON_DPAD_RIGHT,
    SNES_BUTTON_A, SNES_BUTTON_X, SNES_BUTTON_L, SNES_BUTTON_R
};
_Static_assert(sizeof(snes_map) / sizeof(snes_map[0]) == SNES_BUTTON_COUNT, "snes_map[] needs SNES_BUTTON_COUNT entries");

/// Read the pad: 12 us LATCH pulse, then clock out the 12 button bits (DATA is active low)
static uint16_t snes_read(void)
{
    uint16_t buttons = 0;

    GPIO_SetActive(SNES_LATCH_PORT, SNES_LATCH_PIN);
    arch_asm_delay_us(12);
    GPIO_SetInactive(SNES_LATCH_PORT, SNES_LATCH_PIN);
    arch_asm_delay_us(6);

    for (int i = 0; i < SNES_BUTTON_COUNT; i++)
    {
        if (!GPIO_GetPinStatus(SNES_DATA_PORT, SNES_DATA_PIN))
        {
            buttons |= snes_map[i];
        }
        GPIO_SetInactive(SNES_CLOCK_PORT, SNES_CLOCK_PIN);      // clock idles high; pad shifts on the rising edge
        arch_asm_delay_us(6);
        GPIO_SetActive(SNES_CLOCK_PORT, SNES_CLOCK_PIN);
        arch_asm_delay_us(6);
    }
    return buttons;
}

uint16_t user_hid_read_pad(void)
{
    return snes_read();
}

/// Poll the pad every 10 ms while the host has notifications enabled; report on change
static timer_hnd poll_timer __SECTION_ZERO("retention_mem_area0");
#define POLL_PERIOD                     (1)        // timer units of 10 ms

/// Held this long while running, L + R + Start + Select reboots into OTA mode
#define OTA_COMBO                       (SNES_BUTTON_L | SNES_BUTTON_R | SNES_BUTTON_START | SNES_BUTTON_SELECT)
#define OTA_COMBO_POLLS                 (300)       // x POLL_PERIOD (10 ms) = 3 s

static void poll_timer_cb(void)
{
    static uint16_t combo_polls;
    uint16_t buttons = snes_read();

    combo_polls = ((buttons & OTA_COMBO) == OTA_COMBO) ? combo_polls + 1 : 0;
    if (combo_polls >= OTA_COMBO_POLLS)
    {
        user_hid_request_ota();                     // does not return
    }

    if (buttons != snes_buttons)
    {
        snes_buttons = buttons;
        app_hogpd_send_gamepad_report(app_connection_idx, buttons);
        user_hid_activity();
    }
    poll_timer = app_easy_timer(POLL_PERIOD, poll_timer_cb);
}

void user_hid_poll_stop(void)
{
    if (poll_timer != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(poll_timer);
        poll_timer = EASY_TIMER_INVALID_TIMER;
    }
}

/// HID Control Point: the host suspended the device (cleared on Exit Suspend and on connect)
static bool hid_suspended __SECTION_ZERO("retention_mem_area0");

/// (Re)start the pad polling if the host has notifications enabled and hasn't suspended
void user_hid_poll_resume(void)
{
    user_hid_poll_stop();
    if ((ntf_cfg & HOGPD_CFG_REPORT_NTF_EN) && !hid_suspended)
    {
        poll_timer = app_easy_timer(POLL_PERIOD, poll_timer_cb);
    }
}

void user_hid_set_suspended(bool suspended)
{
    hid_suspended = suspended;
    user_hid_poll_resume();
}

/// snes_buttons is updated before the report is sent; on failure make it differ from any real
/// state (only 12 bits are used) so the next poll resends instead of leaving a key stuck on the host
void user_hid_report_failed(void)
{
    snes_buttons = 0xFFFF;
}

/*
 * GLOBAL FUNCTION DEFINITIONS
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @brief Create the HID service database. Sends a GAPM_PROFILE_TASK_ADD_CMD to the
 *        GAPM task; the parameter payload is a struct hogpd_db_cfg describing one
 *        HID gamepad service instance.
 ****************************************************************************************
 */
void app_hogpd_create_db(void)
{
    struct hogpd_db_cfg* db_cfg;
    static const uint8_t report_char_cfg[HOGPD_NB_REPORT_INST_MAX] = APP_HOGPD_REPORT_CHAR_CFG;
    static const uint8_t report_ids[HOGPD_NB_REPORT_INST_MAX]      = APP_HOGPD_REPORT_IDS;

#if defined (CFG_SPI_FLASH_ENABLE)
    ccc_flash_load();   // runs once per boot (retained across sleep)
#endif

    // Allocate the message
    struct gapm_profile_task_add_cmd *req =
            KE_MSG_ALLOC_DYN(GAPM_PROFILE_TASK_ADD_CMD,
                             TASK_GAPM,
                             TASK_APP,
                             gapm_profile_task_add_cmd,
                             sizeof(struct hogpd_db_cfg));

    // Fill message
    req->operation   = GAPM_PROFILE_TASK_ADD;
    req->sec_lvl     = get_user_prf_srv_perm(TASK_ID_HOGPD);
    req->prf_task_id = TASK_ID_HOGPD;
    req->app_task    = TASK_APP;
    req->start_hdl   = 0;

    // Set the HID service configuration
    db_cfg = (struct hogpd_db_cfg *) req->param;
    db_cfg->hids_nb = APP_HOGPD_HIDS_NB;

    db_cfg->cfg[0].svc_features     = APP_HOGPD_SVC_FEATURES;
    db_cfg->cfg[0].report_nb        = APP_HOGPD_REPORT_NB;
    memcpy(&db_cfg->cfg[0].report_char_cfg[0], report_char_cfg, HOGPD_NB_REPORT_INST_MAX);
    memcpy(&db_cfg->cfg[0].report_id[0], report_ids, HOGPD_NB_REPORT_INST_MAX);
    db_cfg->cfg[0].hid_info.bcdHID        = APP_HOGPD_BCD_HID;
    db_cfg->cfg[0].hid_info.bCountryCode  = 0;
    db_cfg->cfg[0].hid_info.flags         = APP_HOGPD_HID_FLAGS;
    db_cfg->cfg[0].ext_ref.inc_svc_hdl    = 0;
    db_cfg->cfg[0].ext_ref.rep_ref_uuid   = 0;

    // Send the message
    ke_msg_send(req);

    printk("app_hogpd_create_db\n");
}

/**
 ****************************************************************************************
 * @brief Enable the HOGPD profile for a connection (restores notification
 *        configuration; zero until the HID host re-enables notifications).
 ****************************************************************************************
 */
void app_hogpd_enable(uint8_t conidx)
{
    // Allocate the message
    struct hogpd_enable_req *req = KE_MSG_ALLOC(HOGPD_ENABLE_REQ,
                                                prf_get_task_from_id(TASK_ID_HOGPD),
                                                TASK_APP,
                                                hogpd_enable_req);

    // Fill in the parameter structure
    req->conidx = conidx;
    req->ntf_cfg[0] = ntf_cfg;          // Restore the bonded host's CCC (0 until it first enables)

    snes_buttons  = 0;
    hid_suspended = false;
    poll_timer    = EASY_TIMER_INVALID_TIMER;
    user_hid_poll_resume();

    // Send the message
    ke_msg_send(req);

    printk("app_hogpd_enable: conidx=%d\n", conidx);
}

/**
 ****************************************************************************************
 * @brief Send any Report-protocol input report as a notification to the HID host.
 ****************************************************************************************
 */
uint8_t app_hogpd_send_input_report(uint8_t conidx, const uint8_t *report, uint16_t length)
{
    if (app_connection_idx == -1)
    {
        return PRF_ERR_REQ_DISALLOWED;
    }

    // Notifications must be enabled by the host for report #0 (bit set in the CCC)
    if ((ntf_cfg & HOGPD_CFG_REPORT_NTF_EN) == 0)
    {
        return PRF_ERR_IND_DISABLED;
    }

    if (length > HOGPD_REPORT_MAX_LEN)
    {
        return PRF_ERR_INVALID_PARAM;
    }

    // Allocate the message
    struct hogpd_report_upd_req *req = KE_MSG_ALLOC_DYN(HOGPD_REPORT_UPD_REQ,
                                                        prf_get_task_from_id(TASK_ID_HOGPD),
                                                        TASK_APP,
                                                        hogpd_report_upd_req,
                                                        length);

    // Fill in the parameter structure
    req->conidx           = conidx;
    req->report.hid_idx   = 0;
    req->report.type      = HOGPD_REPORT;
    req->report.idx       = 0;
    req->report.length    = length;
    memcpy(&req->report.value[0], report, length);

    // Send the message
    ke_msg_send(req);

    return GAP_ERR_NO_ERROR;
}

/**
 ****************************************************************************************
 * @brief Send a SNES gamepad input report: 2-byte button bitmap.
 * @param[in] conidx  Connection index
 * @param[in] buttons Button bitmask (OR of SNES_BUTTON_* defines)
 ****************************************************************************************
 */
void app_hogpd_send_gamepad_report(uint8_t conidx, uint16_t buttons)
{
    uint8_t report[HID_REPORT_LEN];

    pack_report(report, buttons);

    app_hogpd_send_input_report(conidx, report, HID_REPORT_LEN);
}

/**
 ****************************************************************************************
 * @brief Handle HOGPD_NTF_CFG_IND. The HID host enabled/disabled notifications.
 ****************************************************************************************
 */
void user_hid_ntf_cfg_ind_handler(struct hogpd_ntf_cfg_ind const *param)
{
    uint16_t cfg = param->ntf_cfg[0];
    bool changed = (cfg != ntf_cfg);

    ntf_cfg = cfg;
#if defined (CFG_SPI_FLASH_ENABLE)
    if (changed)
    {
        ccc_flash_save();
    }
#endif
    printk("HOGPD_NTF_CFG_IND: %04x\n", cfg);

    user_hid_poll_resume();
}

/**
 ****************************************************************************************
 * @brief Handle HOGPD_REPORT_REQ_IND (host read of the Report Map or input report).
 *        Replies with HOGPD_REPORT_CFM - mandatory for every request.
 ****************************************************************************************
 */
void user_hid_report_req_ind_handler(struct hogpd_report_req_ind const *param,
                                     ke_task_id_t const dest_id,
                                     ke_task_id_t const src_id)
{
    const uint8_t *value = NULL;
    uint16_t length = 0;
    uint8_t status = ATT_ERR_NO_ERROR;

    if (param->operation == HOGPD_OP_REPORT_READ)
    {
        switch (param->report.type)
        {
            case HOGPD_REPORT_MAP:
            {
                value  = hid_report_map;
                length = sizeof(hid_report_map);
            } break;

            case HOGPD_REPORT:
            {
                // Return the last known gamepad state
                static uint8_t last_report[HID_REPORT_LEN];
                pack_report(last_report, snes_buttons);
                value  = last_report;
                length = HID_REPORT_LEN;
            } break;

            default:
            {
                status = ATT_ERR_REQUEST_NOT_SUPPORTED;
            } break;
        }
    }
    else if (param->operation == HOGPD_OP_REPORT_WRITE)
    {
        status = ATT_ERR_WRITE_NOT_PERMITTED;
    }
    else
    {
        status = ATT_ERR_REQUEST_NOT_SUPPORTED;
    }

    // Allocate and send the confirmation
    struct hogpd_report_cfm *cfm = KE_MSG_ALLOC_DYN(HOGPD_REPORT_CFM,
                                                    src_id,
                                                    dest_id,
                                                    hogpd_report_cfm,
                                                    length);

    cfm->conidx    = param->conidx;
    cfm->operation = param->operation;
    cfm->status    = status;
    cfm->report.hid_idx = param->report.hid_idx;
    cfm->report.type    = param->report.type;
    cfm->report.idx     = param->report.idx;
    cfm->report.length  = length;

    if ((value != NULL) && (length > 0))
    {
        memcpy(&cfm->report.value[0], value, length);
    }

    ke_msg_send(cfm);
}

/**
 ****************************************************************************************
 * @brief Handle HOGPD_PROTO_MODE_REQ_IND (host switches between Boot and Report
 *        protocol). Replies with HOGPD_PROTO_MODE_CFM - mandatory for every request.
 ****************************************************************************************
 */
void user_hid_proto_mode_req_ind_handler(struct hogpd_proto_mode_req_ind const *param,
                                         ke_task_id_t const dest_id,
                                         ke_task_id_t const src_id)
{
    printk("HOGPD_PROTO_MODE_REQ_IND: mode=%d\n", param->proto_mode);

    // Report protocol only: there is no boot report for a gamepad, and in boot mode HOGPD
    // rejects every input report. A non-OK status leaves the mode unchanged.
    struct hogpd_proto_mode_cfm *cfm = KE_MSG_ALLOC(HOGPD_PROTO_MODE_CFM,
                                                    src_id,
                                                    dest_id,
                                                    hogpd_proto_mode_cfm);

    cfm->conidx     = param->conidx;
    cfm->status     = (param->proto_mode == HOGP_REPORT_PROTOCOL_MODE) ? ATT_ERR_NO_ERROR
                                                                       : ATT_ERR_REQUEST_NOT_SUPPORTED;
    cfm->hid_idx    = param->hid_idx;
    cfm->proto_mode = param->proto_mode;

    ke_msg_send(cfm);
}

/*
 * BATTERY SERVICE (standard BAS). Level = VBAT via the on-chip ADC, same code and table as HMCLOCK.
 ****************************************************************************************
 */

#define BATT_POLL_PERIOD                (6000)     // timer units of 10 ms = 60 s

/// Last VBAT reading in mV (read it with a debugger if the percentage looks wrong)
uint16_t user_batt_mv __SECTION_ZERO("retention_mem_area0");
static uint8_t batt_level __SECTION_ZERO("retention_mem_area0");
static timer_hnd batt_timer __SECTION_ZERO("retention_mem_area0");

/// Piecewise-linear percent from the raw VBAT ADC count (225/128 mV per count). HMCLOCK's curve
/// (0/4/28/100 % at 2.0/2.39/2.78/3.0 V) stretched onto the adapter's supply range:
/// 2.2 V = 0 %, 2.43 V = 4 %, 2.67 V = 28 %, 2.8 V = 100 %. Change the two ends to move it.
#define BATT_EMPTY_MV                   (2200)
#define BATT_FULL_MV                    (2800)
#define BATT_MV_CNT(mv)                 ((uint32_t)(mv) * 128 / 225)
/// HMCLOCK breakpoint count (1136..1705) -> the same relative point between our ends
#define BATT_SPAN                       (BATT_MV_CNT(BATT_FULL_MV) - BATT_MV_CNT(BATT_EMPTY_MV))
#define BATT_CNT(hm)                    ((uint16_t)(BATT_MV_CNT(BATT_EMPTY_MV) + ((hm) - 1136) * BATT_SPAN / (1705 - 1136)))
static uint8_t batt_cal(uint16_t s)
{
    const uint16_t c100 = BATT_CNT(1705), c28 = BATT_CNT(1584), c4 = BATT_CNT(1360), c0 = BATT_CNT(1136);

    if (s > c100) return 100;
    if (s > c28)  return 28 + (uint8_t)((((uint32_t)(s - c28) << 16) / (c100 - c28) * 72) >> 16);
    if (s > c4)   return 4  + (uint8_t)((((uint32_t)(s - c4)  << 16) / (c28 - c4)   * 24) >> 16);
    if (s > c0)   return      (uint8_t)((((uint32_t)(s - c0)  << 16) / (c4 - c0)    * 4)  >> 16);
    return 0;
}

/// Sample VBAT (same calls as HMCLOCK adc1_update()) and return the charge percentage.
/// Calibrates every time: the ADC offset registers are in the peripheral power domain, which
/// is off during sleep, so a cached calibration is lost (costs microseconds, once a minute).
static uint8_t batt_read_level(void)
{
    adc_offset_calibrate(ADC_INPUT_MODE_SINGLE_ENDED);
    uint16_t adc = (uint16_t)adc_get_vbat_sample(false);
    user_batt_mv = (uint16_t)((adc * 225) >> 7);          // 225/128 mV per count
    return batt_cal(adc);
}

/// Store the level in the Battery Service (what a host read returns) and notify it if enabled
static void batt_notify(uint8_t level)
{
    struct bass_batt_level_upd_req *req = KE_MSG_ALLOC(BASS_BATT_LEVEL_UPD_REQ,
                                                        prf_get_task_from_id(TASK_ID_BASS),
                                                        TASK_APP,
                                                        bass_batt_level_upd_req);
    req->bas_instance = 0;
    req->batt_level   = level;
    ke_msg_send(req);
}

static void batt_poll_cb(void)
{
    uint8_t level = batt_read_level();

    if (level != batt_level)
    {
        batt_level = level;
        batt_notify(level);
    }
    batt_timer = app_easy_timer(BATT_POLL_PERIOD, batt_poll_cb);
}

void user_batt_stop(void)
{
    if (batt_timer != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(batt_timer);
        batt_timer = EASY_TIMER_INVALID_TIMER;
    }
}

/// (Re)start the battery poll if the host has battery notifications enabled
static void batt_timer_update(void)
{
    user_batt_stop();
    if (batt_ntf_cfg)
    {
        batt_timer = app_easy_timer(BATT_POLL_PERIOD, batt_poll_cb);
    }
}

/// Handle BASS_BATT_LEVEL_NTF_CFG_IND: start/stop the poll timer, and push the current level
/// immediately so the host doesn't sit at its default (0%) until the level happens to change -
/// batt_poll_cb() only notifies on change.
void user_batt_ntf_cfg_ind_handler(struct bass_batt_level_ntf_cfg_ind const *param)
{
    bool changed = (param->ntf_cfg != batt_ntf_cfg);

    batt_ntf_cfg = param->ntf_cfg;
#if defined (CFG_SPI_FLASH_ENABLE)
    if (changed)
    {
        ccc_flash_save();
    }
#endif
    batt_timer_update();

    if (batt_ntf_cfg == 0)
        return;

    batt_notify(batt_level);
}

void user_batt_create_db(void)
{
    struct gapm_profile_task_add_cmd *req = KE_MSG_ALLOC_DYN(GAPM_PROFILE_TASK_ADD_CMD,
                                                             TASK_GAPM,
                                                             TASK_APP,
                                                             gapm_profile_task_add_cmd,
                                                             sizeof(struct bass_db_cfg));
    struct bass_db_cfg *db_cfg = (struct bass_db_cfg *)req->param;

    req->operation   = GAPM_PROFILE_TASK_ADD;
    req->sec_lvl     = get_user_prf_srv_perm(TASK_ID_BASS);
    req->prf_task_id = TASK_ID_BASS;
    req->app_task    = TASK_APP;
    req->start_hdl   = 0;

    db_cfg->bas_nb       = 1;
    db_cfg->features[0]  = BAS_BATT_LVL_NTF_SUP;
    db_cfg->batt_level_pres_format[0].unit        = 0x27AD;     // percentage
    db_cfg->batt_level_pres_format[0].description = 0;
    db_cfg->batt_level_pres_format[0].format      = 4;          // uint8
    db_cfg->batt_level_pres_format[0].exponent    = 0;
    db_cfg->batt_level_pres_format[0].name_space  = 1;

    ke_msg_send(req);
}

void user_batt_enable(uint8_t conidx)
{
    struct bass_enable_req *req = KE_MSG_ALLOC(BASS_ENABLE_REQ,
                                               prf_get_task_from_id(TASK_ID_BASS),
                                               TASK_APP,
                                               bass_enable_req);

    batt_level = batt_read_level();
    req->conidx          = conidx;
    req->ntf_cfg         = batt_ntf_cfg;    // Restore the bonded host's CCC (0 until it first enables)
    req->old_batt_lvl[0] = batt_level;
    ke_msg_send(req);

    // BASS only stores a level via BASS_BATT_LEVEL_UPD_REQ; without this a host read returns
    // 0% until the level changes. Also notifies a bonded host that doesn't rewrite its CCC.
    // Queued by BASS until the enable above completes.
    batt_notify(batt_level);

    batt_timer = EASY_TIMER_INVALID_TIMER;
    batt_timer_update();
    printk("battery: %d mV, %d%%\n", user_batt_mv, batt_level);
}

/// @} APP
