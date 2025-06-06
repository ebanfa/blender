/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup render
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "MEM_guardedalloc.h"

#include "BLI_color.hh"
#include "BLI_kdopbvh.hh"
#include "BLI_listbase.h"
#include "BLI_math_color.h"
#include "BLI_math_matrix.h"
#include "BLI_math_vector.h"
#include "BLI_noise.h"
#include "BLI_task.h"
#include "BLI_utildefines.h"

#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_texture_types.h"

#include "BKE_attribute.hh"
#include "BKE_colorband.hh"
#include "BKE_colortools.hh"
#include "BKE_customdata.hh"
#include "BKE_deform.hh"
#include "BKE_mesh.hh"
#include "BKE_object.hh"
#include "BKE_particle.h"
#include "BKE_scene.hh"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_query.hh"

#include "RE_texture.h"

static blender::Mutex sample_mutex;

enum {
  POINT_DATA_VEL = 1 << 0,
  POINT_DATA_LIFE = 1 << 1,
  POINT_DATA_COLOR = 1 << 2,
};

static int point_data_used(PointDensity *pd)
{
  int pd_bitflag = 0;

  if (pd->source == TEX_PD_PSYS) {
    if ((pd->falloff_type == TEX_PD_FALLOFF_PARTICLE_VEL) ||
        (pd->color_source == TEX_PD_COLOR_PARTVEL) || (pd->color_source == TEX_PD_COLOR_PARTSPEED))
    {
      pd_bitflag |= POINT_DATA_VEL;
    }
    if ((pd->color_source == TEX_PD_COLOR_PARTAGE) ||
        (pd->falloff_type == TEX_PD_FALLOFF_PARTICLE_AGE))
    {
      pd_bitflag |= POINT_DATA_LIFE;
    }
  }
  else if (pd->source == TEX_PD_OBJECT) {
    if (ELEM(pd->ob_color_source,
             TEX_PD_COLOR_VERTCOL,
             TEX_PD_COLOR_VERTWEIGHT,
             TEX_PD_COLOR_VERTNOR))
    {
      pd_bitflag |= POINT_DATA_COLOR;
    }
  }

  return pd_bitflag;
}

static void point_data_pointers(PointDensity *pd,
                                float **r_data_velocity,
                                float **r_data_life,
                                float **r_data_color)
{
  const int data_used = point_data_used(pd);
  const int totpoint = pd->totpoints;
  float *data = pd->point_data;
  int offset = 0;

  if (data_used & POINT_DATA_VEL) {
    if (r_data_velocity) {
      *r_data_velocity = data + offset;
    }
    offset += 3 * totpoint;
  }
  else {
    if (r_data_velocity) {
      *r_data_velocity = nullptr;
    }
  }

  if (data_used & POINT_DATA_LIFE) {
    if (r_data_life) {
      *r_data_life = data + offset;
    }
    offset += totpoint;
  }
  else {
    if (r_data_life) {
      *r_data_life = nullptr;
    }
  }

  if (data_used & POINT_DATA_COLOR) {
    if (r_data_color) {
      *r_data_color = data + offset;
    }
    offset += 3 * totpoint;
  }
  else {
    if (r_data_color) {
      *r_data_color = nullptr;
    }
  }
}

/* additional data stored alongside the point density BVH,
 * accessible by point index number to retrieve other information
 * such as particle velocity or lifetime */
static void alloc_point_data(PointDensity *pd)
{
  const int totpoints = pd->totpoints;
  int data_used = point_data_used(pd);
  int data_size = 0;

  if (data_used & POINT_DATA_VEL) {
    /* store 3 channels of velocity data */
    data_size += 3;
  }
  if (data_used & POINT_DATA_LIFE) {
    /* store 1 channel of lifetime data */
    data_size += 1;
  }
  if (data_used & POINT_DATA_COLOR) {
    /* store 3 channels of RGB data */
    data_size += 3;
  }

  if (data_size) {
    pd->point_data = MEM_calloc_arrayN<float>(size_t(data_size) * size_t(totpoints),
                                              "particle point data");
  }
}

