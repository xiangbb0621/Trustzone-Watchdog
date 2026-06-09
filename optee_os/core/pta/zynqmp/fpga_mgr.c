#include <io.h>
#include <trace.h>
#include <kernel/pseudo_ta.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>

#include <platform_config.h>
#include <drivers/zynqmp_csu_pcap.h>
#include <pta_zynqmp_fpga_mgr.h>

/*
 * DFX Decoupler 管理：在 PCAP 重燒 RP 前後，隔離/接通 RP 與靜態區之間的介面。
 * 不隔離的話，RP 重配置中(~數十 ms)的垃圾 AXI 訊號會灌進共用的 smartconnect，
 * 波及靜態區周邊（例如 TMR watchdog @0xA0100000）造成 AXI SError → kernel panic。
 * 兩個 RP 的 decoupler s_axi_reg：0xA0020000、0xA0030000，bit0 = decouple。
 */
#define ZYNQMP_DFX_DECOUPLER_BASE	0xA0020000
#define ZYNQMP_DFX_DECOUPLER_SIZE	0x20000		/* 涵蓋 0xA0020000 與 0xA0030000 */
#define DFX_DECOUPLER_1_OFFSET		0x00000		/* -> 0xA0020000 */
#define DFX_DECOUPLER_0_OFFSET		0x10000		/* -> 0xA0030000 */
#define DFX_DECOUPLE			0x1U		/* 隔離 */
#define DFX_COUPLE			0x0U		/* 接通 */

register_phys_mem_pgdir(MEM_AREA_IO_SEC, ZYNQMP_DFX_DECOUPLER_BASE,
			ZYNQMP_DFX_DECOUPLER_SIZE);

/* decouple=DFX_DECOUPLE 隔離兩個 RP；=DFX_COUPLE 接通 */
static void zynqmp_dfx_decouple(uint32_t decouple)
{
	vaddr_t base = (vaddr_t)core_mmu_get_va(ZYNQMP_DFX_DECOUPLER_BASE,
						MEM_AREA_IO_SEC,
						ZYNQMP_DFX_DECOUPLER_SIZE);

	if (!base) {
		EMSG("DFX decoupler MMIO map failed");
		return;
	}

	io_write32(base + DFX_DECOUPLER_1_OFFSET, decouple);
	io_write32(base + DFX_DECOUPLER_0_OFFSET, decouple);
}

static TEE_Result pta_zynqmp_fpga_write(uint32_t param_types,
					TEE_Param params[TEE_NUM_PARAMS])
{
	vaddr_t va;
	size_t size;
	
	TEE_Result result = TEE_SUCCESS;
	uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
							TEE_PARAM_TYPE_NONE,
							TEE_PARAM_TYPE_NONE,
							TEE_PARAM_TYPE_NONE);
	
	if (param_types != exp_param_types) {
		EMSG("Invalid Param types");
		return TEE_ERROR_BAD_PARAMETERS;
	}
	va = (vaddr_t)params[0].memref.buffer;
	size = (size_t)params[0].memref.size;
	DMSG("BS va:%p, size:%zu", (void*)va, size);

	if (!va || !size)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Verify the bitstream image format */
	// result = validateBitstream((uint8_t*)va, &size);

	/* Verify the encrypted partial bitstream image format */
	result = validateSecureBitstream((uint8_t*)va, &size);
	if (result == TEE_SUCCESS)
		DMSG("The bitstream verification is successful. Len: %zu bytes", size);
	else
		return result;

	/* PCAP init */
	result = zynqmp_csu_pcap_init();

	if (result != TEE_SUCCESS) {
		EMSG("PCAP init failed!");
		return TEE_ERROR_GENERIC;
	}

	DMSG("zynqmp_csu_pcap_init() successful");

	/* PCAP write */
	// result = zynqmp_csu_pcap_write((void*)va, size);

	/* Secure PCAP write
	 * DFX：重燒前先 decouple 隔離兩個 RP，重燒後再接通。避免 RP 重配置期間的
	 * 垃圾 AXI 訊號灌進共用 smartconnect、害靜態區周邊(TMR watchdog) AXI 掛掉。
	 * decouple 改由 Secure World 統一掌控（取代先前 Linux 手動 devmem）。
	 */
	zynqmp_dfx_decouple(DFX_DECOUPLE);
	result = zynqmp_csu_secure_pcap_write((void*)va, size);
	zynqmp_dfx_decouple(DFX_COUPLE);
	
	if (result != TEE_SUCCESS) {
		EMSG("PCAP write failed!");
		return result;
	}

	DMSG("pta_zynqmp_fpga_write() success");
	return result;
}


static TEE_Result invoke_command(void *session_context __unused,
				 uint32_t cmd_id,
				 uint32_t param_types,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result result = TEE_SUCCESS;

	DMSG("command entry point[%d] for \"%s\"", cmd_id, ZYNQMP_FPGA_MGR_TA_NAME);

	switch (cmd_id) {
	case PTA_ZYNQMP_FPGA_MGR_CMD_WRITE:
		result = pta_zynqmp_fpga_write(param_types, params);
		return result;
	default:
		EMSG("cmd: %d Not supported %s\n", cmd_id, ZYNQMP_FPGA_MGR_TA_NAME);
		result = TEE_ERROR_BAD_PARAMETERS;
		break;
	}

	return result;
}

pseudo_ta_register(.uuid = ZYNQMP_FPGA_MGR_SERVICE_UUID,
			.name = ZYNQMP_FPGA_MGR_TA_NAME,
			.flags = PTA_DEFAULT_FLAGS,
			.invoke_command_entry_point = invoke_command);