import shutil, sys

path = "MainWindow.cpp"
shutil.copyfile(path, path + ".bak2")

with open(path, "r", encoding="utf-8", newline="") as f:
    src = f.read()

def replace_once(old, new, label):
    global src
    n = src.count(old)
    if n != 1:
        print(f"FAIL[{label}]: expected 1 occurrence, found {n}")
        sys.exit(1)
    src = src.replace(old, new, 1)
    print(f"OK[{label}]")

# Edit 1: selectFile() — handle synthetic aspectra:// artboard paths instead of
# routing them through the async file/video loader (which always fails for them).
old1 = (
'void MainWindow::selectFile(const QString &path){\n'
'      if(batchStrip){batchStrip->show();if(auto *dock=batchStrip->parentWidget())dock->setFixedHeight(164);}if(exportButton)exportButton->setEnabled(true);\n'
'      if(loading)return;stopPlayback();player->setSource({});currentFile=path;if(!canvasLayers.contains(path)){CanvasLayer layer;layer.source=path;layer.name=QFileInfo(inputTitles.value(path,inputAliases.value(path,path))).completeBaseName();canvasLayers[path]=layer;}restoreLayerOverride();bool video=VideoProcessor::isVideo(path);modes->button(video?1:0)->setChecked(true);captureRow->hide();videoRow->setVisible(video);inMarker->setEnabled(video);outMarker->setEnabled(video);\n'
'    QString name=inputTitles.value(path,QFileInfo(inputAliases.value(path,path)).fileName());filename->setText(fontMetrics().elidedText(name,Qt::ElideMiddle,qMax(160,width()-190)));filename->setToolTip(inputAliases.value(path,path));{QSignalBlocker block(navigation);navigation->setCurrentIndex(batch.files.indexOf(path));}navLabel->setText(QString("%1 / %2").arg(batch.files.indexOf(path)+1).arg(batch.files.size()));\n'
'    status->setText("Loading…");loading=true;QString ffmpeg=VideoProcessor::findFfmpeg();loadWatcher.setFuture(QtConcurrent::run([path,video,ffmpeg]{LoadedMedia result;try{if(video){result.info=VideoProcessor::inspect(ffmpeg,path);result.image=VideoProcessor::firstFrame(ffmpeg,path);}else{result.image=ImageProcessor::read(path);result.info.size=result.image.size();}}catch(const std::exception &e){result.error=QString::fromUtf8(e.what());}return result;}));\n'
'}\n'
)
new1 = (
'void MainWindow::selectFile(const QString &path){\n'
'      if(batchStrip){batchStrip->show();if(auto *dock=batchStrip->parentWidget())dock->setFixedHeight(164);}if(exportButton)exportButton->setEnabled(true);\n'
'      if(loading)return;stopPlayback();player->setSource({});currentFile=path;const bool isArtboard=path.startsWith(QLatin1String("aspectra://"));if(!canvasLayers.contains(path)){CanvasLayer layer;layer.source=path;layer.name=isArtboard?QString("Artboard %1").arg(batch.files.indexOf(path)+1):QFileInfo(inputTitles.value(path,inputAliases.value(path,path))).completeBaseName();if(isArtboard)layer.nativeSize=QSize(widthInput->value(),heightInput->value());canvasLayers[path]=layer;}restoreLayerOverride();bool video=!isArtboard&&VideoProcessor::isVideo(path);modes->button(video?1:0)->setChecked(true);captureRow->hide();videoRow->setVisible(video);inMarker->setEnabled(video);outMarker->setEnabled(video);\n'
'    QString name=isArtboard?canvasLayers.value(path).name:inputTitles.value(path,QFileInfo(inputAliases.value(path,path)).fileName());filename->setText(fontMetrics().elidedText(name,Qt::ElideMiddle,qMax(160,width()-190)));filename->setToolTip(isArtboard?name:inputAliases.value(path,path));{QSignalBlocker block(navigation);navigation->setCurrentIndex(batch.files.indexOf(path));}navLabel->setText(QString("%1 / %2").arg(batch.files.indexOf(path)+1).arg(batch.files.size()));\n'
'    if(isArtboard){\n'
'        // Artboards are synthetic canvases with no file on disk (identified by\n'
'        // an aspectra:// URI). Routing them through the async file/video loader\n'
'        // below would always fail (no such file), silently pop an error and\n'
'        // leave the previous artboard\'s image on screen. Build a blank\n'
'        // transparent frame from the stored layer size instead.\n'
'        loading=false;const auto &layer=canvasLayers[path];QSize size=layer.nativeSize.isValid()?layer.nativeSize:QSize(widthInput->value(),heightInput->value());\n'
'        original=QImage(size,QImage::Format_ARGB32);original.fill(Qt::transparent);texturePreviewSource={};media.size=size;\n'
'        refresh();status->setText(QString("Artboard \\"%1\\" ready").arg(layer.name));\n'
'        return;\n'
'    }\n'
'    status->setText("Loading…");loading=true;QString ffmpeg=VideoProcessor::findFfmpeg();loadWatcher.setFuture(QtConcurrent::run([path,video,ffmpeg]{LoadedMedia result;try{if(video){result.info=VideoProcessor::inspect(ffmpeg,path);result.image=VideoProcessor::firstFrame(ffmpeg,path);}else{result.image=ImageProcessor::read(path);result.info.size=result.image.size();}}catch(const std::exception &e){result.error=QString::fromUtf8(e.what());}return result;}));\n'
'}\n'
)
replace_once(old1, new1, "selectFile")

# Edit 2: base layer row label — prefer the stored CanvasLayer name (so artboards
# show "Artboard N" instead of a garbled name derived from the aspectra:// URI).
old2 = 'auto *baseWidget = createLayerRow(sourcePath.isEmpty() ? "Canvas" : QFileInfo(sourcePath).completeBaseName(), baseVisible, hasMask, true, true, [this, sourcePath](bool visible) {'
new2 = ('QString baseName = sourcePath.isEmpty() ? "Canvas" : (canvasLayers.contains(sourcePath) && !canvasLayers[sourcePath].name.isEmpty() ? canvasLayers[sourcePath].name : QFileInfo(sourcePath).completeBaseName());\n'
        '                    auto *baseWidget = createLayerRow(baseName, baseVisible, hasMask, true, true, [this, sourcePath](bool visible) {')
replace_once(old2, new2, "baseName-decl")

# Edit 3: breadcrumb label — reuse the same resolved name.
old3 = 'crumbLabel->setText("Artboards / " + QFileInfo(sourcePath).completeBaseName());'
new3 = 'crumbLabel->setText("Artboards / " + baseName);'
replace_once(old3, new3, "crumbLabel")

with open(path, "w", encoding="utf-8", newline="") as f:
    f.write(src)

# Brace balance sanity check
opens = src.count("{")
closes = src.count("}")
print(f"MainWindow.cpp: {{ {opens} }} {closes} balanced={opens==closes}")
print("ALL EDITS APPLIED OK")
