/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) Foundries Ltd. 2021
 * Author: Jorge Ramirez <jorge@foundries.io>
 */

#ifndef __DRIVERS_ZYNQMP_CSU_H_
#define __DRIVERS_ZYNQMP_CSU_H_

/* CSU registers */
#define ZYNQMP_CSU_STATUS_OFFSET		0x00
#define ZYNQMP_CSU_CTRL_OFFSET			0x04
#define ZYNQMP_CSU_SSS_CFG_OFFSET		0x08
#define ZYNQMP_CSU_DMA_RESET_OFFSET		0x0c
#define ZYNQMP_CSU_MULTI_BOOT_OFFSET		0x10
#define ZYNQMP_CSU_TAMPER_TRIG_OFFSET		0x14
#define ZYNQMP_CSU_FT_STATUS_OFFSET		0x18
#define ZYNQMP_CSU_ISR_OFFSET			0x20

#define ZYNQMP_CSU_AES_OFFSET		    0x1000
#define AES_STATUS_OFFSET		        0x00
#define AES_KEY_SRC_OFFSET		        0x04
#define AES_KEY_LOAD_OFFSET		        0x08
#define AES_START_MSG_OFFSET		    0x0c
#define AES_RESET_OFFSET		        0x10
#define AES_KEY_CLEAR_OFFSET		    0x14
#define AES_CFG_OFFSET		            0x18
#define AES_KUP_WR_OFFSET			    0x1C
#define AES_KUP0_OFFSET			        0x20
#define AES_KUP1_OFFSET			        0x24
#define AES_KUP2_OFFSET			        0x28
#define AES_KUP3_OFFSET			        0x2C
#define AES_KUP4_OFFSET			        0x30
#define AES_KUP5_OFFSET			        0x34
#define AES_KUP6_OFFSET			        0x38
#define AES_KUP7_OFFSET			        0x3C
#define AES_IV0_OFFSET			        0x40
#define AES_IV1_OFFSET			        0x44
#define AES_IV2_OFFSET			        0x48
#define AES_IV3_OFFSET			        0x4C

#define AES_KUP_WR                      BIT(0)
#define AES_IV_WR                       BIT(1)

#define AES_BLK_TYPE_SECURE_HEADER  0
#define AES_BLK_TYPE_DATA_BLOCK		1
#define SECURE_HDR_SIZE         48

#define ZYNQMP_CSU_STATUS_AUTH			BIT(0)
#define ZYNQMP_CSU_SSS_DMA_STREAM_LOOPBACK			0x50
#define ZYNQMP_CSU_SSS_DMA_STREAM_TO_PCAP			0x05
#define ZYNQMP_CSU_SSS_DMA_STREAM_TO_AES_TO_DMA		0x5A0
#define ZYNQMP_CSU_SSS_DMA_STREAM_TO_AES_TO_PCAP	0x50A
#define ZYNQMP_CSU_SSS_DMA_STREAM_TO_DMA_TO_SHA		0x5050
#define ZYNQMP_CSU_SSS_DMA_STREAM_FROM_PCAP         0x30
#define ZYNQMP_CSU_DMA_RESET_SET		1
#define ZYNQMP_CSU_DMA_RESET_CLR		0
#define ZYNQMP_CSU_ISR_PUF_ACC_ERROR_MASK	BIT(12)

/* AES-GCM */
#define ZYNQMP_CSU_AES_BASE			(CSU_BASE + 0x1000)
#define ZYNQMP_CSU_AES_SIZE			0x1000

/* SHA */
#define ZYNQMP_CSU_SHA_BASE			(CSU_BASE + 0x2000)
#define ZYNQMP_CSU_SHA_SIZE			0x1000

/* PCAP */
#define ZYNQMP_CSU_PCAP_BASE			(CSU_BASE + 0x3000)
#define ZYNQMP_CSU_PCAP_SIZE			0x1000

/* PUF */
#define ZYNQMP_CSU_PUF_BASE			(CSU_BASE + 0x4000)
#define ZYNQMP_CSU_PUF_SIZE			0x1000

/* TAMPER */
#define ZYNQMP_CSU_TAMPER_BASE			(CSU_BASE + 0x5000)
#define ZYNQMP_CSU_TAMPER_SIZE			0x38

#endif
