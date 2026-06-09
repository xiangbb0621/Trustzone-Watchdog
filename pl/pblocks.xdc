# =============================================================================
# pblocks.xdc — DFX floorplan for project_zcu102_dfx_bdc_lab10_2rp
#
# 還原自 .runs/synth_1/.Xil/design_1_wrapper_propImpl.xdc（原檔由外部絕對路徑
# C:/Users/tim/.../ug947/ipi_bdc_dfx_zu/constraints/pblocks.xdc 引用，copy 專案時
# 沒跟過來而遺失）。內容與原始一致。
#
# RP1 -> CLOCKREGION_X1Y3 ; RP2 -> CLOCKREGION_X2Y3
# 靜態區的 tmr_wdt 不需 pblock，由工具自動擺進靜態 fabric。
# =============================================================================
create_pblock pblock_rp1
add_cells_to_pblock [get_pblocks pblock_rp1] [get_cells -quiet [list design_1_i/rp1]]
resize_pblock [get_pblocks pblock_rp1] -add {CLOCKREGION_X1Y3:CLOCKREGION_X1Y3}
set_property SNAPPING_MODE ON [get_pblocks pblock_rp1]

create_pblock pblock_rp2
add_cells_to_pblock [get_pblocks pblock_rp2] [get_cells -quiet [list design_1_i/rp2]]
resize_pblock [get_pblocks pblock_rp2] -add {CLOCKREGION_X2Y3:CLOCKREGION_X2Y3}
set_property SNAPPING_MODE ON [get_pblocks pblock_rp2]
