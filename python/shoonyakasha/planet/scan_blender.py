"""
Run inside Blender by scan_export.py (blender -b -Y scene.blend --python
scan_blender.py -- job.json): each object the job names prepared into its
kind's folder as scans.py describes. The job is JSON: {"out": folder,
"texture_size": texels, "objects": [{"kind", "object", "triangles",
"rock", "hull_points"}]}.

An object (by its name, else its name with a numeric suffix) has its evaluated mesh (its modifiers applied) is turned and scaled
as it stands in the scene, triangulated, decimated to its triangle budget
(Blender's collapse decimation) and given tangents from its UV map; its
points go from Blender's z up to y up, its base's centre to the origin.
A vertex is one of each distinct (position, normal, uv, tangent). Each
material's base colour, alpha and normal map images are found upstream of
its Principled BSDF; the albedo (with the alpha in its alpha) and the
normal map are written as PNG of at most texture_size texels a side. A
rock's hull is the convex hull of at most hull_points of its points, chosen
farthest first from the points of the full hull.
"""

import json
import os
import sys

import bmesh
import bpy
import numpy as np


def arguments():
    return json.load(open(sys.argv[sys.argv.index("--") + 1], encoding="utf-8"))


def evaluated_mesh(obj):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph), preserve_all_data_layers=True,
                                           depsgraph=depsgraph)
    turn = obj.matrix_world.copy()
    turn.translation = (0.0, 0.0, 0.0)
    mesh.transform(turn)
    return mesh


def triangulate(mesh):
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(mesh)
    bm.free()


def decimated(mesh, triangles):
    """A copy of `mesh` decimated to about `triangles` (itself when it has fewer)."""
    if len(mesh.polygons) <= triangles:
        return mesh
    holder = bpy.data.objects.new("scan_decimate", mesh)
    bpy.context.scene.collection.objects.link(holder)
    modifier = holder.modifiers.new("decimate", "DECIMATE")
    modifier.ratio = triangles / len(mesh.polygons)
    modifier.use_collapse_triangulate = True
    depsgraph = bpy.context.evaluated_depsgraph_get()
    result = bpy.data.meshes.new_from_object(holder.evaluated_get(depsgraph), preserve_all_data_layers=True,
                                             depsgraph=depsgraph)
    bpy.data.objects.remove(holder)
    triangulate(result)
    return result


def loops(mesh):
    """Per loop: position, normal, uv, tangent (xyz, sign) and material, in y up."""
    uv = mesh.uv_layers.active
    mesh.calc_tangents(uvmap=uv.name)
    n = len(mesh.loops)
    co = np.zeros(len(mesh.vertices) * 3, np.float32)
    mesh.vertices.foreach_get("co", co)
    vertex = np.zeros(n, np.int32)
    mesh.loops.foreach_get("vertex_index", vertex)
    normals = np.zeros(n * 3, np.float32)
    mesh.corner_normals.foreach_get("vector", normals)
    tangents = np.zeros(n * 3, np.float32)
    mesh.loops.foreach_get("tangent", tangents)
    signs = np.zeros(n, np.float32)
    mesh.loops.foreach_get("bitangent_sign", signs)
    uvs = np.zeros(n * 2, np.float32)
    uv.data.foreach_get("uv", uvs)
    starts = np.zeros(len(mesh.polygons), np.int32)
    mesh.polygons.foreach_get("loop_start", starts)
    materials = np.zeros(len(mesh.polygons), np.int32)
    mesh.polygons.foreach_get("material_index", materials)
    loop_material = np.zeros(n, np.int32)
    for k in range(3):
        loop_material[starts + k] = materials

    def y_up(a):
        a = a.reshape(-1, 3)
        return np.stack([a[:, 0], a[:, 2], -a[:, 1]], axis=1)

    positions = y_up(co)[vertex]
    uvs = uvs.reshape(-1, 2)
    uvs = np.stack([uvs[:, 0], 1.0 - uvs[:, 1]], axis=1)
    triangles = (starts[:, None] + np.arange(3)[None, :]).reshape(-1)
    return positions, y_up(normals), uvs, np.concatenate([y_up(tangents), signs[:, None]], axis=1), \
        loop_material, triangles


