/**
 ****************************************************************************************
 *
 * @file user_hid.c
 *
 * @brief BLE HID gamepad device application source file. Implements the GAP
 *        peripheral flow (advertising, connection, security) and dispatches the
 *        HOGPD profile messages to the HID application layer (user_hid_hogpd.c).
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

#include "rwip_config.h"             // SW configuration
#include "gap.h"                     // GAP definitions
#include "gapc_task.h"               // GAP task definitions
#include "gattc_task.h"              // GATT task definitions
#include "gapm_task.h"               // GAPM task definitions
#include "app_easy_timer.h"          // Application timer
#include "user_config.h"             // User configuration (adv data, name, params)
#include "user_periph_setup.h"       // Peripheral setup
#include "user_hid.h"
#include "app_easy_security.h"
#include "user_hid_hogpd.h"          // HID application layer (HOGPD glue)
#include "selflash.h"               // Persist firmware in flash
#include "wkupct_quadec.h"           // GPIO wakeup controller (deep-sleep wake on SNES B)
#include "arch_api.h"                // arch_set_deep_sleep()
#include "spi_flash.h"               // spi_flash_power_down()
#include "app_suotar.h"             // suota_state, SUOTAR_START/END
#include "app_prf_perm_types.h"      // app_set_prf_srv_perm()
#include "ota_verify.h"              // ota_check_new_image()
#include "arch.h"                    // RESET_AFTER_SUOTA_UPDATE

#define PRINT_BDADDR(a) (a)[5], (a)[4], (a)[3], (a)[2], (a)[1], (a)[0]

/// The device's own BD address (set during system init, well before user_app_init() runs)
extern struct bd_addr dev_bdaddr;


/*
 * GLOBAL VARIABLE DEFINITIONS
 ****************************************************************************************
 */

int app_connection_idx                        __SECTION_ZERO("retention_mem_area0"); // @RETENTION MEMORY
timer_hnd app_param_update_request_timer_used __SECTION_ZERO("retention_mem_area0"); // @RETENTION MEMORY

/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @brief Parameter-update-request timer callback. Issues a connection parameter
 *        update request once the timer expires.
 ****************************************************************************************
 */
static void param_update_request_timer_cb(void)
{
    app_easy_gap_param_update_start(app_connection_idx);
    app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;
}

/// Advertise this long without a connection, then deep sleep until SNES B is pressed
#define ADV_IDLE_TIMEOUT                MS_TO_TIMERUNITS(180000)    // 3 minutes

/// Connected with no button change this long -> disconnect and deep sleep
#define CONN_IDLE_TIMEOUT               MS_TO_TIMERUNITS(180000)    // 3 minutes

/// Both idle timeouts in OTA mode: long enough for an update (1-2 min), but a pad left in OTA mode
/// still ends up in deep sleep (which also leaves OTA mode) instead of draining its battery
#define OTA_IDLE_TIMEOUT                MS_TO_TIMERUNITS(600000)    // 10 minutes

static timer_hnd idle_timer __SECTION_ZERO("retention_mem_area0");
/// Powered on with Start + Select held: OTA service unlocked, idle timeouts off
static bool ota_mode __SECTION_ZERO("retention_mem_area0");

extern void platform_reset(uint32_t error);

/// Set just before a software reset to come back up in OTA mode. Uninitialised retained RAM
/// (like selflash_magic): a SW reset keeps it, a power cycle or deep-sleep wake doesn't.
#define OTA_REQUEST_MAGIC               (0x07A0B007u)
volatile uint32_t ota_request __attribute__((section(".bss.retention_mem_area_uninit"), used));

void user_hid_request_ota(void)
{
    printk("OTA combo held: rebooting into OTA mode\n");
    ota_request = OTA_REQUEST_MAGIC;
    platform_reset(RESET_AFTER_SUOTA_UPDATE);       // SW reset through the boot ROM
}
/// Set when the idle timer disconnected us: deep sleep on disconnect instead of advertising
static bool idle_sleep __SECTION_ZERO("retention_mem_area0");

static void idle_timer_cb(void)
{
    idle_timer = EASY_TIMER_INVALID_TIMER;
    idle_sleep = true;
    printk("idle: no input, disconnecting\n");
    app_easy_gap_disconnect(app_connection_idx);
}

/// Called on every button change: restart the connected-idle timeout
void user_hid_activity(void)
{
    if (idle_timer != EASY_TIMER_INVALID_TIMER)
    {
        idle_timer = app_easy_timer_modify(idle_timer, ota_mode ? OTA_IDLE_TIMEOUT : CONN_IDLE_TIMEOUT);
    }
}

