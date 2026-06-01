################################################################################
# Following variables defines how the NS_USER (Non Secure User - Client
# Application), NS_KERNEL (Non Secure Kernel), S_KERNEL (Secure Kernel) and
# S_USER (Secure User - TA) are compiled
################################################################################
override COMPILE_NS_USER   := 64
override COMPILE_NS_KERNEL := 64
override COMPILE_S_USER    := 64
override COMPILE_S_KERNEL  := 64

PLATFORM = zynqmp-zcu102
OPTEE_OS_PLATFORM = zynqmp-zcu102

################################################################################
# Paths to git projects and various binaries
################################################################################
TF_A_PATH		?= $(ROOT)/arm-trusted-firmware
U-BOOT_PATH		?= $(ROOT)/u-boot-xlnx
BOOTGEN_PATH		?= $(ROOT)/bootgen
LINUX_PATH		?= $(ROOT)/linux-xlnx

include common.mk

################################################################################
# Targets
################################################################################

all: tfa optee-os u-boot linux dtbo dtb-zcu102 buildroot
clean: tfa-clean optee-os-clean u-boot-clean linux-clean dtbo-clean buildroot-clean

include toolchain.mk

################################################################################
# ARM Trusted Firmware
################################################################################

TF_A_EXPORTS = CROSS_COMPILE="$(CCACHE)$(AARCH64_CROSS_COMPILE)"
TF_A_FLAGS  = PLAT=zynqmp RESET_TO_BL31=1 NEED_BL32=yes SPD=opteed LOG_LEVEL=LOG_LEVEL_INFO


tfa:
	$(TF_A_EXPORTS) $(MAKE) -C $(TF_A_PATH) $(TF_A_FLAGS) bl31

tfa-clean:
	$(TF_A_EXPORTS) $(MAKE) -C $(TF_A_PATH) $(TF_A_FLAGS) clean

################################################################################
# OP-TEE
#################################################################################
OPTEE_OS_COMMON_FLAGS += CFG_ARM64_core=y
OPTEE_OS_COMMON_FLAGS += CFG_ZYNQMP_CSU_SECURE=y
OPTEE_OS_COMMON_FLAGS += CFG_ZYNQMP_CSU_PCAP=y
CFG_TEE_CORE_LOG_LEVEL := 1
# CFG_TEE_CORE_LOG_LEVEL := 3

optee-os: optee-os-common
	${OPTEE_OS_PATH}/scripts/gen_tee_bin.py --input ${OPTEE_OS_PATH}/out/arm/core/tee.elf --out_tee_raw_bin ${OPTEE_OS_PATH}/out/arm/core/tee_raw.bin

optee-os-clean: optee-os-clean-common
	rm -f ${OPTEE_OS_PATH}/out/arm/core/tee_raw.bin

################################################################################
# U-Boot
################################################################################

U-BOOT_EXPORTS = CROSS_COMPILE="$(CCACHE)$(AARCH64_CROSS_COMPILE)"
U-BOOT_CONFIG = xilinx_zynqmp_virt_defconfig
U-BOOT_DTS = zynqmp-zcu102-rev1.0

u-boot:
	$(U-BOOT_EXPORTS) $(MAKE) -C $(U-BOOT_PATH) $(U-BOOT_CONFIG)
	$(U-BOOT_EXPORTS) $(MAKE) -C $(U-BOOT_PATH) DEVICE_TREE=$(U-BOOT_DTS) DTC_FLAGS="-@"

u-boot-clean:
	$(U-BOOT_EXPORTS) $(MAKE) -C $(U-BOOT_PATH) clean

###############################################################################
# Device-Tree
###############################################################################
dtbo: linux
	${LINUX_PATH}/scripts/dtc/dtc -@ -I dts -O dtb -o zynqmp/zynqmp-optee.dtbo zynqmp/zynqmp-optee.dtso

dtbo-clean:
	rm -f zynqmp/zynqmp-optee.dtbo

dtb-zcu102: linux
# 1. Import the device tree of PL(if needed):
# 		You need to import .xsa with Vitis first to generate the device tree.
# 		Put 'pcw.dtsi' and 'pl.dtsi' in zynqmp/bsp/.
# 2. Customize system-user.dtsi (if needed).

# Origin zcu102 ver1.0 DT 
	gcc -I my_dts -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp -I $(U-BOOT_PATH)/arch/arm/dts/include -o zynqmp/bsp/zcu102-rev1.0.dtsi $(U-BOOT_PATH)/arch/arm/dts/zynqmp-zcu102-rev1.0.dts
