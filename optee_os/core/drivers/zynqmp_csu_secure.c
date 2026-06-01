// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (C) Foundries Ltd. 2021
 * Author: Jorge Ramirez <jorge@foundries.io>
 */
#include <config.h>
#include <drivers/zynqmp_csu.h>
#include <drivers/zynqmp_csu_secure.h>
#include <io.h>
#include <kernel/boot.h>
#include <kernel/delay.h>
#include <kernel/dt.h>
#include <libfdt.h>
#include <malloc.h>
#include <mm/core_memprot.h>
#include <string.h>
#include <tee/cache.h>


/* CSU AES registers */
#define AES_STS_OFFSET			0x00
#define AES_KEY_SRC_OFFSET		0x04
#define AES_KEY_LOAD_OFFSET		0x08
#define AES_START_MSG_OFFSET	0x0C
#define AES_RESET_OFFSET		0x10
#define AES_KEY_CLR_OFFSET		0x14
#define AES_CFG_OFFSET			0x18

#define AES_STS_AES_BUSY	    BIT(0)
#define AES_STS_GCM_TAG_OK		BIT(3)
#define AES_START_MSG			1

#define SECURE_PCAP_STS_OFFSET	0x10
#define SECURE_CSU_PCAP_STATUS_PCAP_WR_IDLE_MASK 1

#define AES_TIMEOUT_USEC		2000000

static TEE_Result aes_wait(uint32_t event, bool set)
{
	vaddr_t aes = core_mmu_get_va(ZYNQMP_CSU_AES_BASE, MEM_AREA_IO_SEC,
				      ZYNQMP_CSU_AES_SIZE);
	uint64_t tref = timeout_init_us(AES_TIMEOUT_USEC);
	uint32_t status = 0;

	if (!aes)
		return TEE_ERROR_GENERIC;

	while (!timeout_elapsed(tref)) {
		status = io_read32(aes + AES_STS_OFFSET) & event;
		if ((set && status == event) || (!set && status != event))
			return TEE_SUCCESS;
	}

	return TEE_ERROR_GENERIC;
}

static TEE_Result aes_transfer_dec(const void *src, size_t len,
				   const void *tag, const void *iv, uint32_t block_type)
{
    vaddr_t aes = core_mmu_get_va(ZYNQMP_CSU_AES_BASE, MEM_AREA_IO_SEC,
				      ZYNQMP_CSU_AES_SIZE);
    vaddr_t pcap = core_mmu_get_va(ZYNQMP_CSU_PCAP_BASE, MEM_AREA_IO_SEC,
									  ZYNQMP_CSU_PCAP_SIZE);

    uint32_t gcm_status;
	TEE_Result ret = TEE_SUCCESS;

    if (!aes)
		return TEE_ERROR_GENERIC;
    
    io_write32(aes + AES_START_MSG_OFFSET, AES_START_MSG);
    
    // for (uint8_t i=0; i<4; ++i) {
    //     DMSG("iv_padded%d is:0x%x\n", i, io_read32(iv+(i*4U)));
    // }
    
	/* Push IV into the AES engine. */
	ret = zynqmp_csudma_transfer(ZYNQMP_CSUDMA_SRC_CHANNEL,
				     (void *)iv, ZYNQMP_CSUDMA_MIN_SIZE,
				     0);
	if (ret) {
		EMSG("DMA transfer failed, invalid IV buffer");
		goto out;
	}

	ret = zynqmp_csudma_sync(ZYNQMP_CSUDMA_SRC_CHANNEL);
	if (ret) {
		EMSG("DMA IV transfer timeout");
		goto out;
	}

    /* Enable CSU DMA Src channel for byte swapping.*/
	ret = zynqmp_csudma_prepare(ZYNQMP_CSUDMA_SRC_CHANNEL);
	if (ret) {
		EMSG("DMA can't initialize");
		return ret;
	}

    if (block_type != AES_BLK_TYPE_SECURE_HEADER)
    {
        ret = zynqmp_csudma_transfer(ZYNQMP_CSUDMA_SRC_CHANNEL, (void *)src,
				     len, 0);
	    if (ret) {
		    EMSG("DMA transfer failed, invalid source buffer");
		    goto out;
	    }

        ret = zynqmp_csudma_sync(ZYNQMP_CSUDMA_SRC_CHANNEL);
	    if (ret) {
		    EMSG("DMA source transfer timeout");
		    goto out;
	    }
        
        while ((io_read32(pcap+SECURE_PCAP_STS_OFFSET) &
			SECURE_CSU_PCAP_STATUS_PCAP_WR_IDLE_MASK) !=
			SECURE_CSU_PCAP_STATUS_PCAP_WR_IDLE_MASK);
    }

    io_write32(aes+AES_KUP_WR_OFFSET, AES_IV_WR | AES_KUP_WR);

    /*loading secure header*/
	ret = zynqmp_csudma_transfer(ZYNQMP_CSUDMA_SRC_CHANNEL, (void *)src + len,
				     SECURE_HDR_SIZE, 1);
	if (ret) {
		EMSG("Loading secure header failed, invalid source buffer");
		goto out;
	}

	ret = zynqmp_csudma_sync(ZYNQMP_CSUDMA_SRC_CHANNEL);
	if (ret) {
		EMSG("DMA source transfer timeout");
		goto out;
	}

    /* Restore Key write register to 0. */
    io_write32(aes+AES_KUP_WR_OFFSET, 0U);

    /*loading tags*/
    // uint32_t val = 0;
    // for (uint8_t i=0; i<4; ++i) {
    //     val = io_read32(tag+(i*4U));
    //     DMSG("tag is:0x%x", val);
    // }

	ret = zynqmp_csudma_transfer(ZYNQMP_CSUDMA_SRC_CHANNEL, (void *)tag,
				     ZYNQMP_GCM_TAG_SIZE, 0U);
	if (ret) {
		EMSG("Loading tags failed, invalid tag buffer");
		goto out;
	}

	ret = zynqmp_csudma_sync(ZYNQMP_CSUDMA_SRC_CHANNEL);
	if (ret) {
		EMSG("DMA tag transfer timeout");
		goto out;
	}

    /* Get the AES status to know if GCM check passed. */
    gcm_status = io_read32(aes + AES_STS_OFFSET) & AES_STS_GCM_TAG_OK;
    if (!gcm_status) {
        EMSG("GCM Tag Mismatch!");
        ret = TEE_ERROR_GENERIC;
    }

out:
	zynqmp_csudma_unprepare(ZYNQMP_CSUDMA_SRC_CHANNEL);

	return ret;
}

TEE_Result zynqmp_csu_aes_decrypt_data(const void *src, size_t src_len, 
						const void *tag, const void *iv, uint32_t block_type)
{
	TEE_Result ret = TEE_SUCCESS;

	if (!src || !tag || !iv) {
		EMSG("Invalid input value");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	ret = aes_transfer_dec(src, src_len, tag, iv, block_type);
	if (ret) {
		EMSG("DMA transfer failed");
		goto out;
	}

	ret = aes_wait(AES_STS_AES_BUSY, false);
	if (ret)
		EMSG("AES-GCM transfer failed");
out:
	return ret;
}