static void pointdensity_cache_psys(
    Depsgraph *depsgraph, Scene *scene, PointDensity *pd, Object *ob, ParticleSystem *psys)
{
  ParticleKey state;
  ParticleCacheKey *cache;
  ParticleSimulationData sim = {nullptr};
  ParticleData *pa = nullptr;
  float cfra = BKE_scene_ctime_get(scene);
  int i;
  // int childexists = 0; /* UNUSED */
  int total_particles;
  int data_used;
  float *data_vel, *data_life;
  float partco[3];
  const bool use_render_params = (DEG_get_mode(depsgraph) == DAG_EVAL_RENDER);

  data_used = point_data_used(pd);

  if (!psys_check_enabled(ob, psys, use_render_params)) {
    return;
  }

  sim.depsgraph = depsgraph;
  sim.scene = scene;
  sim.ob = ob;
  sim.psys = psys;
  sim.psmd = psys_get_modifier(ob, psys);

  /* in case ob->world_to_object isn't up-to-date */
  invert_m4_m4(ob->runtime->world_to_object.ptr(), ob->object_to_world().ptr());

  total_particles = psys->totpart + psys->totchild;
  psys_sim_data_init(&sim);

  pd->point_tree = BLI_bvhtree_new(total_particles, 0.0, 4, 6);
  pd->totpoints = total_particles;
  alloc_point_data(pd);
  point_data_pointers(pd, &data_vel, &data_life, nullptr);

#if 0 /* UNUSED */
  if (psys->totchild > 0 && !(psys->part->draw & PART_DRAW_PARENT)) {
    childexists = 1;
  }
#endif

  for (i = 0, pa = psys->particles; i < total_particles; i++, pa++) {

    if (psys->part->type == PART_HAIR) {
      /* hair particles */
      if (i < psys->totpart && psys->pathcache) {
        cache = psys->pathcache[i];
      }
      else if (i >= psys->totpart && psys->childcache) {
        cache = psys->childcache[i - psys->totpart];
      }
      else {
        continue;
      }

      cache += cache->segments; /* use endpoint */

      copy_v3_v3(state.co, cache->co);
      zero_v3(state.vel);
      state.time = 0.0f;
    }
    else {
      /* emitter particles */
      state.time = cfra;

      if (!psys_get_particle_state(&sim, i, &state, false)) {
        continue;
      }

      if (data_used & POINT_DATA_LIFE) {
        if (i < psys->totpart) {
          state.time = (cfra - pa->time) / pa->lifetime;
        }
        else {
          ChildParticle *cpa = (psys->child + i) - psys->totpart;
          float pa_birthtime, pa_dietime;

          state.time = psys_get_child_time(psys, cpa, cfra, &pa_birthtime, &pa_dietime);
        }
      }
    }

    copy_v3_v3(partco, state.co);

    if (pd->psys_cache_space == TEX_PD_OBJECTSPACE) {
      mul_m4_v3(ob->world_to_object().ptr(), partco);
    }
    else if (pd->psys_cache_space == TEX_PD_OBJECTLOC) {
      sub_v3_v3(partco, ob->loc);
    }
    else {
      /* TEX_PD_WORLDSPACE */
    }

    BLI_bvhtree_insert(static_cast<BVHTree *>(pd->point_tree), i, partco, 1);

    if (data_vel) {
      data_vel[i * 3 + 0] = state.vel[0];
      data_vel[i * 3 + 1] = state.vel[1];
      data_vel[i * 3 + 2] = state.vel[2];
    }
    if (data_life) {
      data_life[i] = state.time;
    }
  }

  BLI_bvhtree_balance(static_cast<BVHTree *>(pd->point_tree));

  psys_sim_data_free(&sim);
}

