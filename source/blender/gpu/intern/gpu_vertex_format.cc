/* SPDX-FileCopyrightText: 2016 by Mike Erwin. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gpu
 *
 * GPU vertex format
 */

#include "GPU_vertex_format.hh"
#include "BLI_assert.h"
#include "BLI_math_base.h"
#include "GPU_capabilities.hh"

#include "gpu_shader_create_info.hh"
#include "gpu_shader_private.hh"
#include "gpu_vertex_format_private.hh"

#include <cstddef>
#include <cstring>

#include "BLI_hash_mm2a.hh"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#define PACK_DEBUG 0

#if PACK_DEBUG
#  include <stdio.h>
#endif

namespace blender::gpu {

/* Used to combine legacy enums into new vertex attribute type. */
static VertAttrType vertex_format_combine(GPUVertCompType component_type,
                                          GPUVertFetchMode fetch_mode,
                                          uint32_t component_len)
{
  switch (component_type) {
    case GPU_COMP_I8: {
      switch (fetch_mode) {
        case GPU_FETCH_INT_TO_FLOAT_UNIT:
          switch (component_len) {
            case 1:
              return VertAttrType::SNORM_8_DEPRECATED;
            case 2:
              return VertAttrType::SNORM_8_8_DEPRECATED;
            case 3:
              return VertAttrType::SNORM_8_8_8_DEPRECATED;
            case 4:
              return VertAttrType::SNORM_8_8_8_8;
          }
          break;
        case GPU_FETCH_INT:
          switch (component_len) {
            case 1:
              return VertAttrType::SINT_8_DEPRECATED;
            case 2:
              return VertAttrType::SINT_8_8_DEPRECATED;
            case 3:
              return VertAttrType::SINT_8_8_8_DEPRECATED;
            case 4:
              return VertAttrType::SINT_8_8_8_8;
          }
          break;
        default:
          break;
      }
      break;
    }
    case GPU_COMP_U8: {
      switch (fetch_mode) {
        case GPU_FETCH_INT_TO_FLOAT_UNIT:
          switch (component_len) {
            case 1:
              return VertAttrType::UNORM_8_DEPRECATED;
            case 2:
              return VertAttrType::UNORM_8_8_DEPRECATED;
            case 3:
              return VertAttrType::UNORM_8_8_8_DEPRECATED;
            case 4:
              return VertAttrType::UNORM_8_8_8_8;
          }
          break;
        case GPU_FETCH_INT:
          switch (component_len) {
            case 1:
              return VertAttrType::UINT_8_DEPRECATED;
            case 2:
              return VertAttrType::UINT_8_8_DEPRECATED;
            case 3:
              return VertAttrType::UINT_8_8_8_DEPRECATED;
            case 4:
              return VertAttrType::UINT_8_8_8_8;
          }
          break;
        default:
          break;
      }
      break;
    }
    case GPU_COMP_I16: {
      switch (fetch_mode) {
        case GPU_FETCH_INT_TO_FLOAT_UNIT:
          switch (component_len) {
            case 1:
              return VertAttrType::SNORM_16_DEPRECATED;
            case 2:
              return VertAttrType::SNORM_16_16;
            case 3:
              return VertAttrType::SNORM_16_16_16_DEPRECATED;
            case 4:
              return VertAttrType::SNORM_16_16_16_16;
          }
          break;
        case GPU_FETCH_INT:
          switch (component_len) {
            case 1:
              return VertAttrType::SINT_16_DEPRECATED;
            case 2:
              return VertAttrType::SINT_16_16;
            case 3:
              return VertAttrType::SINT_16_16_16_DEPRECATED;
            case 4:
              return VertAttrType::SINT_16_16_16_16;
          }
          break;
        default:
          break;
      }
      break;
    }
    case GPU_COMP_U16: {
      switch (fetch_mode) {
        case GPU_FETCH_INT_TO_FLOAT_UNIT:
          switch (component_len) {
            case 1:
              return VertAttrType::UNORM_16_DEPRECATED;
            case 2:
              return VertAttrType::UNORM_16_16;
            case 3:
              return VertAttrType::UNORM_16_16_16_DEPRECATED;
            case 4:
              return VertAttrType::UNORM_16_16_16_16;
          }
          break;
        case GPU_FETCH_INT:
          switch (component_len) {
            case 1:
              return VertAttrType::UINT_16_DEPRECATED;
            case 2:
              return VertAttrType::UINT_16_16;
            case 3:
              return VertAttrType::UINT_16_16_16_DEPRECATED;
            case 4:
              return VertAttrType::UINT_16_16_16_16;
          }
          break;
        default:
          break;
      }
      break;
    }
    case GPU_COMP_I32: {
      switch (fetch_mode) {
        case GPU_FETCH_INT:
          switch (component_len) {
            case 1:
              return VertAttrType::SINT_32;
            case 2:
              return VertAttrType::SINT_32_32;
            case 3:
              return VertAttrType::SINT_32_32_32;
            case 4:
              return VertAttrType::SINT_32_32_32_32;
          }
          break;
        default:
          break;
      }
      break;
    }
    case GPU_COMP_U32: {
      switch (fetch_mode) {
        case GPU_FETCH_INT:
          switch (component_len) {
            case 1:
              return VertAttrType::UINT_32;
            case 2:
              return VertAttrType::UINT_32_32;
            case 3:
              return VertAttrType::UINT_32_32_32;
            case 4:
              return VertAttrType::UINT_32_32_32_32;
          }
          break;
        default:
          break;
      }
      break;
    }
    case GPU_COMP_F32: {
      switch (fetch_mode) {
        case GPU_FETCH_FLOAT:
          switch (component_len) {
            case 1:
              return VertAttrType::SFLOAT_32;
            case 2:
              return VertAttrType::SFLOAT_32_32;
            case 3:
              return VertAttrType::SFLOAT_32_32_32;
            case 4:
              return VertAttrType::SFLOAT_32_32_32_32;
          }
          break;
        default:
          break;
      }
      break;
    }
    case GPU_COMP_I10: {
      switch (fetch_mode) {
        case GPU_FETCH_INT_TO_FLOAT_UNIT:
          return VertAttrType::SNORM_10_10_10_2;
        default:
          break;
      }
      break;
    }
    case GPU_COMP_MAX:
      break;
  }

  return VertAttrType::Invalid;
};

bool is_fetch_normalized(VertAttrType attr_type)
{
  switch (attr_type) {
    case VertAttrType::SNORM_8_8_8_8:
    case VertAttrType::SNORM_16_16:
    case VertAttrType::SNORM_16_16_16_16:
    case VertAttrType::UNORM_8_8_8_8:
    case VertAttrType::UNORM_16_16:
    case VertAttrType::UNORM_16_16_16_16:
    case VertAttrType::SNORM_10_10_10_2:
    case VertAttrType::UNORM_10_10_10_2:
      return true;
    default:
      return false;
  }
};

bool is_fetch_int_to_float(VertAttrType attr_type)
{
  switch (attr_type) {
    case VertAttrType::SINT_TO_FLT_32:
    case VertAttrType::SINT_TO_FLT_32_32:
    case VertAttrType::SINT_TO_FLT_32_32_32:
    case VertAttrType::SINT_TO_FLT_32_32_32_32:
      return true;
    default:
      return false;
  }
};

bool is_fetch_float(VertAttrType attr_type)
{
  switch (attr_type) {
    case VertAttrType::SFLOAT_32:
    case VertAttrType::SFLOAT_32_32:
    case VertAttrType::SFLOAT_32_32_32:
    case VertAttrType::SFLOAT_32_32_32_32:
      return true;
    default:
      return false;
  }
};

}  // namespace blender::gpu

