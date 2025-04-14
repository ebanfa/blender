/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

void main()
{
  int index = int(gl_GlobalInvocationID.x);
  float4 pos = float4(gl_GlobalInvocationID.x);
  imageStore(img_output, index, pos);
}
