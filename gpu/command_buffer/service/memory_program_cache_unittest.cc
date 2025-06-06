// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/memory_program_cache.h"

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <memory>
#include <numeric>

#include "base/compiler_specific.h"
#include "base/containers/heap_array.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_span.h"
#include "base/numerics/safe_conversions.h"
#include "build/build_config.h"
#include "gpu/command_buffer/common/gles2_cmd_format.h"
#include "gpu/command_buffer/common/shm_count.h"
#include "gpu/command_buffer/service/gl_utils.h"
#include "gpu/command_buffer/service/gpu_service_test.h"
#include "gpu/command_buffer/service/shader_manager.h"
#include "gpu/command_buffer/service/shader_translator.h"
#include "gpu/command_buffer/service/test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_mock.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::SetArgPointee;

namespace gpu {
namespace gles2 {

class ProgramBinaryEmulator {
 public:
  ProgramBinaryEmulator(GLenum format, base::span<const char> binary)
      : format_(format), binary_(binary) {}

  void GetProgramBinary(GLuint program,
                        GLsizei buffer_size,
                        GLsizei* length,
                        GLenum* format,
                        GLvoid* binary) {
    if (length) {
      *length = binary_.size();
    }
    *format = format_;

    // SAFETY: This is an OpenGL entry point and cannot be spanified:
    // https://docs.gl/es3/glGetProgramBinary
    // We can only trust what we're given.
    base::span<char> out_arg =
        UNSAFE_BUFFERS(base::span(static_cast<char*>(binary), binary_.size()));
    out_arg.copy_from(binary_);
  }

  void ProgramBinary(GLuint program,
                     GLenum format,
                     const GLvoid* binary,
                     GLsizei length) {
    size_t unsigned_length = base::checked_cast<size_t>(length);

    // SAFETY: This is an OpenGL entry point and cannot be spanified:
    // https://docs.gl/es3/glProgramBinary
    // We can only trust what we're given.
    base::span<const char> cast_binary = UNSAFE_BUFFERS(
        base::span(static_cast<const char*>(binary), unsigned_length));

    // format and length are verified by matcher
    EXPECT_EQ(binary_.first(unsigned_length),
              cast_binary.first(unsigned_length));
  }

  GLsizei length() const { return binary_.size(); }
  GLenum format() const { return format_; }
  const char* binary() const { return binary_.data(); }

 private:
  GLenum format_;
  base::raw_span<const char> binary_;
};

class MemoryProgramCacheTest : public GpuServiceTest, public DecoderClient {
 public:
  static const size_t kCacheSizeBytes = 1024;
  static const bool kDisableGpuDiskCache = false;
  static const bool kDisableCachingForTransformFeedback = false;
  static const GLuint kVertexShaderClientId = 90;
  static const GLuint kVertexShaderServiceId = 100;
  static const GLuint kFragmentShaderClientId = 91;
  static const GLuint kFragmentShaderServiceId = 100;

  MemoryProgramCacheTest()
      : cache_(new MemoryProgramCache(kCacheSizeBytes,
                                      kDisableGpuDiskCache,
                                      kDisableCachingForTransformFeedback,
                                      &use_shader_cache_shm_count_)),
        shader_manager_(nullptr),
        vertex_shader_(nullptr),
        fragment_shader_(nullptr),
        shader_cache_count_(0) {}
  ~MemoryProgramCacheTest() override {
    vertex_shader_ = nullptr;
    fragment_shader_ = nullptr;
    shader_manager_.Destroy(false);
  }

  void OnConsoleMessage(int32_t id, const std::string& message) override {}
  void CacheBlob(gpu::GpuDiskCacheType type,
                 const std::string& key,
                 const std::string& blob) override {
    ASSERT_EQ(type, gpu::GpuDiskCacheType::kGlShaders);
    shader_cache_count_++;
    shader_cache_shader_ = blob;
  }
  void OnFenceSyncRelease(uint64_t release) override {}
  void OnDescheduleUntilFinished() override {}
  void OnRescheduleAfterFinished() override {}
  void ScheduleGrContextCleanup() override {}
  void HandleReturnData(base::span<const uint8_t> data) override {}
  bool ShouldYield() override { return false; }

  int32_t shader_cache_count() { return shader_cache_count_; }
  const std::string& shader_cache_shader() { return shader_cache_shader_; }