/**
 ****************************************************************************************
 * @brief Deep sleep until SNES B is pressed; waking reboots the chip (boots from flash
 *        and advertises again). Holding LATCH high makes the pad load its shift register
 *        continuously, so DATA follows B live (active low) instead of staying frozen on
 *        the last clocked bit. Pads keep their state during deep sleep.
 ****************************************************************************************
 */
static void user_hid_deep_sleep(void)
{
    printk("idle: deep sleep, press B to wake\n");

    GPIO_SetActive(SNES_LATCH_PORT, SNES_LATCH_PIN);
    spi_flash_power_down();
    wkupct_enable_irq(WKUPCT_PIN_SELECT(SNES_DATA_PORT, SNES_DATA_PIN),
                      WKUPCT_PIN_POLARITY(SNES_DATA_PORT, SNES_DATA_PIN, WKUPCT_PIN_POLARITY_LOW),
                      1, 0);    // 1 event, no debounce (the wake is a reboot anyway)
    arch_set_deep_sleep(true);  // does not return
}

/**
 ****************************************************************************************
 * @brief Application initialization. Sets the GAP appearance (Gamepad) and
 *        runs the default application initialization flow (which in turn creates
 *        the attribute database, starting with the profiles registered in the
 *        user_prf_funcs table of user_callback_config.h).
 ****************************************************************************************
 */
void user_app_init(void)
{
    printk("\n\nBLE_HID v" APP_VERSION " user_app_init! %s\n", __TIME__);

    app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;
    app_connection_idx = -1;
    idle_timer = EASY_TIMER_INVALID_TIMER;

    // Persist this image in external flash so it boots after a power cycle
    selflash_run();

    // Load the bond DB from SPI flash (default_app_on_init does not)
    app_easy_security_bdb_init();

    // Advertise and expose the Gamepad appearance
    device_info.appearance = APP_HID_APPEARANCE_GAMEPAD;

    // OTA mode: Start + Select held at boot (power-on, or B-wake from deep sleep), or the
    // L + R + Start + Select combo held while running (user_hid_request_ota() -> SW reset)
    ota_mode = ((user_hid_read_pad() & (SNES_BUTTON_START | SNES_BUTTON_SELECT))
                                    == (SNES_BUTTON_START | SNES_BUTTON_SELECT)) ||
               ota_request == OTA_REQUEST_MAGIC;
    ota_request = 0;

    default_app_on_init();      // resets all service permissions (prf_init_srv_perm())

    // SUOTA is locked unless in OTA mode, and then needs an encrypted (bonded) link
    app_set_prf_srv_perm(TASK_ID_SUOTAR, ota_mode ? SRV_PERM_UNAUTH : SRV_PERM_DISABLE);
    printk("OTA mode: %d\n", ota_mode);
}

/**
 ****************************************************************************************
 * @brief SUOTA started/ended (app_suotar_cb, user_callback_config.h). Wakes the SPI flash
 *        and stops pad polling while an image is being written.
 ****************************************************************************************
 */
void user_ota_status_change(const uint8_t suotar_event)
{
    uint8_t dev_id;

    spi_flash_release_from_power_down();
    spi_flash_auto_detect(&dev_id);
    spi_flash_configure_memory_protection(SPI_FLASH_MEM_PROT_NONE);

    if (suotar_event == SUOTAR_START)
    {
        user_hid_activity();        // the transfer counts as activity: full idle timeout ahead of it
        user_hid_poll_stop();
    }
    else
    {
        // SUOTAR_END. After a complete transfer the SDK has just marked the new slot valid
        // (CRC OK) and queued CMP_OK; keep it only if it's signed with our key, and send a
        // second status with the verdict (tools/suota.py waits for it)
        if (suota_state.suota_image_len && suota_state.suota_img_idx >= suota_state.suota_image_len)
        {
            enum ota_check r = ota_check_new_image(suota_state.mem_base_add);
            if (r != OTA_CHECK_NONE)
            {
                suotar_send_status_update_req(r == OTA_CHECK_OK ? SUOTAR_CMP_OK : SUOTAR_APP_ERROR);
            }
        }
        spi_flash_power_down();
        user_hid_poll_resume();
    }
}

/**
 ****************************************************************************************
 * @brief Database initialization complete callback. The HID service has been added
 *        to the attribute database; start advertising.
 ****************************************************************************************
 */
void user_app_on_db_init_complete(void)
{
    printk("\nuser_app_on_db_init_complete!\n");

    user_app_adv_start();
}

/**
 ****************************************************************************************
 * @brief Append the Complete Local Name AD structure to the advertising data:
 *        "BT-SNES-XXXXXX", XXXXXX = the device's own MAC suffix (last 3 octets, same byte
 *        order as PRINT_BDADDR) so multiple pads show up as distinct devices. USER_DEVICE_NAME
 *        is left empty (user_config.h) so the SDK doesn't also append a static name.
 ****************************************************************************************
 */
