/**
 ****************************************************************************************
 *
 * @file user_periph_setup.h
 *
 * @brief Definitions of peripherals configuration - BLE HID gamepad device.
 *
 * Copyright (C) 2015-2023 Renesas Electronics Corporation and/or its affiliates.
 * All rights reserved. Confidential Information.
 *
 ****************************************************************************************
 */

#ifndef _USER_PERIPH_SETUP_H_
#define _USER_PERIPH_SETUP_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "gpio.h"
#include "uart.h"
#include "spi.h"
#include "spi_flash.h"

/*
 * DEFINES
 ****************************************************************************************
 */

/****************************************************************************************/
/* GPIO map, DA14585 QFN40 (23 GPIOs; source of truth is the defines below)              */
/* Same PCB as HMCLOCK; P1_4/P1_5 = SWCLK/SWDIO (keep for SWD), P3_1..P3_7 don't exist.  */
/*   P0_0  USED  SPI flash CLK                                                          */
/*   P0_1  NC    not found (traced)                                                     */
/*   P0_2  NC    not found (traced)                                                     */
/*   P0_3  USED  SPI flash EN (CS)                                                      */
/*   P0_4  USED  UART2 TX, PCB TP7 (debug, 115200 8N1)                                  */
/*   P0_5  USED  UART2 RX, PCB TP8 (not driven by firmware); also SPI flash DI          */
/*   P0_6  USED  SPI flash DO                                                           */
/*   P0_7  USED  SNES DATA, PCB pad E10 (traced)                                        */
/*   P1_0  NC    not found (traced); HMCLOCK: e-paper RST                               */
/*   P1_1  NC    not found (traced); HMCLOCK: e-paper BUSY                              */
/*   P1_2  NC    not found (traced)                                                     */
/*   P1_3  NC    not found (traced)                                                     */
/*   P2_0  USED  SNES LATCH, PCB pad E09 (traced)                                       */
/*   P2_1  USED  SNES CLOCK, PCB pad E12 (traced)                                       */
/*   P2_2  NC    not on pad E01 (traced); HMCLOCK: e-paper HLT_CTL                      */
/*   P2_3  free  to transistor Q3 (traced); HMCLOCK: PWR_EN                             */
/*   P2_4  NC    not found (traced); HMCLOCK: U4                                        */
/*   P2_5  free  only to green LED D1 via R4, both unpopulated (solderable spare)       */
/*   P2_6  NC    not found (traced)                                                     */
/*   P2_7  NC    not found (traced)                                                     */
/*   P2_8  NC    not found (traced)                                                     */
/*   P2_9  NC    not connected here (traced, no blue LED)                               */
/*   P3_0  NC    not found (traced)                                                     */
/****************************************************************************************/

/****************************************************************************************/
/* UART2 configuration (debug console)                                                  */
/****************************************************************************************/
#if defined (__DA14531__)
    #define UART2_TX_PORT           GPIO_PORT_0
    #define UART2_TX_PIN            GPIO_PIN_6
#else
    #define UART2_TX_PORT           GPIO_PORT_0
    #define UART2_TX_PIN            GPIO_PIN_4
#endif

#define UART2_BAUDRATE              UART_BAUDRATE_115200
#define UART2_DATABITS              UART_DATABITS_8
#define UART2_PARITY                UART_PARITY_NONE
#define UART2_STOPBITS              UART_STOPBITS_1
#define UART2_AFCE                  UART_AFCE_DIS
#define UART2_FIFO                  UART_FIFO_EN
#define UART2_TX_FIFO_LEVEL         UART_TX_FIFO_LEVEL_0
#define UART2_RX_FIFO_LEVEL         UART_RX_FIFO_LEVEL_0

/****************************************************************************************/
/* SNES controller (LATCH/CLOCK outputs, DATA input with pull-up)                       */
/****************************************************************************************/
#define SNES_LATCH_PORT             GPIO_PORT_2     // PCB pad E09 (traced)
#define SNES_LATCH_PIN              GPIO_PIN_0
#define SNES_CLOCK_PORT             GPIO_PORT_2     // PCB pad E12 (traced)
#define SNES_CLOCK_PIN              GPIO_PIN_1
#define SNES_DATA_PORT              GPIO_PORT_0     // PCB pad E10 (traced)
#define SNES_DATA_PIN               GPIO_PIN_7

