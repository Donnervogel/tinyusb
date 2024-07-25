/*
 * The MIT License (MIT)
 *
 * Copyright(c) 2016 STMicroelectronics
 * Copyright(c) N Conrad
 * Copyright (c) 2024, hathach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef TUSB_FSDEV_COMMON_H
#define TUSB_FSDEV_COMMON_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stdint.h"

// If sharing with CAN, one can set this to be non-zero to give CAN space where it wants it
// Both of these MUST be a multiple of 2, and are in byte units.
#ifndef FSDEV_BTABLE_BASE
#define FSDEV_BTABLE_BASE 0U
#endif

TU_VERIFY_STATIC(FSDEV_BTABLE_BASE % 8 == 0, "BTABLE base must be aligned to 8 bytes");

// FSDEV_PMA_SIZE is PMA buffer size in bytes.
// - 512-byte devices, access with a stride of two words (use every other 16-bit address)
// - 1024-byte devices, access with a stride of one word (use every 16-bit address)
// - 2048-byte devices, access with 32-bit address

// For purposes of accessing the packet
#if FSDEV_PMA_SIZE == 512
  #define FSDEV_PMA_STRIDE  (2u) // 1x16 bit access scheme
  #define pma_aligned TU_ATTR_ALIGNED(4)
#elif FSDEV_PMA_SIZE == 1024
  #define FSDEV_PMA_STRIDE  (1u) // 2x16 bit access scheme
  #define pma_aligned
#elif FSDEV_PMA_SIZE == 2048
  #ifndef FSDEV_BUS_32BIT
    #warning "FSDEV_PMA_SIZE is 2048, but FSDEV_BUS_32BIT is not defined"
  #endif
  #define FSDEV_PMA_STRIDE  (1u) // 32 bit access scheme
  #define pma_aligned
#endif

// hardware limit endpoint
#define FSDEV_EP_COUNT 8

// Buffer Table is located in Packet Memory Area (PMA) and therefore its address access is forced to either
// 16-bit or 32-bit depending on FSDEV_BUS_32BIT.
typedef struct {
  union {
    // 0: tx, 1: rx

    // strictly 16-bit access (could be 32-bit aligned)
    struct {
      volatile pma_aligned uint16_t addr;
      volatile pma_aligned uint16_t count;
    } ep16[FSDEV_EP_COUNT][2];

    // strictly 32-bit access
    struct {
      volatile uint32_t count_addr;
    } ep32[FSDEV_EP_COUNT][2];
  };
} fsdev_btable_t;

TU_VERIFY_STATIC(sizeof(fsdev_btable_t) == FSDEV_EP_COUNT*8*FSDEV_PMA_STRIDE, "size is not correct");
TU_VERIFY_STATIC(FSDEV_BTABLE_BASE + FSDEV_EP_COUNT*8 <= FSDEV_PMA_SIZE, "BTABLE does not fit in PMA RAM");


#define FSDEV_BTABLE ((volatile fsdev_btable_t*) (USB_PMAADDR+FSDEV_BTABLE_BASE))

//--------------------------------------------------------------------+
// Helper
//--------------------------------------------------------------------+

// The fsdev_bus_t type can be used for both register and PMA access necessities
// For type-safety create a new macro for the volatile address of PMAADDR
// The compiler should warn us if we cast it to a non-volatile type?
#ifdef FSDEV_BUS_32BIT
typedef uint32_t fsdev_bus_t;
static volatile uint32_t * const pma32 = (volatile uint32_t*)USB_PMAADDR;

#else
typedef uint16_t fsdev_bus_t;
// Volatile is also needed to prevent the optimizer from changing access to 32-bit (as 32-bit access is forbidden)
static volatile uint16_t * const pma = (volatile uint16_t*)USB_PMAADDR;

TU_ATTR_ALWAYS_INLINE static inline volatile uint16_t * pcd_btable_word_ptr(USB_TypeDef * USBx, size_t x) {
  size_t total_word_offset = (((USBx)->BTABLE)>>1) + x;
  total_word_offset *= FSDEV_PMA_STRIDE;
  return &(pma[total_word_offset]);
}

TU_ATTR_ALWAYS_INLINE static inline volatile uint16_t* pcd_ep_tx_cnt_ptr(USB_TypeDef * USBx, uint32_t bEpIdx) {
  return pcd_btable_word_ptr(USBx,(bEpIdx)*4u + 1u);
}

TU_ATTR_ALWAYS_INLINE static inline volatile uint16_t* pcd_ep_rx_cnt_ptr(USB_TypeDef * USBx, uint32_t bEpIdx) {
  return pcd_btable_word_ptr(USBx,(bEpIdx)*4u + 3u);
}
#endif

/* Aligned buffer size according to hardware */
TU_ATTR_ALWAYS_INLINE static inline uint16_t pcd_aligned_buffer_size(uint16_t size) {
  /* The STM32 full speed USB peripheral supports only a limited set of
   * buffer sizes given by the RX buffer entry format in the USB_BTABLE. */
  uint16_t blocksize = (size > 62) ? 32 : 2;

  // Round up while dividing requested size by blocksize
  uint16_t numblocks = (size + blocksize - 1) / blocksize ;

  return numblocks * blocksize;
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_endpoint(USB_TypeDef * USBx, uint32_t bEpIdx, uint32_t wRegValue) {
#ifdef FSDEV_BUS_32BIT
  (void) USBx;
  volatile uint32_t *reg = (volatile uint32_t *)(USB_DRD_BASE + bEpIdx*4);
  *reg = wRegValue;
#else
  volatile uint16_t *reg = (volatile uint16_t *)((&USBx->EP0R) + bEpIdx*2u);
  *reg = (uint16_t)wRegValue;
#endif
}

TU_ATTR_ALWAYS_INLINE static inline uint32_t pcd_get_endpoint(USB_TypeDef * USBx, uint32_t bEpIdx) {
#ifdef FSDEV_BUS_32BIT
  (void) USBx;
  volatile const uint32_t *reg = (volatile const uint32_t *)(USB_DRD_BASE + bEpIdx*4);
#else
  volatile const uint16_t *reg = (volatile const uint16_t *)((&USBx->EP0R) + bEpIdx*2u);
#endif
  return *reg;
}

/**
  * @brief  Sets address in an endpoint register.
  * @param  USBx USB peripheral instance register address.
  * @param  bEpIdx Endpoint Number.
  * @param  bAddr Address.
  * @retval None
  */
TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_address(USB_TypeDef * USBx,  uint32_t bEpIdx, uint32_t bAddr) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  regVal &= USB_EPREG_MASK;
  regVal |= bAddr;
  regVal |= USB_EP_CTR_RX|USB_EP_CTR_TX;
  pcd_set_endpoint(USBx, bEpIdx,regVal);
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_eptype(USB_TypeDef * USBx, uint32_t bEpIdx, uint32_t wType) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  regVal &= (uint32_t)USB_EP_T_MASK;
  regVal |= wType;
  regVal |= USB_EP_CTR_RX | USB_EP_CTR_TX; // These clear on write0, so must set high
  pcd_set_endpoint(USBx, bEpIdx, regVal);
}

