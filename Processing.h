#pragma once
#include <QtCore>
#include <QtGui>
#include <atomic>
#include <functional>
#include <memory>

struct RecolorPreset {
    QString name;
    QColor color;
    double threshold = 0.0;
    double gamma = 1.0;
    bool enabled = false;
};
struct Adjustments {
    int rotation = 0;
    double zoom = 1.0;
    double hue = 0, saturation = 1, brightness = 0;
    double blacks = 0, gamma = 1, whites = 1;
    double red = 0, green = 0, blue = 0;
    bool flat = false;
    double contrast = 1, exposure = 0;
    bool chromaKey = false;
    QColor keyColor{0,255,0};
    double keyTolerance = .18, keySoftness = .08, keyDespill = 0, keyLumaProtect = 0, keyMatteBias = 0, keyCleanBlack = 0, keyCleanWhite = 0;
    double keyContract = 0, keySmooth = 0, keyEdgeContrast = 0, keyRadius = 0, keyDensity = 1, keyShiftEdge = 0, keySmartRadius = 0;
    QMarginsF padding; // Fractions of the output canvas; exported, unlike safe guides.
    double opacity = 1;
    bool overlay = false, gradient = false, stroke = false, shadow = false, glow = false, innerShadow = false, bevel = false;
    QColor overlayColor{"#3d91fb"}, gradientEnd{"#ca267a"}, strokeColor{"#fac665"}, shadowColor{Qt::black}, glowColor{"#3d91fb"};
    double effectOpacity = .75, strokeSize = .01, shadowSize = .025, shadowDistance = .025, glowSize = .025, bevelSize = .01;
    int blendMode = 0;
    bool hasStyles() const { return opacity!=1 || overlay || gradient || stroke || shadow || glow || innerShadow || bevel; }
    bool hasColor() const { return hue!=0||saturation!=1||brightness!=0||blacks!=0||gamma!=1||whites!=1||red!=0||green!=0||blue!=0||flat||contrast!=1||exposure!=0||chromaKey; }
};
struct ClipRange { QString name; double start = 0, end = 0; bool enabled = true; };
struct TimelineKeyframe {
    double time=0; QPointF position; QPointF scale{1,1}; double rotation=0,opacity=1;
    TimelineKeyframe()=default;
    TimelineKeyframe(double at,QPointF point,double alpha):time(at),position(point),opacity(alpha){}
    TimelineKeyframe(double at,QPointF point,QPointF size,double turn,double alpha):time(at),position(point),scale(size),rotation(turn),opacity(alpha){}
};
struct TimelineTrack {
    enum Type { Video, Audio, Image, Text, Effect };
    Type type=Text;
    QString name,text,source,blendMode{"Normal"},transition{"Fade"},groupId,artboardSource;
    QImage image;
    QColor color{Qt::white};
    QPointF position{24,56},scale{1,1};
    double rotation=0,opacity=1,fill=1,start=0,end=0,transitionIn=0,transitionOut=0,volume=1;
    int fontSize=32;
    QString fontFamily{"Segoe UI"},fontStyle{"Regular"},language{"System"};
    QColor highlightColor{Qt::transparent};
    double letterSpacing=0.,lineSpacing=100.,kerning=0.,verticalScale=100.,horizontalScale=100.,baseline=0.,highlightWidth=0.,sharpness=100.;
    bool fontBold=true,fontItalic=false,allCaps=false,smallCaps=false,superscript=false,subscript=false,underline=false,textStroke=false;
    bool standardLigature=true,contextualAlternates=true,discretionaryLigature=false,swash=false,stylisticAlternates=false,tiltingAlternates=false,ordinals=false,fractions=false;
    bool enabled=true,locked=false,muted=false,lockPixels=false,lockPosition=false,isGroup=false;
    QVector<TimelineKeyframe> keyframes;
};
struct MediaInfo {
    QSize size;
    double fps = 30, duration = 0;
};
struct ExportJob {
    QStringList files;
    QVector<QSize> sizes;
    QVector<RecolorPreset> presets;
    Adjustments adjustments;
    QString outputRoot, ffmpeg, rar;
    QString imageFormat = "PNG", videoFormat = "MP4";
    QStringList imageFormats, videoFormats;
    QStringList audioFormats;
    bool explicitFormats = false, allowDownscale = false, stripMetadata = true;
    qint64 targetBytes = 0;
    int sizeMode = 0; // 0: supplied sizes; 1: each source's native rotated size.
    int outputFps = 0, audioKbps = 128, gifColors = 256;
    QString encoderPreset = "veryfast";
    QMap<QString,QVector<ClipRange>> clips;
    QVector<TimelineTrack> timelineTracks;
    QMap<QString,QString> inputAliases;
    QMap<QString,QImage> masks;
    int workers = 2, pngCompression = 1, archiveLevel = 1;
    int quality = 90, crf = 23;
    double start = 0, duration = 0;
    bool zip = true, createRar = false;
};
struct ExportResult {
    QString folder, error;
    QStringList log;
    int written = 0;
    bool cancelled = false;
};
class ImageProcessor {
public:
    static QRectF cropRect(QSize source, QSize target, double zoom);
    static QImage render(const QImage &, QSize, const Adjustments &, const RecolorPreset * = nullptr);
    static QImage compositeTimeline(const QImage &, const QVector<TimelineTrack> &, double);
    static QImage read(const QString &);
};
class VideoProcessor {
public:
    static bool isVideo(const QString &);
    static QString findFfmpeg();
    static QString findRar();
    static QImage firstFrame(const QString &tool, const QString &file, double seconds = 0);
    static MediaInfo inspect(const QString &tool, const QString &file);
    static void render(const ExportJob &, const QString &input, const QString &output,
                       QSize, const RecolorPreset *, std::atomic_bool &, const std::function<void(QString)> &);
};
class ExportManager {
public:
    static void zip(const QString &folder, const QString &archive, std::atomic_bool &, int level = 1);
    static ExportResult run(const ExportJob &, std::atomic_bool &, const std::function<void(int,int,QString)> &);
};
class BatchManager {
public:
    QStringList files;
    void add(const QStringList &paths);
    static bool supported(const QString &);
};
int runSelfTests();