 protected:
  void SetUp() override {
    GpuServiceTest::SetUpWithGLVersion("OpenGL ES 3.0",
                                       "GL_OES_get_program_binary");

    vertex_shader_ = shader_manager_.CreateShader(kVertexShaderClientId,
                                                  kVertexShaderServiceId,
                                                  GL_VERTEX_SHADER);
    fragment_shader_ = shader_manager_.CreateShader(
        kFragmentShaderClientId,
        kFragmentShaderServiceId,
        GL_FRAGMENT_SHADER);
    ASSERT_TRUE(vertex_shader_ != nullptr);
    ASSERT_TRUE(fragment_shader_ != nullptr);
    AttributeMap vertex_attrib_map;
    UniformMap vertex_uniform_map;
    VaryingMap vertex_varying_map;
    OutputVariableList vertex_output_variable_list;
    AttributeMap fragment_attrib_map;
    UniformMap fragment_uniform_map;
    VaryingMap fragment_varying_map;
    OutputVariableList fragment_output_variable_list;

    vertex_attrib_map["a"] = TestHelper::ConstructAttribute(
        GL_FLOAT_VEC2, 34, GL_LOW_FLOAT, false, "a");
    vertex_uniform_map["a"] = TestHelper::ConstructUniform(
        GL_FLOAT, 10, GL_MEDIUM_FLOAT, true, "a");
    vertex_uniform_map["b"] = TestHelper::ConstructUniform(
        GL_FLOAT_VEC3, 3114, GL_HIGH_FLOAT, true, "b");
    vertex_varying_map["c"] = TestHelper::ConstructVarying(
        GL_FLOAT_VEC4, 2, GL_HIGH_FLOAT, true, "c");
    vertex_output_variable_list.push_back(TestHelper::ConstructOutputVariable(
        GL_FLOAT, 0, GL_HIGH_FLOAT, true, "d"));
    fragment_attrib_map["jjjbb"] = TestHelper::ConstructAttribute(
        GL_FLOAT_MAT4, 1114, GL_MEDIUM_FLOAT, false, "jjjbb");
    fragment_uniform_map["k"] = TestHelper::ConstructUniform(
        GL_FLOAT_MAT2, 34413, GL_MEDIUM_FLOAT, true, "k");
    fragment_varying_map["c"] = TestHelper::ConstructVarying(
        GL_FLOAT_VEC4, 2, GL_HIGH_FLOAT, true, "c");
    fragment_output_variable_list.push_back(TestHelper::ConstructOutputVariable(
        GL_FLOAT, 0, GL_HIGH_FLOAT, true, "d"));

    vertex_shader_->set_source("bbbalsldkdkdkd");
    fragment_shader_->set_source("bbbal   sldkdkdkas 134 ad");

    TestHelper::SetShaderStates(gl_.get(), vertex_shader_, true, nullptr,
                                nullptr, nullptr, &vertex_attrib_map,
                                &vertex_uniform_map, &vertex_varying_map,
                                nullptr, &vertex_output_variable_list, nullptr);
    TestHelper::SetShaderStates(
        gl_.get(), fragment_shader_, true, nullptr, nullptr, nullptr,
        &fragment_attrib_map, &fragment_uniform_map, &fragment_varying_map,
        nullptr, &fragment_output_variable_list, nullptr);
  }

  void SetExpectationsForSaveLinkedProgram(
      const GLint program_id,
      ProgramBinaryEmulator* emulator) const {
    EXPECT_CALL(*gl_.get(),
                GetProgramiv(program_id, GL_PROGRAM_BINARY_LENGTH_OES, _))
        .WillOnce(SetArgPointee<2>(emulator->length()));
    EXPECT_CALL(*gl_.get(),
                GetProgramBinary(program_id, emulator->length(), _, _, _))
        .WillOnce(Invoke(emulator, &ProgramBinaryEmulator::GetProgramBinary));
  }

  void SetExpectationsForLoadLinkedProgram(
      const GLint program_id,
      ProgramBinaryEmulator* emulator) const {
    EXPECT_CALL(*gl_.get(),
                ProgramBinary(program_id,
                              emulator->format(),
                              _,
                              emulator->length()))
        .WillOnce(Invoke(emulator, &ProgramBinaryEmulator::ProgramBinary));
    EXPECT_CALL(*gl_.get(),
                GetProgramiv(program_id, GL_LINK_STATUS, _))
                .WillOnce(SetArgPointee<2>(GL_TRUE));
  }

