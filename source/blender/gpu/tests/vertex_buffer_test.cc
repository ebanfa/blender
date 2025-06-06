/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: Apache-2.0 */

#include "testing/testing.h"

#include "GPU_attribute_convert.hh"
#include "GPU_batch.hh"
#include "GPU_framebuffer.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_vertex_buffer.hh"
#include "GPU_vertex_format.hh"

#include "BLI_index_range.hh"
#include "BLI_math_vector_types.hh"

#include "gpu_testing.hh"

namespace blender::gpu::tests {

static constexpr int Size = 2;

template<GPUVertCompType comp_type, GPUVertFetchMode fetch_mode, typename ColorType>
static void vertex_buffer_fetch_mode(ColorType color)
{
  eGPUTextureUsage usage = GPU_TEXTURE_USAGE_ATTACHMENT | GPU_TEXTURE_USAGE_HOST_READ;
  GPUOffScreen *offscreen = GPU_offscreen_create(Size, Size, false, GPU_RGBA32F, usage, nullptr);
  BLI_assert(offscreen != nullptr);
  GPU_offscreen_bind(offscreen, false);
  GPUTexture *color_texture = GPU_offscreen_color_texture(offscreen);
  GPU_texture_clear(color_texture, GPU_DATA_FLOAT, float4(1.0f, 2.0f, 3.0f, 0.0f));

  GPUVertFormat format = {0};
  GPU_vertformat_attr_add(&format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  GPU_vertformat_attr_add(&format, "color", comp_type, 4, fetch_mode);

  struct Vert {
    float2 pos;
    ColorType color;
  };
  std::array<Vert, 3> data = {
      Vert{float2(-1.0, -1.0), color},
      Vert{float2(3.0, -1.0), color},
      Vert{float2(-1.0, 3.0), color},
  };

  VertBuf *vbo = GPU_vertbuf_create_with_format(format);
  GPU_vertbuf_data_alloc(*vbo, data.size());

  for (int i : IndexRange(data.size())) {
    GPU_vertbuf_vert_set(vbo, i, &data[i]);
  }

  Batch *batch = GPU_batch_create_ex(GPU_PRIM_TRIS, vbo, nullptr, GPU_BATCH_OWNS_VBO);
  GPU_batch_program_set_builtin(batch, GPU_SHADER_3D_FLAT_COLOR);
  GPU_batch_draw(batch);

  GPU_offscreen_unbind(offscreen, false);
  GPU_flush();

  /* Read back data and perform some basic tests. */
  Vector<float4> read_data(Size * Size);

  GPU_offscreen_read_color(offscreen, GPU_DATA_FLOAT, read_data.data());

  if constexpr (fetch_mode == GPU_FETCH_INT_TO_FLOAT_UNIT) {
    switch (comp_type) {
      case GPU_COMP_I8:
        read_data[0] = read_data[0] * float(127);
        break;
      case GPU_COMP_U8:
        read_data[0] = read_data[0] * float(255);
        break;
      case GPU_COMP_I16:
        read_data[0] = read_data[0] * float(32767);
        break;
      case GPU_COMP_U16:
        read_data[0] = read_data[0] * float(65535);
        break;
      case GPU_COMP_I10:
        read_data[0] = read_data[0] * float4(511, 511, 511, 1);
        break;
      default:
        BLI_assert_unreachable();
    }
  }

  if (fetch_mode == GPU_FETCH_FLOAT) {
    EXPECT_EQ(read_data[0], float4(color));
  }
  else {
    /* Do integer comparison to avoid floating point inaccuracies from each conversion steps. */
    EXPECT_EQ(int4(read_data[0]), int4(float4(color)));
  }

  GPU_batch_discard(batch);
  GPU_offscreen_free(offscreen);
}

static void test_vertex_buffer_fetch_mode__GPU_COMP_I8__GPU_FETCH_INT_TO_FLOAT_UNIT()
{
  vertex_buffer_fetch_mode<GPU_COMP_I8, GPU_FETCH_INT_TO_FLOAT_UNIT, char4>(
      char4(100, -127, 127, 0));
}
GPU_TEST(vertex_buffer_fetch_mode__GPU_COMP_I8__GPU_FETCH_INT_TO_FLOAT_UNIT);

static void test_vertex_buffer_fetch_mode__GPU_COMP_U8__GPU_FETCH_INT_TO_FLOAT_UNIT()
{
  vertex_buffer_fetch_mode<GPU_COMP_U8, GPU_FETCH_INT_TO_FLOAT_UNIT, uchar4>(
      uchar4(100, 0, 255, 127));
}
GPU_TEST(vertex_buffer_fetch_mode__GPU_COMP_U8__GPU_FETCH_INT_TO_FLOAT_UNIT);

static void test_vertex_buffer_fetch_mode__GPU_COMP_I16__GPU_FETCH_INT_TO_FLOAT_UNIT()
{
  vertex_buffer_fetch_mode<GPU_COMP_I16, GPU_FETCH_INT_TO_FLOAT_UNIT, short4>(
      short4(12034, -32767, 32767, 0));
}
GPU_TEST(vertex_buffer_fetch_mode__GPU_COMP_I16__GPU_FETCH_INT_TO_FLOAT_UNIT);

static void test_vertex_buffer_fetch_mode__GPU_COMP_U16__GPU_FETCH_INT_TO_FLOAT_UNIT()
{
  vertex_buffer_fetch_mode<GPU_COMP_U16, GPU_FETCH_INT_TO_FLOAT_UNIT, ushort4>(
      ushort4(12034, 0, 65535, 32767));
}
GPU_TEST(vertex_buffer_fetch_mode__GPU_COMP_U16__GPU_FETCH_INT_TO_FLOAT_UNIT);

static void test_vertex_buffer_fetch_mode__GPU_COMP_I10__GPU_FETCH_INT_TO_FLOAT_UNIT()
{
  vertex_buffer_fetch_mode<GPU_COMP_I10, GPU_FETCH_INT_TO_FLOAT_UNIT, PackedNormal>(
      PackedNormal(321, -511, 511, 0));
}
GPU_TEST(vertex_buffer_fetch_mode__GPU_COMP_I10__GPU_FETCH_INT_TO_FLOAT_UNIT);

static void test_vertex_buffer_fetch_mode__GPU_COMP_F32__GPU_FETCH_FLOAT()
{
  vertex_buffer_fetch_mode<GPU_COMP_F32, GPU_FETCH_FLOAT, float4>(float4(4, 5, 6, 1));
}
GPU_TEST(vertex_buffer_fetch_mode__GPU_COMP_F32__GPU_FETCH_FLOAT);

}  // namespace blender::gpu::tests