def upstream_image(socket, seen=None):
    """The first image node upstream of `socket`, and the name of its output used."""
    seen = seen or set()
    for link in socket.links:
        node = link.from_node
        if node.name in seen:
            continue
        seen.add(node.name)
        if node.type == "TEX_IMAGE" and node.image is not None:
            return node.image, link.from_socket.name
        for given in node.inputs:
            found = upstream_image(given, seen)
            if found[0] is not None:
                return found
    return None, None


def linked_images(tree):
    """The images of the image nodes in `tree` and the groups in it whose outputs are linked."""
    found = []
    for node in tree.nodes:
        if node.type == "TEX_IMAGE" and node.image is not None and any(o.links for o in node.outputs):
            found.append(node.image)
        elif node.type == "GROUP" and node.node_tree is not None:
            found += linked_images(node.node_tree)
    return found


def named_image(images, words):
    """The first of `images` whose name holds one of `words`."""
    return next((i for i in images if any(w in i.name.lower() for w in words)), None)


def material_images(material):
    """(albedo image, alpha image or None, whether the alpha is the albedo's own, normal image) of a
    material: those upstream of its Principled BSDF's base colour, alpha and normal; a role not found
    that way, by Poly Haven's naming among its linked images ("diff"; "alpha", "mask" or "opacity";
    "nor")."""
    if material is None or not material.use_nodes:
        return None, None, False, None
    albedo = alpha = normal = None
    output = None
    bsdf = next((n for n in material.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf is not None:
        albedo, _ = upstream_image(bsdf.inputs["Base Color"])
        alpha, output = upstream_image(bsdf.inputs["Alpha"])
        normal, _ = upstream_image(bsdf.inputs["Normal"])
    images = linked_images(material.node_tree)
    albedo = albedo or named_image(images, ("diff", "albedo", "color", "colour"))
    if alpha is None:
        alpha, output = named_image(images, ("alpha", "mask", "opacity")), "Color"
    normal = normal or named_image(images, ("nor",))
    own = alpha is not None and alpha == albedo and output == "Alpha"
    return albedo, alpha, own, normal


def pixels(image, size):
    """The image's RGBA, at most `size` texels a side (its aspect kept), as (h, w, 4) float, row 0 at the top."""
    copy = image.copy()
    w, h = copy.size
    scale = min(1.0, size / max(w, h))
    if scale < 1.0:
        copy.scale(max(1, int(w * scale)), max(1, int(h * scale)))
    w, h = copy.size
    data = np.zeros(w * h * 4, np.float32)
    copy.pixels.foreach_get(data)
    bpy.data.images.remove(copy)
    return data.reshape(h, w, 4)[::-1]


def resized(a, h, w):
    """`a` resampled to (h, w) by nearest texels."""
    rows = (np.arange(h) * a.shape[0] // h).clip(0, a.shape[0] - 1)
    cols = (np.arange(w) * a.shape[1] // w).clip(0, a.shape[1] - 1)
    return a[rows][:, cols]


def save_png(data, path):
    h, w, _ = data.shape
    image = bpy.data.images.new(os.path.basename(path), w, h, alpha=True)
    image.pixels.foreach_set(np.ascontiguousarray(data[::-1]).reshape(-1))
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    bpy.data.images.remove(image)


def hull(points, most):
    """At most `most` points of the convex hull of `points`, farthest first, and their own hull's points."""
    bm = bmesh.new()
    verts = [bm.verts.new(p) for p in points]
    result = bmesh.ops.convex_hull(bm, input=verts)
    on = np.array([v.co[:] for v in result["geom"] if isinstance(v, bmesh.types.BMVert)], np.float32)
    bm.free()
    if len(on) <= most:
        return on
    chosen = [int(np.argmax(np.linalg.norm(on - on.mean(axis=0), axis=1)))]
    distance = np.linalg.norm(on - on[chosen[0]], axis=1)
    while len(chosen) < most:
        k = int(np.argmax(distance))
        chosen.append(k)
        distance = np.minimum(distance, np.linalg.norm(on - on[k], axis=1))
    return hull(on[chosen], most)


def find_object(name):
    """The object called `name`, else the first (by name) called `name` with a numeric suffix (".001")."""
    if name in bpy.data.objects:
        return bpy.data.objects[name]
    suffixed = sorted(o.name for o in bpy.data.objects
                      if o.name.startswith(name + ".") and o.name[len(name) + 1:].isdigit())
    if not suffixed:
        raise KeyError("no object called %s" % name)
    return bpy.data.objects[suffixed[0]]


def prepare(entry, out, size):
    obj = find_object(entry["object"])
    mesh = evaluated_mesh(obj)
    triangulate(mesh)
    mesh = decimated(mesh, entry["triangles"])
    positions, normals, uvs, tangents, materials, triangles = loops(mesh)
    low, high = positions.min(axis=0), positions.max(axis=0)
    positions = positions - np.array([(low[0] + high[0]) / 2.0, low[1], (low[2] + high[2]) / 2.0], np.float32)
    keys = np.concatenate([np.round(positions, 5), np.round(normals, 3), np.round(uvs, 5), np.round(tangents, 3),
                           materials[:, None].astype(np.float32)], axis=1)
    _, first, inverse = np.unique(keys, axis=0, return_index=True, return_inverse=True)
    inverse = inverse.reshape(-1)
    folder = os.path.join(out, entry["kind"])
    os.makedirs(folder, exist_ok=True)
    textures = []
    for slot, material_slot in enumerate(obj.material_slots):
        albedo, alpha, own, normal = material_images(material_slot.material)
        record = {"name": material_slot.material.name if material_slot.material else "none", "albedo": None,
                  "normal": None, "cutout": False}
        if albedo is not None:
            colour = pixels(albedo, size)
            if alpha is not None and not own:
                cut = resized(pixels(alpha, size), colour.shape[0], colour.shape[1])
                colour[:, :, 3] = cut[:, :, 0]
            record["cutout"] = bool(alpha is not None and colour[:, :, 3].min() < 0.5)
            record["albedo"] = "albedo_%d.png" % slot
            save_png(colour, os.path.join(folder, record["albedo"]))
        if normal is not None:
            record["normal"] = "normal_%d.png" % slot
            save_png(pixels(normal, size), os.path.join(folder, record["normal"]))
        textures.append(record)
    rock_hull = hull(positions[first], entry["hull_points"]) if entry["rock"] else np.zeros((0, 3), np.float32)
    np.savez(os.path.join(folder, "mesh.npz"), positions=positions[first], normals=normals[first], uvs=uvs[first],
             tangents=tangents[first], materials=materials[first].astype(np.uint8),
             indices=inverse[triangles].astype(np.uint32), hull=rock_hull.astype(np.float32))
    with open(os.path.join(folder, "materials.json"), "w", encoding="utf-8") as f:
        json.dump(textures, f, indent=1)
    with open(os.path.join(folder, "meta.json"), "w", encoding="utf-8") as f:
        json.dump({"kind": entry["kind"], "object": entry["object"], "blend": os.path.basename(bpy.data.filepath),
                   "triangles": len(triangles) // 3, "vertices": int(len(first)),
                   "height": float(positions[:, 1].max())}, f, indent=1)
    print("[scans] %s: %d triangles, %d vertices, %.2f m tall" % (entry["kind"], len(triangles) // 3, len(first),
                                                                 float(positions[:, 1].max())), flush=True)


def main():
    job = arguments()
    for entry in job["objects"]:
        prepare(entry, job["out"], job["texture_size"])


main()