using blender::StringRef;
using namespace blender::gpu;
using namespace blender::gpu::shader;

void GPU_vertformat_clear(GPUVertFormat *format)
{
#ifndef NDEBUG
  memset(format, 0, sizeof(GPUVertFormat));
#else
  format->attr_len = 0;
  format->packed = false;
  format->name_offset = 0;
  format->name_len = 0;
  format->deinterleaved = false;

  for (uint i = 0; i < GPU_VERT_ATTR_MAX_LEN; i++) {
    format->attrs[i].name_len = 0;
  }
#endif
}

void GPU_vertformat_copy(GPUVertFormat *dest, const GPUVertFormat &src)
{
  /* copy regular struct fields */
  memcpy(dest, &src, sizeof(GPUVertFormat));
}

static uint comp_size(GPUVertCompType type)
{
  BLI_assert(type <= GPU_COMP_F32); /* other types have irregular sizes (not bytes) */
  const uint sizes[] = {1, 1, 2, 2, 4, 4, 4};
  return sizes[type];
}

static uint attr_size(const GPUVertAttr *a)
{
  if (a->comp_type == GPU_COMP_I10) {
    return 4; /* always packed as 10_10_10_2 */
  }
  return a->comp_len * comp_size(static_cast<GPUVertCompType>(a->comp_type));
}

