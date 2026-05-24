#ifndef DOBBY_HOOK_MANAGER_H
#define DOBBY_HOOK_MANAGER_H

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

#include "dobby.h"

// Project-level wrapper for the updated Dobby Android API.
// Public helpers keep the old project call style (`DobbyHookManaged(...)`) while
// routing installs/uninstalls through DobbyAndroidHookFunction / DobbyAndroidUnhook.

struct DobbyManagedHookRecord {
  void *target;
  void *replacement;
  void **origin_slot;
  void *origin;
  int status;
  DobbyHookBackend backend;
};

inline std::mutex &DobbyManagedHookMutex() {
  static std::mutex mutex;
  return mutex;
}

inline std::vector<DobbyManagedHookRecord> &DobbyManagedHookRecords() {
  static std::vector<DobbyManagedHookRecord> records;
  return records;
}

inline bool DobbyManagedIsHooked(void *target) {
  return target && DobbyAndroidIsHooked(target) > 0;
}

inline std::vector<DobbyManagedHookRecord>::iterator DobbyFindManagedHookRecordUnlocked(void *target) {
  auto &records = DobbyManagedHookRecords();
  return std::find_if(records.begin(), records.end(), [target](const DobbyManagedHookRecord &record) {
    return record.target == target;
  });
}

inline DobbyManagedHookRecord *DobbyFindManagedHookRecord(void *target) {
  if (!target) return nullptr;
  std::lock_guard<std::mutex> lock(DobbyManagedHookMutex());
  auto &records = DobbyManagedHookRecords();
  auto it = DobbyFindManagedHookRecordUnlocked(target);
  return it == records.end() ? nullptr : &(*it);
}

inline int DobbyFinishManagedHook(void *target,
                                  void *replacement,
                                  void **origin_slot,
                                  void *origin,
                                  int status,
                                  DobbyHookBackend backend) {
  if (status != DOBBY_ANDROID_OK) return status;
  if (!target) return DOBBY_ANDROID_OK;

  auto &records = DobbyManagedHookRecords();
  auto it = DobbyFindManagedHookRecordUnlocked(target);
  if (it == records.end()) {
    records.push_back({target, replacement, origin_slot, origin, status, backend});
  } else {
    it->replacement = replacement;
    it->origin_slot = origin_slot;
    it->origin = origin;
    it->status = status;
    it->backend = backend;
  }
  return DOBBY_ANDROID_OK;
}

inline int DobbyHookManagedBackend(void *target, void *replacement, void **origin_slot, DobbyHookBackend backend) {
  if (!target || !replacement) return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;

  std::lock_guard<std::mutex> lock(DobbyManagedHookMutex());
  auto &records = DobbyManagedHookRecords();
  auto it = DobbyFindManagedHookRecordUnlocked(target);

  if (it != records.end() && DobbyManagedIsHooked(target)) {
    if (origin_slot) *origin_slot = it->origin;
    return it->replacement == replacement ? DOBBY_ANDROID_OK : DOBBY_ANDROID_ERR_ALREADY_HOOKED;
  }

  void *origin = nullptr;
  void **origin_receiver = origin_slot ? origin_slot : &origin;
  int status = DobbyAndroidHookBackend(target, replacement, origin_receiver, backend);
  if (status == DOBBY_ANDROID_OK && origin_slot) origin = *origin_slot;

  return DobbyFinishManagedHook(target, replacement, origin_slot, origin, status, backend);
}

inline int DobbyHookManaged(void *target, void *replacement, void **origin_slot) {
  return DobbyHookManagedBackend(target, replacement, origin_slot, DOBBY_HOOK_BACKEND_AUTO);
}

inline int DobbyHookSymbolManagedBackend(const char *image_name,
                                         const char *symbol_name,
                                         void *replacement,
                                         void **origin_slot,
                                         DobbyHookBackend backend) {
  if (!symbol_name || !replacement) return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;

  std::lock_guard<std::mutex> lock(DobbyManagedHookMutex());

  void *target = DobbyAndroidFindSymbol(image_name, symbol_name);
  if (target) {
    auto it = DobbyFindManagedHookRecordUnlocked(target);
    if (it != DobbyManagedHookRecords().end() && DobbyManagedIsHooked(target)) {
      if (origin_slot) *origin_slot = it->origin;
      return it->replacement == replacement ? DOBBY_ANDROID_OK : DOBBY_ANDROID_ERR_ALREADY_HOOKED;
    }
  }

  void *origin = nullptr;
  void **origin_receiver = origin_slot ? origin_slot : &origin;
  int status = DobbyAndroidHookSymbolBackend(image_name, symbol_name, replacement, origin_receiver, backend);
  if (status == DOBBY_ANDROID_OK) {
    if (!target) target = DobbyAndroidFindSymbol(image_name, symbol_name);
    if (origin_slot) origin = *origin_slot;
    if (target) return DobbyFinishManagedHook(target, replacement, origin_slot, origin, status, backend);
  }
  return status;
}

