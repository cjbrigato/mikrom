// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/base/buffered_socket_writer.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/numerics/byte_conversions.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/log/net_log.h"
#include "net/socket/socket_test_util.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace remoting {

namespace {

const int kTestBufferSize = 10000;
const size_t kWriteChunkSize = 1024U;

int WriteNetSocket(net::Socket* socket,
                   const scoped_refptr<net::IOBuffer>& buf,
                   int buf_len,
                   net::CompletionOnceCallback callback,
                   const net::NetworkTrafficAnnotationTag& traffic_annotation) {
  return socket->Write(buf.get(), buf_len, std::move(callback),
                       traffic_annotation);
}

class SocketDataProvider : public net::SocketDataProvider {
 public:
  SocketDataProvider()
      : write_limit_(-1), async_write_(false), next_write_error_(net::OK) {}

  net::MockRead OnRead() override {
    return net::MockRead(net::ASYNC, net::ERR_IO_PENDING);
  }

  net::MockWriteResult OnWrite(const std::string& data) override {
    if (next_write_error_ != net::OK) {
      int r = next_write_error_;
      next_write_error_ = net::OK;
      return net::MockWriteResult(async_write_ ? net::ASYNC : net::SYNCHRONOUS,
                                  r);
    }
    int size = data.size();
    if (write_limit_ > 0) {
      size = std::min(write_limit_, size);
    }
    written_data_.append(data, 0, size);
    return net::MockWriteResult(async_write_ ? net::ASYNC : net::SYNCHRONOUS,
                                size);
  }

  bool AllReadDataConsumed() const override { return true; }

  bool AllWriteDataConsumed() const override { return true; }

  void Reset() override {}

  base::span<const uint8_t> written_bytes() const {
    return base::as_byte_span(written_data_);
  }

  void set_write_limit(int limit) { write_limit_ = limit; }
  void set_async_write(bool async_write) { async_write_ = async_write; }
  void set_next_write_error(int error) { next_write_error_ = error; }

 private:
  std::string written_data_;
  int write_limit_;
  bool async_write_;
  int next_write_error_;
};

}  // namespace

class BufferedSocketWriterTest : public testing::Test {
 public:
  BufferedSocketWriterTest() : write_error_(0) {}

  void DestroyWriter() {
    writer_.reset();
    socket_.reset();
  }

  void Unexpected() { EXPECT_TRUE(false); }

 protected:
  void SetUp() override {
    socket_ = std::make_unique<net::MockTCPClientSocket>(
        net::AddressList(), net::NetLog::Get(), &socket_data_provider_);
    socket_data_provider_.set_connect_data(
        net::MockConnect(net::SYNCHRONOUS, net::OK));
    ASSERT_EQ(net::OK, socket_->Connect(net::CompletionOnceCallback()));

    writer_ = std::make_unique<BufferedSocketWriter>();
    InitializeTestBuffer(test_buffer_, kTestBufferSize);
    InitializeTestBuffer(test_buffer_2_, kTestBufferSize);
  }

  void InitializeTestBuffer(scoped_refptr<net::IOBufferWithSize>& buffer,
                            size_t size) {
    buffer = base::MakeRefCounted<net::IOBufferWithSize>(size);
    std::ranges::generate(buffer->span(), []() { return rand() % 256; });
  }

  void StartWriter() {
    writer_->Start(base::BindRepeating(&WriteNetSocket, socket_.get()),
                   base::BindOnce(&BufferedSocketWriterTest::OnWriteFailed,
                                  base::Unretained(this)));
  }

  void OnWriteFailed(int error) { write_error_ = error; }

  void VerifyWrittenData() {
    const auto written_bytes = socket_data_provider_.written_bytes();
    const size_t expected_size = test_buffer_->size() + test_buffer_2_->size();

    ASSERT_EQ(expected_size, written_bytes.size());
    const auto buffer1_bytes = base::as_byte_span(test_buffer_->span());
    const auto buffer2_bytes = base::as_byte_span(test_buffer_2_->span());
    const auto first_segment = written_bytes.first(buffer1_bytes.size());
    EXPECT_TRUE(std::ranges::equal(first_segment, buffer1_bytes));

    const auto second_segment = written_bytes.subspan(buffer1_bytes.size());
    EXPECT_TRUE(std::ranges::equal(second_segment.first(buffer2_bytes.size()),
                                   buffer2_bytes));
  }

  void TestWrite() {
    writer_->Write(test_buffer_, base::OnceClosure(),
                   TRAFFIC_ANNOTATION_FOR_TESTS);
    writer_->Write(test_buffer_2_, base::OnceClosure(),
                   TRAFFIC_ANNOTATION_FOR_TESTS);
    base::RunLoop().RunUntilIdle();
    VerifyWrittenData();
  }

  void TestAppendInCallback() {
    writer_->Write(
        test_buffer_,
        base::BindOnce(base::IgnoreResult(&BufferedSocketWriter::Write),
                       base::Unretained(writer_.get()), test_buffer_2_,
                       base::OnceClosure(), TRAFFIC_ANNOTATION_FOR_TESTS),
        TRAFFIC_ANNOTATION_FOR_TESTS);
    base::RunLoop().RunUntilIdle();
    VerifyWrittenData();
  }

