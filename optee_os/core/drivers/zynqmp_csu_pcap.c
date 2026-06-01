/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2022, JA1221
 */

#include <config.h>
#include <drivers/zynqmp_csu.h>
#include <drivers/zynqmp_csu_pcap.h>
#include <drivers/zynqmp_csu_secure.h>
//#include <drivers/zynqmp_csudma.h>
#include <io.h>
#include <kernel/delay.h>

/* dt */
#include <kernel/boot.h>
#include <kernel/dt.h>
#include <libfdt.h>

#include <mm/core_memprot.h>

/* Register: PCAP_CLK_CTRL Address */
#define PCAP_CLK_CTRL_OFFSET		0xA4U
#define PCAP_CLK_EN_MASK	        0x01000000U
#define PCAP_CLK_CTRL_SIZE			0x4U

/* Secure bitstream header */
#define AUTHCERT_HEADER_OFFSET	0x98
#define AUTHCERT_OFFSET			0x10
#define SECURE_SYNC_WORD_OFFSET 0x20
#define KEY_SOURCE_OFFSET		0x28
#define IV_OFFSET				0xA0
#define IV_LEN					0x0C
#define HEADER_TABLE_OFFSET		0x9c
#define SECURE_WORD_LEN			4
#define SECURE_KEY_LEN			8
#define SECURE_RESET_SET		1
#define SECURE_RESET_UNSET		0
#define KEY_SRC_KUP				0xA3A5C3C5

/* CSU AES registers */
#define AES_CFG_DEC				0 
#define AES_CFG_ENC				1 
#define AES_KEY_SRC_KUP			0
#define AES_KEY_SRC_DEV			1
#define AES_KEY_LOAD			1
#define AES_STS_KEY_INIT_DONE 	BIT(4)
#define SECURE_AES_DISABLE_KEY_CLEAR 0

/* CSU PCAP registers */
#define PCAP_PROG_OFFSET			0x00
#define PCAP_RDWR_OFFSET			0x04
#define PCAP_CTRL_OFFSET			0x08
#define PCAP_RESET_OFFSET			0x0c
#define PCAP_STS_OFFSET				0x10

#define PCAP_PROG_MASK			BIT(0)
#define PCAP_RDWR_MASK			BIT(0)
#define PCAP_CTRL_PR_MASK		BIT(0)
#define PCAP_RESET_MASK			BIT(0)
#define PCAP_STS_PL_GPWRDWN_B_MASK	BIT(9)
#define PCAP_STS_PL_CFG_RST_B_MASK	BIT(6)
#define PCAP_STS_PL_DONE_MASK		BIT(3)
#define PCAP_STS_PL_INIT_MASK		BIT(2)
#define PCAP_STS_RD_IDLE_MASK		BIT(1)
#define PCAP_STS_WR_IDLE_MASK		BIT(0)

#define PL_RESET_PERIOD_IN_US	1

#define DUMMY_BYTE				0xFFU
#define SYNC_BYTE_POSITION		64
#define VIVADO_BIT_HEADER		0x100U

#define TIMEOUT_USEC			3000000
#define SW_BYTE_SWAP			0		

static vaddr_t pcap;
static vaddr_t csu;
static vaddr_t aes;
static vaddr_t crl_apb;
static void * volatile bs_iv;

enum pcap_op {
	PCAP_WRITE = 0,
	PCAP_READ = 1
};

enum pcap_pr {
	ICAP_MODE = 0,
	MCAP_MODE = 0,
	PCAP_MODE = 1
};

/* Xilinx ZynqMp Bootgen generated Secure Bitstream header format */
static const uint8_t SecureBootgenBinFormat[] = {
	0x66, 0x55, 0x99, 0xAA,
	0x58, 0x4E, 0x4C, 0x58,
};

