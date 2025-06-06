// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for event trace consumer base class.

#include "base/win/event_trace_consumer.h"

#include <objbase.h>

#include <initguid.h>

#include <iterator>
#include <list>

#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/process/process_handle.h"
#include "base/strings/string_number_conversions_win.h"
#include "base/strings/string_util.h"
#include "base/win/event_trace_controller.h"
#include "base/win/event_trace_provider.h"
#include "base/win/scoped_handle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace win {

namespace {

using EventQueue = std::list<EVENT_TRACE>;

class TestConsumer : public EtwTraceConsumerBase<TestConsumer> {
 public:
  TestConsumer() {
    sank_event_.Set(::CreateEvent(nullptr, TRUE, FALSE, nullptr));
    ClearQueue();
  }

  TestConsumer(const TestConsumer&) = delete;
  TestConsumer& operator=(const TestConsumer&) = delete;

  ~TestConsumer() {
    ClearQueue();
    sank_event_.Close();
  }

  void ClearQueue() {
    for (EventQueue::const_iterator it(events_.begin()), end(events_.end());
         it != end; ++it) {
      delete[] reinterpret_cast<char*>(it->MofData);
    }

    events_.clear();
  }

  static void EnqueueEvent(EVENT_TRACE* event) {
    events_.push_back(*event);
    EVENT_TRACE& back = events_.back();

    if (event->MofData != nullptr && event->MofLength != 0) {
      back.MofData = new char[event->MofLength];
      UNSAFE_TODO(memcpy(back.MofData, event->MofData, event->MofLength));
    }
  }

  static void ProcessEvent(EVENT_TRACE* event) {
    EnqueueEvent(event);
    ::SetEvent(sank_event_.get());
  }

  static ScopedHandle sank_event_;
  static EventQueue events_;
};

ScopedHandle TestConsumer::sank_event_;
EventQueue TestConsumer::events_;

class EtwTraceConsumerBaseTest : public testing::Test {
 public:
  EtwTraceConsumerBaseTest()
      : session_name_(L"TestSession-" + NumberToWString(GetCurrentProcId())) {}

  void SetUp() override {
    // Cleanup any potentially dangling sessions.
    EtwTraceProperties ignore;
    EtwTraceController::Stop(session_name_.c_str(), &ignore);

    // Allocate a new GUID for each provider test.
    ASSERT_HRESULT_SUCCEEDED(::CoCreateGuid(&test_provider_));
  }

  void TearDown() override {
    // Cleanup any potentially dangling sessions.
    EtwTraceProperties ignore;
    EtwTraceController::Stop(session_name_.c_str(), &ignore);
  }

 protected:
  GUID test_provider_;
  std::wstring session_name_;
};

}  // namespace

TEST_F(EtwTraceConsumerBaseTest, Initialize) {
  TestConsumer consumer_;
}

TEST_F(EtwTraceConsumerBaseTest, OpenRealtimeSucceedsWhenNoSession) {
  TestConsumer consumer_;
  ASSERT_HRESULT_SUCCEEDED(
      consumer_.OpenRealtimeSession(session_name_.c_str()));
}

TEST_F(EtwTraceConsumerBaseTest, ConsumerImmediateFailureWhenNoSession) {
  TestConsumer consumer_;
  ASSERT_HRESULT_SUCCEEDED(
      consumer_.OpenRealtimeSession(session_name_.c_str()));
  ASSERT_HRESULT_FAILED(consumer_.Consume());
}

namespace {

class EtwTraceConsumerRealtimeTest : public EtwTraceConsumerBaseTest {
 public:
  void SetUp() override {
    EtwTraceConsumerBaseTest::SetUp();
    ASSERT_HRESULT_SUCCEEDED(
        consumer_.OpenRealtimeSession(session_name_.c_str()));
  }

  void TearDown() override {
    consumer_.Close();
    EtwTraceConsumerBaseTest::TearDown();
  }

  DWORD ConsumerThread() {
    ::SetEvent(consumer_ready_.get());
    return consumer_.Consume();
  }

  static DWORD WINAPI ConsumerThreadMainProc(void* arg) {
    return reinterpret_cast<EtwTraceConsumerRealtimeTest*>(arg)
        ->ConsumerThread();
  }