static void pointdensity_cache_vertex_color(PointDensity *pd,
                                            Object * /*ob*/,
                                            Mesh *mesh,
                                            float *data_color)
{
  using namespace blender;
  const blender::Span<int> corner_verts = mesh->corner_verts();
  const int totloop = mesh->corners_num;
  int i;

  BLI_assert(data_color);

  const bke::AttributeAccessor attributes = mesh->attributes();
  const StringRef name = attributes.contains(pd->vertex_attribute_name) ?
                             pd->vertex_attribute_name :
                             (mesh->active_color_attribute ? mesh->active_color_attribute : "");

  const VArray mcol = *attributes.lookup<ColorGeometry4b>(name, bke::AttrDomain::Corner);
  if (!mcol) {
    return;
  }

  /* Stores the number of MLoops using the same vertex, so we can normalize colors. */
  int *mcorners = MEM_calloc_arrayN<int>(pd->totpoints, "point density corner count");

  for (i = 0; i < totloop; i++) {
    int v = corner_verts[i];

    if (mcorners[v] == 0) {
      rgb_uchar_to_float(&data_color[v * 3], mcol[i]);
    }
    else {
      float col[3];
      rgb_uchar_to_float(col, mcol[i]);
      add_v3_v3(&data_color[v * 3], col);
    }

    ++mcorners[v];
  }

  /* Normalize colors by averaging over mcorners.
   * All the corners share the same vertex, ie. occupy the same point in space.
   */
  for (i = 0; i < pd->totpoints; i++) {
    if (mcorners[i] > 0) {
      mul_v3_fl(&data_color[i * 3], 1.0f / mcorners[i]);
    }
  }

  MEM_freeN(mcorners);
}

static void pointdensity_cache_vertex_weight(PointDensity *pd,
                                             Object *ob,
                                             Mesh *mesh,
                                             float *data_color)
{
  const int totvert = mesh->verts_num;
  int mdef_index;
  int i;

  BLI_assert(data_color);

  const MDeformVert *mdef = static_cast<const MDeformVert *>(
      CustomData_get_layer(&mesh->vert_data, CD_MDEFORMVERT));
  if (!mdef) {
    return;
  }
  mdef_index = BKE_id_defgroup_name_index(&mesh->id, pd->vertex_attribute_name);
  if (mdef_index < 0) {
    mdef_index = BKE_object_defgroup_active_index_get(ob) - 1;
  }
  if (mdef_index < 0) {
    return;
  }

  const MDeformVert *dv;
  for (i = 0, dv = mdef; i < totvert; i++, dv++, data_color += 3) {
    MDeformWeight *dw;
    int j;

    for (j = 0, dw = dv->dw; j < dv->totweight; j++, dw++) {
      if (dw->def_nr == mdef_index) {
        copy_v3_fl(data_color, dw->weight);
        break;
      }
    }
  }
}

static void pointdensity_cache_vertex_normal(Mesh *mesh, float *data_color)
{
  BLI_assert(data_color);
  const blender::Span<blender::float3> normals = mesh->vert_normals();
  memcpy(data_color, normals.data(), sizeof(float[3]) * mesh->verts_num);
}

static void pointdensity_cache_object(PointDensity *pd, Object *ob)
{
  float *data_color;
  int i;
  Mesh *mesh = static_cast<Mesh *>(ob->data);

#if 0 /* UNUSED */
  CustomData_MeshMasks mask = CD_MASK_BAREMESH;
  mask.fmask |= CD_MASK_MTFACE | CD_MASK_MCOL;
  switch (pd->ob_color_source) {
    case TEX_PD_COLOR_VERTCOL:
      mask.lmask |= CD_MASK_PROP_BYTE_COLOR;
      break;
    case TEX_PD_COLOR_VERTWEIGHT:
      mask.vmask |= CD_MASK_MDEFORMVERT;
      break;
  }
#endif

  const blender::Span<blender::float3> positions = mesh->vert_positions(); /* local object space */
  pd->totpoints = mesh->verts_num;
  if (pd->totpoints == 0) {
    return;
  }

  pd->point_tree = BLI_bvhtree_new(pd->totpoints, 0.0, 4, 6);
  alloc_point_data(pd);
  point_data_pointers(pd, nullptr, nullptr, &data_color);

  for (i = 0; i < pd->totpoints; i++) {
    float co[3];

    copy_v3_v3(co, positions[i]);

    switch (pd->ob_cache_space) {
      case TEX_PD_OBJECTSPACE:
        break;
      case TEX_PD_OBJECTLOC:
        mul_m4_v3(ob->object_to_world().ptr(), co);
        sub_v3_v3(co, ob->loc);
        break;
      case TEX_PD_WORLDSPACE:
      default:
        mul_m4_v3(ob->object_to_world().ptr(), co);
        break;
    }

    BLI_bvhtree_insert(static_cast<BVHTree *>(pd->point_tree), i, co, 1);
  }

  switch (pd->ob_color_source) {
    case TEX_PD_COLOR_VERTCOL:
      pointdensity_cache_vertex_color(pd, ob, mesh, data_color);
      break;
    case TEX_PD_COLOR_VERTWEIGHT:
      pointdensity_cache_vertex_weight(pd, ob, mesh, data_color);
      break;
    case TEX_PD_COLOR_VERTNOR:
      pointdensity_cache_vertex_normal(mesh, data_color);
      break;
  }

  BLI_bvhtree_balance(static_cast<BVHTree *>(pd->point_tree));
}