/* Xilinx ZynqMp Vivado generated Bitstream header format */
static const uint8_t VivadoBinFormat[] = {
	0x00U, 0x00U, 0x00U, 0xBBU, /* Bus Width Sync Word */
	0x11U, 0x22U, 0x00U, 0x44U, /* Bus Width Detect Pattern */
	0xFFU, 0xFFU, 0xFFU, 0xFFU,
	0xFFU, 0xFFU, 0xFFU, 0xFFU,
	0xAAU, 0x99U, 0x55U, 0x66U, /* Sync Word */
};

/* Xilinx ZynqMp Bootgen generated Bitstream header format */
static const uint8_t BootgenBinFormat[] = {
	0xBBU, 0x00U, 0x00U, 0x00U, /* Bus Width Sync Word */
	0x44U, 0x00U, 0x22U, 0x11U, /* Bus Width Detect Pattern */
	0xFFU, 0xFFU, 0xFFU, 0xFFU,
	0xFFU, 0xFFU, 0xFFU, 0xFFU,
	0x66U, 0x55U, 0x99U, 0xAAU, /* Sync Word */
};

static inline void endian_swap(uint32_t *word)
{
	if (*word == 0)
		return;

	*word = (*word >> 24) |
			((*word >> 8) & 0x0000FF00) |
			((*word << 8) & 0x00FF0000) |
			(*word << 24);
}

static TEE_Result waitForEvent(vaddr_t addr, uint32_t mask,
							   uint32_t val, ssize_t timeout)
{
	uint64_t tref = timeout_init_us(timeout);
	uint32_t status = 0;
	
	while (!timeout_elapsed(tref)) {
		status = io_read32(addr);
		if ((status & mask) == val) {
			return TEE_SUCCESS;
		}
	}

	return TEE_ERROR_BUSY;
}

/* This function waits for PCAP transfer to complete */
static TEE_Result pcap_waitForDone(void)
{
	TEE_Result ret = TEE_ERROR_GENERIC;

	if (!pcap)
		return TEE_ERROR_GENERIC;

	ret = waitForEvent(pcap + PCAP_STS_OFFSET, PCAP_STS_WR_IDLE_MASK,
						PCAP_STS_WR_IDLE_MASK, TIMEOUT_USEC);

	if (ret != TEE_SUCCESS)
		EMSG("timeout waiting for [PCAP transfer]");

	return ret;
}

/* This function waits for PL Done bit to be set
	or till timeout and resets PCAP after this.*/
static TEE_Result pcap_waitForPLDone(void)
{
	TEE_Result ret = TEE_ERROR_GENERIC;

	if (!pcap)
		return TEE_ERROR_GENERIC;

	/* Check for PL_DONE bit */
	ret = waitForEvent(pcap + PCAP_STS_OFFSET, PCAP_STS_PL_DONE_MASK,
					   PCAP_STS_PL_DONE_MASK, TIMEOUT_USEC);
	
	if (ret == TEE_SUCCESS) {
		DMSG("PL Configuration done successfully\n");
	} else {
		EMSG("timeout waiting for [PL configuration is done]\n");
		return ret;
	}

	/* Reset PCAP after data transfer */
	io_write32(pcap + PCAP_RESET_OFFSET, PCAP_RESET_MASK);
	io_write32(pcap + PCAP_RESET_OFFSET, 0);

	return TEE_SUCCESS;
}

