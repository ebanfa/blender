/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "GEO_separate_geometry.hh"

#include "BKE_curves.hh"
#include "BKE_customdata.hh"
#include "BKE_geometry_fields.hh"
#include "BKE_grease_pencil.hh"
#include "BKE_instances.hh"
#include "BKE_mesh.hh"
#include "BKE_pointcloud.hh"

#include "DNA_pointcloud_types.h"

#include "GEO_mesh_copy_selection.hh"

namespace blender::geometry {

using bke::AttrDomain;

/** \return std::nullopt if the geometry should remain unchanged. */
static std::optional<bke::CurvesGeometry> separate_curves_selection(
    const bke::CurvesGeometry &src_curves,
    const fn::FieldContext &field_context,
    const fn::Field<bool> &selection_field,
    const AttrDomain domain,
    const bke::AttributeFilter &attribute_filter)
{
  const int domain_size = src_curves.attributes().domain_size(domain);
  fn::FieldEvaluator evaluator{field_context, domain_size};
  evaluator.set_selection(selection_field);
  evaluator.evaluate();
  const IndexMask selection = evaluator.get_evaluated_selection_as_mask();
  if (selection.size() == domain_size) {
    return std::nullopt;
  }
  if (selection.is_empty()) {
    return bke::CurvesGeometry();
  }

  if (domain == AttrDomain::Point) {
    return bke::curves_copy_point_selection(src_curves, selection, attribute_filter);
  }
  if (domain == AttrDomain::Curve) {
    return bke::curves_copy_curve_selection(src_curves, selection, attribute_filter);
  }
  BLI_assert_unreachable();
  return std::nullopt;
}

/** \return std::nullopt if the geometry should remain unchanged. */
static std::optional<PointCloud *> separate_pointcloud_selection(
    const PointCloud &src_pointcloud,
    const fn::Field<bool> &selection_field,
    const bke::AttributeFilter &attribute_filter)
{
  const bke::PointCloudFieldContext context{src_pointcloud};
  fn::FieldEvaluator evaluator{context, src_pointcloud.totpoint};
  evaluator.set_selection(selection_field);
  evaluator.evaluate();
  const IndexMask selection = evaluator.get_evaluated_selection_as_mask();
  if (selection.size() == src_pointcloud.totpoint) {
    return std::nullopt;
  }
  if (selection.is_empty()) {
    return nullptr;
  }

  PointCloud *pointcloud = BKE_pointcloud_new_nomain(selection.size());
  bke::gather_attributes(src_pointcloud.attributes(),
                         AttrDomain::Point,
                         AttrDomain::Point,
                         attribute_filter,
                         selection,
                         pointcloud->attributes_for_write());
  return pointcloud;
}

static void delete_selected_instances(bke::GeometrySet &geometry_set,
                                      const fn::Field<bool> &selection_field,
                                      const bke::AttributeFilter &attribute_filter)
{
  bke::Instances &instances = *geometry_set.get_instances_for_write();
  bke::InstancesFieldContext field_context{instances};

  fn::FieldEvaluator evaluator{field_context, instances.instances_num()};
  evaluator.set_selection(selection_field);
  evaluator.evaluate();
  const IndexMask selection = evaluator.get_evaluated_selection_as_mask();
  if (selection.is_empty()) {
    geometry_set.remove<bke::InstancesComponent>();
    return;
  }

  instances.remove(selection, attribute_filter);
}

static std::optional<Mesh *> separate_mesh_selection(const Mesh &mesh,
                                                     const fn::Field<bool> &selection_field,
                                                     const AttrDomain selection_domain,
                                                     const GeometryNodeDeleteGeometryMode mode,
                                                     const bke::AttributeFilter &attribute_filter)
{
  const bke::AttributeAccessor attributes = mesh.attributes();
  const bke::MeshFieldContext context(mesh, selection_domain);
  fn::FieldEvaluator evaluator(context, attributes.domain_size(selection_domain));
  evaluator.add(selection_field);
  evaluator.evaluate();
  const VArray<bool> selection = evaluator.get_evaluated<bool>(0);

  switch (mode) {
    case GEO_NODE_DELETE_GEOMETRY_MODE_ALL:
      return mesh_copy_selection(mesh, selection, selection_domain, attribute_filter);
    case GEO_NODE_DELETE_GEOMETRY_MODE_EDGE_FACE:
      return mesh_copy_selection_keep_verts(mesh, selection, selection_domain, attribute_filter);
    case GEO_NODE_DELETE_GEOMETRY_MODE_ONLY_FACE:
      return mesh_copy_selection_keep_edges(mesh, selection, selection_domain, attribute_filter);
  }
  return nullptr;
}

static std::optional<GreasePencil *> separate_grease_pencil_layer_selection(
    const GreasePencil &src_grease_pencil,
    const fn::Field<bool> &selection_field,
    const bke::AttributeFilter &attribute_filter)
{
  const bke::AttributeAccessor attributes = src_grease_pencil.attributes();
  const bke::GeometryFieldContext context(src_grease_pencil);

  fn::FieldEvaluator evaluator(context, attributes.domain_size(AttrDomain::Layer));
  evaluator.set_selection(selection_field);
  evaluator.evaluate();

  const IndexMask selection = evaluator.get_evaluated_selection_as_mask();
  if (selection.size() == attributes.domain_size(AttrDomain::Layer)) {
    return std::nullopt;
  }
  if (selection.is_empty()) {
    return nullptr;
  }

  const int dst_layers_num = selection.size();

  GreasePencil *dst_grease_pencil = BKE_grease_pencil_new_nomain();
  BKE_grease_pencil_copy_parameters(src_grease_pencil, *dst_grease_pencil);
  dst_grease_pencil->add_layers_with_empty_drawings_for_eval(dst_layers_num);

  selection.foreach_index([&](const int src_layer_i, const int dst_layer_i) {
    const bke::greasepencil::Layer &src_layer = src_grease_pencil.layer(src_layer_i);
    const bke::greasepencil::Drawing *src_drawing = src_grease_pencil.get_eval_drawing(src_layer);

    bke::greasepencil::Layer &dst_layer = dst_grease_pencil->layer(dst_layer_i);
    bke::greasepencil::Drawing &dst_drawing = *dst_grease_pencil->get_eval_drawing(dst_layer);

    BKE_grease_pencil_copy_layer_parameters(src_layer, dst_layer);
    dst_layer.set_name(src_layer.name());

    if (src_drawing) {
      dst_drawing = *src_drawing;
    }
  });

  bke::gather_attributes(src_grease_pencil.attributes(),
                         AttrDomain::Layer,
                         AttrDomain::Layer,
                         attribute_filter,
                         selection,
                         dst_grease_pencil->attributes_for_write());

  return dst_grease_pencil;
}

void separate_geometry(bke::GeometrySet &geometry_set,
                       const AttrDomain domain,
                       const GeometryNodeDeleteGeometryMode mode,
                       const fn::Field<bool> &selection,
                       const bke::AttributeFilter &attribute_filter,
                       bool &r_is_error)
{
  bool some_valid_domain = false;
  if (const PointCloud *points = geometry_set.get_pointcloud()) {
    if (domain == AttrDomain::Point) {
      std::optional<PointCloud *> dst_points = separate_pointcloud_selection(
          *points, selection, attribute_filter);
      if (dst_points) {
        geometry_set.replace_pointcloud(*dst_points);
      }
      some_valid_domain = true;
    }
  }
  if (const Mesh *mesh = geometry_set.get_mesh()) {
    if (ELEM(domain, AttrDomain::Point, AttrDomain::Edge, AttrDomain::Face)) {
      std::optional<Mesh *> dst_mesh = separate_mesh_selection(
          *mesh, selection, domain, mode, attribute_filter);
      if (dst_mesh) {
        if (*dst_mesh) {
          const char *active_layer = CustomData_get_active_layer_name(&mesh->corner_data,
                                                                      CD_PROP_FLOAT2);
          if (active_layer != nullptr) {
            int id = CustomData_get_named_layer(
                &((*dst_mesh)->corner_data), CD_PROP_FLOAT2, active_layer);
            if (id >= 0) {
              CustomData_set_layer_active(&((*dst_mesh)->corner_data), CD_PROP_FLOAT2, id);
            }
          }

          const char *render_layer = CustomData_get_render_layer_name(&mesh->corner_data,
                                                                      CD_PROP_FLOAT2);
          if (render_layer != nullptr) {
            int id = CustomData_get_named_layer(
                &((*dst_mesh)->corner_data), CD_PROP_FLOAT2, render_layer);
            if (id >= 0) {
              CustomData_set_layer_render(&((*dst_mesh)->corner_data), CD_PROP_FLOAT2, id);
            }
          }
        }
        geometry_set.replace_mesh(*dst_mesh);
      }
      some_valid_domain = true;
    }
  }
  if (const Curves *src_curves_id = geometry_set.get_curves()) {
    if (ELEM(domain, AttrDomain::Point, AttrDomain::Curve)) {
      const bke::CurvesGeometry &src_curves = src_curves_id->geometry.wrap();
      const bke::CurvesFieldContext field_context{*src_curves_id, domain};
      std::optional<bke::CurvesGeometry> dst_curves = separate_curves_selection(
          src_curves, field_context, selection, domain, attribute_filter);
      if (dst_curves) {
        if (dst_curves->is_empty()) {
          geometry_set.remove<bke::CurveComponent>();
        }
        else {
          Curves *dst_curves_id = bke::curves_new_nomain(*dst_curves);
          bke::curves_copy_parameters(*src_curves_id, *dst_curves_id);
          geometry_set.replace_curves(dst_curves_id);
        }
      }
      some_valid_domain = true;
    }
  }
  if (geometry_set.get_grease_pencil()) {
    using namespace blender::bke::greasepencil;
    if (domain == AttrDomain::Layer) {
      const GreasePencil &grease_pencil = *geometry_set.get_grease_pencil();
      std::optional<GreasePencil *> dst_grease_pencil = separate_grease_pencil_layer_selection(
          grease_pencil, selection, attribute_filter);
      if (dst_grease_pencil) {
        geometry_set.replace_grease_pencil(*dst_grease_pencil);
      }
      some_valid_domain = true;
    }
    else if (ELEM(domain, AttrDomain::Point, AttrDomain::Curve)) {
      GreasePencil &grease_pencil = *geometry_set.get_grease_pencil_for_write();
      for (const int layer_index : grease_pencil.layers().index_range()) {
        Drawing *drawing = grease_pencil.get_eval_drawing(grease_pencil.layer(layer_index));
        if (drawing == nullptr) {
          continue;
        }
        const bke::CurvesGeometry &src_curves = drawing->strokes();
        const bke::GreasePencilLayerFieldContext field_context(grease_pencil, domain, layer_index);
        std::optional<bke::CurvesGeometry> dst_curves = separate_curves_selection(
            src_curves, field_context, selection, domain, attribute_filter);
        if (!dst_curves) {
          continue;
        }
        drawing->strokes_for_write() = std::move(*dst_curves);
        drawing->tag_topology_changed();
        some_valid_domain = true;
      }
    }
  }
  if (geometry_set.has_instances()) {
    if (domain == AttrDomain::Instance) {
      delete_selected_instances(geometry_set, selection, attribute_filter);
      some_valid_domain = true;
    }
  }
  r_is_error = !some_valid_domain && geometry_set.has_realized_data();
}

}  // namespace blender::geometry
