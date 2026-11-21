#pragma once

#include <arrow/io/file.h>
#include <arrow/result.h>
#include <parquet/column_writer.h>
#include <parquet/exception.h>
#include <parquet/file_writer.h>

#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Strings refer to the pipeline's immutable dimension dictionaries until flush().
// The Parquet library owns the file format, encodings, row groups and footer.
class DailyWriter {
  using Bytes = parquet::ByteArray;
  static constexpr size_t batch_size = 65536;
  bool users_;
  std::shared_ptr<arrow::io::FileOutputStream> sink_;
  std::unique_ptr<parquet::ParquetFileWriter> file_;
  std::vector<Bytes> ids_, labels_;
  std::vector<int16_t> label_levels_;
  std::vector<int32_t> days_;
  std::vector<int64_t> sessions_, counts_, revenue_, assists_;

  static Bytes bytes(const std::string &s) {
    if (s.size() > std::numeric_limits<int32_t>::max())
      throw std::length_error("string exceeds Parquet BYTE_ARRAY limit");
    return Bytes(static_cast<uint32_t>(s.size()),
                 reinterpret_cast<const uint8_t *>(s.data()));
  }

  template <class Type>
  static void column(parquet::RowGroupWriter *group,
                     std::vector<typename Type::c_type> &values,
                     const std::vector<int16_t> *levels = nullptr) {
    auto *writer = static_cast<parquet::TypedColumnWriter<Type> *>(group->NextColumn());
    writer->WriteBatch(levels ? levels->size() : values.size(),
                       levels ? levels->data() : nullptr, nullptr, values.data());
    values.clear();
  }

  void flush() {
    if (days_.empty())
      return;
    auto *group = file_->AppendRowGroup();
    column<parquet::ByteArrayType>(group, ids_);
    if (!users_)
      column<parquet::ByteArrayType>(group, labels_);
    column<parquet::Int32Type>(group, days_);
    column<parquet::Int64Type>(group, sessions_);
    column<parquet::Int64Type>(group, counts_);
    column<parquet::Int64Type>(group, revenue_);
    if (users_) {
      column<parquet::ByteArrayType>(group, labels_, &label_levels_);
      label_levels_.clear();
    } else {
      column<parquet::Int64Type>(group, assists_);
    }
    group->Close();
  }

public:
  DailyWriter(const std::filesystem::path &path, bool users) : users_(users) {
    using namespace parquet;
    schema::NodeVector fields;
    auto field = [&](const char *name, Type::type type,
                     ConvertedType::type logical = ConvertedType::NONE,
                     Repetition::type rep = Repetition::REQUIRED) {
      fields.push_back(schema::PrimitiveNode::Make(name, rep, type, logical));
    };
    field(users ? "user_id" : "campaign_id", Type::BYTE_ARRAY, ConvertedType::UTF8);
    if (!users)
      field("channel", Type::BYTE_ARRAY, ConvertedType::UTF8);
    field("day", Type::INT32, ConvertedType::DATE);
    field("sessions", Type::INT64);
    field(users ? "events" : "purchasing_sessions", Type::INT64);
    field("revenue_cents", Type::INT64);
    if (users)
      field("top_category", Type::BYTE_ARRAY, ConvertedType::UTF8, Repetition::OPTIONAL);
    else
      field("assisted_sessions", Type::INT64);
    auto root = schema::GroupNode::Make("daily", Repetition::REQUIRED, fields);
    PARQUET_ASSIGN_OR_THROW(sink_, ::arrow::io::FileOutputStream::Open(path.string()));
    file_ = ParquetFileWriter::Open(sink_, std::static_pointer_cast<schema::GroupNode>(root));
  }

  void append(const std::string &id, int64_t day, int64_t sessions,
              int64_t count, int64_t revenue, const std::string *label,
              int64_t assists = 0) {
    if (day < INT32_MIN || day > INT32_MAX)
      throw std::out_of_range("day exceeds Parquet DATE range");
    ids_.push_back(bytes(id));
    days_.push_back(static_cast<int32_t>(day));
    sessions_.push_back(sessions);
    counts_.push_back(count);
    revenue_.push_back(revenue);
    if (label)
      labels_.push_back(bytes(*label));
    if (users_)
      label_levels_.push_back(label != nullptr);
    else
      assists_.push_back(assists);
    if (days_.size() == batch_size)
      flush();
  }

  void close() {
    flush();
    file_->Close();
    PARQUET_THROW_NOT_OK(sink_->Close());
  }
};