static uint attr_align(const GPUVertAttr *a, uint minimum_stride)
{
  if (a->comp_type == GPU_COMP_I10) {
    return 4; /* always packed as 10_10_10_2 */
  }
  uint c = comp_size(static_cast<GPUVertCompType>(a->comp_type));
  if (a->comp_len == 3 && c <= 2) {
    return 4 * c; /* AMD HW can't fetch these well, so pad it out (other vendors too?) */
  }

  /* Most fetches are ok if components are naturally aligned.
   * However, in Metal,the minimum supported per-vertex stride is 4,
   * so we must query the GPU and pad out the size accordingly. */
  return max_ii(minimum_stride, c);
}

uint vertex_buffer_size(const GPUVertFormat *format, uint vertex_len)
{
  BLI_assert(format->packed && format->stride > 0);
  return format->stride * vertex_len;
}

static uchar copy_attr_name(GPUVertFormat *format, const StringRef name)
{
  const uchar name_offset = format->name_offset;
  /* Subtract one to make sure there's enough space for the last null terminator. */
  const int64_t available = GPU_VERT_ATTR_NAMES_BUF_LEN - name_offset - 1;
  const int64_t chars_to_copy = std::min(name.size(), available);

  name.substr(0, available).copy_unsafe(format->names + name_offset);
  format->name_offset += chars_to_copy + 1;

  BLI_assert(format->name_offset <= GPU_VERT_ATTR_NAMES_BUF_LEN);
  return name_offset;
}

