#include "sip_memory_diag.h"

#include <stdatomic.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "SIP_MEM";
static atomic_uint s_failure_count;
static atomic_size_t s_failed_size;
static atomic_uint s_failed_caps;
static _Atomic(const char *) s_failed_api;

/* This callback may execute in a critical allocator path; only store metadata. */
static void sip_alloc_failed_hook(size_t bytes, uint32_t caps, const char *api)
{
    atomic_store_explicit(&s_failed_size, bytes, memory_order_relaxed);
    atomic_store_explicit(&s_failed_caps, caps, memory_order_relaxed);
    atomic_store_explicit(&s_failed_api, api, memory_order_relaxed);
    atomic_fetch_add_explicit(&s_failure_count, 1, memory_order_release);
}

void sip_memory_diag_init(void)
{
    esp_err_t ret = heap_caps_register_failed_alloc_callback(sip_alloc_failed_hook);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Cannot install heap-failure callback: %s", esp_err_to_name(ret));
    }
    sip_memory_diag_snapshot("boot");
}

void sip_memory_diag_snapshot(const char *stage)
{
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const uint32_t dma = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    ESP_LOGW(TAG, "%s internal free=%u largest=%u minimum=%u",
             stage, (unsigned)heap_caps_get_free_size(internal),
             (unsigned)heap_caps_get_largest_free_block(internal),
             (unsigned)heap_caps_get_minimum_free_size(internal));
    ESP_LOGW(TAG, "%s psram free=%u largest=%u | int-dma free=%u largest=%u",
             stage, (unsigned)heap_caps_get_free_size(psram),
             (unsigned)heap_caps_get_largest_free_block(psram),
             (unsigned)heap_caps_get_free_size(dma),
             (unsigned)heap_caps_get_largest_free_block(dma));
    ESP_LOGW(TAG, "%s malloc-default free=%u largest=%u",
             stage, (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
}

void sip_memory_diag_report_failures(void)
{
    static unsigned last_reported;
    unsigned n = atomic_load_explicit(&s_failure_count, memory_order_acquire);
    if (n == last_reported) return;
    last_reported = n;
    const char *api = atomic_load_explicit(&s_failed_api, memory_order_relaxed);
    ESP_LOGE(TAG, "IDF allocator failures=%u latest size=%u caps=0x%08x api=%s",
             n, (unsigned)atomic_load_explicit(&s_failed_size, memory_order_relaxed),
             atomic_load_explicit(&s_failed_caps, memory_order_relaxed),
             api ? api : "(unknown)");
    sip_memory_diag_snapshot("allocation failure");
}
