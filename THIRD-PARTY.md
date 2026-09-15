# Third-party components

This local build dynamically links Qt 6.8.3 Core, Gui and Widgets and deploys their runtime dependencies and plugins. Qt is copyright The Qt Company Ltd. and contributors and is available under its applicable LGPL/GPL/commercial licenses. Qt source and notices for the exact release are available from:

- https://download.qt.io/archive/qt/6.8/6.8.3/submodules/
- https://code.qt.io/cgit/qt/qtbase.git/tree/LICENSES?h=v6.8.3

The Qt DLLs may be replaced with compatible builds. The complete Aspectra source and CMake project are included for rebuilding and relinking. No restriction on debugging modifications to the Qt libraries is imposed by this project.

FFmpeg and RAR are invoked as external programs and are not distributed here. Their own license terms apply. The supplied artwork belongs to its respective owner.

Version 2 adds Qt Multimedia and the FFmpeg runtime libraries supplied with Qt 6.8.3. Qt Multimedia source and license notices are available in the same Qt release submodule archive. It also bundles CPython 3.12.10 (see app/psd-runtime/LICENSE.txt) and psd-tools 1.19.0 with its compositing dependencies. Their license files are retained in each package's .dist-info directory under app/psd-runtime/Lib/site-packages. The PSD helper source is included as psd_bridge.py. External FFmpeg for exports and Rar.exe remain separate programs.
