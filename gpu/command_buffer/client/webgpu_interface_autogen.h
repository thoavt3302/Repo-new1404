// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is auto-generated from
// gpu/command_buffer/build_webgpu_cmd_buffer.py
// It's formatted by clang-format using chromium coding style:
//    clang-format -i -style=chromium filename
// DO NOT EDIT!

// This file is included by webgpu_interface.h to declare the
// GL api functions.
#ifndef GPU_COMMAND_BUFFER_CLIENT_WEBGPU_INTERFACE_AUTOGEN_H_
#define GPU_COMMAND_BUFFER_CLIENT_WEBGPU_INTERFACE_AUTOGEN_H_

virtual void DissociateMailbox(GLuint texture_id,
                               GLuint texture_generation) = 0;
virtual void DissociateMailboxForBuffer(GLuint buffer_id,
                                        GLuint buffer_generation) = 0;
virtual void DissociateMailboxForPresent(GLuint device_id,
                                         GLuint device_generation,
                                         GLuint texture_id,
                                         GLuint texture_generation) = 0;
virtual void SetWebGPUExecutionContextToken(uint32_t type,
                                            uint32_t high_high,
                                            uint32_t high_low,
                                            uint32_t low_high,
                                            uint32_t low_low) = 0;
#endif  // GPU_COMMAND_BUFFER_CLIENT_WEBGPU_INTERFACE_AUTOGEN_H_