  HRESULT StartConsumerThread() {
    consumer_ready_.Set(::CreateEvent(nullptr, TRUE, FALSE, nullptr));
    EXPECT_TRUE(consumer_ready_.is_valid());
    consumer_thread_.Set(
        ::CreateThread(nullptr, 0, ConsumerThreadMainProc, this, 0, nullptr));
    if (consumer_thread_.get() == nullptr) {
      return HRESULT_FROM_WIN32(::GetLastError());
    }

    HANDLE events[] = {consumer_ready_.get(), consumer_thread_.get()};
    DWORD result =
        ::WaitForMultipleObjects(std::size(events), events, FALSE, INFINITE);
    switch (result) {
      case WAIT_OBJECT_0:
        // The event was set, the consumer_ is ready.
        return S_OK;
      case WAIT_OBJECT_0 + 1: {
        // The thread finished. This may race with the event, so check
        // explicitly for the event here, before concluding there's trouble.
        if (::WaitForSingleObject(consumer_ready_.get(), 0) == WAIT_OBJECT_0) {
          return S_OK;
        }
        DWORD exit_code = 0;
        if (::GetExitCodeThread(consumer_thread_.get(), &exit_code)) {
          return exit_code;
        }
        return HRESULT_FROM_WIN32(::GetLastError());
      }
      default:
        return E_UNEXPECTED;
    }
  }

  // Waits for consumer_ thread to exit, and returns its exit code.
  HRESULT JoinConsumerThread() {
    if (::WaitForSingleObject(consumer_thread_.get(), INFINITE) !=
        WAIT_OBJECT_0) {
      return HRESULT_FROM_WIN32(::GetLastError());
    }

    DWORD exit_code = 0;
    if (::GetExitCodeThread(consumer_thread_.get(), &exit_code)) {
      return exit_code;
    }

    return HRESULT_FROM_WIN32(::GetLastError());
  }

  TestConsumer consumer_;
  ScopedHandle consumer_ready_;
  ScopedHandle consumer_thread_;
};

}  // namespace

TEST_F(EtwTraceConsumerRealtimeTest, ConsumerReturnsWhenSessionClosed) {
  EtwTraceController controller;
  if (controller.StartRealtimeSession(session_name_.c_str(), 1024) ==
      E_ACCESSDENIED) {
    VLOG(1) << "You must be an administrator to run this test on Vista";
    return;
  }

  // Start the consumer_.
  ASSERT_HRESULT_SUCCEEDED(StartConsumerThread());

  // Wait around for the consumer_ thread a bit. This is inherently racy because
  // the consumer thread says that it is ready and then calls Consume() which
  // calls ::ProcessTrace. We need to call WaitForSingleObject after the call to
  // ::ProcessTrace but there is no way to know when that call has been made.
  // With a timeout of 50 ms this test was failing frequently when the system
  // was under load. It is hoped that 500 ms will be enough.
  ASSERT_EQ(static_cast<DWORD>(WAIT_TIMEOUT),
            ::WaitForSingleObject(consumer_thread_.get(), 500));
  ASSERT_HRESULT_SUCCEEDED(controller.Stop(nullptr));

  // The consumer_ returns success on session stop.
  ASSERT_HRESULT_SUCCEEDED(JoinConsumerThread());
}

namespace {

// clang-format off
// {57E47923-A549-476f-86CA-503D57F59E62}
DEFINE_GUID(
    kTestEventType,
    0x57e47923, 0xa549, 0x476f, 0x86, 0xca, 0x50, 0x3d, 0x57, 0xf5, 0x9e, 0x62);
// clang-format on

}  // namespace

TEST_F(EtwTraceConsumerRealtimeTest, ConsumeEvent) {
  EtwTraceController controller;
  if (controller.StartRealtimeSession(session_name_.c_str(), 1024) ==
      E_ACCESSDENIED) {
    VLOG(1) << "You must be an administrator to run this test on Vista";
    return;
  }

  ASSERT_HRESULT_SUCCEEDED(controller.EnableProvider(
      test_provider_, TRACE_LEVEL_VERBOSE, 0xFFFFFFFF));

  EtwTraceProvider provider(test_provider_);
  ASSERT_EQ(static_cast<DWORD>(ERROR_SUCCESS), provider.Register());

  // Start the consumer_.
  ASSERT_HRESULT_SUCCEEDED(StartConsumerThread());
  ASSERT_EQ(0u, TestConsumer::events_.size());

  EtwMofEvent<1> event(kTestEventType, 1, TRACE_LEVEL_ERROR);
  EXPECT_EQ(static_cast<DWORD>(ERROR_SUCCESS), provider.Log(&event.header));
  EXPECT_EQ(WAIT_OBJECT_0,
            ::WaitForSingleObject(TestConsumer::sank_event_.get(), INFINITE));
  ASSERT_HRESULT_SUCCEEDED(controller.Stop(nullptr));
  ASSERT_HRESULT_SUCCEEDED(JoinConsumerThread());
  ASSERT_NE(0u, TestConsumer::events_.size());
}

