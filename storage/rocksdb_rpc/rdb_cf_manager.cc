/*
   Copyright (c) 2014, SkySQL Ab

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation  // gcc: Class implementation
#endif

#include "./debug_sync.h"

#include "./rdb_threads.h"
/* This C++ files header file */
#include "./rdb_cf_manager.h"

/* MyRocks header files */
#include <string>
#include <vector>
#include "./ha_rocksdb.h"
#include "./ha_rocksdb_proto.h"
#include "./rdb_datadic.h"
#include "./rdb_psi.h"

#include "rpcclient.hpp"

namespace myrocks_rpc {

/* Check if ColumnFamily name says it's a reverse-ordered CF */
bool Rdb_cf_manager::is_cf_name_reverse(const char *const name) {
  /* nullptr means the default CF is used.. (TODO: can the default CF be
   * reverse?) */
  return (name && !strncmp(name, "rev:", 4));
}

void Rdb_cf_manager::init(
    std::unique_ptr<Rdb_cf_options> &&cf_options,
    std::vector<rocksdb::ColumnFamilyHandle *> *const handles) {
  mysql_mutex_init(rdb_cfm_mutex_key, &m_mutex, MY_MUTEX_INIT_FAST);

  DBUG_ASSERT(cf_options != nullptr);
  DBUG_ASSERT(handles != nullptr);
  DBUG_ASSERT(handles->size() > 0);

  m_cf_options = std::move(cf_options);

  for (auto cfh_ptr : *handles) {
    DBUG_ASSERT(cfh_ptr != nullptr);

    // ALTER
    // std::shared_ptr<rocksdb::ColumnFamilyHandle> cfh(cfh_ptr);
    // m_cf_name_map[cfh_ptr->GetName()] = cfh;
    // m_cf_id_map[cfh_ptr->GetID()] = cfh;
    m_cf_name_map[rocksdb_ColumnFamilyHandle__GetName(cfh_ptr)] = cfh_ptr;
    m_cf_id_map[rocksdb_ColumnFamilyHandle__GetID(cfh_ptr)] = cfh_ptr;
  }
}

void Rdb_cf_manager::cleanup() {
  m_cf_name_map.clear();
  m_cf_id_map.clear();
  mysql_mutex_destroy(&m_mutex);
  m_cf_options = nullptr;
}

/*
  @brief
  Find column family by name. If it doesn't exist, create it

  @detail
    See Rdb_cf_manager::get_cf
*/
rocksdb::ColumnFamilyHandle *Rdb_cf_manager::get_or_create_cf(
    rocksdb::DB *const rdb, const std::string &cf_name) {
  rocksdb_rpc_log(85,
                  "rocksdb_init_func: Rdb_cf_manager::get_or_create_cf start");

  DBUG_ASSERT(rdb != nullptr);
  DBUG_ASSERT(!cf_name.empty());
  std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle;
  rocksdb::ColumnFamilyHandle *cf_handle_ = nullptr;

  if (cf_name == PER_INDEX_CF_NAME) {
    // per-index column families is no longer supported.
    my_error(ER_PER_INDEX_CF_DEPRECATED, MYF(0));
    // ALTER
    // return cf_handle;
    return cf_handle_;
  }

  RDB_MUTEX_LOCK_CHECK(m_mutex);

  const auto it = m_cf_name_map.find(cf_name);

  if (it != m_cf_name_map.end()) {
    // ALTER
    // cf_handle = it->second;
    cf_handle_ = it->second;
  } else {
    /* Create a Column Family. */
    // ALTER
    // rocksdb::ColumnFamilyOptions opts;
    // m_cf_options->get_cf_options(cf_name, &opts);
    rocksdb_rpc_log(
        115, "Rdb_cf_manager::get_or_create_cf rocksdb_ColumnFamilyOptions");
    rocksdb::ColumnFamilyOptions *opts = rocksdb_ColumnFamilyOptions();
    m_cf_options->get_cf_options(cf_name, opts);

    // NO_LINT_DEBUG
    sql_print_information("RocksDB: creating a column family %s",
                          cf_name.c_str());
    // NO_LINT_DEBUG
    sql_print_information(
        "    write_buffer_size=%ld",
        rocksdb_ColumnFamilyOptions__GetSizeTProp(opts, "write_buffer_size"));

    // NO_LINT_DEBUG
    sql_print_information("    target_file_size_base=%" PRIu64,
                          rocksdb_ColumnFamilyOptions__GetUInt64Prop(
                              opts, "target_file_size_base"));

    rocksdb::ColumnFamilyHandle *cf_handle_ptr = nullptr;
    // ALTER
    // const rocksdb::Status s =
    //     rdb->CreateColumnFamily(opts, cf_name, &cf_handle_ptr);
    rocksdb_rpc_log(135,
                    "rocksdb_init_func: Rdb_cf_manager::get_or_create_cf "
                    "rocksdb_DB__CreateColumnFamilyWithPointer");

    assert(rdb != nullptr);
    const rocksdb::Status s = rocksdb_DB__CreateColumnFamilyWithPointer(
        rdb, opts, cf_name, cf_handle_ptr);

    if (s.ok()) {
      DBUG_ASSERT(cf_handle_ptr != nullptr);
      // ALTER
      // cf_handle.reset(cf_handle_ptr);
      // m_cf_name_map[cf_handle_ptr->GetName()] = cf_handle;
      // m_cf_id_map[cf_handle_ptr->GetID()] = cf_handle;
      std::string name = rocksdb_ColumnFamilyHandle__GetName(cf_handle_ptr);
      auto id = rocksdb_ColumnFamilyHandle__GetID(cf_handle_ptr);
      std::cout << "line 148 ******** cf id and name: " << name << " " << id
                << std::endl;
      cf_handle_ = cf_handle_ptr;
      m_cf_name_map[name] = cf_handle_;
      m_cf_id_map[id] = cf_handle_;
    }
  }

  RDB_MUTEX_UNLOCK_CHECK(m_mutex);

  return cf_handle_;
}

/*
  Find column family by its cf_name.
*/
rocksdb::ColumnFamilyHandle *Rdb_cf_manager::get_cf(
    const std::string &cf_name) const {
  return get_cf(cf_name, false /*lock_held_by_caller*/);
}

rocksdb::ColumnFamilyHandle *Rdb_cf_manager::get_cf(
    const std::string &cf_name, const bool lock_held_by_caller) const {
  DBUG_ASSERT(!cf_name.empty());
  // ALTER
  // std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle;
  rocksdb::ColumnFamilyHandle *cf_handle = nullptr;

  if (!lock_held_by_caller) {
    RDB_MUTEX_LOCK_CHECK(m_mutex);
  }

  const auto it = m_cf_name_map.find(cf_name);

  if (it != m_cf_name_map.end()) {
    cf_handle = it->second;
  }

  if (!cf_handle) {
    // NO_LINT_DEBUG
    sql_print_warning("Column family '%s' not found.", cf_name.c_str());
  }

  if (!lock_held_by_caller) {
    RDB_MUTEX_UNLOCK_CHECK(m_mutex);
  }

  return cf_handle;
}

rocksdb::ColumnFamilyHandle *Rdb_cf_manager::get_cf(const uint32_t id) const {
  // ALTER
  // std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle;
  rocksdb::ColumnFamilyHandle *cf_handle = nullptr;

  RDB_MUTEX_LOCK_CHECK(m_mutex);
  const auto it = m_cf_id_map.find(id);
  if (it != m_cf_id_map.end()) cf_handle = it->second;
  RDB_MUTEX_UNLOCK_CHECK(m_mutex);

  return cf_handle;
}

std::vector<std::string> Rdb_cf_manager::get_cf_names(void) const {
  std::vector<std::string> names;

  RDB_MUTEX_LOCK_CHECK(m_mutex);
  for (const auto &it : m_cf_name_map) {
    names.push_back(it.first);
  }
  RDB_MUTEX_UNLOCK_CHECK(m_mutex);

  return names;
}

std::vector<rocksdb::ColumnFamilyHandle *> Rdb_cf_manager::get_all_cf(
    void) const {
  // ALTER
  // std::vector<std::shared_ptr<rocksdb::ColumnFamilyHandle>> list;
  std::vector<rocksdb::ColumnFamilyHandle *> list;

  RDB_MUTEX_LOCK_CHECK(m_mutex);

  for (auto it : m_cf_id_map) {
    DBUG_ASSERT(it.second != nullptr);
    list.push_back(it.second);
  }

  RDB_MUTEX_UNLOCK_CHECK(m_mutex);

  return list;
}

// TODO: ALTER
int Rdb_cf_manager::remove_dropped_cf(Rdb_dict_manager *const dict_manager,
                                      rocksdb::TransactionDB *const rdb,
                                      const uint32 &cf_id) {
  dict_manager->assert_lock_held();
  RDB_MUTEX_LOCK_CHECK(m_mutex);
  // ALTER
  // const std::unique_ptr<rocksdb::WriteBatch> wb = dict_manager->begin();
  // rocksdb::WriteBatch *const batch = wb.get();
  rocksdb::WriteBatch *const batch = dict_manager->begin();

  const auto it = m_cf_id_map.find(cf_id);
  if (it == m_cf_id_map.end()) {
    dict_manager->delete_dropped_cf_and_flags(batch, cf_id);
    dict_manager->commit(batch);
    RDB_MUTEX_UNLOCK_CHECK(m_mutex);

    // NO_LINT_DEBUG
    sql_print_warning(
        "RocksDB: Column family with id %u is marked as dropped, "
        "but doesn't exist in cf manager",
        cf_id);

    return HA_EXIT_FAILURE;
  }

  // ALTER
  // auto cf_handle = it->second.get();
  // const std::string cf_name = cf_handle->GetName();
  auto cf_handle = it->second;
  const std::string cf_name = rocksdb_ColumnFamilyHandle__GetName(cf_handle);

  if (!dict_manager->get_dropped_cf(cf_id)) {
    RDB_MUTEX_UNLOCK_CHECK(m_mutex);
    // NO_LINT_DEBUG
    sql_print_warning(
        "RocksDB: Column family %s with id %u is not in "
        "the list of cf ids to be dropped",
        cf_name.c_str(), cf_id);
    return HA_EXIT_FAILURE;
  }

  // ALTER
  // auto status = rdb->DropColumnFamily(cf_handle);
  auto status = rocksdb_TransactionDB__DropColumnFamily(rdb, cf_handle);

  if (!status.ok()) {
    dict_manager->delete_dropped_cf(batch, cf_id);
    dict_manager->commit(batch);
    RDB_MUTEX_UNLOCK_CHECK(m_mutex);

    // NO_LINT_DEBUG
    sql_print_error(
        "RocksDB: Dropping column family %s with id %u on RocksDB failed",
        cf_name.c_str(), cf_id);

    return ha_rocksdb::rdb_error_to_mysql(status);
  }

  DBUG_EXECUTE_IF("rocksdb_remove_dropped_cf", {
    THD *thd = new THD();
    thd->thread_stack = reinterpret_cast<char *>(&(thd));
    thd->store_globals();
    const char act[] =
        "now signal ready_to_restart_during_drop_cf wait_for "
        "restart_done_during_drop_cf";
    DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));
    thd->restore_globals();
    delete thd;
  });

  auto id_iter = m_cf_id_map.find(cf_id);
  DBUG_ASSERT(id_iter != m_cf_id_map.end());
  m_cf_id_map.erase(id_iter);

  auto name_iter = m_cf_name_map.find(cf_name);
  DBUG_ASSERT(name_iter != m_cf_name_map.end());
  m_cf_name_map.erase(name_iter);

  dict_manager->delete_dropped_cf_and_flags(batch, cf_id);

  dict_manager->commit(batch);
  RDB_MUTEX_UNLOCK_CHECK(m_mutex);

  // NO_LINT_DEBUG
  sql_print_information(
      "RocksDB: Column family %s with id %u has been dropped successfully",
      cf_name.c_str(), cf_id);

  return HA_EXIT_SUCCESS;
}

