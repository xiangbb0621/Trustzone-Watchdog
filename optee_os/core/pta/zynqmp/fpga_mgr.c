#include <io.h>
#include <trace.h>
#include <kernel/pseudo_ta.h>
#include <mm/core_memprot.h>

#include <platform_config.h>
#include <drivers/zynqmp_csu_pcap.h>
#include <pta_zynqmp_fpga_mgr.h>

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

	/* Secure PCAP write */
	result = zynqmp_csu_secure_pcap_write((void*)va, size);
	
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