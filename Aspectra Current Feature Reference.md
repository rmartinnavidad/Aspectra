# Aspectra Current Feature Reference

## What Aspectra is

Aspectra is a Windows desktop media workspace for preparing images, textures, captured screens, GIFs, and video. Its primary interaction model is one main preview paired with inspector tabs. The preview changes channel according to the tool being used: ordinary image/video preview, texture-map inspection, seamless-pattern preview, or material preview. Imported items can be treated as a batch, while the current item remains the active editing target.

## Main user interface

The toolbar provides import, batch management, image/video/capture mode switching, reset, project save, design export, and layered PSD export. Reset removes the current batch and document state but does not delete source files. The navigation strip moves through imported files.

The inspector tabs are Frame, Color, Layer Styles, Clips, Texture, Patterns, and Output. Video mode exposes the player, timeline, in/out controls, and clips tab. Capture mode exposes screen capture controls.

## Frame and document controls

Frame controls include output dimensions, aspect-ratio choices, lock ratio, crop zoom, safe margins, and export padding. Safe margins are guides only; padding is part of the rendered output. The selected layer has X and Y canvas coordinates. A non-zero coordinate is shown on the live canvas preview.

Each imported item is represented as a linked canvas layer with a source path, name, native size, visibility flag, canvas position, optional adjustment override, and optional grayscale mask. The Per-image adjustment override switch stores the current adjustment state for the active batch item and restores it when returning to that item in the current session.

Layer masks can be created from the current color key, cleared, or painted directly on the main preview. Paint add and Paint remove use the configured brush size. The mask changes the visible alpha in the ordinary preview.

## Color and keying

Color tabs provide hue, saturation, brightness, contrast, exposure, black point, gamma, white point, and three color-balance axes. Recolor variants support white and black output variants.

The Keying tab provides enable/disable, background-border auto detection, native color picker, in-preview eyedropper, color distance, edge feather, spill suppression, dark-detail protection, matte choke/expand, clean black, and clean white. Import performs a fast green-screen border test and can enable an initial key when a green-dominant border is detected. Auto Color analyzes the frame and sets a conservative starting grade for levels, brightness, contrast, and saturation.

These controls are practical chroma-key and matte controls. They are not a trained AI rotoscoping, subject-selection, or Photoshop-equivalent content-aware system.

## Layer styles and canvas preview

The app includes opacity and layer-style controls such as overlay, gradient, stroke, shadow, glow, inner shadow, and bevel. The main preview shows alpha with a checkerboard, safe/padding guides, and the active image or video frame.

Current limitation: the canvas position is live for the selected-layer preview and persists in Aspectra projects, but it is not yet a complete multi-layer compositor for ordinary batch export or fully positioned PSD layer bounds.

## Video, GIF, and capture

Video mode uses a media player, timeline, play/pause, muted playback, in/out markers, named clip ranges, and optional full-screen preview. Video and GIF trimming is associated with the current source. Supported video outputs include MP4, MOV, WEBM, and GIF, subject to FFmpeg availability for video processing.

Capture supports region, window, full monitor, and repeated region capture. It can capture stills, copy an image to the clipboard, and record screen video with optional microphone input when FFmpeg is configured. Aspectra hides while capture selection is active so it does not appear in the capture.

## Texture and material workflow

Texture tools generate and inspect Base Color, Normal, Roughness, Metallic, Ambient Occlusion, Height/Displacement, and a full shaded PBR preview. Each map has an enable checkbox, strength control, value box, and Tune dialog with HSL, levels, brightness, contrast, and gamma controls. Moving a map strength control selects that map for inspection in the main texture preview.

Global BGW levels, auto normalize, histogram overlay, and clipping display are available. Texture maps and material packages can be exported as PNG, JPG, TIFF, or the current EXR fallback behavior. ZIP and RAR archiving are offered when the required archiver is available.

The material preview opens a live sphere view with orbit, pan, zoom, and lighting controls for azimuth, elevation, intensity, and ambient light.

## Seamless patterns

The Patterns tab routes a seamless-tile channel into the same main preview. It offers edge blend, mirrored alternate tiles, and local variation/warp. The current implementation creates a 2x2 tile preview and localized variation; it is not yet a full brush-driven content-aware seam-healing workflow with saved per-quad warp regions.

## Import and export

Supported still imports include PNG, JPG, JPEG, BMP, WEBP, TIFF, PSD, and PSB. Raster batch export offers PNG, JPG, WEBP, TIFF, and BMP. Output controls include JPEG quality, video CRF, target-file-size mode, encoding speed, PNG compression level, GIF palette size, video/GIF frame-rate control, audio bitrate, metadata stripping, and file-size presets.

Design export can write a flattened PSD/PSB attempt, SVG or AI wrapper containing embedded raster art, PDF, and EPS. Layered PSD export writes each imported still as a Photoshop raster layer. This is an in-progress interoperability writer: it should not be represented as a full PSD/PSB document editor with positioned layer bounds, editable layer masks, vector paths, smart objects, or Photoshop Pattern/Shape resources.

## Aspectra projects

Save Project writes an `.aspectra` JSON document containing canvas size, linked source paths, layer names, positions, native dimensions, visibility values, embedded PNG masks, and current global adjustment/key settings. Ctrl+O opens a saved project and restores linked media, canvas placement, and masks when the sources are still present.

Current limitation: project reopen does not yet restore the complete per-layer override payload or a full multi-layer render stack.

## Keyboard shortcuts

- Ctrl+O: open an Aspectra project
- Ctrl+Shift+Delete: reset current media/document
- Ctrl+Shift+S: start capture
- Double-click the preview: open full-screen player where applicable

## Current boundary

Aspectra is usable today as a batch image/video adjustment tool, capture tool, chroma-key tool, texture-map generator, seamless-preview tool, and output utility. Its persistent document foundation is present, but the following are still engineering work rather than completed claims: true subject selection/roto brush, AI-quality enlargement, full multi-layer canvas compositing, format-correct PSB documents, editable Photoshop masks in exported PSDs, Photoshop Pattern/Shape formats, and genuine editable AI/EPS vector authoring.