/* This function validate the Secure Bitstream Header format(Encrypted Image cant validate) */
TEE_Result validateSecureBitstream(uint8_t *buf, size_t *len)
{
	uint32_t bitHdrSize = sizeof(SecureBootgenBinFormat)/sizeof(SecureBootgenBinFormat[0]);
	TEE_Result ret = TEE_ERROR_BAD_FORMAT;
	static uint32_t bs_keysrc;
	uint32_t AuthHdr_offset, AuthCert_offset;
	static uint32_t iv_arr[4] __aligned_csudma = {0};

	/* validate the Secure Bitstream Header */
	if((memcmp((buf + SECURE_SYNC_WORD_OFFSET), SecureBootgenBinFormat, bitHdrSize)) != 0){
		EMSG("Bitstream header format error!\n");
		ret = TEE_ERROR_BAD_FORMAT;
		return ret;
	}

	/* validate that the bitstream has no authentication */
	AuthHdr_offset = io_read32(buf + AUTHCERT_HEADER_OFFSET);
	AuthCert_offset = io_read32(buf + AuthHdr_offset + AUTHCERT_OFFSET);
	if(AuthCert_offset != 0x00U) {
		EMSG("Authentication is not allowed!\n");
		ret = TEE_ERROR_BAD_FORMAT;
		return ret;
	}

	/* validate Key Source is KUP */
	bs_keysrc = io_read32(buf + KEY_SOURCE_OFFSET);
	if (bs_keysrc != KEY_SRC_KUP) {
		EMSG("Key Source not allowed!\n");
		ret = TEE_ERROR_BAD_FORMAT;
		return ret;
	}

	if (iv_arr != 0x00) {
		(void)memcpy(iv_arr, buf + IV_OFFSET, IV_LEN);
		ret = TEE_SUCCESS;
	}
	bs_iv = iv_arr;
	
	// for (uint8_t i=0; i<3; ++i)
    //     DMSG("bs_iv%d is:0x%x\n", i, io_read32(bs_iv+(i*4U)));
	// DMSG("AuthHdr_offset is:0x%x\n", AuthHdr_offset);
	// DMSG("AuthCert_offset is:0x%x\n", AuthCert_offset);
	// DMSG("bs_keysrc is:0x%x\n", bs_keysrc);
	
	if (ret != TEE_SUCCESS) {
		EMSG("validate Secure Bitstream failed!\n");
	}
	
	return ret;
}

/* This function validate the Bitstream Image format(remove vivado header)
	ans set the Endianess of the CSU_DMA */
TEE_Result validateBitstream(uint8_t *buf, size_t *len)
{
	uint32_t index;
	uint32_t bitHdrSize = sizeof(BootgenBinFormat)/sizeof(BootgenBinFormat[0]);
	TEE_Result ret = TEE_ERROR_BAD_FORMAT;

	/* Check for Bitstream Size */
	if (*len < SYNC_BYTE_POSITION + bitHdrSize)
		return TEE_ERROR_BAD_PARAMETERS;

	for (index = 0; index <= VIVADO_BIT_HEADER; index++) {
	/* Find the First Dummy Byte */
		if (buf[index] == DUMMY_BYTE) {
			if (memcmp(&buf[index + SYNC_BYTE_POSITION], BootgenBinFormat, bitHdrSize) == 0) {
				DMSG("Bootgen .Bin Format(Big endian)\n");
				ret = zynqmp_csudma_unprepare(ZYNQMP_CSUDMA_SRC_CHANNEL);
				break;
			}

			if (memcmp(&buf[index + SYNC_BYTE_POSITION], VivadoBinFormat, bitHdrSize) == 0) {
				DMSG("Vivado .Bit Format(Little endian)\n");

				if (!SW_BYTE_SWAP){
					/* CSUDMA flip*/
					ret = zynqmp_csudma_prepare(ZYNQMP_CSUDMA_SRC_CHANNEL);
				}else {
					/* APU SW flip */
					for (uint32_t i = index; i < *len; i+=4)
						endian_swap((void*)(buf + i));
					ret = TEE_SUCCESS;
				}

				break;
			}
		}
	}

	if (ret == TEE_SUCCESS) {
		/* remove vivado header */
		if (index != 0) {
			(void)memcpy(buf, buf + index, *len - index);
			*len -= index;
			DMSG("remove Bitstream header. Len: %d\n", index);
		}
	} else {
		EMSG("validate Bitstream failed!\n");
	}

	return ret;
}