uint GPU_vertformat_attr_add(GPUVertFormat *format,
                             const StringRef name,
                             GPUVertCompType comp_type,
                             uint comp_len,
                             GPUVertFetchMode fetch_mode)
{
  BLI_assert(format->name_len < GPU_VERT_FORMAT_MAX_NAMES); /* there's room for more */
  BLI_assert(format->attr_len < GPU_VERT_ATTR_MAX_LEN);     /* there's room for more */
  BLI_assert(!format->packed);                              /* packed means frozen/locked */
  BLI_assert((comp_len >= 1 && comp_len <= 4) || comp_len == 8 || comp_len == 12 ||
             comp_len == 16);

  switch (comp_type) {
    case GPU_COMP_F32:
      /* float type can only kept as float */
      BLI_assert(fetch_mode == GPU_FETCH_FLOAT);
      break;
    case GPU_COMP_I10:
      /* 10_10_10 format intended for normals (XYZ) or colors (RGB)
       * extra component packed.w can be manually set to { -2, -1, 0, 1 } */
      BLI_assert(ELEM(comp_len, 3, 4));

      /* Not strictly required, may relax later. */
      BLI_assert(fetch_mode == GPU_FETCH_INT_TO_FLOAT_UNIT);

      break;
    default:
      /* integer types can be kept as int or converted/normalized to float */
      BLI_assert(fetch_mode != GPU_FETCH_FLOAT);
      /* only support float matrices (see Batch_update_program_bindings) */
      BLI_assert(!ELEM(comp_len, 8, 12, 16));
  }

  format->name_len++; /* Multi-name support. */

  const uint attr_id = format->attr_len++;
  GPUVertAttr *attr = &format->attrs[attr_id];

  attr->names[attr->name_len++] = copy_attr_name(format, name);
  attr->comp_type = comp_type;
  attr->comp_len = (comp_type == GPU_COMP_I10) ?
                       4 :
                       comp_len; /* system needs 10_10_10_2 to be 4 or BGRA */
  attr->size = attr_size(attr);
  attr->offset = 0; /* offsets & stride are calculated later (during pack) */
  attr->fetch_mode = fetch_mode;
  attr->format = vertex_format_combine(comp_type, fetch_mode, comp_len);
  BLI_assert(attr->format != blender::gpu::VertAttrType::Invalid);

  return attr_id;
}

void GPU_vertformat_alias_add(GPUVertFormat *format, const StringRef alias)
{
  GPUVertAttr *attr = &format->attrs[format->attr_len - 1];
  BLI_assert(format->name_len < GPU_VERT_FORMAT_MAX_NAMES); /* there's room for more */
  BLI_assert(attr->name_len < GPU_VERT_ATTR_MAX_NAMES);
  format->name_len++; /* Multi-name support. */
  attr->names[attr->name_len++] = copy_attr_name(format, alias);
}

GPUVertFormat GPU_vertformat_from_attribute(const StringRef name,
                                            const GPUVertCompType comp_type,
                                            const uint comp_len,
                                            const GPUVertFetchMode fetch_mode)
{
  GPUVertFormat format{};
  GPU_vertformat_attr_add(&format, name, comp_type, comp_len, fetch_mode);
  return format;
}

void GPU_vertformat_multiload_enable(GPUVertFormat *format, int load_count)
{
  /* Sanity check. Maximum can be upgraded if needed. */
  BLI_assert(load_count > 1 && load_count < 5);
  /* We need a packed format because of format->stride. */
  if (!format->packed) {
    VertexFormat_pack(format);
  }

  BLI_assert((format->name_len + 1) * load_count < GPU_VERT_FORMAT_MAX_NAMES);
  BLI_assert(format->attr_len * load_count <= GPU_VERT_ATTR_MAX_LEN);
  BLI_assert(format->name_offset * load_count < GPU_VERT_ATTR_NAMES_BUF_LEN);

  const GPUVertAttr *attr = format->attrs;
  int attr_len = format->attr_len;
  for (int i = 0; i < attr_len; i++, attr++) {
    const char *attr_name = GPU_vertformat_attr_name_get(format, attr, 0);
    for (int j = 1; j < load_count; j++) {
      char load_name[68 /* MAX_CUSTOMDATA_LAYER_NAME */];
      SNPRINTF(load_name, "%s%d", attr_name, j);
      GPUVertAttr *dst_attr = &format->attrs[format->attr_len++];
      *dst_attr = *attr;

      dst_attr->names[0] = copy_attr_name(format, load_name);
      dst_attr->name_len = 1;
      dst_attr->offset += format->stride * j;
    }
  }
}

int GPU_vertformat_attr_id_get(const GPUVertFormat *format, const StringRef name)
{
  for (int i = 0; i < format->attr_len; i++) {
    const GPUVertAttr *attr = &format->attrs[i];
    for (int j = 0; j < attr->name_len; j++) {
      const char *attr_name = GPU_vertformat_attr_name_get(format, attr, j);
      if (name == attr_name) {
        return i;
      }
    }
  }
  return -1;
}

