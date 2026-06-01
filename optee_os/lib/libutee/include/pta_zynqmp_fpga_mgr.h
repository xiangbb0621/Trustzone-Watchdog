#ifndef __PTA_ZYNQMP_FPGA_MGR_H
#define __PTA_ZYNQMP_FPGA_MGR_H

#define ZYNQMP_FPGA_MGR_SERVICE_UUID \
		{ 0x14f7c2e1, 0xc9a2, 0x40ae, \
		{ 0x87, 0x4e, 0x3c, 0xd6, 0x32, 0x77, 0x20, 0xf1} }

#define ZYNQMP_FPGA_MGR_TA_NAME		"pta_zynqmp_fpga_mgr.ta"

// #define ZCU102_BS_FRAMES	71260
// #define BS_FRAMES 			ZCU102_BS_FRAMES
// #define BITSTREAM_SIZE		(((ZCU102_BS_FRAMES * 93 ) + 515) * 32 / 8)

#define BITSTREAM_SIZE			(5000000)
#define BITSTREAM_BUFFER_SIZE	(30000000) // 30MB

/*
 * Configure PL
 *
 * [in/out]    memref[0].buffer:    PL bitstream address
 * [in/out]    memref[0].size:      PL bitstream size
 */
#define PTA_ZYNQMP_FPGA_MGR_CMD_WRITE	0

#endif /* __PTA_ZYNQMP_FPGA_MGR_H */