static void user_app_adv_set_name(struct gapm_start_advertise_cmd *cmd)
{
    static const char prefix[] = "BT-SNES-";
    static const char hex[]    = "0123456789ABCDEF";
    uint8_t *data = &cmd->info.host.adv_data[cmd->info.host.adv_data_len];
    const uint8_t name_len = (sizeof(prefix) - 1) + 6;   // prefix + 3 MAC octets as hex

    data[0] = name_len + 1;                // AD structure length (type byte + name)
    data[1] = GAP_AD_TYPE_COMPLETE_NAME;
    memcpy(&data[2], prefix, sizeof(prefix) - 1);
    for (uint8_t i = 0; i < 3; i++)
    {
        uint8_t b = dev_bdaddr.addr[2 - i];
        data[2 + (sizeof(prefix) - 1) + i * 2]     = hex[b >> 4];
        data[2 + (sizeof(prefix) - 1) + i * 2 + 1] = hex[b & 0x0F];
    }
    cmd->info.host.adv_data_len += name_len + 2;
}

/**
 ****************************************************************************************
 * @brief Start undirected advertising. The advertising data (HID service UUID +
 *        gamepad appearance) is filled in by the SDK from user_config.h; the MAC-suffixed
 *        device name is appended here (see user_app_adv_set_name()).
 ****************************************************************************************
 */
void user_app_adv_start(void)
{
    struct gapm_start_advertise_cmd *cmd = app_easy_gap_undirected_advertise_get_active();
    user_app_adv_set_name(cmd);

    // Stops after the idle timeout -> user_app_adv_undirect_complete(GAP_ERR_CANCELED) -> deep sleep
    app_easy_gap_undirected_advertise_with_timeout_start(ota_mode ? OTA_IDLE_TIMEOUT : ADV_IDLE_TIMEOUT, NULL);

    printk("\nuser_app_adv_start! BT-SNES-%02X%02X%02X\n",
          dev_bdaddr.addr[2], dev_bdaddr.addr[1], dev_bdaddr.addr[0]);
}

/**
 ****************************************************************************************
 * @brief Connection established callback. On a valid connection the HID profile
 *        is enabled by the SDK (app_prf_enable() calls app_hogpd_enable()) and the
 *        security request follows (DEF_SEC_REQ_ON_CONNECT).
 ****************************************************************************************
 */
void user_app_connection(uint8_t connection_idx, struct gapc_connection_req_ind const *param)
{
    printk("user_app_connection: %d\n", connection_idx);
    printk("  peer %02X:%02X:%02X:%02X:%02X:%02X type=%d\n", PRINT_BDADDR(param->peer_addr.addr), param->peer_addr_type);

    if (app_env[connection_idx].conidx != GAP_INVALID_CONIDX)
    {
        app_connection_idx = connection_idx;
        app_easy_gap_advertise_with_timeout_stop();
        idle_timer = app_easy_timer(ota_mode ? OTA_IDLE_TIMEOUT : CONN_IDLE_TIMEOUT, idle_timer_cb);

        // Check whether the connection parameters are the preferred ones.
        // If not, schedule a connection parameter update request.
        if ((param->con_interval < user_connection_param_conf.intv_min) ||
            (param->con_interval > user_connection_param_conf.intv_max) ||
            (param->con_latency != user_connection_param_conf.latency) ||
            (param->sup_to != user_connection_param_conf.time_out))
        {
            app_param_update_request_timer_used = app_easy_timer(APP_PARAM_UPDATE_REQUEST_TO, param_update_request_timer_cb);
        }
    }

    default_app_on_connection(connection_idx, param);
}

/**
 ****************************************************************************************
 * @brief Undirected advertising completed callback.
 ****************************************************************************************
 */
void user_app_adv_undirect_complete(uint8_t status)
{
    printk("user_app_adv_undirect_complete: %02x\n", status);

    if (status == GAP_ERR_CANCELED)
    {
        // ADV_IDLE_TIMEOUT expired with no connection
        user_hid_deep_sleep();
    }
    else if (status != GAP_ERR_NO_ERROR)
    {
        // Advertising ended abnormally; restart it
        user_app_adv_start();
    }
}

/**
 ****************************************************************************************
 * @brief Disconnection callback. Restart advertising so the host can re-connect.
 ****************************************************************************************
 */
void user_app_disconnect(struct gapc_disconnect_ind const *param)
{
    printk("user_app_disconnect! reason=%02x\n", param->reason);

    // Cancel the parameter-update-request timer
    if (app_param_update_request_timer_used != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(app_param_update_request_timer_used);
        app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;
    }

    if (idle_timer != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(idle_timer);
        idle_timer = EASY_TIMER_INVALID_TIMER;
    }
    user_hid_poll_stop();
    user_batt_stop();
    app_connection_idx = -1;

    if (suota_state.reboot_requested)
    {
        // SUOTA finished and the tool asked for a reboot: boot the new image
        platform_reset(RESET_AFTER_SUOTA_UPDATE);
    }
    if (idle_sleep)
    {
        user_hid_deep_sleep();      // does not return
    }
    user_app_adv_start();
}