  void SetExpectationsForLoadLinkedProgramFailure(
      const GLint program_id,
      ProgramBinaryEmulator* emulator) const {
    EXPECT_CALL(*gl_.get(),
                ProgramBinary(program_id,
                              emulator->format(),
                              _,
                              emulator->length()))
        .WillOnce(Invoke(emulator, &ProgramBinaryEmulator::ProgramBinary));
    EXPECT_CALL(*gl_.get(),
                GetProgramiv(program_id, GL_LINK_STATUS, _))
                .WillOnce(SetArgPointee<2>(GL_FALSE));
  }

  GpuProcessShmCount use_shader_cache_shm_count_;
  std::unique_ptr<MemoryProgramCache> cache_;
  ShaderManager shader_manager_;
  // These shaders are owned by |shader_manager_|.
  raw_ptr<Shader> vertex_shader_;
  raw_ptr<Shader> fragment_shader_;
  int32_t shader_cache_count_;
  std::string shader_cache_shader_;
  std::vector<std::string> varyings_;
};

namespace {

std::array<char, 20> TwentyIncrementingChars() {
  std::array<char, 20> chars;
  std::iota(chars.begin(), chars.end(), 0);
  return chars;
}

}  // namespace

TEST_F(MemoryProgramCacheTest, CacheSave) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);

  EXPECT_EQ(ProgramCache::LINK_SUCCEEDED,
            cache_->GetLinkedProgramStatus(
                vertex_shader_->last_compiled_signature(),
                fragment_shader_->last_compiled_signature(), nullptr, varyings_,
                GL_NONE));
  EXPECT_EQ(1, shader_cache_count());
}

TEST_F(MemoryProgramCacheTest, LoadProgram) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);

  EXPECT_EQ(ProgramCache::LINK_SUCCEEDED,
            cache_->GetLinkedProgramStatus(
                vertex_shader_->last_compiled_signature(),
                fragment_shader_->last_compiled_signature(), nullptr, varyings_,
                GL_NONE));
  EXPECT_EQ(1, shader_cache_count());

  cache_->Clear();

  std::string blank;
  cache_->LoadProgram(blank, shader_cache_shader());
  EXPECT_EQ(ProgramCache::LINK_SUCCEEDED,
            cache_->GetLinkedProgramStatus(
                vertex_shader_->last_compiled_signature(),
                fragment_shader_->last_compiled_signature(), nullptr, varyings_,
                GL_NONE));
}

TEST_F(MemoryProgramCacheTest, CacheLoadMatchesSave) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);
  EXPECT_EQ(1, shader_cache_count());

  AttributeMap vertex_attrib_map = vertex_shader_->attrib_map();
  UniformMap vertex_uniform_map = vertex_shader_->uniform_map();
  VaryingMap vertex_varying_map = vertex_shader_->varying_map();
  OutputVariableList vertex_output_variable_list =
      vertex_shader_->output_variable_list();
  AttributeMap fragment_attrib_map = fragment_shader_->attrib_map();
  UniformMap fragment_uniform_map = fragment_shader_->uniform_map();
  VaryingMap fragment_varying_map = fragment_shader_->varying_map();
  OutputVariableList fragment_output_variable_list =
      fragment_shader_->output_variable_list();

  vertex_shader_->set_attrib_map(AttributeMap());
  vertex_shader_->set_uniform_map(UniformMap());
  vertex_shader_->set_varying_map(VaryingMap());
  vertex_shader_->set_output_variable_list(OutputVariableList());
  fragment_shader_->set_attrib_map(AttributeMap());
  fragment_shader_->set_uniform_map(UniformMap());
  fragment_shader_->set_varying_map(VaryingMap());
  fragment_shader_->set_output_variable_list(OutputVariableList());
  SetExpectationsForLoadLinkedProgram(kProgramId, &emulator);

  EXPECT_EQ(
      ProgramCache::PROGRAM_LOAD_SUCCESS,
      cache_->LoadLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                                nullptr, varyings_, GL_NONE, this));

  // apparently the hash_map implementation on android doesn't have the
  // equality operator