/* This function does the necessary initialization of PCAP interface*/
TEE_Result zynqmp_csu_pcap_init(void)
{
	uint32_t status = 0;
	TEE_Result ret = TEE_ERROR_GENERIC;
	uint32_t RegVal;
	
	/* Enable the PCAP clk */
	RegVal = io_read32(crl_apb + PCAP_CLK_CTRL_OFFSET);
	io_write32(crl_apb + PCAP_CLK_CTRL_OFFSET, RegVal | PCAP_CLK_EN_MASK);

	if (!pcap)
		return TEE_ERROR_GENERIC;

	/* Check PL power state is ON */
	status = io_read32(pcap + PCAP_STS_OFFSET);
	if ((status & PCAP_STS_PL_GPWRDWN_B_MASK) == 0) {
		EMSG("PL is in a power down state!\n");
		return TEE_ERROR_BAD_STATE;
	}

	/* Reset PCAP */
	io_write32(pcap + PCAP_RESET_OFFSET, PCAP_RESET_MASK);
	io_write32(pcap + PCAP_RESET_OFFSET, 0);

	/* Select PCAP mode and change PCAP to write mode */
	io_write32(pcap + PCAP_CTRL_OFFSET, PCAP_MODE);
	io_write32(pcap + PCAP_RDWR_OFFSET, PCAP_WRITE);

	/* Reset PL is Full bitstream only */
	// io_write32(pcap + PCAP_PROG_OFFSET, 0);
	// udelay(PL_RESET_PERIOD_IN_US);
	// io_write32(pcap + PCAP_PROG_OFFSET, PCAP_PROG_MASK);

	/* Wait for PL_init completion */
	ret = waitForEvent(pcap + PCAP_STS_OFFSET, PCAP_STS_PL_INIT_MASK,
						PCAP_STS_PL_INIT_MASK, TIMEOUT_USEC);
	if (ret != TEE_SUCCESS) {
		EMSG("timeout waiting for [Reset PL]");
		return ret;
	}
	DMSG("PL has completed it init");

	return TEE_SUCCESS;
}