TU_ATTR_ALWAYS_INLINE static inline uint32_t pcd_get_eptype(USB_TypeDef * USBx, uint32_t bEpIdx) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  regVal &= USB_EP_T_FIELD;
  return regVal;
}

/**
  * @brief  Clears bit CTR_RX / CTR_TX in the endpoint register.
  * @param  USBx USB peripheral instance register address.
  * @param  bEpIdx Endpoint Number.
  * @retval None
  */
TU_ATTR_ALWAYS_INLINE static inline void pcd_clear_rx_ep_ctr(USB_TypeDef * USBx, uint32_t bEpIdx) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  regVal &= USB_EPREG_MASK;
  regVal &= ~USB_EP_CTR_RX;
  regVal |= USB_EP_CTR_TX; // preserve CTR_TX (clears on writing 0)
  pcd_set_endpoint(USBx, bEpIdx, regVal);
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_clear_tx_ep_ctr(USB_TypeDef * USBx, uint32_t bEpIdx) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  regVal &= USB_EPREG_MASK;
  regVal &= ~USB_EP_CTR_TX;
  regVal |= USB_EP_CTR_RX; // preserve CTR_RX (clears on writing 0)
  pcd_set_endpoint(USBx, bEpIdx,regVal);
}

TU_ATTR_ALWAYS_INLINE static inline uint32_t btable_get_count(uint32_t ep_id, uint8_t is_rx) {
  uint16_t count;
#ifdef FSDEV_BUS_32BIT
  count = (FSDEV_BTABLE->ep32[ep_id][is_rx].count_addr >> 16);
#else
  count = FSDEV_BTABLE->ep16[ep_id][is_rx].count;
#endif
  return count & 0x3FFU;
}

TU_ATTR_ALWAYS_INLINE static inline uint32_t btable_get_addr(uint32_t ep_id, uint8_t is_rx) {
#ifdef FSDEV_BUS_32BIT
  return FSDEV_BTABLE->ep32[ep_id][is_rx].count_addr & 0x0000FFFFu;
#else
  return FSDEV_BTABLE->ep16[ep_id][is_rx].addr;
#endif
}

/**
  * @brief  gets counter of the tx buffer.
  * @param  USBx USB peripheral instance register address.
  * @param  bEpIdx Endpoint Number.
  * @retval Counter value
  */