void GPU_vertformat_attr_rename(GPUVertFormat *format, int attr_id, const char *new_name)
{
  BLI_assert(attr_id > -1 && attr_id < format->attr_len);
  GPUVertAttr *attr = &format->attrs[attr_id];
  char *attr_name = (char *)GPU_vertformat_attr_name_get(format, attr, 0);
  BLI_assert(strlen(attr_name) == strlen(new_name));
  int i = 0;
  while (attr_name[i] != '\0') {
    attr_name[i] = new_name[i];
    i++;
  }
  attr->name_len = 1;
}

/* Encode 8 original bytes into 11 safe bytes. */
static void safe_bytes(char out[11], const char data[8])
{
  const char safe_chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

  uint64_t in = *(uint64_t *)data;
  for (int i = 0; i < 11; i++) {
    out[i] = safe_chars[in % 62lu];
    in /= 62lu;
  }
}

void GPU_vertformat_safe_attr_name(const StringRef attr_name, char *r_safe_name, uint /*max_len*/)
{
  char data[8] = {0};
  uint len = attr_name.size();

  if (len > 8) {
    /* Start with the first 4 chars of the name. */
    memcpy(data, attr_name.data(), 4);
    /* We use a hash to identify each data layer based on its name.
     * NOTE: This is still prone to hash collision but the risks are very low. */
    /* Start hashing after the first 2 chars. */
    const StringRef to_hash = attr_name.drop_prefix(4);
    *(uint *)&data[4] = BLI_hash_mm2(
        reinterpret_cast<const uchar *>(to_hash.data()), to_hash.size(), 0);
  }
  else {
    /* Copy the whole name. Collision is barely possible
     * (hash would have to be equal to the last 4 bytes). */
    memcpy(data, attr_name.data(), std::min<int>(8, len));
  }
  /* Convert to safe bytes characters. */
  safe_bytes(r_safe_name, data);
  /* End the string */
  r_safe_name[11] = '\0';

  BLI_assert(GPU_MAX_SAFE_ATTR_NAME >= 12);
#if 0 /* For debugging */
  printf("%s > %lx > %s\n", attr_name, *(uint64_t *)data, r_safe_name);
#endif
}

void GPU_vertformat_deinterleave(GPUVertFormat *format)
{
  /* Ideally we should change the stride and offset here. This would allow
   * us to use GPU_vertbuf_attr_set / GPU_vertbuf_attr_fill. But since
   * we use only 11 bits for attr->offset this limits the size of the
   * buffer considerably. So instead we do the conversion when creating
   * bindings in create_bindings(). */
  format->deinterleaved = true;
}

uint padding(uint offset, uint alignment)
{
  const uint mod = offset % alignment;
  return (mod == 0) ? 0 : (alignment - mod);
}

#if PACK_DEBUG
static void show_pack(uint a_idx, uint size, uint pad)
{
  const char c = 'A' + a_idx;
  for (uint i = 0; i < pad; i++) {
    putchar('-');
  }
  for (uint i = 0; i < size; i++) {
    putchar(c);
  }
}
#endif

static void VertexFormat_pack_impl(GPUVertFormat *format, uint minimum_stride)
{
  GPUVertAttr *a0 = &format->attrs[0];
  a0->offset = 0;
  uint offset = a0->size;

#if PACK_DEBUG
  show_pack(0, a0->size, 0);
#endif

  for (uint a_idx = 1; a_idx < format->attr_len; a_idx++) {
    GPUVertAttr *a = &format->attrs[a_idx];
    uint mid_padding = padding(offset, attr_align(a, minimum_stride));
    offset += mid_padding;
    a->offset = offset;
    offset += a->size;

#if PACK_DEBUG
    show_pack(a_idx, a->size, mid_padding);
#endif
  }

  uint end_padding = padding(offset, attr_align(a0, minimum_stride));

#if PACK_DEBUG
  show_pack(0, 0, end_padding);
  putchar('\n');
#endif
  format->stride = offset + end_padding;
  format->packed = true;
}