namespace {
struct Rdb_cf_scanner : public Rdb_tables_scanner {
  uint32_t m_cf_id;

  explicit Rdb_cf_scanner(uint32_t cf_id) : m_cf_id(cf_id) {}

  int add_table(Rdb_tbl_def *tdef) override {
    DBUG_ASSERT(tdef != nullptr);

    for (uint i = 0; i < tdef->m_key_count; i++) {
      const Rdb_key_def &kd = *tdef->m_key_descr_arr[i];

      // ALTER
      // if (kd.get_cf()->GetID() == m_cf_id) {
      //   return HA_EXIT_FAILURE;
      // }
      if (rocksdb_ColumnFamilyHandle__GetID(kd.get_cf()) == m_cf_id) {
        return HA_EXIT_FAILURE;
      }
    }
    return HA_EXIT_SUCCESS;
  }
};

}  // namespace

int Rdb_cf_manager::drop_cf(Rdb_ddl_manager *const ddl_manager,
                            Rdb_dict_manager *const dict_manager,
                            const std::string &cf_name) {
  DBUG_ASSERT(!cf_name.empty());
  dict_manager->assert_lock_held();
  uint32_t cf_id = 0;

  if (cf_name == DEFAULT_SYSTEM_CF_NAME || cf_name == DEFAULT_CF_NAME) {
    return HA_EXIT_FAILURE;
  }

  // We should have already acquired dict manager lock. The order
  // of lock acquisition here is:
  //  dict_manager -> cf_manager -> ddl_manager

  RDB_MUTEX_LOCK_CHECK(m_mutex);

  // ALTER
  // auto cf_handle = get_cf(cf_name, true /* lock_held_by_caller */).get();
  auto cf_handle = get_cf(cf_name, true /* lock_held_by_caller */);

  if (cf_handle == nullptr) {
    RDB_MUTEX_UNLOCK_CHECK(m_mutex);
    // NO_LINT_DEBUG
    sql_print_warning(
        "RocksDB: Cannot mark Column family %s to be dropped, "
        "because it doesn't exist in cf manager",
        cf_name.c_str());

    return HA_EXIT_FAILURE;
  }

  // ALTER
  // cf_id = cf_handle->GetID();
  cf_id = rocksdb_ColumnFamilyHandle__GetID(cf_handle);

  Rdb_cf_scanner scanner(cf_id);

  auto ret = ddl_manager->scan_for_tables(&scanner);
  if (ret) {
    RDB_MUTEX_UNLOCK_CHECK(m_mutex);
    // NO_LINT_DEBUG
    sql_print_warning(
        "RocksDB: Cannot mark Column family %s with id %u to be dropped, "
        "because it is in use",
        cf_name.c_str(), cf_id);
    return ret;
  }

  ret = ddl_manager->find_in_uncommitted_keydef(cf_id);
  if (ret) {
    RDB_MUTEX_UNLOCK_CHECK(m_mutex);
    // NO_LINT_DEBUG
    sql_print_warning(
        "RocksDB: Cannot mark Column family %s with id %u to be dropped, "
        "because it is used by an ongoing add index command",
        cf_name.c_str(), cf_id);
    return ret;
  }

  // We don't call DropColumnFamily on RocksDB here and
  // we don't delete handle object. Here we mark the column family
  // as dropped.

  // ALTER
  // const std::unique_ptr<rocksdb::WriteBatch> wb = dict_manager->begin();
  // rocksdb::WriteBatch *const batch = wb.get();

  rocksdb::WriteBatch *batch = dict_manager->begin();
  dict_manager->add_dropped_cf(batch, cf_id);
  dict_manager->commit(batch);

  RDB_MUTEX_UNLOCK_CHECK(m_mutex);

  // NO_LINT_DEBUG
  sql_print_information(
      "RocksDB: Column family %s with id %u has been marked to be dropped",
      cf_name.c_str(), cf_id);

  return HA_EXIT_SUCCESS;
}

