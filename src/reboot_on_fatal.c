/*
 * Reboot instead of halting on a fatal error (Zephyr 3.5 has no Kconfig for
 * this; the default handler spins with interrupts locked). With stack
 * protection on, an overflow or other fault then costs a ~1 s reboot and
 * reconnect instead of a frozen dongle that has to be replugged.
 */
#include <zephyr/fatal.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf) {
    ARG_UNUSED(reason);
    ARG_UNUSED(esf);
    LOG_PANIC();
    sys_reboot(SYS_REBOOT_COLD);
    CODE_UNREACHABLE;
}
