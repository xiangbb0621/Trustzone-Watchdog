/*
 * Copyright (c) 2026, TrustZone Watchdog Project.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * zynqmp_swdt.c - TrustZone Layer-3 SWDT heartbeat via TTC2 Counter 0
 *
 * ZCU102 SWDT (0xFF150000) is locked by PMUFW after boot; APU writes are
 * rejected even from EL3.  Instead we update PMU_GLOBAL_GEN_STORAGE4 every
 * ~2 s.  PMUFW monitors that register and controls SWDT accordingly.
 *
 * Timer source: TTC2 Counter 0, IRQ 74, configured as GIC Group 0 (EL3 FIQ).
 * Handler is registered via request_intr_type_el3() so it is dispatched by
 * rdo_el3_interrupt_handler() regardless of current security state.
 *
 * Requires ZYNQMP_WDT_RESTART=1 (enables the per-IRQ EL3 dispatch table).
 */

#include <arch_helpers.h>
#include <common/debug.h>
#include <lib/mmio.h>
#include <plat/common/platform.h>
#include <bl31/interrupt_mgmt.h>

#include <zynqmp_def.h>
#include <plat_private.h>

static volatile uint32_t ttc2_hb_seq;

/* ── TTC2 interrupt handler (EL3, Group 0 FIQ) ──────────────────────────── */

static uint64_t ttc2_fiq_handler(uint32_t id __unused,
				  uint32_t flags __unused,
				  void *handle __unused,
				  void *cookie __unused)
{
	/* Read-to-clear interrupt status */
	(void)mmio_read_32(TTC2_ISR_0);

	/*
	 * Bump the heartbeat counter that PMUFW polls.
	 *
	 * IMPORTANT: do NOT use PMU GEN_STORAGE4 (0xFFD80040) — that register
	 * is reserved by the ZynqMP restart framework (restart scope bits[4:3],
	 * boot-health bit0) and is read by TF-A's ZYNQMP_WDT_RESTART path.
	 * Writing our counter there corrupts the restart scope and breaks
	 * reboot. Use a free TTC2 scratch register instead (LPD, PMU-readable).
	 *   0xFF130048 = TTC2 Counter0 Match_3 (unused in interval mode).
	 */
	mmio_write_32(TTC2_BASE_ADDR + 0x48U, ++ttc2_hb_seq);

	/* Debug beacon (Linux-readable) – confirms this handler is running */
	mmio_write_32(TTC2_BASE_ADDR + 0x30U, ttc2_hb_seq);

	plat_ic_end_of_interrupt(IRQ_TTC2_0);

	return 0;
}

/* ── TTC2 hardware initialisation ───────────────────────────────────────── */

static void ttc2_hw_init(void)
{
	/*
	 * Correct RST sequence: counter MUST be disabled (DIS=1) before
	 * writing RST=1, otherwise the reset is ignored and the counter
	 * continues from its current (possibly residual) value.
	 */

	/* 1. Disable counter */
	mmio_write_32(TTC2_CNT_CNTRL_0, TTC_CNTRL_DIS);

	/* 2. Configure prescaler: PS_EN=1, PS_VAL=15 → divide by 2^16 ≈ 1526 Hz */
	mmio_write_32(TTC2_CLK_CNTRL_0,
		      TTC_CLK_PS_EN | (TTC2_CLK_PS_VAL << TTC_CLK_PS_VAL_SHIFT));

	/* 3. Reset counter to 0 while disabled (RST self-clears after write) */
	mmio_write_32(TTC2_CNT_CNTRL_0, TTC_CNTRL_DIS | TTC_CNTRL_RST);

	/* 4. Set ~2 s interval */
	mmio_write_32(TTC2_INTERVAL_VAL_0, TTC2_INTERVAL_TICKS);

	/* 5. Clear any stale interrupt status */
	(void)mmio_read_32(TTC2_ISR_0);

	/* 6. Enable interval interrupt */
	mmio_write_32(TTC2_IER_0, 0x1U);

	/* 7. Start in interval mode (DIS=0, INT=1) */
	mmio_write_32(TTC2_CNT_CNTRL_0, TTC_CNTRL_INT_MODE);
}

/* ── Public entry point – called from bl31_plat_runtime_setup() ─────────── */

void zynqmp_swdt_init(void)
{
	int ret;

	/*
	 * Configure IRQ 74 as Group 0 (EL3 FIQ) in GIC before starting the
	 * counter.  If the GIC is not set up first, the first interrupt fires
	 * as NS Group 1 and goes to Linux (which hasn't booted yet) → hang.
	 */
	plat_ic_set_interrupt_type(IRQ_TTC2_0, INTR_TYPE_EL3);
	plat_ic_set_interrupt_priority(IRQ_TTC2_0, TTC2_IRQ_PRIORITY);
	/*
	 * Route the SPI to a CPU interface. Without this, GICD_ITARGETSR[74]
	 * stays 0 and the GIC never forwards the interrupt to any core —
	 * TTC2 ISR latches but no FIQ is ever delivered. Route to ANY CPU so
	 * whichever core is running takes the FIQ to EL3.
	 */
	plat_ic_set_spi_routing(IRQ_TTC2_0, INTR_ROUTING_MODE_ANY, 0U);
	plat_ic_enable_interrupt(IRQ_TTC2_0);

	/* Register per-IRQ handler in the EL3 dispatch table */
	ret = request_intr_type_el3(IRQ_TTC2_0, ttc2_fiq_handler);
	if (ret != 0) {
		WARN("SWDT: TTC2 handler registration failed (%d)\n", ret);
		return;
	}

	/* Start hardware only after GIC + handler are ready */
	ttc2_hw_init();

	/*
	 * EL3-side GIC readback (Secure view = real state).
	 * IRQ 74 lives in register index 74/32 = 2, bit 74%32 = 10.
	 *   IGROUPR[2]   @ GICD + 0x080 + (2*4) = 0xF9010088
	 *   ISENABLER[2] @ GICD + 0x100 + (2*4) = 0xF9010108
	 * Remove this block once the path is confirmed.
	 */
	INFO("SWDT-DBG: GICD_CTLR=0x%x GICC_CTLR=0x%x\n",
	     mmio_read_32(BASE_GICD_BASE + 0x0U),
	     mmio_read_32(BASE_GICC_BASE + 0x0U));
	INFO("SWDT-DBG: IGROUPR[2]=0x%x ISENABLER[2]=0x%x (IRQ74 = bit10)\n",
	     mmio_read_32(BASE_GICD_BASE + 0x088U),
	     mmio_read_32(BASE_GICD_BASE + 0x108U));
	INFO("SWDT-DBG: SCR_EL3=0x%llx (FIQ bit2 must be 1)\n",
	     (unsigned long long)read_scr_el3());

	INFO("SWDT: Layer-3 active – TTC2 heartbeat ~2 s, IRQ %u → GEN_STORAGE4\n",
	     IRQ_TTC2_0);
}