static void cache_pointdensity(Depsgraph *depsgraph, Scene *scene, PointDensity *pd)
{
  if (pd == nullptr) {
    return;
  }

  if (pd->point_tree) {
    BLI_bvhtree_free(static_cast<BVHTree *>(pd->point_tree));
    pd->point_tree = nullptr;
  }

  if (pd->source == TEX_PD_PSYS) {
    Object *ob = pd->object;
    ParticleSystem *psys;

    if (!ob || !pd->psys) {
      return;
    }

    psys = static_cast<ParticleSystem *>(BLI_findlink(&ob->particlesystem, pd->psys - 1));
    if (!psys) {
      return;
    }

    pointdensity_cache_psys(depsgraph, scene, pd, ob, psys);
  }
  else if (pd->source == TEX_PD_OBJECT) {
    Object *ob = pd->object;
    if (ob && ob->type == OB_MESH) {
      pointdensity_cache_object(pd, ob);
    }
  }
}

static void free_pointdensity(PointDensity *pd)
{
  if (pd == nullptr) {
    return;
  }

  if (pd->point_tree) {
    BLI_bvhtree_free(static_cast<BVHTree *>(pd->point_tree));
    pd->point_tree = nullptr;
  }

  MEM_SAFE_FREE(pd->point_data);
  pd->totpoints = 0;
}

struct PointDensityRangeData {
  float *density;
  float squared_radius;
  float *point_data_life;
  float *point_data_velocity;
  float *point_data_color;
  float *vec;
  float *col;
  float softness;
  short falloff_type;
  short noise_influence;
  float *age;
  CurveMapping *density_curve;
  float velscale;
};

static float density_falloff(PointDensityRangeData *pdr, int index, float squared_dist)
{
  const float dist = (pdr->squared_radius - squared_dist) / pdr->squared_radius * 0.5f;
  float density = 0.0f;

  switch (pdr->falloff_type) {
    case TEX_PD_FALLOFF_STD:
      density = dist;
      break;
    case TEX_PD_FALLOFF_SMOOTH:
      density = 3.0f * dist * dist - 2.0f * dist * dist * dist;
      break;
    case TEX_PD_FALLOFF_SOFT:
      density = pow(dist, pdr->softness);
      break;
    case TEX_PD_FALLOFF_CONSTANT:
      density = pdr->squared_radius;
      break;
    case TEX_PD_FALLOFF_ROOT:
      density = sqrtf(dist);
      break;
    case TEX_PD_FALLOFF_PARTICLE_AGE:
      if (pdr->point_data_life) {
        density = dist * std::min(pdr->point_data_life[index], 1.0f);
      }
      else {
        density = dist;
      }
      break;
    case TEX_PD_FALLOFF_PARTICLE_VEL:
      if (pdr->point_data_velocity) {
        density = dist * len_v3(&pdr->point_data_velocity[index * 3]) * pdr->velscale;
      }
      else {
        density = dist;
      }
      break;
  }

  if (pdr->density_curve && dist != 0.0f) {
    BKE_curvemapping_init(pdr->density_curve);
    density = BKE_curvemapping_evaluateF(pdr->density_curve, 0, density / dist) * dist;
  }

  return density;
}

static void accum_density(void *userdata, int index, const float co[3], float squared_dist)
{
  PointDensityRangeData *pdr = (PointDensityRangeData *)userdata;
  float density = 0.0f;

  UNUSED_VARS(co);

  if (pdr->point_data_velocity) {
    pdr->vec[0] += pdr->point_data_velocity[index * 3 + 0];  // * density;
    pdr->vec[1] += pdr->point_data_velocity[index * 3 + 1];  // * density;
    pdr->vec[2] += pdr->point_data_velocity[index * 3 + 2];  // * density;
  }
  if (pdr->point_data_life) {
    *pdr->age += pdr->point_data_life[index];  // * density;
  }
  if (pdr->point_data_color) {
    add_v3_v3(pdr->col, &pdr->point_data_color[index * 3]);  // * density;
  }

  density = density_falloff(pdr, index, squared_dist);

  *pdr->density += density;
}