/* This is the function to write Encrypted data into PCAP interface */
TEE_Result zynqmp_csu_secure_pcap_write(void *addr, size_t len)
{
	TEE_Result ret = TEE_ERROR_GENERIC;
	uint32_t AesKupKey[SECURE_KEY_LEN] = {0xAD00C023, 0xE238AC90, 0x39EA984D, 0x49AA8C81, 0x9456A98C, 0x124AE890, 0xACEF0021, 0x00128932};
	void *EncStartAddr;
	void *TagAddr;
	void *HeaderTableAddr;
	uint32_t UnEncDataOffset;
	uint32_t UnEncDataLen;
	uint32_t NextBlkLen = 0;
	uint32_t CurrentImgLen = 0;
	uint32_t BlockType;
	uint32_t key_value;

	/* Get encrypted data length and start address */
	HeaderTableAddr = addr + io_read32(addr + HEADER_TABLE_OFFSET);
	UnEncDataOffset = io_read32(HeaderTableAddr + 0x20) * SECURE_WORD_LEN;
	UnEncDataLen = io_read32(HeaderTableAddr + 0x4) * SECURE_WORD_LEN;
	EncStartAddr = (void*)(addr + UnEncDataOffset);

	// DMSG("UnEncDataOffset is: 0x%x", UnEncDataOffset);
	// DMSG("UnEncDataLen is: 0x%x", UnEncDataLen);
	// DMSG("EncStartAddr is: 0x%x", (int)EncStartAddr);

	if (!csu || !aes)
		return TEE_ERROR_GENERIC;

	/* Setup the CSU_SSS, setup the PCAP to receive from DMA source */
	io_write32(csu + ZYNQMP_CSU_SSS_CFG_OFFSET,
				ZYNQMP_CSU_SSS_DMA_STREAM_TO_AES_TO_PCAP);

	/* Configure AES for Decryption */
	io_write32(aes + AES_CFG_OFFSET, AES_CFG_DEC);

	/* Clear AES contents by resetting it. */
	io_write32(aes + AES_RESET_OFFSET, SECURE_RESET_SET);
	io_write32(aes + AES_RESET_OFFSET, SECURE_RESET_UNSET);

	/* Clear AES_KEY_CLEAR bits to avoid clearing of key */
	io_write32(aes + AES_KEY_CLEAR_OFFSET, SECURE_AES_DISABLE_KEY_CLEAR);

	/* Write AES Key0 to AES KUP register */
	for(uint8_t Count = 0U; Count < SECURE_KEY_LEN; Count++)
	{
		/* Helion AES block expects the key in big-endian. */
		key_value = io_read32(AesKupKey+Count);
		io_write32(aes+AES_KUP0_OFFSET+(Count*SECURE_WORD_LEN), key_value);
		//DMSG("KEY value is:0x%x", key_value);
	}

	/* Set the CSU Key Source to KUP key*/
	io_write32(aes + AES_KEY_SRC_OFFSET, AES_KEY_SRC_KUP);

	/* Trig loading of key. */
	io_write32(aes + AES_KEY_LOAD_OFFSET, AES_KEY_LOAD);

	/* Wait for AES key loading.*/
	ret = waitForEvent(aes + AES_STATUS_OFFSET, AES_STS_KEY_INIT_DONE,
						AES_STS_KEY_INIT_DONE, TIMEOUT_USEC);
	if (ret != TEE_SUCCESS)
		EMSG("timeout waiting for [AES key load]");

	/* Enable CSU DMA Src channel for byte swapping.*/
	ret = zynqmp_csudma_prepare(ZYNQMP_CSUDMA_SRC_CHANNEL);
	if (ret != TEE_SUCCESS)
		EMSG("CSU source channel error");

	/* First block is always secure header */
	BlockType = AES_BLK_TYPE_SECURE_HEADER;
	TagAddr = (void*)(EncStartAddr+SECURE_HDR_SIZE);
	do
	{		
		ret = zynqmp_csu_aes_decrypt_data((void*)EncStartAddr, NextBlkLen, 
										(void*)(TagAddr), (void*)bs_iv, BlockType);

		if (ret != TEE_SUCCESS)
			EMSG("CSU AES decrypt error");
		
		NextBlkLen = io_read32(aes+AES_IV3_OFFSET);
		endian_swap((void*)&NextBlkLen);
		NextBlkLen *= 4;

		/* Update the current image size. */
		CurrentImgLen += NextBlkLen;

		/*this is the last block*/
		if (0U == NextBlkLen)
		{
			if (CurrentImgLen != UnEncDataLen)
			{
				EMSG("AES Image length error");
				ret = TEE_ERROR_GENERIC;
				return ret;
			}
			break;
		}
		/*if current image size > uncrypted data size return error*/
		if (CurrentImgLen > UnEncDataLen)
		{
			EMSG("AES Image length bigger than data length");
			ret = TEE_ERROR_GENERIC;
			return ret;
		}

		/* Trig loading of key. */
		io_write32(aes + AES_KEY_LOAD_OFFSET, AES_KEY_LOAD);

		/* Wait for AES key loading.*/
		ret = waitForEvent(aes + AES_STATUS_OFFSET, AES_STS_KEY_INIT_DONE,
						AES_STS_KEY_INIT_DONE, TIMEOUT_USEC);
		if (ret != TEE_SUCCESS)
		{
			EMSG("timeout waiting for [AES key load]");
			ret = TEE_ERROR_GENERIC;
			return ret;
		}

		/*first block is secure header, rest of the blocks are data blocks*/
		BlockType = AES_BLK_TYPE_DATA_BLOCK;
		EncStartAddr = (void*)(TagAddr + ZYNQMP_GCM_TAG_SIZE);
		TagAddr = (void*)(EncStartAddr + NextBlkLen + SECURE_HDR_SIZE);
		bs_iv = (void*)(aes + AES_IV0_OFFSET);

		// DMSG("NextBlkLen size is:0x%x", NextBlkLen);
		// DMSG("UnEncDataLen is: 0x%x", UnEncDataLen);
		// DMSG("EncStartAddr is: 0x%x", (int)EncStartAddr);
		// DMSG("TagAddr is: 0x%x", TagAddr);
		// DMSG("bs_iv is: 0x%x", bs_iv);
		// DMSG("block complete");
	} while(1);

	//ret = aes_done_op(AES_CFG_DEC, ret);
	if (ret != TEE_SUCCESS)
		return ret;

	ret = pcap_waitForDone();
	if (ret != TEE_SUCCESS)
		return ret;
	
	ret = pcap_waitForPLDone();
	if (ret != TEE_SUCCESS)
		return ret;
	
	// DMSG("PCAP write done!\n");
	return ret;
}

