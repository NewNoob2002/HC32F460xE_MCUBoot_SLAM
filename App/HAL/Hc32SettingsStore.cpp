#include "SettingsStore.h"
extern "C" {
#include <flashdb.h>
}

namespace {
// Only linked into Application; Boot opens product_kv exclusively.
fdb_kvdb settings_db{};
constexpr char SettingsKey[] = "app.settings";
} // namespace
namespace hal {
bool Hc32SettingsStore::init() {
    ready_ = fdb_kvdb_init(&settings_db, "app", "app_kv", nullptr, nullptr) == FDB_NO_ERR;
    return ready_;
}
bool Hc32SettingsStore::load(uint8_t* data, std::size_t capacity, std::size_t& length) {
    length = 0U;
    if (!ready_ || !data || capacity == 0U)
        return false;
    fdb_blob blob{};
    length = fdb_kv_get_blob(&settings_db, SettingsKey, fdb_blob_make(&blob, data, capacity));
    return length != 0U && length == blob.saved.len;
}
bool Hc32SettingsStore::save(const uint8_t* data, std::size_t length) {
    if (!ready_ || !data || length == 0U)
        return false;
    fdb_blob blob{};
    return fdb_kv_set_blob(&settings_db, SettingsKey, fdb_blob_make(&blob, data, length)) == FDB_NO_ERR;
}
} // namespace hal
