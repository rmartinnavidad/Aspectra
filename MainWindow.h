#pragma once
#include <QtWidgets>
#include <QtConcurrent>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>
#include <QMovie>
#include "Processing.h"
#include "PreviewWidget.h"
#include "TimelineWidget.h"
#include "MaterialViewport.h"
#include "ModelImporter.h"
#include "ModernUi.h"
class AspectraGalleryScreen;
struct LoadedMedia { QImage image;MediaInfo info;QString error;quint64 serial=0;QVector<int> gifTimes; };
struct CanvasLayer { QString source,name,text,blendMode{"Normal"};QPointF position{0,0},textPosition{20,52};QSize nativeSize;QColor textColor{Qt::white};int textSize=32;double opacity=1.,fill=1.;bool visible=true,textVisible=false,hasOverride=false;Adjustments overrideAdjustments;QImage mask,selection;QPointF cloneSource;bool hasCloneSource=false; };
class MainWindow : public AspectraWindow {
    Q_OBJECT
    friend int runUiTests(MainWindow &);
    friend int runFocusZoomTest(MainWindow &);
    friend int runPowerUiTests(MainWindow &);
public:
    explicit MainWindow(QWidget *parent=nullptr);
    ~MainWindow() override;
    void loadFiles(const QStringList &paths);
    void savePreview(const QString &path);
    void showWelcomeScreen();
protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *,QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    BatchManager batch;QImage original;QString currentFile,lastFolder,recordFile;MediaInfo media;
    QMap<QString,CanvasLayer> canvasLayers;QVector<TimelineTrack> timelineTracks;
    PreviewWidget *preview=nullptr;
    QLabel *filename=nullptr,*status=nullptr,*info=nullptr,*batchLabel=nullptr,*zoomValue=nullptr,*thumbnail=nullptr,*timeLabel=nullptr,*navLabel=nullptr;
    QSpinBox *widthInput=nullptr,*heightInput=nullptr,*quality=nullptr,*crf=nullptr,*textLayerSize=nullptr,*textLayerX=nullptr,*textLayerY=nullptr,*trackX=nullptr,*trackY=nullptr,*trackScale=nullptr,*trackRotation=nullptr,*trackOpacity=nullptr;
    QSpinBox *canvasX=nullptr,*canvasY=nullptr;
    QSpinBox *maskBrushSize=nullptr;
    QSlider *zoom=nullptr,*batchNavigator=nullptr;
    QCheckBox *lock=nullptr,*zip=nullptr,*rar=nullptr,*flat=nullptr,*guides=nullptr,*chromaKeyEnabled=nullptr,*perImageOverride=nullptr;QSlider *keyToleranceSlider=nullptr,*keySoftnessSlider=nullptr,*keyDespillSlider=nullptr,*keyLumaProtectSlider=nullptr,*keyMatteBiasSlider=nullptr,*keyCleanBlackSlider=nullptr,*keyCleanWhiteSlider=nullptr;
    QComboBox *ratio=nullptr,*previewVariant=nullptr,*navigation=nullptr,*archiveMode=nullptr,*workerCount=nullptr,*trackBlend=nullptr,*trackTransition=nullptr;
    QDoubleSpinBox *inMarker=nullptr,*outMarker=nullptr;
    QListWidget *sizes=nullptr,*batchStrip=nullptr,*trackList=nullptr,*modelTextureList=nullptr;QTableWidget *clipTable=nullptr;QScrollArea *tabCarousel=nullptr;QWidget *bottomDock=nullptr,*carouselContent=nullptr,*carouselLeftFade=nullptr,*carouselRightFade=nullptr;QPointer<QPropertyAnimation> carouselBounce;
    QTabWidget *tabs=nullptr;
    QPushButton *exportButton=nullptr,*playButton=nullptr,*captureButton=nullptr,*saveTraceButton=nullptr;QToolButton *modelRail=nullptr,*undoButton=nullptr,*redoButton=nullptr;
    QWidget *captureRow=nullptr,*videoRow=nullptr,*videoWorkspace=nullptr;QButtonGroup *modes=nullptr;
    QMap<QString,QSlider*> sliders;QMap<QString,QCheckBox*> styleChecks;
    QVector<QDoubleSpinBox*> safeInputs,paddingInputs;
    QMap<QString,QCheckBox*> imageFormats,videoFormats;
    QVector<RecolorPreset> presets{{"White",Qt::white,0,1,false},{"Black",Qt::black,0,1,false}};
    Adjustments layerStyle;
    QMap<QString,QVector<ClipRange>> clips;
    QMap<QString,QPair<double,double>> draftRanges;
    QMap<QString,QString> inputAliases;
    // Temporary renders (such as PSD artboards) retain the source document but display their own name.
    QMap<QString,QString> inputTitles;
    QFutureWatcher<ExportResult> exportWatcher;
    QFutureWatcher<LoadedMedia> loadWatcher;
    std::shared_ptr<std::atomic_bool> cancelFlag;
    QProcess *recorder=nullptr;
    QMediaPlayer *player=nullptr;QAudioOutput *audio=nullptr;QVideoSink *sink=nullptr;TimelineWidget *timeline=nullptr;
    QDialog *exportDialog=nullptr;QProgressBar *exportProgress=nullptr;QLabel *exportStatus=nullptr;QPlainTextEdit *exportLog=nullptr;QPushButton *exportCancel=nullptr,*exportDone=nullptr,*exportOpen=nullptr;
    QPoint dragOffset;
    QPoint carouselDragStart,carouselRestPosition;int carouselScrollStart=0;bool carouselDragging=false,carouselOverscrolling=false;
    bool rangePlayback=false,compositionMode=false,compositionPlaying=false,rasterStrokeOpen=false;
    QStack<QImage> undoHistory,redoHistory;
    bool changing=false,loading=false,recordMode=false,closing=false,updatingClips=false,applyingLayerOverride=false;
    double lockedRatio=1;
    QStringList logs;QString pendingTraceSvg;int pendingTraceColors=0;bool tracePreviewActive=false;QTimer traceRefreshTimer;QTemporaryDir captures;
    quint64 loadGeneration=0;
    QMovie *gif=nullptr;QVector<int> gifTimes;QTimer compositionTimer;double compositionPosition=0;
    QTimer autosaveTimer;
    QDialog *fullscreen=nullptr;PreviewWidget *fullPreview=nullptr;
    AspectraGalleryScreen *galleryScreen=nullptr;
    QCheckBox *followSource=nullptr,*targetEnabled=nullptr,*allowSmaller=nullptr,*stripMetadata=nullptr,*copyCapture=nullptr;
    QComboBox *marginUnits=nullptr,*sizingMode=nullptr,*targetUnit=nullptr,*encodeSpeed=nullptr,*pngLevel=nullptr,*paletteSize=nullptr,*captureKind=nullptr,*captureScreen=nullptr,*captureAudio=nullptr;
    QSpinBox *fpsLimit=nullptr,*audioRate=nullptr,*captureDelay=nullptr,*captureFps=nullptr,*captureLimit=nullptr;
    QDoubleSpinBox *targetSize=nullptr;
    QMap<QString,QCheckBox*> audioFormats;
    QMap<QString,QCheckBox*> textureMaps;
    QMap<QString,QSlider*> textureMapSliders;
    QMap<QString,QVector<int>> textureMapAdvanced;
    QComboBox *textureMapPreview=nullptr,*textureLighting=nullptr,*textureFormat=nullptr;QSlider *textureOffsetX=nullptr,*textureOffsetY=nullptr;
    QVector<QDoubleSpinBox*> textureBgw;
    QCheckBox *textureAutoNormalize=nullptr,*textureHistogram=nullptr,*textureShowClipping=nullptr,*textureZip=nullptr,*textureRar=nullptr;
    QTimer textureRefreshTimer;
    QImage texturePreviewSource;
    QDialog *materialPreviewDialog=nullptr;MaterialViewport *materialPreviewView=nullptr,*texturePreviewSphere=nullptr;QSlider *lightAzimuth=nullptr,*lightElevation=nullptr,*lightIntensity=nullptr,*lightAmbient=nullptr;
    QSlider *patternBlend=nullptr,*patternVariation=nullptr,*modelScale=nullptr,*modelUvScale=nullptr,*modelUvX=nullptr,*modelUvY=nullptr;QCheckBox *patternMirror=nullptr;QLabel *patternPreview=nullptr,*modelSummary=nullptr;ModelAsset modelAsset;
    QWidget *imageFormatRow=nullptr,*videoFormatRow=nullptr,*audioFormatRow=nullptr;
    QRect lastCaptureArea;
    bool pixelMargins=false,discardRecording=false,capturePending=false;
    void buildPowerFeatures();void clearMedia();void mediaContext();void routeMode(int);void openImageInCompositor();
    void rotateMedia(int);void changeMarginUnits(int);QMarginsF normalizedMargins(const QVector<QDoubleSpinBox*>&) const;
    void seekMedia(double);void deliverFrame(const QImage &);void fullScreenPlayer();
    void ingestCapture(QImage);void annotateMedia();void configureCapture();
    void buildUi();void loadProject();void createNewProject(int,int,int,bool,const QString &,const QString &,const QColor &);void autosaveProject();void openProjectFile(const QString &);void restoreLayerOverride();
    QWidget *makeAdjustTab(const QStringList &,const QStringList &,const QVector<int>&,const QVector<int>&,const QVector<int>&);
    QWidget *framePanel();QWidget *colorPanel();QWidget *selectRefinePanel();QWidget *stylesPanel();QWidget *clipsPanel();QWidget *outputPanel();QWidget *texturePanel();QWidget *patternsPanel();QWidget *modelPanel();
    Adjustments adjustments() const;
    void refresh();void refreshTexturePreview();void updatePatternPreview();void showMaterialPreview();void chooseFiles();void loadModelFile(const QString &);void manageBatch();void selectFile(const QString &);void navigate(int);
    void setDimensions(int,int);void aspectChanged();void updateBatchLabel();void updateLayerDrawer();void addSize(int=0);
    void exportFiles();void settingsDialog();void editPreset(int);void startCapture();void startRecording(QRect);
    void togglePlayback();void stopPlayback();void showError(const QString &);void showLog();
    void recordUndoState();void undoRaster();void redoRaster();void updateUndoControls();
    void syncRange(bool save=true);void updateClipTable();void addClip();void updateTrackPanel();QImage compositeTimelineTracks(const QImage &) const;void showExportWindow();
};