void VertexFormat_pack(GPUVertFormat *format)
{
  /* Perform standard vertex packing, ensuring vertex format satisfies
   * minimum stride requirements for vertex assembly. */
  VertexFormat_pack_impl(format, GPU_minimum_per_vertex_stride());
}

void VertexFormat_texture_buffer_pack(GPUVertFormat *format)
{
  /* Validates packing for vertex formats used with texture buffers.
   * In these cases, there must only be a single vertex attribute.
   * This attribute should be tightly packed without padding, to ensure
   * it aligns with the backing texture data format, skipping
   * minimum per-vertex stride, which mandates 4-byte alignment in Metal.
   * This additional alignment padding caused smaller data types, e.g. U16,
   * to mis-align. */
  for (int i = 0; i < format->attr_len; i++) {
    /* The buffer texture setup uses the first attribute for type and size.
     * Make sure all attributes use the same size. */
    BLI_assert_msg(format->attrs[i].size == format->attrs[0].size,
                   "Texture buffer mode should only use a attributes with the same size.");
  }

  /* Pack vertex format without minimum stride, as this is not required by texture buffers. */
  VertexFormat_pack_impl(format, 1);
}

static uint component_size_get(const Type gpu_type)
{
  switch (gpu_type) {
    case Type::float2_t:
    case Type::int2_t:
    case Type::uint2_t:
      return 2;
    case Type::float3_t:
    case Type::int3_t:
    case Type::uint3_t:
      return 3;
    case Type::float4_t:
    case Type::int4_t:
    case Type::uint4_t:
      return 4;
    case Type::float3x3_t:
      return 12;
    case Type::float4x4_t:
      return 16;
    default:
      return 1;
  }
}

static void recommended_fetch_mode_and_comp_type(Type gpu_type,
                                                 GPUVertCompType *r_comp_type,
                                                 GPUVertFetchMode *r_fetch_mode)
{
  switch (gpu_type) {
    case Type::float_t:
    case Type::float2_t:
    case Type::float3_t:
    case Type::float4_t:
    case Type::float3x3_t:
    case Type::float4x4_t:
      *r_comp_type = GPU_COMP_F32;
      *r_fetch_mode = GPU_FETCH_FLOAT;
      break;
    case Type::int_t:
    case Type::int2_t:
    case Type::int3_t:
    case Type::int4_t:
      *r_comp_type = GPU_COMP_I32;
      *r_fetch_mode = GPU_FETCH_INT;
      break;
    case Type::uint_t:
    case Type::uint2_t:
    case Type::uint3_t:
    case Type::uint4_t:
      *r_comp_type = GPU_COMP_U32;
      *r_fetch_mode = GPU_FETCH_INT;
      break;
    default:
      BLI_assert(0);
  }
}

void GPU_vertformat_from_shader(GPUVertFormat *format, const GPUShader *shader)
{
  GPU_vertformat_clear(format);

  uint attr_len = GPU_shader_get_attribute_len(shader);
  int location_test = 0, attrs_added = 0;
  while (attrs_added < attr_len) {
    char name[256];
    Type gpu_type;
    if (!GPU_shader_get_attribute_info(shader, location_test++, name, (int *)&gpu_type)) {
      continue;
    }

    GPUVertCompType comp_type;
    GPUVertFetchMode fetch_mode;
    recommended_fetch_mode_and_comp_type(gpu_type, &comp_type, &fetch_mode);

    int comp_len = component_size_get(gpu_type);

    GPU_vertformat_attr_add(format, name, comp_type, comp_len, fetch_mode);
    attrs_added++;
  }
}
