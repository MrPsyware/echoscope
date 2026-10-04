// LodePNG uses PSRAM exclusively so map decoding cannot exhaust TLS/internal RAM.
#include <esp_heap_caps.h>
#include <stdlib.h>
void *lodepng_malloc(size_t n) { return n<=768*1024?heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL; }
void *lodepng_realloc(void *p,size_t n) { return n<=768*1024?heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL; }
void lodepng_free(void *p) { free(p); }
