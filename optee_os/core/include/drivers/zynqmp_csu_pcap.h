/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2022, JA1221
 */

#ifndef __DRIVERS_ZYNQMP_CSU_AES_H_
#define __DRIVERS_ZYNQMP_CSU_AES_H_

#include <drivers/zynqmp_csu_secure.h>
//#include <drivers/zynqmp_csudma.h>
#include <tee_api_types.h>
#include <types_ext.h>

#define __aligned_csupcap	__aligned_csudma


TEE_Result zynqmp_csu_pcap_init(void);

TEE_Result validateSecureBitstream(uint8_t *buf, size_t *len);

TEE_Result validateBitstream(uint8_t *buf, size_t *len);

TEE_Result zynqmp_csu_secure_pcap_write(void *addr, size_t len);

TEE_Result zynqmp_csu_pcap_write(void *addr, size_t len);

// TEE_Result zynqmp_csu_pcap_read(void *addr, size_t len);

// TEE_Result zynqmp_csu_pcap_dt_enable_secure_status(void);

#endif