/* This is the function to write data to PCAP interface */
TEE_Result zynqmp_csu_pcap_write(void *addr, size_t len)
{
	TEE_Result ret = TEE_ERROR_GENERIC;

	if (!pcap || !csu)
		return TEE_ERROR_GENERIC;

	/* Setup the CSU_SSS, setup the PCAP to receive from DMA source */
	io_write32(csu + ZYNQMP_CSU_SSS_CFG_OFFSET,
				ZYNQMP_CSU_SSS_DMA_STREAM_TO_PCAP);

	/* Start DMA transfer */
	ret = zynqmp_csudma_transfer(ZYNQMP_CSUDMA_SRC_CHANNEL, addr, len, 0);
	if (ret != TEE_SUCCESS)
		return ret;

	/* Wait for the DMA transfer to complete */
	ret = zynqmp_csudma_sync(ZYNQMP_CSUDMA_SRC_CHANNEL);
	if (ret != TEE_SUCCESS)
		return ret;
	
	DMSG("CSU DMA transfer done!\n");

	ret = pcap_waitForDone();
	if (ret != TEE_SUCCESS)
		return ret;
	
	ret = pcap_waitForPLDone();
	if (ret != TEE_SUCCESS)
		return ret;

	DMSG("PCAP write done!\n");

	return ret;
}

static TEE_Result csu_pcap_init(void)
{
	pcap = core_mmu_get_va(ZYNQMP_CSU_PCAP_BASE, MEM_AREA_IO_SEC,
									  ZYNQMP_CSU_PCAP_SIZE);
	csu = core_mmu_get_va(CSU_BASE, MEM_AREA_IO_SEC, CSU_SIZE);

	aes = core_mmu_get_va(ZYNQMP_CSU_AES_BASE, MEM_AREA_IO_SEC,
				      ZYNQMP_CSU_AES_SIZE);

	crl_apb = core_mmu_get_va(CRL_APB_BASE, MEM_AREA_IO_SEC, CRL_APB_SIZE);

	if (!pcap || !csu || !aes || !crl_apb)
		return TEE_ERROR_DEFER_DRIVER_INIT;

	return TEE_SUCCESS;
}

driver_init(csu_pcap_init);


/*
static const char *const dt_ctrl_match_table[] = {
	"xlnx,zynqmp-pcap-fpga",
};

TEE_Result zynqmp_csu_pcap_dt_enable_secure_status(void)
{
	unsigned int i = 0;
	void *fdt = NULL;
	int node = -1;

	fdt = get_external_dt();
	if (!fdt)
		return TEE_SUCCESS;

	for (i = 0; i < ARRAY_SIZE(dt_ctrl_match_table); i++) {
		node = fdt_node_offset_by_compatible(fdt, 0,
						     dt_ctrl_match_table[i]);
		if (node >= 0)
			break;
	}

	if (node < 0)
		return TEE_SUCCESS;

	if (_fdt_get_status(fdt, node) == DT_STATUS_DISABLED)
		return TEE_SUCCESS;

	if (dt_enable_secure_status(fdt, node)) {
		EMSG("Not able to set the PCAP-FPGA DTB entry secure");
		return TEE_ERROR_NOT_SUPPORTED;
	}

	return TEE_SUCCESS;
}
*/