#include "main.h"
#include "ih_settings.h"
#include <string.h>
#include <stddef.h>

/* Last 8 KiB are excluded from the linker. Alternate 4 KiB slots so a failed
 * write leaves the preceding valid copy available. Works with 2/4 KiB pages.
 * Only call Save with the gate disabled, outputs/counters stopped, RX idle. */
#define SLOT0 0x0807E000UL
#define SLOT1 0x0807F000UL
#define MAGIC 0x49485031UL
typedef struct {
  uint32_t magic, version, sequence, reserved;
  ProtectionParams params;
  uint32_t crc, padding;
} SettingsRecord;
typedef char SettingsRecordSizeCheck[(sizeof(SettingsRecord) == 48U) ? 1 : -1];

static uint32_t Crc(const SettingsRecord *r)
{
  const uint8_t *p = (const uint8_t *)r;
  uint32_t crc = 0xFFFFFFFFU, i, bit;
  for (i = 0; i < offsetof(SettingsRecord, crc); ++i) {
    crc ^= p[i];
    for (bit = 0; bit < 8U; ++bit)
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
  }
  return ~crc;
}
static int Valid(const SettingsRecord *r)
{
  return r->magic == MAGIC && r->version == 1U && r->crc == Crc(r) &&
         Protection_ParamsValid(&r->params);
}
static const SettingsRecord *Latest(void)
{
  const SettingsRecord *a = (const SettingsRecord *)SLOT0;
  const SettingsRecord *b = (const SettingsRecord *)SLOT1;
  if (!Valid(a)) return Valid(b) ? b : NULL;
  if (!Valid(b)) return a;
  return (int32_t)(b->sequence - a->sequence) > 0 ? b : a;
}
int Settings_Load(ProtectionParams *params)
{
  const SettingsRecord *r = Latest();
  if (r == NULL) return 0;
  *params = r->params; return 1;
}
int Settings_Save(const ProtectionParams *params)
{
  const SettingsRecord *old = Latest();
  SettingsRecord r;
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t address, error, offset;
  uint64_t word;
  int ok = 1;
  if (!Protection_ParamsValid(params) || FLASH_SIZE != 0x80000U ||
      (FLASH->OPTR & FLASH_OPTR_BFB2) != 0U || (SYSCFG->MEMRMP & SYSCFG_MEMRMP_FB_MODE) != 0U)
    return 0; /* explicit 512 KiB normal-bank mapping only */
  if (old != NULL && memcmp(params, &old->params, sizeof(*params)) == 0) return 1;
  address = ((uint32_t)old == SLOT0) ? SLOT1 : SLOT0;
  memset(&r, 0, sizeof(r));
  r.magic = MAGIC; r.version = 1U; r.sequence = old ? old->sequence + 1U : 1U;
  r.params = *params; r.crc = Crc(&r);
  erase.TypeErase = FLASH_TYPEERASE_PAGES; erase.NbPages = 1U;
  if (FLASH->OPTR & FLASH_OPTR_DBANK) {
    erase.Banks = FLASH_BANK_2;
    erase.Page = (address - FLASH_BASE - 0x40000U) / FLASH_PAGE_SIZE;
  } else {
    erase.Banks = FLASH_BANK_1;
    erase.Page = (address - FLASH_BASE) / FLASH_PAGE_SIZE_128_BITS;
  }
  if (HAL_FLASH_Unlock() != HAL_OK) return 0;
  if (HAL_FLASHEx_Erase(&erase, &error) != HAL_OK) ok = 0;
  for (offset = 0U; ok && offset < sizeof(r); offset += 8U) {
    memcpy(&word, (const uint8_t *)&r + offset, sizeof(word));
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address + offset, word) != HAL_OK) ok = 0;
  }
  HAL_FLASH_Lock();
  return ok && Valid((const SettingsRecord *)address);
}
