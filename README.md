# Aspectra 2

Launch **app/Aspectra2.exe** directly. This is the updated Windows EXE. The original `Aspectra.exe` is kept so an already-open version is not interrupted. Keep the DLLs and `psd-runtime` folder beside the new executable.

## Live workspace

- **Navigation:** previous/next buttons and the file dropdown cycle through all imported images and videos. Batch lets you add/remove files.
- **Frame:** choose an aspect ratio and output reference dimensions. Safe margins and padding have independent left/top/right/bottom percentages. Green guides mark the safe region; red tint marks the area outside it. Drag a green edge to change a safe margin. Hold Alt while dragging a padding edge to change padding.
- **Padding is exported. Safe markers are guides only.** Padding is transparent for PNG, white when saved to JPEG/BMP, and opaque in the current video encoders. A safe guide does not automatically detect a subject or guarantee that a subject fits.
- **Color:** live hue, saturation, brightness, contrast, exposure, black/white points, gamma and channel balance. Numeric fields and gradient sliders stay synchronized. White/Black recolor variants have editable colors and alpha controls.
- **Layer Styles:** live opacity, Normal/Multiply/Screen/Overlay color blending, color overlay, gradient overlay, outside stroke, drop shadow, outer glow, inner shadow and bevel/emboss. The color pickers preview changes immediately; Cancel restores the previous color. Effects apply to the current composite, and use sizes relative to the output canvas. Add padding to leave space for outside effects.

The script logo is centered at the top. Slider fill runs from #57dd7b to #3d91fb at the handle. Handles are #3d91fb. Action buttons use blue, pink and gold gradients with visible borders; checkboxes use blue checkmarks. Controls are grouped into Frame, Color, Layer Styles, Clips and Output sections.

## Player and multiple clip ranges

Import a video to show the player. Play/Pause includes audio, with a Mute checkbox. Click the timeline to seek. The blue line is the playhead; green/pink handles are the In/Out markers. Marker values can also be typed precisely in the Clips section, or set from the playhead with Set In / Set Out.

- **Range** plays from In and pauses at Out.
- **Add clip range** stores the current marker pair. Repeat for multiple sections from the same source.
- Click a clip to load its markers. Change them, then use **Update** to replace that clip's range.
- Uncheck a range to omit it; Remove deletes it from the list.
- Every checked range becomes a separate output in every checked video format, including GIF.
- With no saved clip list, export uses the current In/Out marker pair if edited, otherwise the full video.
- Ranges are independent for each video in the batch. They do not concatenate into one video.

## Multiple formats and faster exports

Output contains independent format checkboxes:

- Images: PNG, JPG and BMP, in any combination.
- Videos: MP4, MOV, WEBM and GIF, in any combination.

Check output widths; height follows the ratio shown in Frame. Select JPEG quality, video CRF and 1–4 parallel jobs. Crop/resize-only video uses a direct FFmpeg path. Adjusted video transfers frames only at output resolution. Image color levels use lookup tables and parallel rows. A rendered image is reused for its different formats.

Each export opens a separate progress window over the app. You can cancel it, inspect its log, and open the finished folder. If the window is hidden, **View export** reopens it. Each run gets a new destination subfolder and subfolders named by width. Saved files are retained on cancellation.

ZIP and RAR are optional and off by default for speed. Choose fast compression, store-only or maximum compression. Archives sit beside the output folder. RAR requires an installed Rar.exe selected in Settings. ZIP is built in; it supports files up to 512 MiB each and archives below 4 GiB. Turn ZIP off for larger video exports.

## PSD / PSB without Photoshop

Import a PSD or PSB to open a layer-selection dialog. Toggle individual layers or groups; the composite preview updates after each change. Import selected layers to process that composite using all the normal controls. Names in navigation and exported filenames retain the original PSD name.

The Python/psd-tools reader is bundled inside `app/psd-runtime`; no separate Python or Photoshop installation is needed. It supports many Photoshop layer/mask/blending features, but it is not Photoshop's rendering engine. Some adjustment layers, smart objects or advanced styles may render differently. The new Aspectra layer styles are raster effects on the chosen composite; they do not edit and re-save native Photoshop layer styles. PSD sources are never modified.

## Capture and practical limits

Image capture and silent area recording remain under Capture. Temporary captures and PSD composites are deleted when the app exits, so export results you want to keep. Area recording is limited to one monitor per capture. Mixed-DPI monitor layouts need device-specific testing.

The window remains 524×980 logical pixels. Inspector panels scroll if needed. Maximum output dimensions are 8192×8192. MP4/MOV/WEBM may pad odd dimensions to even pixels. Video exports use constant frame rates; this is not an HDR or managed-color workflow. GIF exports have no audio. The posterize option creates raster flat color, not SVG paths. Imported image decoding is limited to the bundled Qt plugins; PNG/JPG/BMP and PSD/PSB are the intended image formats.

Video export uses an external FFmpeg detected from ShareX, PATH, the app folder or Settings. The player uses bundled Qt multimedia libraries. Microsoft runtime DLLs are included. Keep the entire app directory together.

## Build and verification

Source requirements: Visual Studio 2022 C++ tools, CMake 3.21+, Qt 6.8.3 with Widgets, Concurrent and Multimedia. The code uses C++17 and OpenMP on MSVC.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release --parallel
C:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe --release build/Release/Aspectra.exe
```

The CMake target is named Aspectra; the packaged version-2 executable is named Aspectra2.exe. Also copy `psd_bridge.py` beside it and retain the provided `psd-runtime` folder for layered PSD support. Python packages in that folder include their own license notices.

Run `app/Aspectra2.exe --self-test` from a writable directory for processing tests. Set ASPECTRA_TEST_FFMPEG to ffmpeg.exe and ASPECTRA_TEST_OUTPUT to a temporary output folder to enable video/multi-format integration tests. `--ui-test` exercises navigation, live controls, playback, seeking, clip markers and the export overlay. Reports are written in the working directory. See TEST-RESULTS.txt for the verification performed on this build.
