SD_BOOT=/media/`whoami`/BOOT
SD_ROOTFS=/media/`whoami`/rootfs
IMAGE_PATH=~/optee_zcu102/build/zynqmp
FIRMWARE_PATH=~/optee_zcu102/zynqmp-zcu102-release

echo "rm files..."
rm -rf $SD_BOOT/*

echo "copy Image..."
cd $IMAGE_PATH
cp BOOT.bin $SD_BOOT/

# cp uimage
cp zynqmp-zcu102.ub $SD_BOOT/image.ub

# [Required for non-ramdisk] Copy rootfs
# sudo rm -rf $SD_ROOTFS/*
# sudo tar -xvf ~/optee_zcu102/out-br/images/rootfs.tar -C $SD_ROOTFS/

# cp $FIRMWARE_PATH/boot.scr $SD_BOOT
cp $IMAGE_PATH/boot.scr $SD_BOOT
sync