static void init_pointdensityrangedata(PointDensity *pd,
                                       PointDensityRangeData *pdr,
                                       float *density,
                                       float *vec,
                                       float *age,
                                       float *col,
                                       CurveMapping *density_curve,
                                       float velscale)
{
  pdr->squared_radius = pd->radius * pd->radius;
  pdr->density = density;
  pdr->falloff_type = pd->falloff_type;
  pdr->vec = vec;
  pdr->age = age;
  pdr->col = col;
  pdr->softness = pd->falloff_softness;
  pdr->noise_influence = pd->noise_influence;
  point_data_pointers(
      pd, &pdr->point_data_velocity, &pdr->point_data_life, &pdr->point_data_color);
  pdr->density_curve = density_curve;
  pdr->velscale = velscale;
}

static int pointdensity(PointDensity *pd,
                        const float texvec[3],
                        TexResult *texres,
                        float r_vec[3],
                        float *r_age,
                        float r_col[3])
{
  int retval = TEX_INT;
  PointDensityRangeData pdr;
  float density = 0.0f, age = 0.0f;
  float vec[3] = {0.0f, 0.0f, 0.0f}, col[3] = {0.0f, 0.0f, 0.0f}, co[3];
  float turb, noise_fac;
  int num = 0;

  texres->tin = 0.0f;

  init_pointdensityrangedata(pd,
                             &pdr,
                             &density,
                             vec,
                             &age,
                             col,
                             (pd->flag & TEX_PD_FALLOFF_CURVE ? pd->falloff_curve : nullptr),
                             pd->falloff_speed_scale * 0.001f);
  noise_fac = pd->noise_fac * 0.5f; /* better default */

  copy_v3_v3(co, texvec);

  if (point_data_used(pd)) {
    /* does a BVH lookup to find accumulated density and additional point data *
     * stores particle velocity vector in 'vec', and particle lifetime in 'time' */
    num = BLI_bvhtree_range_query(
        static_cast<const BVHTree *>(pd->point_tree), co, pd->radius, accum_density, &pdr);
    if (num > 0) {
      age /= num;
      mul_v3_fl(vec, 1.0f / num);
      mul_v3_fl(col, 1.0f / num);
    }

    /* reset */
    density = 0.0f;
    zero_v3(vec);
    zero_v3(col);
  }

  if (pd->flag & TEX_PD_TURBULENCE) {
    turb = BLI_noise_generic_turbulence(pd->noise_size,
                                        texvec[0] + vec[0],
                                        texvec[1] + vec[1],
                                        texvec[2] + vec[2],
                                        pd->noise_depth,
                                        false,
                                        pd->noise_basis);

    turb -= 0.5f; /* re-center 0.0-1.0 range around 0 to prevent offsetting result */

    /* now we have an offset coordinate to use for the density lookup */
    co[0] = texvec[0] + noise_fac * turb;
    co[1] = texvec[1] + noise_fac * turb;
    co[2] = texvec[2] + noise_fac * turb;
  }

  /* BVH query with the potentially perturbed coordinates */
  num = BLI_bvhtree_range_query(
      static_cast<const BVHTree *>(pd->point_tree), co, pd->radius, accum_density, &pdr);
  if (num > 0) {
    age /= num;
    mul_v3_fl(vec, 1.0f / num);
    mul_v3_fl(col, 1.0f / num);
  }

  texres->tin = density;
  if (r_age != nullptr) {
    *r_age = age;
  }
  if (r_vec != nullptr) {
    copy_v3_v3(r_vec, vec);
  }
  if (r_col != nullptr) {
    copy_v3_v3(r_col, col);
  }

  return retval;
}