TU_ATTR_ALWAYS_INLINE static inline uint32_t pcd_get_ep_tx_cnt(USB_TypeDef * USBx, uint32_t bEpIdx) {
  (void) USBx;
  return btable_get_count(bEpIdx, 0);
}

TU_ATTR_ALWAYS_INLINE static inline uint32_t pcd_get_ep_rx_cnt(USB_TypeDef * USBx, uint32_t bEpIdx) {
  (void) USBx;
  return btable_get_count(bEpIdx, 1);
}

#define pcd_get_ep_dbuf0_cnt pcd_get_ep_tx_cnt
#define pcd_get_ep_dbuf1_cnt pcd_get_ep_rx_cnt

TU_ATTR_ALWAYS_INLINE static inline uint32_t pcd_get_ep_tx_address(USB_TypeDef * USBx, uint32_t bEpIdx) {
  (void) USBx;
  return btable_get_addr(bEpIdx, 0);
}

TU_ATTR_ALWAYS_INLINE static inline uint32_t pcd_get_ep_rx_address(USB_TypeDef * USBx, uint32_t bEpIdx) {
  (void) USBx;
  return btable_get_addr(bEpIdx, 1);
}

#define pcd_get_ep_dbuf0_address pcd_get_ep_tx_address
#define pcd_get_ep_dbuf1_address pcd_get_ep_rx_address


TU_ATTR_ALWAYS_INLINE static inline void btable_set_addr(uint32_t ep_id, uint8_t is_rx, uint16_t addr) {
#ifdef FSDEV_BUS_32BIT
  uint32_t count_addr = FSDEV_BTABLE->ep32[ep_id][is_rx].count_addr;
  count_addr = (count_addr & 0xFFFF0000u) | (addr & 0x0000FFFCu);
  FSDEV_BTABLE->ep32[ep_id][is_rx].count_addr = count_addr;
#else
  FSDEV_BTABLE->ep16[ep_id][is_rx].addr = addr;
#endif
}

