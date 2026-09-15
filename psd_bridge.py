"""PSD layer inspection/compositing; invoked only by the local desktop app."""
import json
import re
import sys
from pathlib import Path
from psd_tools import PSDImage
from psd_tools.api.layers import Artboard


def safe_artboard_name(name, used):
    """Create a portable, collision-free file stem while preserving the artboard name."""
    stem = re.sub(r'[<>:"/\\|?*\x00-\x1f]+', '_', name).strip(' .') or 'Artboard'
    stem = stem[:180]
    candidate, suffix = stem, 2
    while candidate.casefold() in used:
        candidate = f"{stem}-{suffix}"
        suffix += 1
    used.add(candidate.casefold())
    return candidate

def main():
    mode, source, destination = sys.argv[1:4]
    out = Path(destination)
    out.mkdir(parents=True, exist_ok=True)
    psd = PSDImage.open(source)
    layers = list(psd.descendants())
    ids = {id(layer): str(index) for index, layer in enumerate(layers)}
    if mode == "inspect":
        manifest = {"width": psd.width, "height": psd.height, "layers": []}
        for index, layer in enumerate(layers):
            depth = 0
            parent = layer.parent
            while parent is not None and parent is not psd:
                depth += 1
                parent = parent.parent
            manifest["layers"].append({"id": str(index), "name": layer.name, "depth": depth,
                "group": layer.is_group(), "visible": layer.is_visible(), "kind": layer.kind,
                "opacity": layer.opacity, "blend": str(layer.blend_mode)})
        # Render once. The full cached composite is imported immediately when the
        # user keeps the default visible-layer selection; a later selection edit
        # is the only time another composite is needed.
        composite = psd.composite()
        composite.save(out / "composite.png", compress_level=1)
        preview = composite.copy()
        preview.thumbnail((700, 700))
        preview.save(out / "preview.png", compress_level=1)
        (out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False), encoding="utf-8")
    elif mode == "render":
        selected = set(json.loads(Path(sys.argv[4]).read_text(encoding="utf-8")))
        allowed = set(selected)
        for layer in layers:
            if ids[id(layer)] in selected:
                parent = layer.parent
                while parent is not None and parent is not psd:
                    allowed.add(ids[id(parent)])
                    parent = parent.parent
        image = psd.composite(ignore_preview=True, layer_filter=lambda layer: ids.get(id(layer)) in allowed) if layers else psd.composite()
        image.save(out / "composite.png", compress_level=1)
        image.thumbnail((700, 700))
        image.save(out / "preview.png")
    elif mode == "export-artboards":
        artboards = [layer for layer in psd.descendants() if isinstance(layer, Artboard)]
        if not artboards:
            raise ValueError("No Photoshop artboards were found in this PSD or PSB.")
        used, exported = set(), []
        print(json.dumps({"event": "begin", "total": len(artboards)}), flush=True)
        for index, artboard in enumerate(artboards, start=1):
            image = artboard.composite()
            if image is None:
                continue
            stem = safe_artboard_name(artboard.name, used)
            filename = f"{stem}.png"
            image.save(out / filename, format="PNG", compress_level=1)
            exported.append({
                "name": artboard.name,
                "file": filename,
                "width": image.width,
                "height": image.height,
                "bounds": list(artboard.bbox),
            })
            print(json.dumps({"event": "progress", "completed": index, "total": len(artboards), "name": artboard.name}), flush=True)
        if not exported:
            raise ValueError("The PSD contains artboards, but none could be rendered.")
        (out / "aspectra-artboards.json").write_text(
            json.dumps({"count": len(exported), "artboards": exported}, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        print(json.dumps({"count": len(exported)}))
    else:
        raise ValueError(f"Unknown PSD bridge mode: {mode}")

if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)