static void pointdensity_color(
    PointDensity *pd, TexResult *texres, float age, const float vec[3], const float col[3])
{
  copy_v4_fl(texres->trgba, 1.0f);

  if (pd->source == TEX_PD_PSYS) {
    float rgba[4];

    switch (pd->color_source) {
      case TEX_PD_COLOR_PARTAGE:
        if (pd->coba) {
          if (BKE_colorband_evaluate(pd->coba, age, rgba)) {
            texres->talpha = true;
            copy_v3_v3(texres->trgba, rgba);
            texres->tin *= rgba[3];
            texres->trgba[3] = texres->tin;
          }
        }
        break;
      case TEX_PD_COLOR_PARTSPEED: {
        float speed = len_v3(vec) * pd->speed_scale;

        if (pd->coba) {
          if (BKE_colorband_evaluate(pd->coba, speed, rgba)) {
            texres->talpha = true;
            copy_v3_v3(texres->trgba, rgba);
            texres->tin *= rgba[3];
            texres->trgba[3] = texres->tin;
          }
        }
        break;
      }
      case TEX_PD_COLOR_PARTVEL:
        texres->talpha = true;
        mul_v3_v3fl(texres->trgba, vec, pd->speed_scale);
        texres->trgba[3] = texres->tin;
        break;
      case TEX_PD_COLOR_CONSTANT:
      default:
        break;
    }
  }
  else {
    float rgba[4];

    switch (pd->ob_color_source) {
      case TEX_PD_COLOR_VERTCOL:
        texres->talpha = true;
        copy_v3_v3(texres->trgba, col);
        texres->trgba[3] = texres->tin;
        break;
      case TEX_PD_COLOR_VERTWEIGHT:
        texres->talpha = true;
        if (pd->coba && BKE_colorband_evaluate(pd->coba, col[0], rgba)) {
          copy_v3_v3(texres->trgba, rgba);
          texres->tin *= rgba[3];
        }
        else {
          copy_v3_v3(texres->trgba, col);
        }
        texres->trgba[3] = texres->tin;
        break;
      case TEX_PD_COLOR_VERTNOR:
        texres->talpha = true;
        copy_v3_v3(texres->trgba, col);
        texres->trgba[3] = texres->tin;
        break;
      case TEX_PD_COLOR_CONSTANT:
      default:
        break;
    }
  }
}

static void sample_dummy_point_density(int resolution, float *values)
{
  memset(values, 0, sizeof(float[4]) * resolution * resolution * resolution);
}

static void particle_system_minmax(Depsgraph *depsgraph,
                                   Scene *scene,
                                   Object *object,
                                   ParticleSystem *psys,
                                   float radius,
                                   float min[3],
                                   float max[3])
{
  const float size[3] = {radius, radius, radius};
  const float cfra = BKE_scene_ctime_get(scene);
  ParticleSettings *part = psys->part;
  ParticleSimulationData sim = {nullptr};
  ParticleData *pa = nullptr;
  int i;
  int total_particles;
  float mat[4][4], imat[4][4];

  INIT_MINMAX(min, max);
  if (part->type == PART_HAIR) {
    /* TODO(sergey): Not supported currently. */
    return;
  }

  unit_m4(mat);

  sim.depsgraph = depsgraph;
  sim.scene = scene;
  sim.ob = object;
  sim.psys = psys;
  sim.psmd = psys_get_modifier(object, psys);

  invert_m4_m4(imat, object->object_to_world().ptr());
  total_particles = psys->totpart + psys->totchild;
  psys_sim_data_init(&sim);

  for (i = 0, pa = psys->particles; i < total_particles; i++, pa++) {
    float co_object[3], co_min[3], co_max[3];
    ParticleKey state;
    state.time = cfra;
    if (!psys_get_particle_state(&sim, i, &state, false)) {
      continue;
    }
    mul_v3_m4v3(co_object, imat, state.co);
    sub_v3_v3v3(co_min, co_object, size);
    add_v3_v3v3(co_max, co_object, size);
    minmax_v3v3_v3(min, max, co_min);
    minmax_v3v3_v3(min, max, co_max);
  }

  psys_sim_data_free(&sim);
}

void RE_point_density_cache(Depsgraph *depsgraph, PointDensity *pd)
{
  Scene *scene = DEG_get_evaluated_scene(depsgraph);

  /* Same matrices/resolution as dupli_render_particle_set(). */
  std::scoped_lock lock(sample_mutex);
  cache_pointdensity(depsgraph, scene, pd);
}

