#ifndef IH_SETTINGS_H
#define IH_SETTINGS_H
#include "ih_protection.h"
int Settings_Load(ProtectionParams *params);
int Settings_Save(const ProtectionParams *params);
#endif
