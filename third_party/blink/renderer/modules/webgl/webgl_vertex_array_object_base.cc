// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/webgl/webgl_vertex_array_object_base.h"

#include "gpu/command_buffer/client/gles2_interface.h"
#include "third_party/blink/renderer/modules/webgl/webgl_context_object_support.h"

namespace blink {

WebGLVertexArrayObjectBase::WebGLVertexArrayObjectBase(
    WebGLContextObjectSupport* ctx,
    VaoType type,
    GLint max_vertex_attribs)
    : WebGLObject(ctx),
      type_(type),
      has_ever_been_bound_(false),
      is_all_enabled_attrib_buffer_bound_(true) {
  if (!ctx || ctx->IsLost()) {
    return;
  }

  array_buffer_list_.resize(max_vertex_attribs);
  attrib_enabled_.resize(max_vertex_attribs);
  for (wtf_size_t i = 0; i < attrib_enabled_.size(); ++i) {
    attrib_enabled_[i] = false;
  }

  switch (type_) {
    case kVaoTypeDefault:
      break;
    default: {
      GLuint vao = 0;
      ctx->ContextGL()->GenVertexArraysOES(1, &vao);
      SetObject(vao);
      break;
    }
  }
}

WebGLVertexArrayObjectBase::~WebGLVertexArrayObjectBase() = default;

void WebGLVertexArrayObjectBase::DispatchDetached(
    gpu::gles2::GLES2Interface* gl) {
  if (bound_element_array_buffer_)
    bound_element_array_buffer_->OnDetached(gl);

  for (WebGLBuffer* buffer : array_buffer_list_) {
    if (buffer)
      buffer->OnDetached(gl);
  }
}

void WebGLVertexArrayObjectBase::DeleteObjectImpl(
    gpu::gles2::GLES2Interface* gl) {
  switch (type_) {
    case kVaoTypeDefault:
      break;
    default:
      gl->DeleteVertexArraysOES(1, &Object());
      break;
  }

  // Member<> objects must not be accessed during the destruction,
  // since they could have been already finalized.
  // The finalizers of these objects will handle their detachment
  // by themselves.
  if (!DestructionInProgress())
    DispatchDetached(gl);
}

void WebGLVertexArrayObjectBase::SetElementArrayBuffer(WebGLBuffer* buffer) {
  if (buffer)
    buffer->OnAttached();
  if (bound_element_array_buffer_)
    bound_element_array_buffer_->OnDetached(Context()->ContextGL());
  bound_element_array_buffer_ = buffer;
}

WebGLBuffer* WebGLVertexArrayObjectBase::GetArrayBufferForAttrib(GLuint index) {
  DCHECK(index < array_buffer_list_.size());
  return array_buffer_list_[index].Get();
}

void WebGLVertexArrayObjectBase::SetArrayBufferForAttrib(GLuint index,
                                                         WebGLBuffer* buffer) {
  if (buffer)
    buffer->OnAttached();
  if (array_buffer_list_[index])
    array_buffer_list_[index]->OnDetached(Context()->ContextGL());

  array_buffer_list_[index] = buffer;
  UpdateAttribBufferBoundStatus();
}

void WebGLVertexArrayObjectBase::SetAttribEnabled(GLuint index, bool enabled) {
  DCHECK(index < attrib_enabled_.size());
  attrib_enabled_[index] = enabled;
  UpdateAttribBufferBoundStatus();
}

void WebGLVertexArrayObjectBase::UpdateAttribBufferBoundStatus() {
  is_all_enabled_attrib_buffer_bound_ = true;
  for (wtf_size_t i = 0; i < attrib_enabled_.size(); ++i) {
    if (attrib_enabled_[i] && !array_buffer_list_[i]) {
      is_all_enabled_attrib_buffer_bound_ = false;
      return;
    }
  }
}

void WebGLVertexArrayObjectBase::UnbindBuffer(WebGLBuffer* buffer) {
  if (bound_element_array_buffer_ == buffer) {
    bound_element_array_buffer_->OnDetached(Context()->ContextGL());
    bound_element_array_buffer_ = nullptr;
  }

  for (wtf_size_t i = 0; i < array_buffer_list_.size(); ++i) {
    if (array_buffer_list_[i] == buffer) {
      array_buffer_list_[i]->OnDetached(Context()->ContextGL());
      array_buffer_list_[i] = nullptr;
    }
  }
  UpdateAttribBufferBoundStatus();
}

void WebGLVertexArrayObjectBase::Trace(Visitor* visitor) const {
  visitor->Trace(bound_element_array_buffer_);
  visitor->Trace(array_buffer_list_);
  WebGLObject::Trace(visitor);
}

}  // namespace blink