/****************************************************************************************/
/* SPI Flash configuration (bond database + saved notification config)                  */
/****************************************************************************************/
#if defined (__DA14531__)
    #define SPI_EN_PORT             GPIO_PORT_0
    #define SPI_EN_PIN              GPIO_PIN_1
    #define SPI_CLK_PORT            GPIO_PORT_0
    #define SPI_CLK_PIN             GPIO_PIN_4
    #define SPI_DO_PORT             GPIO_PORT_0
    #define SPI_DO_PIN              GPIO_PIN_0
    #define SPI_DI_PORT             GPIO_PORT_0
    #define SPI_DI_PIN              GPIO_PIN_3
    #define SPI_SPEED_MODE          SPI_SPEED_MODE_4MHz
    #define SPI_EDGE_CAPTURE        SPI_MASTER_EDGE_CAPTURE
#else
  #if !defined (__DA14586__)
    #define SPI_EN_PORT             GPIO_PORT_0
    #define SPI_EN_PIN              GPIO_PIN_3
    #define SPI_CLK_PORT            GPIO_PORT_0
    #define SPI_CLK_PIN             GPIO_PIN_0
    #define SPI_DO_PORT             GPIO_PORT_0
    #define SPI_DO_PIN              GPIO_PIN_6
    #define SPI_DI_PORT             GPIO_PORT_0
    #define SPI_DI_PIN              GPIO_PIN_5
  #endif
    #define SPI_SPEED_MODE          SPI_SPEED_MODE_4MHz
#endif
#define SPI_MS_MODE                 SPI_MS_MODE_MASTER
#define SPI_CP_MODE                 SPI_CP_MODE_0
#define SPI_WSZ                     SPI_MODE_8BIT
#define SPI_CS                      SPI_CS_0

#if !defined (__DA14586__)
#define SPI_FLASH_DEV_SIZE          (256 * 1024)
#endif

/// Saved notification (CCC) sector - shared between user_hid_hogpd.c (reads/writes it)
/// and selflash.c (keeps its firmware-image writes out of this sector)
#define CCC_FLASH_ADDR              (0x1D000)

/***************************************************************************************/
/* Production debug output configuration                                               */
/***************************************************************************************/
#if PRODUCTION_DEBUG_OUTPUT
#if defined (__DA14531__)
    #define PRODUCTION_DEBUG_PORT   GPIO_PORT_0
    #define PRODUCTION_DEBUG_PIN    GPIO_PIN_11
#else
    #define PRODUCTION_DEBUG_PORT   GPIO_PORT_2
    #define PRODUCTION_DEBUG_PIN    GPIO_PIN_5
#endif
#endif


/*
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */

#if DEVELOPMENT_DEBUG
/**
 ****************************************************************************************
 * @brief   Reserves application's specific GPIOs
 * @details Used only in Development mode (#if DEVELOPMENT_DEBUG)
 *          i.e. to reserve P0_1 as Generic Purpose I/O:
 *          RESERVE_GPIO(DESCRIPTIVE_NAME, GPIO_PORT_0, GPIO_PIN_1, PID_GPIO);
 ****************************************************************************************
 */
void GPIO_reservations(void);
#endif

/**
 ****************************************************************************************
 * @brief   Sets the functionality of application pads
 * @details i.e. to set P0_1 as Generic purpose Output:
 *          GPIO_ConfigurePin(GPIO_PORT_0, GPIO_PIN_1, OUTPUT, PID_GPIO, false);
 ****************************************************************************************
 */
void set_pad_functions(void);

/**
 ****************************************************************************************
 * @brief   Initializes application's peripherals and pads
 ****************************************************************************************
 */
void periph_init(void);


#endif // _USER_PERIPH_SETUP_H_
