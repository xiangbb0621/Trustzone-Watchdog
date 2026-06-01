#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include <bs_downloader_ta.h>
#include <pta_zynqmp_fpga_mgr.h>

#include <tee_tcpsocket.h>
#include <tee_udpsocket.h>

#include <string.h>

#define SERVER_IP		"192.168.100.1"
#define SERVER_PORT		4567U
#define REVC_BLOCK		((1<<16))

static TEE_TASessionHandle pta_zynqmp_fpga_mgr_session = TEE_HANDLE_NULL;
static const TEE_UUID pta_zynqmp_fpga_mgr_uuid = ZYNQMP_FPGA_MGR_SERVICE_UUID;
static uint32_t ta_param_types;
static TEE_Param ta_params[TEE_NUM_PARAMS];

/*
 * Called when the instance of the TA is created. This is the first call in
 * the TA.
 */
TEE_Result TA_CreateEntryPoint(void)
{
	DMSG("has been called");

	return TEE_SUCCESS;
}

/*
 * Called when the instance of the TA is destroyed if the TA has not
 * crashed or panicked. This is the last call in the TA.
 */
void TA_DestroyEntryPoint(void)
{
	DMSG("has been called");
}

/*
 * Called when a new session is opened to the TA. *sess_ctx can be updated
 * with a value to be able to identify this session in subsequent calls to the
 * TA. In this function you will normally do the global initialization for the
 * TA.
 */
TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
		TEE_Param __maybe_unused params[4],
		void __maybe_unused **sess_ctx)
{
	TEE_Result res;
	uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
							   TEE_PARAM_TYPE_NONE,
							   TEE_PARAM_TYPE_NONE,
							   TEE_PARAM_TYPE_NONE);

	DMSG("has been called");

	if (param_types != exp_param_types)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Unused parameters */
	(void)&params;
	(void)&sess_ctx;

	if (pta_zynqmp_fpga_mgr_session == TEE_HANDLE_NULL) {
		res = TEE_OpenTASession(&pta_zynqmp_fpga_mgr_uuid, TEE_TIMEOUT_INFINITE,
								0, NULL, &pta_zynqmp_fpga_mgr_session, NULL);

		if (res != TEE_SUCCESS) {
			EMSG("TEE_OpenTASession to Zynqmp_fpga_mgr_PTA failed");
			return res;
		}
		IMSG("TA open Zyqnmp_fpga_mgr_PTA session successfully");
	}

	ta_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
									 TEE_PARAM_TYPE_NONE,
									 TEE_PARAM_TYPE_NONE,
									 TEE_PARAM_TYPE_NONE);

	/* If return value != TEE_SUCCESS the session will not be created. */
	return TEE_SUCCESS;
}

/*
 * Called when a session is closed, sess_ctx hold the value that was
 * assigned by TA_OpenSessionEntryPoint().
 */
void TA_CloseSessionEntryPoint(void __maybe_unused *sess_ctx)
{
	(void)&sess_ctx; /* Unused parameter */
	IMSG("Goodbye!\n");
}

/* TCP connect */
static TEE_Result tcp_connect(TEE_iSocketHandle *ctx)
{
	TEE_Result res;
	TEE_tcpSocket_Setup setup;
	uint32_t proto_error;

	setup.ipVersion   = TEE_IP_VERSION_DC;
	setup.server_addr = SERVER_IP;
	setup.server_port = SERVER_PORT;

	res = TEE_tcpSocket->open(ctx, &setup, &proto_error);
	if (res != TEE_SUCCESS) {
		EMSG("TCP_Socket open() failed. Return code: %u"
		     ", protocol error: %u", res, proto_error);
		return res;
	}

	return TEE_SUCCESS;
}

