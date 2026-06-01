/*
 * Copyright (c) 2016, Linaro Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <stdio.h>
#include <string.h>

// #include <stdlib.h>
// #include <unistd.h>
// #include <sys/socket.h>
// #include <arpa/inet.h>

/* OP-TEE TEE client API (built by optee_client) */
#include <tee_client_api.h>

/* For the UUID (found in the TA's h-file(s)) */
#include <bs_downloader_ta.h>

// #define REVC_BLOCK 		(1 << 16)
// #define BITSTREAM_SIZE	(951168)

int main(void)
{
	TEEC_Result res;
	TEEC_Context ctx;
	TEEC_Session sess;
	TEEC_Operation op;
	TEEC_UUID uuid = TA_BS_DOWNLOADER_UUID;
	uint32_t err_origin;
	// volatile uint8_t volatile *  data;
	// uint32_t data_size;
	// char serverIP[15] = "140.115.52.75";
    // uint32_t serverPort = 4567;
	// uint8_t *  ptr;
	// uint32_t chunk;

	// printf("Server IP   : %s\n", serverIP);
    // printf("Server Port : %d\n\n", serverPort);

	// // 建立socket
    // int sockfd = socket(AF_INET , SOCK_STREAM , 0);
    // if(sockfd == -1) {
    //     printf("Fail to create a socket.\n");
    //     exit(1);
    // }

    // // socket的連線
    // struct sockaddr_in serverAddr;
    // memset(&serverAddr, 0, sizeof(serverAddr));

    // serverAddr.sin_family = AF_INET;
    // serverAddr.sin_addr.s_addr = inet_addr(serverIP);
    // serverAddr.sin_port = htons(serverPort);
    
    // int err = connect(sockfd,(struct sockaddr *)&serverAddr,sizeof(serverAddr));
    // if(err == -1) {
    //     printf("Connection error\n");
    //     exit(1);
    // }

	// data = (uint8_t*)malloc(BITSTREAM_SIZE * sizeof(uint8_t));
	// ptr = data;

	// do {
	// 	chunk = REVC_BLOCK;
	// 	chunk = recv(sockfd, ptr, REVC_BLOCK, 0);
	// 	ptr += chunk;
	// } while (chunk > 0);

	// data_size = ptr - data;
	// printf("rev size is:%d\n", data_size);
	// printf("data addr is:0x%x\n", data);
	// printf("close Socket\n");
	// close(sockfd);
	printf("TA_DATA_SIZE: %d MB\n", MY_TA_DATA_SIZE);
	
	/* Initialize a context connecting us to the TEE */
	res = TEEC_InitializeContext(NULL, &ctx);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_InitializeContext failed with code 0x%x", res);

	/*
	 * Open a session to the "hello world" TA, the TA will print "hello
	 * world!" in the log when the session is created.
	 */
	res = TEEC_OpenSession(&ctx, &sess, &uuid,
			       TEEC_LOGIN_PUBLIC, NULL, NULL, &err_origin);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_Opensession failed with code 0x%x origin 0x%x",
			res, err_origin);

	/*
	 * Execute a function in the TA by invoking it, in this case
	 * we're incrementing a number.
	 *
	 * The value of command ID part and how the parameters are
	 * interpreted is part of the interface provided by the TA.
	 */

	/* Clear the TEEC_Operation struct */
	memset(&op, 0, sizeof(op));

	/*
	 * Prepare the argument. Pass a value in the first parameter,
	 * the remaining three parameters are unused.
	 */
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE,
					 TEEC_NONE, TEEC_NONE);
	// op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INOUT, TEEC_NONE,
	// 				 TEEC_NONE, TEEC_NONE);
	// op.params[0].tmpref.buffer = data;
	// op.params[0].tmpref.size = data_size;

	/*
	 * TA_HELLO_WORLD_CMD_INC_VALUE is the actual function in the TA to be
	 * called.
	 */
	printf("Invoking BS_Downloader\n");
	res = TEEC_InvokeCommand(&sess, TA_BS_DOWNLOADER_CMD_DOWNLOAD, &op,
				 &err_origin);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
			res, err_origin);

	/*
	 * We're done with the TA, close the session and
	 * destroy the context.
	 *
	 * The TA will print "Goodbye!" in the log when the
	 * session is closed.
	 */

	//free(data);
	TEEC_CloseSession(&sess);

	TEEC_FinalizeContext(&ctx);

	return 0;
}
