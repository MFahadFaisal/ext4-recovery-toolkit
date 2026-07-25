#ifndef FILE_SIGNATURES_H
#define FILE_SIGNATURES_H

#include <stdint.h>
#include <string.h>

typedef struct {
    const char *name;
    const uint8_t *magic;
    int magic_len;
    const uint8_t *footer;   // NULL if no reliable footer
    int footer_len;
    const char *ext;
} file_signature_t;

static const uint8_t SIG_PNG[]   = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t SIG_PNG_END[] = {0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82}; // IEND chunk + CRC

static const uint8_t SIG_JPEG[]  = {0xFF, 0xD8, 0xFF};
static const uint8_t SIG_JPEG_END[] = {0xFF, 0xD9};

static const uint8_t SIG_PDF[]   = {0x25, 0x50, 0x44, 0x46}; // %PDF
static const uint8_t SIG_PDF_END[] = {0x25, 0x25, 0x45, 0x4F, 0x46}; // %%EOF

static const uint8_t SIG_ZIP[]   = {0x50, 0x4B, 0x03, 0x04}; // PK\x03\x04
static const uint8_t SIG_ZIP_END[] = {0x50, 0x4B, 0x05, 0x06}; // PK\x05\x06 (end of central dir)

static const uint8_t SIG_GIF[]   = {0x47, 0x49, 0x46, 0x38}; // GIF8

static const file_signature_t SIGNATURES[] = {
    { "PNG",  SIG_PNG,  sizeof(SIG_PNG),  SIG_PNG_END,  sizeof(SIG_PNG_END),  "png" },
    { "JPEG", SIG_JPEG, sizeof(SIG_JPEG), SIG_JPEG_END, sizeof(SIG_JPEG_END), "jpg" },
    { "PDF",  SIG_PDF,  sizeof(SIG_PDF),  SIG_PDF_END,  sizeof(SIG_PDF_END),  "pdf" },
    { "ZIP",  SIG_ZIP,  sizeof(SIG_ZIP),  SIG_ZIP_END,  sizeof(SIG_ZIP_END),  "zip" },
    { "GIF",  SIG_GIF,  sizeof(SIG_GIF),  NULL, 0, "gif" },
};
#define NUM_SIGNATURES (sizeof(SIGNATURES) / sizeof(SIGNATURES[0]))

// Search for a signature's magic bytes at the start of a buffer
static inline int match_signature(uint8_t *buf, size_t buf_len, const file_signature_t *sig) {
    if (buf_len < (size_t)sig->magic_len) return 0;
    return memcmp(buf, sig->magic, sig->magic_len) == 0;
}

// Search for a footer anywhere within a buffer, return offset+footer_len if found, -1 if not
static inline long find_footer(uint8_t *buf, size_t buf_len, const uint8_t *footer, int footer_len) {
    if (!footer || buf_len < (size_t)footer_len) return -1;
    for (size_t i = 0; i + footer_len <= buf_len; i++) {
        if (memcmp(buf + i, footer, footer_len) == 0) {
            return (long)(i + footer_len);
        }
    }
    return -1;
}

#endif