# merge zcu102 and PL DT
	gcc -I my_dts -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp -I zynqmp/bsp -o zynqmp/bsp/zcu102.dts zynqmp/bsp/system-top.dts
# compile DTS to DTB
	$(U-BOOT_PATH)/scripts/dtc/dtc -I dts -O dtb -o zynqmp/bsp/zcu102.dtb zynqmp/bsp/zcu102.dts

dtb-zcu102-clean:
	rm -f zynqmp/bsp/zcu102*.dt*

################################################################################
# Linux kernel
################################################################################

LINUX_DEFCONFIG_COMMON_ARCH := arm64
LINUX_DEFCONFIG_COMMON_FILES := \
		$(LINUX_PATH)/arch/arm64/configs/xilinx_zynqmp_defconfig \
		$(CURDIR)/kconfigs/zynqmp.conf

linux-defconfig: $(LINUX_PATH)/.config

LINUX_COMMON_FLAGS += ARCH=arm64

linux: linux-common

linux-defconfig-clean: linux-defconfig-clean-common

LINUX_CLEAN_COMMON_FLAGS += ARCH=arm64

linux-clean: linux-clean-common

LINUX_CLEANER_COMMON_FLAGS += ARCH=arm64

linux-cleaner: linux-cleaner-common

###############################################################################
# Bouildroot
###############################################################################

BR2_TARGET_GENERIC_ISSUE="OP-TEE embedded distrib for $(PLATFORM)"
BR2_TARGET_ROOTFS_EXT2=y
BR2_PACKAGE_BUSYBOX_WATCHDOG=y
BR2_TARGET_GENERIC_GETTY_PORT=ttyPS0
BR2_ROOTFS_OVERLAY=$(ROOT)/build/rootfs_overlay

# change init to systemd
# BR2_INIT_BUSYBOX=n
# BR2_INIT_SYSTEMD=y
# BR2_PACKAGE_SYSTEMD=y
# BR2_PACKAGE_SYSTEMD_ANALYZE=y

# change init to sysV
# BR2_INIT_BUSYBOX=n
# BR2_INIT_SYSV=y

# TF-A, Linux kernel, U-Boot and OP-TEE OS/Client/... are not built from their
# related Buildroot native package.
BR2_TARGET_ARM_TRUSTED_FIRMWARE=n
BR2_LINUX_KERNEL=n
BR2_TARGET_OPTEE_OS=n
BR2_TARGET_UBOOT=n
BR2_PACKAGE_OPTEE_CLIENT=n
BR2_PACKAGE_OPTEE_TEST=n
BR2_PACKAGE_OPTEE_EXAMPLES=n
BR2_PACKAGE_OPTEE_BENCHMARK=n


###############################################################################
# Images
###############################################################################
image: bootimage fitimage
image-clean: bootimage-clean fitimage-clean

###############################################################################
# Boot Image
###############################################################################
FIRMWARE_TARBALL = $(subst zynqmp-,2021.2-,$(PLATFORM))-release.tar.xz

bootimage: bootgen firmware tfa optee-os u-boot
	$(BOOTGEN_PATH)/bootgen -arch zynqmp -p xczu9eg -image zynqmp/bootImage-${PLATFORM}.bif -w -o zynqmp/BOOT.bin

bootimage-clean: bootgen-clean firmware-clean tfa-clean optee-os-clean u-boot-clean
	rm -f zynqmp/BOOT.bin


###############################################################################
# Bootgen
###############################################################################

bootgen:
	make -C $(BOOTGEN_PATH)

bootgen-clean:
	make -C $(BOOTGEN_PATH) clean


################################################################################
# ZynqMPSoC Firmware mandatory for the boot
################################################################################

firmware:
ifeq ("$(wildcard ../$(FIRMWARE_TARBALL))","")
	$(error Release image tarball not present ../$(FIRMWARE_TARBALL))
else
	mkdir -p ../$(PLATFORM)-release && tar -xvf ../$(FIRMWARE_TARBALL) -C ../$(PLATFORM)-release --strip-components=1
endif

firmware-clean:
	rm -rf ../zcu102-release

###############################################################################
# FIT Image
###############################################################################

fitimage: linux dtbo dtb-zcu102 buildroot
	${U-BOOT_PATH}/tools/mkimage -f zynqmp/fitImage-${PLATFORM}.its zynqmp/${PLATFORM}.ub
	${U-BOOT_PATH}/tools/mkimage -c none -A arm -T script -d zynqmp/boot.cmd.default zynqmp/boot.scr

fitimage-clean: linux-clean dtbo-clean dtb-zcu102-clean buildroot-clean
	rm -f zynqmp/${PLATFORM}.ub
	rm -f zynqmp/boot.scr