static TEE_Result socket_client(uint8_t *data, uint32_t *data_size)
{
	TEE_Result res;

	TEE_iSocketHandle socketCtx;
	// TEE_tcpSocket_Setup tcpSetup;

	IMSG("Connect to PL Bitstream Server...\n");
	res = tcp_connect(&socketCtx);

	if (res != TEE_SUCCESS) {
        IMSG("TCP connect failed!!\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	IMSG("TCP connected\n");

	// *data = "Hello server!"; //x
	// data_size = snprintf(NULL, 0, "%s", data); //x
	// res = TEE_tcpSocket->send(socketCtx, data, &data_size, TEE_TIMEOUT_INFINITE);

	DMSG("recv buffer: %p, size: %d", data, *data_size);

	uint8_t *ptr = data;
	uint32_t chunk;
	uint32_t sum = 0;

	do {
		chunk = REVC_BLOCK;

		res = TEE_tcpSocket->recv(socketCtx, (void*)ptr, &chunk,
								  TEE_TIMEOUT_INFINITE);
		ptr += chunk;

		/* Print recv process */
		// DMSG("recv: %d Bytes. Sum: %d.", chunk, ptr - data);
	} while (chunk > 0);
	*data_size = ptr - data;

	/* Debug */
	// for (int i = 0x1947DBC / sizeof(uint32_t); i < *data_size/sizeof(uint32_t); i++)
	// 	DMSG("[%X]Data: %08x", i*4,*((uint32_t*)data+i));

	if (res != TEE_SUCCESS) {
		EMSG("BS server socket recv() failed. Error code: %#0" PRIX32, res);
		return res;
	}

	TEE_tcpSocket->close(socketCtx);
	DMSG("Socket close\n");

	return res;
}

static TEE_Result downlaod_bs(uint32_t param_types,
	TEE_Param params[4])
{
	TEE_Result res;
	
	uint32_t bs_len = BITSTREAM_SIZE;
	uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE);
	// uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
	// 					   TEE_PARAM_TYPE_NONE,
	// 					   TEE_PARAM_TYPE_NONE,
	// 					   TEE_PARAM_TYPE_NONE);

	DMSG("has been called");

	if (param_types != exp_param_types)
		return TEE_ERROR_BAD_PARAMETERS;

	uint8_t *ptr;
	DMSG("Malloc:%d", BITSTREAM_SIZE);
	ptr = (uint8_t*)TEE_Malloc(BITSTREAM_SIZE, TEE_MALLOC_FILL_ZERO);

	if (ptr == NULL) {
		EMSG("TEE_Malloc failed");
		return TEE_ERROR_GENERIC;
	}

	res = socket_client(ptr, &bs_len);
	if( res != TEE_SUCCESS) {
		EMSG("Bitstream download failed");
	} else {
		IMSG("Bitstream download successful");
	}

	memset(&ta_params, 0, sizeof(ta_params));
	ta_params[0].memref.buffer = ptr;
	ta_params[0].memref.size = bs_len;
	// ta_params[0].memref.buffer = params[0].memref.buffer;
	// ta_params[0].memref.size = params[0].memref.size;
	DMSG("TEE_InvokeTACommand pta_zynqmp_fpga_mgr");
	// TEE_Time start, stop;
	// TEE_GetSystemTime(&start);
	res = TEE_InvokeTACommand(pta_zynqmp_fpga_mgr_session, TEE_TIMEOUT_INFINITE,\
							PTA_ZYNQMP_FPGA_MGR_CMD_WRITE,\
							ta_param_types, ta_params, NULL);
	// TEE_GetSystemTime(&stop);
	// DMSG("PTA time %u.%03u\n", (unsigned int)(stop.seconds - start.seconds),  (stop.millis - start.millis));
	TEE_Free(ptr);

	if (res != TEE_SUCCESS) {
		EMSG("TEE_InvokeTACommand failed with code 0x%x", res);
		EMSG("Zynqmp fpga mgr pta invoke failed");
		return res;
	}

	IMSG("PL MGR PTA write successful");

	return res;
}
/*
 * Called when a TA is invoked. sess_ctx hold that value that was
 * assigned by TA_OpenSessionEntryPoint(). The rest of the paramters
 * comes from normal world.
 */
TEE_Result TA_InvokeCommandEntryPoint(void __maybe_unused *sess_ctx,
			uint32_t cmd_id,
			uint32_t param_types, TEE_Param params[4])
{
	(void)&sess_ctx; /* Unused parameter */

	switch (cmd_id) {
	case TA_BS_DOWNLOADER_CMD_DOWNLOAD:
		return downlaod_bs(param_types, params);
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}
}
