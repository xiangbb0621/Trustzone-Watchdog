BOOTGEN_PATH=/home/`whoami`/optee_zcu102/bootgen

$BOOTGEN_PATH/bootgen -arch zynqmp -image all.bif -w -o rp2rm2_en.bit.bin