inline int DobbyHookSymbolManaged(const char *image_name,
                                  const char *symbol_name,
                                  void *replacement,
                                  void **origin_slot) {
  return DobbyHookSymbolManagedBackend(image_name, symbol_name, replacement, origin_slot, DOBBY_HOOK_BACKEND_AUTO);
}

inline int DobbyHookOffsetManaged(const char *image_name,
                                  uintptr_t offset,
                                  void *replacement,
                                  void **origin_slot) {
  if (!image_name || !replacement) return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;

  std::lock_guard<std::mutex> lock(DobbyManagedHookMutex());
  uintptr_t base = DobbyAndroidGetModuleBase(image_name);
  void *target = base ? reinterpret_cast<void *>(base + offset) : nullptr;

  if (target) {
    auto it = DobbyFindManagedHookRecordUnlocked(target);
    if (it != DobbyManagedHookRecords().end() && DobbyManagedIsHooked(target)) {
      if (origin_slot) *origin_slot = it->origin;
      return it->replacement == replacement ? DOBBY_ANDROID_OK : DOBBY_ANDROID_ERR_ALREADY_HOOKED;
    }
  }

  void *origin = nullptr;
  void **origin_receiver = origin_slot ? origin_slot : &origin;
  int status = DobbyAndroidHookOffset(image_name, offset, replacement, origin_receiver);
  if (status == DOBBY_ANDROID_OK) {
    if (!target) {
      base = DobbyAndroidGetModuleBase(image_name);
      target = base ? reinterpret_cast<void *>(base + offset) : nullptr;
    }
    if (origin_slot) origin = *origin_slot;
    if (target) return DobbyFinishManagedHook(target, replacement, origin_slot, origin, status, DOBBY_HOOK_BACKEND_AUTO);
  }
  return status;
}

inline int DobbyWaitAndHookManaged(const char *image_name,
                                   const char *symbol_name,
                                   void *replacement,
                                   void **origin_slot,
                                   uint32_t timeout_ms) {
  if (!symbol_name || !replacement) return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;

  int status = DobbyWaitAndHook(image_name, symbol_name, replacement, origin_slot, timeout_ms);
  if (status == DOBBY_ANDROID_OK || status == DOBBY_AUTOHOOK_STATUS_INSTALLED) {
    void *target = DobbyAndroidFindSymbol(image_name, symbol_name);
    void *origin = origin_slot ? *origin_slot : nullptr;
    std::lock_guard<std::mutex> lock(DobbyManagedHookMutex());
    return DobbyFinishManagedHook(target, replacement, origin_slot, origin, DOBBY_ANDROID_OK, DOBBY_HOOK_BACKEND_AUTO);
  }
  return status;
}

inline int DobbyWaitAndHookOffsetManaged(const char *image_name,
                                         uintptr_t offset,
                                         void *replacement,
                                         void **origin_slot,
                                         uint32_t timeout_ms) {
  if (!image_name || !replacement) return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;

  int status = DobbyWaitAndHookOffset(image_name, offset, replacement, origin_slot, timeout_ms);
  if (status == DOBBY_ANDROID_OK || status == DOBBY_AUTOHOOK_STATUS_INSTALLED) {
    uintptr_t base = DobbyAndroidGetModuleBase(image_name);
    void *target = base ? reinterpret_cast<void *>(base + offset) : nullptr;
    void *origin = origin_slot ? *origin_slot : nullptr;
    std::lock_guard<std::mutex> lock(DobbyManagedHookMutex());
    return DobbyFinishManagedHook(target, replacement, origin_slot, origin, DOBBY_ANDROID_OK, DOBBY_HOOK_BACKEND_AUTO);
  }
  return status;
}

inline int DobbyUnhookManaged(void *target) {
  if (!target) return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;

  std::lock_guard<std::mutex> lock(DobbyManagedHookMutex());
  auto &records = DobbyManagedHookRecords();
  auto it = DobbyFindManagedHookRecordUnlocked(target);

  int status = DobbyAndroidUnhook(target);
  if (status == DOBBY_ANDROID_OK || !DobbyManagedIsHooked(target)) {
    if (it != records.end()) records.erase(it);
  }
  return status;
}

inline void DobbyDestroyAllManagedHooks() {
  std::lock_guard<std::mutex> lock(DobbyManagedHookMutex());
  auto &records = DobbyManagedHookRecords();
  for (auto &record : records) {
    if (record.target && DobbyManagedIsHooked(record.target)) {
      DobbyAndroidUnhook(record.target);
    }
  }
  records.clear();
}

#endif // DOBBY_HOOK_MANAGER_H