// Security logging wrappers around the SDK default handlers
void user_app_pairing_succeeded(const uint8_t conidx)
{
    printk("SEC: pairing_succeeded auth=%02x\n", app_sec_env[conidx].auth);
    default_app_on_pairing_succeeded(conidx);
}

void user_app_encrypt_req_ind(const uint8_t conidx, struct gapc_encrypt_req_ind const *param)
{
    printk("SEC: encrypt_req_ind ediv=%04x\n", param->ediv);
    default_app_on_encrypt_req_ind(conidx, param);
}

void user_app_encrypt_ind(const uint8_t conidx, const uint8_t auth)
{
    printk("SEC: encrypt_ind auth=%02x\n", auth);
}

/**
 ****************************************************************************************
 * @brief Catch-all handler for messages that no SDK default handler processes.
 *        The HOGPD profile has no SDK app module, therefore all its messages are
 *        routed here and dispatched to the HID application layer.
 ****************************************************************************************
 */
void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id)
{
    switch (msgid)
    {
        // ---- HOGPD (HID over GATT Device) messages -------------------------------
        case HOGPD_ENABLE_RSP:
        {
            struct hogpd_enable_rsp const *rsp = (struct hogpd_enable_rsp const *)param;
            printk("HOGPD_ENABLE_RSP: conidx=%d status=%02x\n", rsp->conidx, rsp->status);
        } break;

        // The HID host enabled/disabled report notifications (wrote a CCC descriptor)
        case HOGPD_NTF_CFG_IND:
        {
            user_hid_ntf_cfg_ind_handler((struct hogpd_ntf_cfg_ind const *)param);
        } break;

        // The HID host read or wrote a report (report map, input/output reports)
        case HOGPD_REPORT_REQ_IND:
        {
            user_hid_report_req_ind_handler((struct hogpd_report_req_ind const *)param, dest_id, src_id);
        } break;

        // The HID host changed the protocol mode (boot <-> report)
        case HOGPD_PROTO_MODE_REQ_IND:
        {
            user_hid_proto_mode_req_ind_handler((struct hogpd_proto_mode_req_ind const *)param, dest_id, src_id);
        } break;

        // The HID host wrote the HID Control Point (suspend / exit suspend)
        case HOGPD_CTNL_PT_IND:
        {
            struct hogpd_ctnl_pt_ind const *ind = (struct hogpd_ctnl_pt_ind const *)param;
            printk("HOGPD_CTNL_PT_IND: hid_idx=%d ctl_pt=%d\n", ind->hid_idx, ind->hid_ctnl_pt);

            user_hid_set_suspended(ind->hid_ctnl_pt == HOGP_CTNL_PT_SUSPEND);
        } break;

        // Confirmation of a report notification request we initiated
        case HOGPD_REPORT_UPD_RSP:
        {
            struct hogpd_report_upd_rsp const *rsp = (struct hogpd_report_upd_rsp const *)param;
            if (rsp->status != GAP_ERR_NO_ERROR)
            {
                printk("HOGPD_REPORT_UPD_RSP error: %02x\n", rsp->status);
                user_hid_report_failed();
            }
        } break;

        // The HID host enabled/disabled battery-level notifications (wrote a CCC descriptor)
        case BASS_BATT_LEVEL_NTF_CFG_IND:
        {
            user_batt_ntf_cfg_ind_handler((struct bass_batt_level_ntf_cfg_ind const *)param);
        } break;

        // ---- GAP / misc ----------------------------------------------------------
        case GAPC_PARAM_UPDATED_IND:
        {
            struct gapc_param_updated_ind const *msg_param = (struct gapc_param_updated_ind const *)(param);
            printk("GAPC_PARAM_UPDATED_IND!\n");
            printk("  interval: %d\n", msg_param->con_interval);
            printk("  latency : %d\n", msg_param->con_latency);
            printk("  sup_to  : %d\n", msg_param->sup_to);
        } break;

        case GATTC_MTU_CHANGED_IND:
        {
            struct gattc_mtu_changed_ind *ind = (struct gattc_mtu_changed_ind *) param;
            printk("GATTC_MTU_CHANGED_IND: %d\n", ind->mtu);
        } break;

        // Operation-complete events from the stack (advertising start, profile add): nothing to do
        case GAPM_CMP_EVT:
        case GAPC_CMP_EVT:
        break;

        // Unhandled message
        default:
        {
            printk("Unhandled msgid=%08x\n", msgid);
        } break;
    }
}

/// @} APP