#if !BUILDFLAG(IS_ANDROID)
  EXPECT_EQ(vertex_attrib_map, vertex_shader_->attrib_map());
  EXPECT_EQ(vertex_uniform_map, vertex_shader_->uniform_map());
  EXPECT_EQ(vertex_varying_map, vertex_shader_->varying_map());
  EXPECT_EQ(vertex_output_variable_list,
            vertex_shader_->output_variable_list());
  EXPECT_EQ(fragment_attrib_map, fragment_shader_->attrib_map());
  EXPECT_EQ(fragment_uniform_map, fragment_shader_->uniform_map());
  EXPECT_EQ(fragment_varying_map, fragment_shader_->varying_map());
  EXPECT_EQ(fragment_output_variable_list,
            fragment_shader_->output_variable_list());
#endif
}

TEST_F(MemoryProgramCacheTest, LoadProgramMatchesSave) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);
  EXPECT_EQ(1, shader_cache_count());

  AttributeMap vertex_attrib_map = vertex_shader_->attrib_map();
  UniformMap vertex_uniform_map = vertex_shader_->uniform_map();
  VaryingMap vertex_varying_map = vertex_shader_->varying_map();
  OutputVariableList vertex_output_variable_list =
      vertex_shader_->output_variable_list();
  AttributeMap fragment_attrib_map = fragment_shader_->attrib_map();
  UniformMap fragment_uniform_map = fragment_shader_->uniform_map();
  VaryingMap fragment_varying_map = fragment_shader_->varying_map();
  OutputVariableList fragment_output_variable_list =
      fragment_shader_->output_variable_list();

  vertex_shader_->set_attrib_map(AttributeMap());
  vertex_shader_->set_uniform_map(UniformMap());
  vertex_shader_->set_varying_map(VaryingMap());
  vertex_shader_->set_output_variable_list(OutputVariableList());
  fragment_shader_->set_attrib_map(AttributeMap());
  fragment_shader_->set_uniform_map(UniformMap());
  fragment_shader_->set_varying_map(VaryingMap());
  fragment_shader_->set_output_variable_list(OutputVariableList());

  SetExpectationsForLoadLinkedProgram(kProgramId, &emulator);

  std::string blank;
  cache_->Clear();
  cache_->LoadProgram(blank, shader_cache_shader());

  EXPECT_EQ(
      ProgramCache::PROGRAM_LOAD_SUCCESS,
      cache_->LoadLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                                nullptr, varyings_, GL_NONE, this));

  // apparently the hash_map implementation on android doesn't have the
  // equality operator
#if !BUILDFLAG(IS_ANDROID)
  EXPECT_EQ(vertex_attrib_map, vertex_shader_->attrib_map());
  EXPECT_EQ(vertex_uniform_map, vertex_shader_->uniform_map());
  EXPECT_EQ(vertex_varying_map, vertex_shader_->varying_map());
  EXPECT_EQ(vertex_output_variable_list,
            vertex_shader_->output_variable_list());
  EXPECT_EQ(fragment_attrib_map, fragment_shader_->attrib_map());
  EXPECT_EQ(fragment_uniform_map, fragment_shader_->uniform_map());
  EXPECT_EQ(fragment_varying_map, fragment_shader_->varying_map());
  EXPECT_EQ(fragment_output_variable_list,
            fragment_shader_->output_variable_list());
#endif
}

TEST_F(MemoryProgramCacheTest, LoadFailOnLinkFalse) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);

  SetExpectationsForLoadLinkedProgramFailure(kProgramId, &emulator);
  EXPECT_EQ(
      ProgramCache::PROGRAM_LOAD_FAILURE,
      cache_->LoadLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                                nullptr, varyings_, GL_NONE, this));
}

TEST_F(MemoryProgramCacheTest, LoadFailOnDifferentSource) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);

  const std::string vertex_orig_source = vertex_shader_->last_compiled_source();
  vertex_shader_->set_source("different!");
  TestHelper::SetShaderStates(gl_.get(), vertex_shader_, true);
  EXPECT_EQ(
      ProgramCache::PROGRAM_LOAD_FAILURE,
      cache_->LoadLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                                nullptr, varyings_, GL_NONE, this));

  vertex_shader_->set_source(vertex_orig_source);
  TestHelper::SetShaderStates(gl_.get(), vertex_shader_, true);
  fragment_shader_->set_source("different!");
  TestHelper::SetShaderStates(gl_.get(), fragment_shader_, true);
  EXPECT_EQ(
      ProgramCache::PROGRAM_LOAD_FAILURE,
      cache_->LoadLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                                nullptr, varyings_, GL_NONE, this));
}