  base::test::SingleThreadTaskEnvironment task_environment_;
  SocketDataProvider socket_data_provider_;
  std::unique_ptr<net::StreamSocket> socket_;
  std::unique_ptr<BufferedSocketWriter> writer_;
  scoped_refptr<net::IOBufferWithSize> test_buffer_;
  scoped_refptr<net::IOBufferWithSize> test_buffer_2_;
  int write_error_;
};

// Test synchronous write.
TEST_F(BufferedSocketWriterTest, WriteFull) {
  StartWriter();
  TestWrite();
}

// Test synchronous write in 1k chunks.
TEST_F(BufferedSocketWriterTest, WriteChunks) {
  StartWriter();
  socket_data_provider_.set_write_limit(kWriteChunkSize);
  TestWrite();
}

// Test asynchronous write.
TEST_F(BufferedSocketWriterTest, WriteAsync) {
  StartWriter();
  socket_data_provider_.set_async_write(true);
  socket_data_provider_.set_write_limit(kWriteChunkSize);
  TestWrite();
}

// Make sure we can call Write() from the done callback.
TEST_F(BufferedSocketWriterTest, AppendInCallbackSync) {
  StartWriter();
  TestAppendInCallback();
}

// Make sure we can call Write() from the done callback.
TEST_F(BufferedSocketWriterTest, AppendInCallbackAsync) {
  StartWriter();
  socket_data_provider_.set_async_write(true);
  socket_data_provider_.set_write_limit(kWriteChunkSize);
  TestAppendInCallback();
}

// Test that the writer can be destroyed from callback.
TEST_F(BufferedSocketWriterTest, DestroyFromCallback) {
  StartWriter();
  socket_data_provider_.set_async_write(true);
  writer_->Write(test_buffer_,
                 base::BindOnce(&BufferedSocketWriterTest::DestroyWriter,
                                base::Unretained(this)),
                 TRAFFIC_ANNOTATION_FOR_TESTS);
  writer_->Write(test_buffer_2_,
                 base::BindOnce(&BufferedSocketWriterTest::Unexpected,
                                base::Unretained(this)),
                 TRAFFIC_ANNOTATION_FOR_TESTS);
  socket_data_provider_.set_async_write(false);
  base::RunLoop().RunUntilIdle();
  const auto written_bytes = socket_data_provider_.written_bytes();
  const auto expected_bytes = base::as_byte_span(test_buffer_->span());
  ASSERT_GE(written_bytes.size(), expected_bytes.size());
  const auto actual_segment = written_bytes.first(expected_bytes.size());
  EXPECT_TRUE(std::ranges::equal(actual_segment, expected_bytes));
}

// Verify that it stops writing after the first error.
TEST_F(BufferedSocketWriterTest, TestWriteErrorSync) {
  StartWriter();
  socket_data_provider_.set_write_limit(kWriteChunkSize);
  writer_->Write(test_buffer_, base::OnceClosure(),
                 TRAFFIC_ANNOTATION_FOR_TESTS);
  socket_data_provider_.set_async_write(true);
  socket_data_provider_.set_next_write_error(net::ERR_FAILED);
  writer_->Write(test_buffer_2_,
                 base::BindOnce(&BufferedSocketWriterTest::Unexpected,
                                base::Unretained(this)),
                 TRAFFIC_ANNOTATION_FOR_TESTS);
  socket_data_provider_.set_async_write(false);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(net::ERR_FAILED, write_error_);
  EXPECT_EQ(static_cast<size_t>(test_buffer_->size()),
            socket_data_provider_.written_bytes().size());
}

// Verify that it stops writing after the first error.
TEST_F(BufferedSocketWriterTest, TestWriteErrorAsync) {
  StartWriter();
  socket_data_provider_.set_write_limit(kWriteChunkSize);
  writer_->Write(test_buffer_, base::OnceClosure(),
                 TRAFFIC_ANNOTATION_FOR_TESTS);
  socket_data_provider_.set_async_write(true);
  socket_data_provider_.set_next_write_error(net::ERR_FAILED);
  writer_->Write(test_buffer_2_,
                 base::BindOnce(&BufferedSocketWriterTest::Unexpected,
                                base::Unretained(this)),
                 TRAFFIC_ANNOTATION_FOR_TESTS);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(net::ERR_FAILED, write_error_);
  EXPECT_EQ(static_cast<size_t>(test_buffer_->size()),
            socket_data_provider_.written_bytes().size());
}

TEST_F(BufferedSocketWriterTest, WriteBeforeStart) {
  writer_->Write(test_buffer_, base::OnceClosure(),
                 TRAFFIC_ANNOTATION_FOR_TESTS);
  writer_->Write(test_buffer_2_, base::OnceClosure(),
                 TRAFFIC_ANNOTATION_FOR_TESTS);

  StartWriter();
  base::RunLoop().RunUntilIdle();

  VerifyWrittenData();
}

}  // namespace remoting
