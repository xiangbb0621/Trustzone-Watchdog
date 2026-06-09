#!/bin/sh
# 加密 DPR partial bitstream → *_en.bit.bin（給 bs_downloader 經 OP-TEE PCAP 燒錄）
#
# 直接吃 Vivado impl 產出的 *_partial.bit 檔名（不用先改名），對應如下：
#   design_1_i_rp1_rp1rm1_inst_0_partial.bit → rp1rm1_en.bit.bin
#   design_1_i_rp1_rp1rm2_inst_0_partial.bit → rp1rm2_en.bit.bin
#   design_1_i_rp2_rp2rm1_inst_2_partial.bit → rp2rm1_en.bit.bin
#   design_1_i_rp2_rp2rm2_inst_1_partial.bit → rp2rm2_en.bit.bin
#
# 前提：上面 4 個 *_partial.bit 已從 Vivado .runs/ 複製到本目錄。
# 加密設定沿用 encrypt.nky + kup_key（與學長原版一致）。
set -e

BOOTGEN=../../../bootgen/bootgen      # = xiangbb/bootgen/bootgen
BLK="1999952;1999952;1999952;1999952;1999952;1999952;1999952;1999952;1999952;1999952;1999952"

enc() {   # $1=輸入 partial 檔   $2=輸出名(不含 _en.bit.bin)
    if [ ! -f "$1" ]; then
        echo "✗ 找不到 $1，略過（請先從 Vivado impl 複製進來）"
        return
    fi
    cat > "_$2.bif" <<EOF
all:
{
 [keysrc_encryption] kup_key
 [encryption=aes,aeskeyfile=encrypt.nky, blocks=${BLK}] $1
}
EOF
    echo ">> $1 → $2_en.bit.bin"
    "$BOOTGEN" -arch zynqmp -image "_$2.bif" -w -o "$2_en.bit.bin"
    rm -f "_$2.bif"
}

enc design_1_i_rp1_rp1rm1_inst_0_partial.bit rp1rm1
enc design_1_i_rp1_rp1rm2_inst_0_partial.bit rp1rm2
enc design_1_i_rp2_rp2rm1_inst_2_partial.bit rp2rm1
enc design_1_i_rp2_rp2rm2_inst_1_partial.bit rp2rm2
echo "完成"