TEST_F(MemoryProgramCacheTest, LoadFailOnDifferentMap) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator);
  ProgramCache::LocationMap binding_map;
  binding_map["test"] = 512;
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            &binding_map, varyings_, GL_NONE, this);

  binding_map["different!"] = 59;
  EXPECT_EQ(
      ProgramCache::PROGRAM_LOAD_FAILURE,
      cache_->LoadLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                                &binding_map, varyings_, GL_NONE, this));
  EXPECT_EQ(
      ProgramCache::PROGRAM_LOAD_FAILURE,
      cache_->LoadLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                                nullptr, varyings_, GL_NONE, this));
}

TEST_F(MemoryProgramCacheTest, LoadFailOnDifferentTransformFeedbackVaryings) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator);
  varyings_.push_back("test");
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_INTERLEAVED_ATTRIBS, this);

  EXPECT_EQ(
      ProgramCache::PROGRAM_LOAD_FAILURE,
      cache_->LoadLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                                nullptr, varyings_, GL_SEPARATE_ATTRIBS, this));

  varyings_.push_back("different!");
  EXPECT_EQ(ProgramCache::PROGRAM_LOAD_FAILURE,
            cache_->LoadLinkedProgram(kProgramId, vertex_shader_,
                                      fragment_shader_, nullptr, varyings_,
                                      GL_INTERLEAVED_ATTRIBS, this));
}

TEST_F(MemoryProgramCacheTest, LoadFailIfTransformFeedbackCachingDisabled) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator(kFormat, test_binary);

  // Forcibly reset the program cache so we can disable caching of
  // programs which include transform feedback varyings.
  cache_.reset(new MemoryProgramCache(kCacheSizeBytes, kDisableGpuDiskCache,
                                      true, &use_shader_cache_shm_count_));
  varyings_.push_back("test");
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_INTERLEAVED_ATTRIBS, this);
  EXPECT_EQ(ProgramCache::PROGRAM_LOAD_FAILURE,
            cache_->LoadLinkedProgram(kProgramId, vertex_shader_,
                                      fragment_shader_, nullptr, varyings_,
                                      GL_INTERLEAVED_ATTRIBS, this));
}

TEST_F(MemoryProgramCacheTest, MemoryProgramCacheEviction) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator1(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator1);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);

  const int kEvictingProgramId = 11;
  const GLuint kEvictingBinaryLength = kCacheSizeBytes - test_binary.size() + 1;

  // save old source and modify for new program
  const std::string& old_sig = fragment_shader_->last_compiled_signature();
  fragment_shader_->set_source("al sdfkjdk");
  TestHelper::SetShaderStates(gl_.get(), fragment_shader_, true);

  auto bigTestBinary = base::HeapArray<char>::Uninit(kEvictingBinaryLength);
  for (size_t i = 0; i < kEvictingBinaryLength; ++i) {
    bigTestBinary[i] = i % 250;
  }
  ProgramBinaryEmulator emulator2(kFormat, bigTestBinary);

  SetExpectationsForSaveLinkedProgram(kEvictingProgramId, &emulator2);
  cache_->SaveLinkedProgram(kEvictingProgramId, vertex_shader_,
                            fragment_shader_, nullptr, varyings_, GL_NONE,
                            this);

  EXPECT_EQ(ProgramCache::LINK_SUCCEEDED,
            cache_->GetLinkedProgramStatus(
                vertex_shader_->last_compiled_signature(),
                fragment_shader_->last_compiled_signature(), nullptr, varyings_,
                GL_NONE));
  EXPECT_EQ(
      ProgramCache::LINK_UNKNOWN,
      cache_->GetLinkedProgramStatus(vertex_shader_->last_compiled_signature(),
                                     old_sig, nullptr, varyings_, GL_NONE));
}

TEST_F(MemoryProgramCacheTest, SaveCorrectProgram) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator1(kFormat, test_binary);

  vertex_shader_->set_source("different!");
  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator1);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);

  EXPECT_EQ(ProgramCache::LINK_SUCCEEDED,
            cache_->GetLinkedProgramStatus(
                vertex_shader_->last_compiled_signature(),
                fragment_shader_->last_compiled_signature(), nullptr, varyings_,
                GL_NONE));
}

