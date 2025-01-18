#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// Common Configuration
#define CFG_TUSB_MCU             OPT_MCU_RP2040
#define CFG_TUSB_RHPORT0_MODE    (OPT_MODE_DEVICE)
#define CFG_TUSB_OS              OPT_OS_NONE
#define CFG_TUSB_DEBUG           0

// Device Configuration
#define CFG_TUD_ENDOINT0_SIZE    64

// Class Configuration
#define CFG_TUD_CDC              1
#define CFG_TUD_MSC              1
#define CFG_TUD_HID              1
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR           0

// CDC FIFO Configuration
#define CFG_TUD_CDC_RX_BUFSIZE   64
#define CFG_TUD_CDC_TX_BUFSIZE   64

// MSC FIFO Configuration
#define CFG_TUD_MSC_BUFSIZE      512

#endif // TUSB_CONFIG_H