int Rdb_cf_manager::create_cf_flags_if_needed(
    const Rdb_dict_manager *const dict_manager, const uint32 &cf_id,
    const std::string &cf_name, const bool is_per_partition_cf) {
  DBUG_ASSERT(!cf_name.empty());
  uchar flags =
      (is_cf_name_reverse(cf_name.c_str()) ? Rdb_key_def::REVERSE_CF_FLAG : 0) |
      (is_per_partition_cf ? Rdb_key_def::PER_PARTITION_CF_FLAG : 0);

  uint existing_cf_flags;
  if (dict_manager->get_cf_flags(cf_id, &existing_cf_flags)) {
    // For the purposes of comparison we'll clear the partitioning bit. The
    // intent here is to make sure that both partitioned and non-partitioned
    // tables can refer to the same CF.
    existing_cf_flags &= ~Rdb_key_def::CF_FLAGS_TO_IGNORE;
    flags &= ~Rdb_key_def::CF_FLAGS_TO_IGNORE;

    if (existing_cf_flags != flags) {
      my_error(ER_CF_DIFFERENT, MYF(0), cf_name.c_str(), flags,
               existing_cf_flags);

      return HA_EXIT_FAILURE;
    }
  } else {
    // ALTER
    // const std::unique_ptr<rocksdb::WriteBatch> wb = dict_manager->begin();
    // rocksdb::WriteBatch *const batch = wb.get();

    rocksdb::WriteBatch *batch = dict_manager->begin();

    dict_manager->add_cf_flags(batch, cf_id, flags);
    dict_manager->commit(batch);
  }

  return HA_EXIT_SUCCESS;
}

}  // namespace myrocks_rpc
