#pragma once

/* Lightweight, non-invasive ESP-IDF heap diagnostics.
 * Do not log from the allocation-failure callback itself.
 */
void sip_memory_diag_init(void);
void sip_memory_diag_snapshot(const char *stage);
void sip_memory_diag_report_failures(void);