TU_ATTR_ALWAYS_INLINE static inline void btable_set_count(uint32_t ep_id, uint8_t is_rx, uint16_t byte_count) {
#ifdef FSDEV_BUS_32BIT
  uint32_t count_addr = FSDEV_BTABLE->ep32[ep_id][is_rx].count_addr;
  count_addr = (count_addr & ~0x03FF0000u) | ((byte_count & 0x3FFu) << 16);
  FSDEV_BTABLE->ep32[ep_id][is_rx].count_addr = count_addr;
#else
  uint16_t cnt = FSDEV_BTABLE->ep16[ep_id][is_rx].count;
  cnt = (cnt & ~0x3FFU) | (byte_count & 0x3FFU);
  FSDEV_BTABLE->ep16[ep_id][is_rx].count = cnt;
#endif
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_tx_address(USB_TypeDef * USBx, uint32_t bEpIdx, uint32_t addr) {
  (void) USBx;
  btable_set_addr(bEpIdx, 0, addr);
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_rx_address(USB_TypeDef * USBx, uint32_t bEpIdx, uint32_t addr) {
  (void) USBx;
  btable_set_addr(bEpIdx, 1, addr);
}

#define pcd_set_ep_dbuf0_address pcd_set_ep_tx_address
#define pcd_set_ep_dbuf1_address pcd_set_ep_rx_address

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_tx_cnt(USB_TypeDef * USBx, uint32_t bEpIdx, uint32_t wCount) {
  (void) USBx;
  btable_set_count(bEpIdx, 0, wCount);
}

#define pcd_set_ep_tx_dbuf0_cnt pcd_set_ep_tx_cnt

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_tx_dbuf1_cnt(USB_TypeDef * USBx, uint32_t bEpIdx, uint32_t wCount) {
#ifdef FSDEV_BUS_32BIT
  (void) USBx;
  pma32[2*bEpIdx + 1] = (pma32[2*bEpIdx + 1] & ~0x03FF0000u) | ((wCount & 0x3FFu) << 16);
#else
  volatile uint16_t * reg = pcd_ep_rx_cnt_ptr(USBx, bEpIdx);
  *reg = (uint16_t) (*reg & (uint16_t) ~0x3FFU) | (wCount & 0x3FFU);
#endif
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_blsize_num_blocks(USB_TypeDef * USBx, uint32_t rxtx_idx,
                                                                      uint32_t blocksize, uint32_t numblocks) {
  /* Encode into register. When BLSIZE==1, we need to subtract 1 block count */
#ifdef FSDEV_BUS_32BIT
  (void) USBx;
  pma32[rxtx_idx] = (pma32[rxtx_idx] & 0x0000FFFFu) | (blocksize << 31) | ((numblocks - blocksize) << 26);
#else
  volatile uint16_t *pdwReg = pcd_btable_word_ptr(USBx, rxtx_idx*2u + 1u);
  *pdwReg = (blocksize << 15) | ((numblocks - blocksize) << 10);
#endif
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_bufsize(USB_TypeDef * USBx, uint32_t rxtx_idx, uint32_t wCount) {
  wCount = pcd_aligned_buffer_size(wCount);

  /* We assume that the buffer size is already aligned to hardware requirements. */
  uint16_t blocksize = (wCount > 62) ? 1 : 0;
  uint16_t numblocks = wCount / (blocksize ? 32 : 2);

  /* There should be no remainder in the above calculation */
  TU_ASSERT((wCount - (numblocks * (blocksize ? 32 : 2))) == 0, /**/);

  /* Encode into register. When BLSIZE==1, we need to subtract 1 block count */
  pcd_set_ep_blsize_num_blocks(USBx, rxtx_idx, blocksize, numblocks);
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_rx_dbuf0_cnt(USB_TypeDef * USBx, uint32_t bEpIdx, uint32_t wCount) {
  pcd_set_ep_bufsize(USBx, 2*bEpIdx, wCount);
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_rx_cnt(USB_TypeDef * USBx, uint32_t bEpIdx, uint32_t wCount) {
  pcd_set_ep_bufsize(USBx, 2*bEpIdx + 1, wCount);
}

#define pcd_set_ep_rx_dbuf1_cnt pcd_set_ep_rx_cnt

/**
  * @brief  sets the status for tx transfer (bits STAT_TX[1:0]).
  * @param  USBx USB peripheral instance register address.
  * @param  bEpIdx Endpoint Number.
  * @param  wState new state
  * @retval None
  */
TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_tx_status(USB_TypeDef * USBx,  uint32_t bEpIdx, uint32_t wState) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  regVal &= USB_EPTX_DTOGMASK;
  regVal ^= wState;
  regVal |= USB_EP_CTR_RX|USB_EP_CTR_TX;
  pcd_set_endpoint(USBx, bEpIdx, regVal);
}

/**
  * @brief  sets the status for rx transfer (bits STAT_TX[1:0])
  * @param  USBx USB peripheral instance register address.
  * @param  bEpIdx Endpoint Number.
  * @param  wState new state
  * @retval None
  */

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_rx_status(USB_TypeDef * USBx,  uint32_t bEpIdx, uint32_t wState) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  regVal &= USB_EPRX_DTOGMASK;
  regVal ^= wState;
  regVal |= USB_EP_CTR_RX|USB_EP_CTR_TX;
  pcd_set_endpoint(USBx, bEpIdx, regVal);
}

TU_ATTR_ALWAYS_INLINE static inline uint32_t pcd_get_ep_rx_status(USB_TypeDef * USBx,  uint32_t bEpIdx) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  return (regVal & USB_EPRX_STAT) >> (12u);
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_rx_dtog(USB_TypeDef * USBx,  uint32_t bEpIdx) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  regVal &= USB_EPREG_MASK;
  regVal |= USB_EP_CTR_RX|USB_EP_CTR_TX|USB_EP_DTOG_RX;
  pcd_set_endpoint(USBx, bEpIdx, regVal);
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_tx_dtog(USB_TypeDef * USBx,  uint32_t bEpIdx) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  regVal &= USB_EPREG_MASK;
  regVal |= USB_EP_CTR_RX|USB_EP_CTR_TX|USB_EP_DTOG_TX;
  pcd_set_endpoint(USBx, bEpIdx, regVal);
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_clear_rx_dtog(USB_TypeDef * USBx,  uint32_t bEpIdx) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  if((regVal & USB_EP_DTOG_RX) != 0) {
    pcd_rx_dtog(USBx,bEpIdx);
  }
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_clear_tx_dtog(USB_TypeDef * USBx,  uint32_t bEpIdx) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  if((regVal & USB_EP_DTOG_TX) != 0) {
    pcd_tx_dtog(USBx,bEpIdx);
  }
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_set_ep_kind(USB_TypeDef * USBx,  uint32_t bEpIdx) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  regVal |= USB_EP_KIND;
  regVal &= USB_EPREG_MASK;
  regVal |= USB_EP_CTR_RX|USB_EP_CTR_TX;
  pcd_set_endpoint(USBx, bEpIdx, regVal);
}

TU_ATTR_ALWAYS_INLINE static inline void pcd_clear_ep_kind(USB_TypeDef * USBx, uint32_t bEpIdx) {
  uint32_t regVal = pcd_get_endpoint(USBx, bEpIdx);
  regVal &= USB_EPKIND_MASK;
  regVal |= USB_EP_CTR_RX|USB_EP_CTR_TX;
  pcd_set_endpoint(USBx, bEpIdx, regVal);
}

#ifdef __cplusplus
 }
#endif

#endif
