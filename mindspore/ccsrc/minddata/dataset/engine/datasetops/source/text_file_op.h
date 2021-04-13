/**
 * Copyright 2020-2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef MINDSPORE_CCSRC_MINDDATA_DATASET_ENGINE_DATASETOPS_SOURCE_TEXT_FILE_OP_H_
#define MINDSPORE_CCSRC_MINDDATA_DATASET_ENGINE_DATASETOPS_SOURCE_TEXT_FILE_OP_H_

#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "minddata/dataset/util/status.h"
#include "minddata/dataset/util/auto_index.h"
#include "minddata/dataset/engine/data_schema.h"
#include "minddata/dataset/engine/datasetops/parallel_op.h"
#include "minddata/dataset/engine/datasetops/source/nonmappable_leaf_op.h"
#include "minddata/dataset/util/queue.h"
#include "minddata/dataset/util/wait_post.h"
#include "minddata/dataset/engine/jagged_connector.h"

namespace mindspore {
namespace dataset {
using StringIndex = AutoIndexObj<std::string>;

class TextFileOp : public NonMappableLeafOp {
 public:
  class Builder {
   public:
    // Builder constructor. Creates the builder object.
    // @note No default args
    // @return This is a constructor.
    Builder();

    // Default destructor
    ~Builder() = default;

    // Checks if the inputs of the builder is valid.
    // @return Status - the error code returned.
    Status ValidateInputs() const;

    // Create the final object.
    // @param op - dataset op.
    // @return - the error code return.
    Status Build(std::shared_ptr<TextFileOp> *op);

    // Setter method.
    // @return Builder - setter method returns reference to the builder.
    Builder &SetNumWorkers(int32_t num_workers) {
      builder_num_workers_ = num_workers;
      return *this;
    }

    // Setter method.
    // @return Builder - setter method returns reference to the builder.
    Builder &SetOpConnectorSize(int32_t op_connector_size) {
      builder_op_connector_size_ = op_connector_size;
      return *this;
    }

    // Setter method.
    // @return Builder - setter method returns reference to the builder.
    Builder &SetRowsPerBuffer(int64_t rows_per_buffer) {
      builder_rows_per_buffer_ = rows_per_buffer;
      return *this;
    }

    // Setter method.
    // @return Builder - setter method returns reference to the builder.
    Builder &SetNumDevices(int64_t num_dev) {
      builder_num_devices_ = num_dev;
      return *this;
    }

    // Setter method.
    // @return Builder - setter method returns reference to the builder.
    Builder &SetDeviceId(int64_t dev_id) {
      builder_device_id_ = dev_id;
      return *this;
    }

    // Setter method.
    // @return Builder - setter method returns reference to the builder.
    Builder &SetTextFilesList(const std::vector<std::string> &files_list) {
      builder_text_files_list_ = files_list;
      return *this;
    }

    // Setter method.
    // @return Builder - setter method returns reference to the builder.
    Builder &SetShuffleFiles(bool shuffle_files) {
      builder_shuffle_files_ = shuffle_files;
      return *this;
    }

    // Setter method.
    // @return Builder - setter method returns reference to the builder.
    Builder &SetTotalRows(int64_t total_rows) {
      builder_total_rows_ = total_rows;
      return *this;
    }

   private:
    int32_t builder_device_id_;
    int32_t builder_num_devices_;
    int32_t builder_num_workers_;
    int32_t builder_op_connector_size_;
    int64_t builder_rows_per_buffer_;
    int64_t builder_total_rows_;
    int32_t builder_worker_connector_size_;
    std::vector<std::string> builder_text_files_list_;
    bool builder_shuffle_files_;
    std::unique_ptr<DataSchema> builder_schema_;
  };

  // Constructor of TextFileOp
  // @note The builder class should be used to call this constructor.
  // @param num_workers - number of worker threads reading data from tf_file files.
  // @param total_num_rows - number of rows to read
  // @param dataset_files_list - list of filepaths for the dataset files.
  // @param data_schema - the data schema object.
  // @param op_connector_size - size of each queue in the connector that the child operator pulls from.
  // @param columns_to_load - the names of the columns to load data from.
  // @param shuffle_files - whether or not to shuffle the files before reading data.
  // @param equal_rows_per_shard - whether or not to get equal rows for each process.
  TextFileOp(int32_t num_workers, int64_t total_rows, int32_t worker_connector_size, std::unique_ptr<DataSchema>,
             std::vector<std::string> text_files_list, int32_t op_connector_size, bool shuffle_files,
             int32_t num_devices, int32_t device_id);

  // Default destructor
  ~TextFileOp() = default;

  // A print method typically used for debugging
  // @param out - The output stream to write output to
  // @param show_all - A bool to control if you want to show all info or just a summary
  void Print(std::ostream &out, bool show_all) const override;

  // Instantiates the internal queues and connectors
  // @return Status - the error code returned
  Status Init() override;

  // Get total rows in files.
  // @param files - all text files.
  // @param count - number of rows.
  // @return Status - the error coed returned.
  static Status CountAllFileRows(const std::vector<std::string> &files, int64_t *count);

  // Op name getter
  // @return Name of the current Op
  std::string Name() const override { return "TextFileOp"; }

  // File names getter
  // @return Vector of the input file names
  std::vector<std::string> FileNames() { return text_files_list_; }

 private:
  // Parses a single row and puts the data into a tensor table.
  // @param line - the content of the row.
  // @param tensor_table - the tensor table to put the parsed data in.
  // @param row - the id of the row filled in the tensor table.
  // @return Status - the error code returned.
  Status LoadTensor(const std::string &line, TensorRow *out_row);

  // Reads a text file and loads the data into multiple TensorRows.
  // @param file - the file to read.
  // @param start_offset - the start offset of file.
  // @param end_offset - the end offset of file.
  // @param worker_id - the id of the worker that is executing this function.
  // @return Status - the error code returned.
  Status LoadFile(const std::string &file, int64_t start_offset, int64_t end_offset, int32_t worker_id) override;

  // Calculate number of rows in each shard.
  // @return Status - the error code returned.
  Status CalculateNumRowsPerShard() override;

  // Count number of rows in each file.
  // @param filename - text file name.
  // @return int64_t - the total number of rows in file.
  int64_t CountTotalRows(const std::string &file);

  // Fill the IOBlockQueue.
  // @para i_keys - keys of file to fill to the IOBlockQueue
  // @return Status - the error code returned.
  Status FillIOBlockQueue(const std::vector<int64_t> &i_keys) override;

  // Private function for computing the assignment of the column name map.
  // @return - Status
  Status ComputeColMap() override;

  std::vector<std::string> text_files_list_;
  std::unique_ptr<DataSchema> data_schema_;
};
}  // namespace dataset
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_MINDDATA_DATASET_ENGINE_DATASETOPS_SOURCE_TEXT_FILE_OP_H_