TEST_F(MemoryProgramCacheTest, LoadCorrectProgram) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);

  EXPECT_EQ(ProgramCache::LINK_SUCCEEDED,
            cache_->GetLinkedProgramStatus(
                vertex_shader_->last_compiled_signature(),
                fragment_shader_->last_compiled_signature(), nullptr, varyings_,
                GL_NONE));

  SetExpectationsForLoadLinkedProgram(kProgramId, &emulator);

  fragment_shader_->set_source("different!");
  EXPECT_EQ(
      ProgramCache::PROGRAM_LOAD_SUCCESS,
      cache_->LoadLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                                nullptr, varyings_, GL_NONE, this));
}

TEST_F(MemoryProgramCacheTest, OverwriteOnNewSave) {
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);

  std::array<char, test_binary.size()> test_binary2;
  for (size_t i = 0; i < test_binary.size(); ++i) {
    test_binary2[i] = (i*2) % 250;
  }
  ProgramBinaryEmulator emulator2(kFormat, test_binary2);
  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator2);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);

  SetExpectationsForLoadLinkedProgram(kProgramId, &emulator2);
  EXPECT_EQ(
      ProgramCache::PROGRAM_LOAD_SUCCESS,
      cache_->LoadLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                                nullptr, varyings_, GL_NONE, this));
}

TEST_F(MemoryProgramCacheTest, MemoryProgramCacheTrim) {
  // Insert a 20 byte program.
  const GLenum kFormat = 1;
  const int kProgramId = 10;
  const auto test_binary = TwentyIncrementingChars();
  ProgramBinaryEmulator emulator1(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kProgramId, &emulator1);
  cache_->SaveLinkedProgram(kProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);

  // Insert a second 20 byte program.
  const int kSecondProgramId = 11;
  const std::string& first_sig = fragment_shader_->last_compiled_signature();

  fragment_shader_->set_source("al sdfkjdk");
  TestHelper::SetShaderStates(gl_.get(), fragment_shader_, true);
  ProgramBinaryEmulator emulator2(kFormat, test_binary);

  SetExpectationsForSaveLinkedProgram(kSecondProgramId, &emulator2);
  cache_->SaveLinkedProgram(kSecondProgramId, vertex_shader_, fragment_shader_,
                            nullptr, varyings_, GL_NONE, this);

  // Both programs should be present.
  EXPECT_EQ(ProgramCache::LINK_SUCCEEDED,
            cache_->GetLinkedProgramStatus(
                vertex_shader_->last_compiled_signature(),
                fragment_shader_->last_compiled_signature(), nullptr, varyings_,
                GL_NONE));
  EXPECT_EQ(
      ProgramCache::LINK_SUCCEEDED,
      cache_->GetLinkedProgramStatus(vertex_shader_->last_compiled_signature(),
                                     first_sig, nullptr, varyings_, GL_NONE));

  // Trim cache to 20 bytes - this should evict the first program.
  cache_->Trim(20);
  EXPECT_EQ(ProgramCache::LINK_SUCCEEDED,
            cache_->GetLinkedProgramStatus(
                vertex_shader_->last_compiled_signature(),
                fragment_shader_->last_compiled_signature(), nullptr, varyings_,
                GL_NONE));
  EXPECT_EQ(
      ProgramCache::LINK_UNKNOWN,
      cache_->GetLinkedProgramStatus(vertex_shader_->last_compiled_signature(),
                                     first_sig, nullptr, varyings_, GL_NONE));

  // Trim cache to 0 bytes - this should evict both programs.
  cache_->Trim(0);
  EXPECT_EQ(ProgramCache::LINK_UNKNOWN,
            cache_->GetLinkedProgramStatus(
                vertex_shader_->last_compiled_signature(),
                fragment_shader_->last_compiled_signature(), nullptr, varyings_,
                GL_NONE));
  EXPECT_EQ(
      ProgramCache::LINK_UNKNOWN,
      cache_->GetLinkedProgramStatus(vertex_shader_->last_compiled_signature(),
                                     first_sig, nullptr, varyings_, GL_NONE));
}

}  // namespace gles2
}  // namespace gpu
