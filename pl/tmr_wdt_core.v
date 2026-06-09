// =============================================================================
// tmr_wdt_core.v
// 三重備援看門狗 (TMR Watchdog) + Majority Voter —— 論文 Phase 3 的 PL 硬體核心
//
//   * 第一關：maj_2of3 以 2-of-3 過濾「單一 WDT 被 SEU 打壞而誤報」的情況
//   * 第二關：all_3 在三隻 WDT 全 timeout (Linux 真的死了) 時拉高 → 接 pl_ps_irq[0]
//
// 本模組是「純邏輯核心」，不含 AXI。AXI-Lite 暫存器介面由 Vivado 的
// 「Create and Package New IP → AXI4 peripheral」精靈產生的包裝層提供，
// 包裝層把暫存器解出 enable / reload_val / kick_a/b/c，再實例化本模組。
// 這樣論文要講的 TMR 邏輯集中在這個乾淨的檔案，AXI 雜訊交給精靈產生的程式碼。
//
// 時脈：100 MHz (來自 static_region/clk_wiz_0 clk_out1)
// =============================================================================
`timescale 1ns / 1ps

module tmr_wdt_core #(
    parameter integer        CNT_WIDTH      = 32,
    // 100 MHz 下 100,000,000 個 cycle = 1 秒 (Linux 可透過 RELOAD 暫存器覆寫)
    parameter [31:0]         RELOAD_DEFAULT = 32'd100_000_000
)(
    input  wire                  clk,         // 100 MHz fabric clock
    input  wire                  rstn,        // 低有效 reset (來自 proc_sys_reset)
    input  wire                  enable,      // CTRL.EN：1=看門狗計數中；0=保持滿載不誤觸
    input  wire [CNT_WIDTH-1:0]  reload_val,  // RELOAD：timeout 對應的 clock 數
    input  wire                  kick_a,      // 餵狗脈衝 (一個 clock 寬)；由 AXI 寫 KICK 暫存器產生
    input  wire                  kick_b,
    input  wire                  kick_c,

    output wire                  to_a,        // 各狗 timeout (level，counter 歸零時持續拉高)
    output wire                  to_b,
    output wire                  to_c,
    output wire                  maj_2of3,    // 第一關：至少兩隻同意 timeout
    output wire                  all_3,       // 第二關：三隻全 timeout → pl_ps_irq[0]

    output wire [CNT_WIDTH-1:0]  cnt_a,        // debug：目前計數值 (可回讀觀察)
    output wire [CNT_WIDTH-1:0]  cnt_b,
    output wire [CNT_WIDTH-1:0]  cnt_c
);

    // -------------------------------------------------------------------------
    // 三隻獨立的 down-counter。行為三條規則：
    //   1) enable=0 或 reset → 保持滿載 (reload_val)，不會 timeout
    //   2) kick → 重新載入 reload_val (餵狗)
    //   3) 沒被餵 → 每個 clock 減 1；數到 0 停在 0，timeout 持續拉高
    // 顯式寫三份 (而非 generate)，方便論文逐隻說明與波形對照。
    // -------------------------------------------------------------------------
    reg [CNT_WIDTH-1:0] counter_a, counter_b, counter_c;

    // --- WDT A ---
    always @(posedge clk) begin
        if (!rstn)                                counter_a <= reload_val;
        else if (!enable)                         counter_a <= reload_val;
        else if (kick_a)                          counter_a <= reload_val;
        else if (counter_a != {CNT_WIDTH{1'b0}})  counter_a <= counter_a - 1'b1;
    end

    // --- WDT B ---
    always @(posedge clk) begin
        if (!rstn)                                counter_b <= reload_val;
        else if (!enable)                         counter_b <= reload_val;
        else if (kick_b)                          counter_b <= reload_val;
        else if (counter_b != {CNT_WIDTH{1'b0}})  counter_b <= counter_b - 1'b1;
    end

    // --- WDT C ---
    always @(posedge clk) begin
        if (!rstn)                                counter_c <= reload_val;
        else if (!enable)                         counter_c <= reload_val;
        else if (kick_c)                          counter_c <= reload_val;
        else if (counter_c != {CNT_WIDTH{1'b0}})  counter_c <= counter_c - 1'b1;
    end

    // timeout = 已啟用 且 counter 歸零
    assign to_a = enable & (counter_a == {CNT_WIDTH{1'b0}});
    assign to_b = enable & (counter_b == {CNT_WIDTH{1'b0}});
    assign to_c = enable & (counter_c == {CNT_WIDTH{1'b0}});

    // -------------------------------------------------------------------------
    // Majority Voter
    //   maj_2of3：至少兩隻 timeout → 第一關判定「真的有問題」(濾掉單隻誤報)
    //   all_3   ：三隻全 timeout   → 第二關判定「Linux 整個死了」→ 觸發 FIQ
    // -------------------------------------------------------------------------
    assign maj_2of3 = (to_a & to_b) | (to_b & to_c) | (to_a & to_c);
    assign all_3    =  to_a & to_b & to_c;

    assign cnt_a = counter_a;
    assign cnt_b = counter_b;
    assign cnt_c = counter_c;

endmodule
