/*
 * swdt_monitor.c  –  Layer-3 TrustZone health monitor for PMUFW
 *
 * TF-A (EL3) writes an incrementing heartbeat counter to a free TTC2 scratch
 * register every ~2 s. PMUFW polls it; while it advances, TrustZone is alive.
 * When it freezes (TrustZone dead), PMUFW recovers the system (Layer 3 =
 * reset BOTH PS and PL, i.e. a cold reboot). TriggerReset() does, in order:
 *   1. Clear the PL via PCAP (PROG_B pulse) — a warm reset leaves the PL
 *      configured and FSBL hangs re-loading the bitstream; clearing it first
 *      lets FSBL reprogram a blank PL.
 *   2. PmMasterFsm(APU, FORCE_DOWN) — power down the APU cores via PMU ROM so
 *      the surviving PMU's power state is clean; otherwise Linux SMP bring-up
 *      hangs after the reboot (forced reset gives Linux no chance to offline
 *      its secondary cores).
 *   3. XPfw_ResetSystem() — MUST be this, NOT XPfw_ResetPsOnly(): ResetPsOnly
 *      gates the PL PROG signal and isolates the PS↔PL AXI to *preserve* the
 *      PL, which blocks FSBL from re-programming it → the reloaded PL's AXI
 *      GPIO never responds → Linux hangs in xgpio_of_probe. (error→POR was
 *      also tried; on this board it only lights the PS error LED and hangs.)
 *
 * Place this file in your Vitis PMUFW project under src/.
 * Call SwdtMonitorInit() from XPfw_UserStartUp() in xpfw_user_startup.c.
 */

#include "xpfw_config.h"
#include "xpfw_core.h"
#include "xpfw_module.h"
#include "xpfw_default.h"   /* XPfw_Write32/Read32/RMW32 */
#include "xpfw_resets.h"    /* XPfw_ResetSystem() */
#include "pm_master.h"      /* PmMasterFsm() + pmMasterApu_g (needs ENABLE_PM) */
#include "xil_io.h"

/* ── TF-A heartbeat register (free TTC2 scratch; NOT GEN_STORAGE4) ──────── */
#define HEARTBEAT_ADDR          0xFF130048U

/* ── Linux-readable beacons (TTC2 scratch) ──────────────────────────────── */
#define DBG_CB_COUNT_ADDR       0xFF13003CU
#define DBG_HB_SEEN_ADDR        0xFF130040U
#define DBG_MISS_ADDR           0xFF130044U

/* ── PL clear via CSU PCAP ──────────────────────────────────────────────── */
#define CRL_APB_PCAP_CLK_CTRL   0xFF5E00A4U
#define PCAP_CLK_EN             0x01000000U
#define CSU_PCAP_PROG           0xFFCA3000U
#define PCAP_PROG_PL_ASSERT     0x0U
#define PCAP_PROG_PL_RELEASE    0x1U

/* ── Timing (clock-independent software detection) ──────────────────────── */
#define MONITOR_PERIOD_MS       2000U
#define MISS_THRESHOLD          8U

/* ── Module state ────────────────────────────────────────────────────── */
static const XPfw_Module_t *SwdtMod;
static u32  PrevHbVal;
static u32  MissedCount;
static u8   Armed;

/* Layer-3 recovery: clear PL, force APU down, then full system reset. */
static void TriggerReset(void)
{
    volatile u32 d;

    /* 1. Wipe the PL so FSBL reprograms a blank PL on the reboot. */
    XPfw_RMW32(CRL_APB_PCAP_CLK_CTRL, PCAP_CLK_EN, PCAP_CLK_EN);
    Xil_Out32(CSU_PCAP_PROG, PCAP_PROG_PL_ASSERT);
    for (d = 0U; d < 200000U; d++) {
    }
    Xil_Out32(CSU_PCAP_PROG, PCAP_PROG_PL_RELEASE);

    /* 2. Force the APU PM state down (procs OFF, master KILLED) so the PMU's
     *    power-state view is clean. A forced reset doesn't let Linux offline
     *    its secondary cores, so without this the surviving PMU still thinks
     *    CPU1-3 are ON and Linux SMP bring-up hangs after the reboot. */
    (void)PmMasterFsm(&pmMasterApu_g, PM_MASTER_EVENT_FORCE_DOWN);

    /* 3. System reset (NOT PS-only). XPfw_ResetPsOnly() gates the PL PROG
     *    signal and isolates the PS↔PL AXI to *preserve* the PL — which
     *    blocks FSBL from re-programming it, so the reloaded PL's AXI GPIO
     *    never responds and Linux hangs in xgpio_of_probe. ResetSystem does
     *    neither, letting FSBL reprogram the PL cleanly on the reboot.
     *    ResetSystem does not reset the PMU, so clear our monitor state first
     *    to hand the next boot a clean slate (avoid re-trigger before TF-A is
     *    back up). */
    Xil_Out32(HEARTBEAT_ADDR, 0U);
    PrevHbVal   = 0U;
    MissedCount = 0U;
    Armed       = 0U;

    XPfw_ResetSystem();
    /* not reached */
}

/* ── Periodic callback (every MONITOR_PERIOD_MS) ────────────────────────── */
static void SwdtMonitorCallback(const XPfw_Module_t *Module, u32 EventId)
{
    static u32 CbCount;
    u32 CurrentHb;

    (void)Module;
    (void)EventId;

    CurrentHb = Xil_In32(HEARTBEAT_ADDR);

    Xil_Out32(DBG_CB_COUNT_ADDR, ++CbCount);
    Xil_Out32(DBG_HB_SEEN_ADDR, CurrentHb);

    if (Armed == 0U) {
        if (CurrentHb != 0U) {
            Armed     = 1U;
            PrevHbVal = CurrentHb;
        }
        return;
    }

    if (CurrentHb != PrevHbVal) {
        PrevHbVal   = CurrentHb;
        MissedCount = 0U;
    } else {
        MissedCount++;
        Xil_Out32(DBG_MISS_ADDR, MissedCount);

        if (MissedCount >= MISS_THRESHOLD) {
            TriggerReset();
            /* not reached */
        }
    }
}

/* ── Public init – call from XPfw_UserStartUp() ─────────────────────── */
void SwdtMonitorInit(void)
{
    SwdtMod = XPfw_CoreCreateMod();
    if (SwdtMod == NULL) {
        return;
    }

    PrevHbVal   = 0U;
    MissedCount = 0U;
    Armed       = 0U;

    (void)XPfw_CoreScheduleTask(SwdtMod, MONITOR_PERIOD_MS,
                                SwdtMonitorCallback);
}