void RE_point_density_minmax(Depsgraph *depsgraph,
                             PointDensity *pd,
                             float r_min[3],
                             float r_max[3])
{
  using namespace blender;
  Scene *scene = DEG_get_evaluated_scene(depsgraph);
  Object *object = pd->object;
  if (object == nullptr) {
    zero_v3(r_min);
    zero_v3(r_max);
    return;
  }
  if (pd->source == TEX_PD_PSYS) {
    ParticleSystem *psys;

    if (pd->psys == 0) {
      zero_v3(r_min);
      zero_v3(r_max);
      return;
    }
    psys = static_cast<ParticleSystem *>(BLI_findlink(&object->particlesystem, pd->psys - 1));
    if (psys == nullptr) {
      zero_v3(r_min);
      zero_v3(r_max);
      return;
    }

    particle_system_minmax(depsgraph, scene, object, psys, pd->radius, r_min, r_max);
  }
  else {
    const float radius[3] = {pd->radius, pd->radius, pd->radius};
    if (const std::optional<Bounds<float3>> bb = BKE_object_boundbox_get(object)) {
      copy_v3_v3(r_min, bb->min);
      copy_v3_v3(r_max, bb->max);
      /* Adjust texture space to include density points on the boundaries. */
      sub_v3_v3(r_min, radius);
      add_v3_v3(r_max, radius);
    }
    else {
      zero_v3(r_min);
      zero_v3(r_max);
    }
  }
}

struct SampleCallbackData {
  PointDensity *pd;
  int resolution;
  float *min, *dim;
  float *values;
};

static void point_density_sample_func(void *__restrict data_v,
                                      const int iter,
                                      const TaskParallelTLS *__restrict /*tls*/)
{
  SampleCallbackData *data = (SampleCallbackData *)data_v;

  const int resolution = data->resolution;
  const int resolution2 = resolution * resolution;
  const float *min = data->min, *dim = data->dim;
  PointDensity *pd = data->pd;
  float *values = data->values;

  if (!pd || !pd->point_tree) {
    return;
  }

  size_t z = size_t(iter);
  for (size_t y = 0; y < resolution; y++) {
    for (size_t x = 0; x < resolution; x++) {
      size_t index = z * resolution2 + y * resolution + x;
      float texvec[3];
      float age, vec[3], col[3];
      TexResult texres;

      copy_v3_v3(texvec, min);
      texvec[0] += dim[0] * float(x) / float(resolution);
      texvec[1] += dim[1] * float(y) / float(resolution);
      texvec[2] += dim[2] * float(z) / float(resolution);

      pointdensity(pd, texvec, &texres, vec, &age, col);
      pointdensity_color(pd, &texres, age, vec, col);

      copy_v3_v3(&values[index * 4 + 0], texres.trgba);
      values[index * 4 + 3] = texres.tin;
    }
  }
}

void RE_point_density_sample(Depsgraph *depsgraph,
                             PointDensity *pd,
                             const int resolution,
                             float *values)
{
  Object *object = pd->object;
  float min[3], max[3], dim[3];

  /* TODO(sergey): Implement some sort of assert() that point density
   * was cached already.
   */

  if (object == nullptr) {
    sample_dummy_point_density(resolution, values);
    return;
  }

  {
    std::scoped_lock lock(sample_mutex);
    RE_point_density_minmax(depsgraph, pd, min, max);
  }
  sub_v3_v3v3(dim, max, min);
  if (dim[0] <= 0.0f || dim[1] <= 0.0f || dim[2] <= 0.0f) {
    sample_dummy_point_density(resolution, values);
    return;
  }

  SampleCallbackData data;
  data.pd = pd;
  data.resolution = resolution;
  data.min = min;
  data.dim = dim;
  data.values = values;
  TaskParallelSettings settings;
  BLI_parallel_range_settings_defaults(&settings);
  settings.use_threading = (resolution > 32);
  BLI_task_parallel_range(0, resolution, &data, point_density_sample_func, &settings);

  free_pointdensity(pd);
}

void RE_point_density_free(PointDensity *pd)
{
  free_pointdensity(pd);
}

void RE_point_density_fix_linking() {}
