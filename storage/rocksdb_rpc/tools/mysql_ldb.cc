//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
#include "../rdb_comparator.h"
#include "rocksdb/ldb_tool.h"
#include "rocksdb/utilities/object_registry.h"

int main(int argc, char **argv) {
  // Register the comparators so they can be loaded from OPTIONS file when
  // `--try_load_options` is provided.
  rocksdb::ObjectLibrary::Default()->Register(
      myrocks_rpc::Rdb_pk_comparator().Name(),
      rocksdb::FactoryFunc<rocksdb::Comparator>(
          [](const std::string & /* uri */,
             std::unique_ptr<rocksdb::Comparator> * /* res_guard */,
             std::string * /* err_msg */) {
            static myrocks_rpc::Rdb_pk_comparator cmp;
            return &cmp;
          }));
  rocksdb::ObjectLibrary::Default()->Register(
      myrocks_rpc::Rdb_rev_comparator().Name(),
      rocksdb::FactoryFunc<rocksdb::Comparator>(
          [](const std::string & /* uri */,
             std::unique_ptr<rocksdb::Comparator> * /* res_guard */,
             std::string * /* err_msg */) {
            static myrocks_rpc::Rdb_rev_comparator cmp;
            return &cmp;
          }));

  rocksdb::LDBTool tool;
  tool.Run(argc, argv);
  return 0;
}