namespace {

// We run events through a file session to assert that
// the content comes through.
class EtwTraceConsumerDataTest : public EtwTraceConsumerBaseTest {
 public:
  EtwTraceConsumerDataTest() = default;

  void SetUp() override {
    EtwTraceConsumerBaseTest::SetUp();

    EtwTraceProperties prop;
    EtwTraceController::Stop(session_name_.c_str(), &prop);

    // Create a temp dir for this test.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    // Construct a temp file name in our dir.
    temp_file_ = temp_dir_.GetPath().Append(FILE_PATH_LITERAL("test.etl"));
  }

  void TearDown() override {
    EXPECT_TRUE(DeleteFile(temp_file_));

    EtwTraceConsumerBaseTest::TearDown();
  }

  HRESULT LogEventToTempSession(PEVENT_TRACE_HEADER header) {
    EtwTraceController controller;

    // Set up a file session.
    HRESULT hr = controller.StartFileSession(session_name_.c_str(),
                                             temp_file_.value().c_str());
    if (FAILED(hr)) {
      return hr;
    }

    // Enable our provider.
    EXPECT_HRESULT_SUCCEEDED(controller.EnableProvider(
        test_provider_, TRACE_LEVEL_VERBOSE, 0xFFFFFFFF));

    EtwTraceProvider provider(test_provider_);
    // Then register our provider, means we get a session handle immediately.
    EXPECT_EQ(static_cast<DWORD>(ERROR_SUCCESS), provider.Register());
    // Trace the event, it goes to the temp file.
    EXPECT_EQ(static_cast<DWORD>(ERROR_SUCCESS), provider.Log(header));
    EXPECT_HRESULT_SUCCEEDED(controller.DisableProvider(test_provider_));
    EXPECT_HRESULT_SUCCEEDED(provider.Unregister());
    EXPECT_HRESULT_SUCCEEDED(controller.Flush(nullptr));
    EXPECT_HRESULT_SUCCEEDED(controller.Stop(nullptr));

    return S_OK;
  }

  HRESULT ConsumeEventFromTempSession() {
    // Now consume the event(s).
    TestConsumer consumer_;
    HRESULT hr = consumer_.OpenFileSession(temp_file_.value().c_str());
    if (SUCCEEDED(hr)) {
      hr = consumer_.Consume();
    }
    consumer_.Close();
    // And nab the result.
    events_.swap(TestConsumer::events_);
    return hr;
  }

  HRESULT RoundTripEvent(PEVENT_TRACE_HEADER header, PEVENT_TRACE* trace) {
    DeleteFile(temp_file_);

    HRESULT hr = LogEventToTempSession(header);
    if (SUCCEEDED(hr)) {
      hr = ConsumeEventFromTempSession();
    }

    if (FAILED(hr)) {
      return hr;
    }

    // We should now have the event in the queue.
    if (events_.empty()) {
      return E_FAIL;
    }

    *trace = &events_.back();
    return S_OK;
  }

  EventQueue events_;
  ScopedTempDir temp_dir_;
  FilePath temp_file_;
};

}  // namespace

TEST_F(EtwTraceConsumerDataTest, RoundTrip) {
  EtwMofEvent<1> event(kTestEventType, 1, TRACE_LEVEL_ERROR);

  static const char kData[] = "This is but test data";
  event.fields[0].DataPtr = reinterpret_cast<ULONG64>(kData);
  event.fields[0].Length = sizeof(kData);

  PEVENT_TRACE trace = nullptr;
  HRESULT hr = RoundTripEvent(&event.header, &trace);
  if (hr == E_ACCESSDENIED) {
    VLOG(1) << "You must be an administrator to run this test on Vista";
    return;
  }
  ASSERT_HRESULT_SUCCEEDED(hr) << "RoundTripEvent failed";
  ASSERT_TRUE(trace != nullptr);
  ASSERT_EQ(sizeof(kData), trace->MofLength);
  ASSERT_STREQ(kData, reinterpret_cast<const char*>(trace->MofData));
}

}  // namespace win
}  // namespace base
