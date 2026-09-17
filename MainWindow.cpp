#include "MainWindow.h"
#include "AspectraGalleryScreen.h"
#include "ScreenCaptureManager.h"
#include "GradientSlider.h"
#define QSlider(...) GradientSlider(__VA_ARGS__)
#include "PsdImporter.h"
#include "VectorTracer.h"
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QSaveFile>
#include <QDateTime>
#include <QImageReader>
#include <QUuid>
#include <QTimer>
#include <QVideoFrame>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif
namespace {
// Bottom icon-rail baseline.  These values are intentionally the sole
// runtime source of truth: do not change one rail independently.
namespace BottomRailMetrics {
inline constexpr int gap = 1;
inline constexpr int toolbarY = 0;
inline constexpr int toolbarHeight = 40;
inline constexpr int modesY = toolbarY + toolbarHeight + gap;
inline constexpr int modesHeight = 48;
inline constexpr int panelsY = modesY + modesHeight + gap;
inline constexpr int panelsHeight = 40;
static_assert(modesY == 41 && panelsY == 90,
              "Keep the saved one-pixel icon-rail baseline intact.");
}
class TapRelay final : public QObject {
public:
    explicit TapRelay(std::function<void()> callback,QObject *parent=nullptr):QObject(parent),callback(std::move(callback)){}
protected:
    bool eventFilter(QObject *,QEvent *event) override {if(event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton)QTimer::singleShot(0,this,callback);return false;}
private:
    std::function<void()> callback;
};
QIcon settingIcon(const QString &name);
class CanvasAspectPreview final : public QWidget {
public:
    explicit CanvasAspectPreview(QWidget *parent=nullptr):QWidget(parent){setMinimumSize(220,180);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
    void updateAspect(int width,int height,int resolution,const QString &unit){
        m_width=qMax(1,width);m_height=qMax(1,height);m_resolution=qMax(1,resolution);m_unit=unit;update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        const QRectF area=rect().adjusted(18,16,-18,-16);
        const qreal aspect=qMax<qreal>(.01,qreal(m_width)/qreal(m_height));
        const qreal factor=qMin(area.width()/aspect,area.height());
        const QSizeF size(aspect*factor,factor);
        const QRectF canvas(area.center().x()-size.width()/2,area.center().y()-size.height()/2,size.width(),size.height());
        QPainterPath canvasPath;canvasPath.addRoundedRect(canvas,8,8);
        painter.setPen(QPen(QColor("#7a4dff"),2));painter.setBrush(QColor(122,77,255,35));painter.drawPath(canvasPath);
        painter.save();painter.setClipPath(canvasPath);
        const qreal density=qBound<qreal>(1.0,qreal(m_resolution)/24.0,160.0);
        const qreal spacingX=qMax<qreal>(4.0,canvas.width()/density),spacingY=qMax<qreal>(4.0,canvas.height()/density);
        painter.setPen(QPen(QColor(122,77,255,20),1));
        for(qreal x=canvas.left();x<=canvas.right();x+=spacingX)painter.drawLine(QPointF(x,canvas.top()),QPointF(x,canvas.bottom()));
        for(qreal y=canvas.top();y<=canvas.bottom();y+=spacingY)painter.drawLine(QPointF(canvas.left(),y),QPointF(canvas.right(),y));
        painter.restore();
        qreal pixelWidth=m_width,pixelHeight=m_height;
        if(m_unit=="in"){pixelWidth*=m_resolution;pixelHeight*=m_resolution;}
        else if(m_unit=="mm"){pixelWidth=pixelWidth/25.4*m_resolution;pixelHeight=pixelHeight/25.4*m_resolution;}
        const QString primary=QString("%1 × %2 %3").arg(m_width).arg(m_height).arg(m_unit);
        QString secondary;
        if(m_unit=="px")secondary=QString("Physical Print: %1\" × %2\"").arg(QString::number(qreal(m_width)/m_resolution,'f',2)).arg(QString::number(qreal(m_height)/m_resolution,'f',2));
        else if(m_unit=="in")secondary=QString("Digital Size: %1 × %2 px").arg(qRound(pixelWidth)).arg(qRound(pixelHeight));
        else secondary=QString("Digital Size: %1 × %2 px").arg(qRound(pixelWidth)).arg(qRound(pixelHeight));
        const QPointF center=canvas.center();
        painter.setPen(QColor(237,231,255,235));painter.setFont(QFont("Segoe UI",10,QFont::DemiBold));painter.drawText(QRectF(canvas.left()+10,center.y()-19,canvas.width()-20,20),Qt::AlignCenter,primary);
        painter.setPen(QColor(213,203,240,170));painter.setFont(QFont("Segoe UI",8,QFont::Medium));painter.drawText(QRectF(canvas.left()+10,center.y()+2,canvas.width()-20,18),Qt::AlignCenter,secondary);
        painter.setPen(QColor(221,211,255,190));painter.setFont(QFont("Segoe UI",8,QFont::DemiBold));painter.drawText(canvas.adjusted(10,8,-10,-8),Qt::AlignRight|Qt::AlignBottom,QString::number(pixelWidth*pixelHeight/1000000.0,'f',1)+" MP");
    }
private:
    int m_width=1920,m_height=1080,m_resolution=300;
    QString m_unit="px";
};
class FluidToolButton : public QToolButton {
public:
    qreal m_hoverProgress = 0.0;
    qreal m_clickScale = 1.0;
    qreal m_rippleRadius = 0.0;
    QPointF m_parallax;
    QPointF m_clickPos;
    QVariantAnimation *hoverAnim = nullptr;
    QVariantAnimation *clickAnim = nullptr;
    QVariantAnimation *parallaxAnim = nullptr;
    QVariantAnimation *rippleAnim = nullptr;

    explicit FluidToolButton(QWidget *parent = nullptr) : QToolButton(parent) {
        setMouseTracking(true);
        setStyleSheet("background: transparent; border: none;");
        hoverAnim = new QVariantAnimation(this);
        hoverAnim->setDuration(150);
        hoverAnim->setEasingCurve(QEasingCurve::OutQuad);
        connect(hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_hoverProgress = value.toReal();
            update();
        });
        clickAnim = new QVariantAnimation(this);
        clickAnim->setDuration(150);
        clickAnim->setEasingCurve(QEasingCurve::InQuad);
        connect(clickAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { m_clickScale = value.toReal(); update(); });
        parallaxAnim = new QVariantAnimation(this);
        parallaxAnim->setDuration(250);
        parallaxAnim->setEasingCurve(QEasingCurve::OutBack);
        connect(parallaxAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { m_parallax = value.toPointF(); update(); });
        rippleAnim = new QVariantAnimation(this);
        rippleAnim->setDuration(300);
        rippleAnim->setEasingCurve(QEasingCurve::OutQuad);
        connect(rippleAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { m_rippleRadius = value.toReal(); update(); });
        connect(rippleAnim, &QVariantAnimation::finished, this, [this] { m_rippleRadius = 0.0; update(); });
    }
protected:
    void enterEvent(QEnterEvent *event) override {
        hoverAnim->stop(); hoverAnim->setStartValue(m_hoverProgress); hoverAnim->setEndValue(1.0); hoverAnim->start();
        QToolButton::enterEvent(event);
    }
    void leaveEvent(QEvent *event) override {
        hoverAnim->stop(); hoverAnim->setStartValue(m_hoverProgress); hoverAnim->setEndValue(0.0); hoverAnim->setEasingCurve(QEasingCurve::OutCubic); hoverAnim->start();
        parallaxAnim->stop(); parallaxAnim->setStartValue(m_parallax); parallaxAnim->setEndValue(QPointF(0, 0)); parallaxAnim->start();
        QToolButton::leaveEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override {
        m_clickPos = event->position();
        clickAnim->stop(); clickAnim->setEasingCurve(QEasingCurve::InQuad); clickAnim->setStartValue(m_clickScale); clickAnim->setEndValue(.88); clickAnim->start();
        rippleAnim->stop(); rippleAnim->setStartValue(0.0); rippleAnim->setEndValue(width() * 1.5); rippleAnim->start();
        QToolButton::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        clickAnim->stop(); clickAnim->setEasingCurve(QEasingCurve::OutElastic); clickAnim->setStartValue(m_clickScale); clickAnim->setEndValue(1.0); clickAnim->start();
        QToolButton::mouseReleaseEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        const QPointF center(width() / 2.0, height() / 2.0), delta = event->position() - center;
        m_parallax = QPointF(qBound(-4.0, delta.x() * 0.15, 4.0), qBound(-4.0, delta.y() * 0.15, 4.0));
        update();
        QToolButton::mouseMoveEvent(event);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF bounds = rect().adjusted(1, 1, -1, -1);

        // The background is deliberately rendered in untransformed widget
        // coordinates. It can never inherit icon-scale or parallax state.
        p.save();
        QPainterPath clipPath;
        clipPath.addRoundedRect(bounds, 10.0, 10.0);
        QColor baseColor = isChecked() ? QColor(122, 77, 255, 40) : QColor(9, 9, 12, 200);
        if (m_hoverProgress > 0) baseColor = baseColor.lighter(qRound(100 + 15 * m_hoverProgress));
        p.fillPath(clipPath, baseColor);
        if (m_rippleRadius > 0) {
            QRadialGradient ripple(m_clickPos, m_rippleRadius);
            ripple.setColorAt(0, QColor(255, 255, 255, 60));
            ripple.setColorAt(1, QColor(255, 255, 255, 0));
            p.fillPath(clipPath, ripple);
        }
        p.setPen(QPen(isChecked() ? QColor(122, 77, 255) : QColor(255, 255, 255, qRound(20 + 40 * m_hoverProgress)), isChecked() ? 2.0 : 1.0));
        p.drawPath(clipPath);
        p.restore();

        // The icon gets its own centered coordinate system.  Its transform
        // cannot move the painted background or accumulate across frames.
        p.save();
        p.translate(bounds.center());
        p.scale(m_clickScale, m_clickScale);
        p.translate(m_parallax);
        if (!icon().isNull()) {
            // Rail icons start 25% smaller, then grow visually on hover
            // without ever changing their widget geometry or clip region.
            const qreal iconScale = 0.75 + 0.50 * m_hoverProgress;
            const QSize dynamicIconSize = iconSize() * iconScale;
            const QPixmap pix = icon().pixmap(dynamicIconSize);
            p.drawPixmap(QRectF(-pix.width()/2.0, -pix.height()/2.0, pix.width(), pix.height()), pix, pix.rect());
        }
        p.restore();
    }
};
FluidToolButton *fluidizeRailButton(QToolButton *legacy,bool isDockMode=true){
    if(!legacy)return nullptr;
    if(auto *fluid=dynamic_cast<FluidToolButton*>(legacy))return fluid;
    auto *host=legacy->parentWidget();if(!host)return nullptr;
    Q_UNUSED(isDockMode);
    auto *fluid=new FluidToolButton(host);
    fluid->setObjectName(legacy->objectName());fluid->setText(legacy->text());fluid->setIcon(legacy->icon());fluid->setIconSize(legacy->iconSize());
    fluid->setToolButtonStyle(legacy->toolButtonStyle());fluid->setToolTip(legacy->toolTip());fluid->setStatusTip(legacy->statusTip());fluid->setWhatsThis(legacy->whatsThis());
    fluid->setCheckable(legacy->isCheckable());fluid->setChecked(legacy->isChecked());fluid->setEnabled(legacy->isEnabled());fluid->setFixedSize(legacy->size());
    fluid->setContextMenuPolicy(Qt::CustomContextMenu);
    if(auto *layout=host->layout())layout->replaceWidget(legacy,fluid);
    fluid->setGeometry(legacy->geometry());
    QObject::connect(legacy,&QToolButton::toggled,fluid,[fluid](bool checked){fluid->setChecked(checked);});
    QObject::connect(fluid,&QToolButton::clicked,legacy,[legacy,fluid]{legacy->click();QTimer::singleShot(0,fluid,[legacy,fluid]{fluid->setChecked(legacy->isChecked());});});
    QObject::connect(fluid,&QWidget::customContextMenuRequested,legacy,[legacy](const QPoint &point){const QPoint global=legacy->mapToGlobal(point);QContextMenuEvent forwarded(QContextMenuEvent::Mouse,legacy->mapFromGlobal(global),global);QCoreApplication::sendEvent(legacy,&forwarded);});
    legacy->hide();
    return fluid;
}
void fluidizeRailButtons(QWidget *host,bool isDockMode=true){
    if(!host)return;
    const auto legacyButtons=host->findChildren<QToolButton*>();
    for(auto *button:legacyButtons)fluidizeRailButton(button,isDockMode);
}
void provideFluidRailRoom(QWidget *rail){
    if(!rail)return;
    if(auto *scroll=qobject_cast<QScrollArea*>(rail)){
        scroll->setFixedHeight(54);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->viewport()->setStyleSheet("background: transparent; border: none;");
        if(auto *host=scroll->widget()){
            host->setMinimumHeight(0);
            host->setMaximumHeight(QWIDGETSIZE_MAX);
            if(auto *layout=qobject_cast<QHBoxLayout*>(host->layout())){
                for(int index=layout->count()-1;index>=0;--index)if(layout->itemAt(index)->spacerItem())delete layout->takeAt(index);
                layout->setAlignment(Qt::AlignVCenter|Qt::AlignHCenter);
                layout->setContentsMargins(8,4,8,4);
            }
        }
    }else if(auto *layout=qobject_cast<QHBoxLayout*>(rail->layout())){
        rail->setFixedHeight(54);
        for(int index=layout->count()-1;index>=0;--index)if(layout->itemAt(index)->spacerItem())delete layout->takeAt(index);
        layout->setAlignment(Qt::AlignVCenter|Qt::AlignHCenter);
        layout->setContentsMargins(8,4,8,4);
    }
}
class SettingRailButton final : public FluidToolButton {
public:
    explicit SettingRailButton(QString name,QWidget *parent=nullptr):FluidToolButton(parent),glyph(name.left(2).toUpper()),icon(settingIcon(name)){setIcon(icon);setIconSize(QSize(20,20));setFixedSize(38,38);}
    void setSelected(bool on){selected=on;update();}
    void setLiveRange(int low,int high){minimum=low;maximum=qMax(low+1,high);}
    void setLiveValue(int next){value=next;update();}
protected:
    void paintEvent(QPaintEvent *event) override {FluidToolButton::paintEvent(event);if(!selected)return;QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);const QRectF circle=rect().adjusted(2,2,-2,-2);const double amount=qBound(0.,double(value-minimum)/double(maximum-minimum),1.);const QColor fill=QColor::fromHsvF(.38+.20*amount,.78,1.0);painter.setPen(QPen(fill,2.2));painter.setBrush(Qt::NoBrush);painter.drawEllipse(circle);if(property("textureMode").toBool())return;painter.setPen(Qt::NoPen);painter.setBrush(fill);painter.drawPie(circle,90*16,-qRound(amount*5760));painter.setPen(Qt::white);painter.setFont(QFont("Segoe UI",10,QFont::Bold));painter.drawText(circle,Qt::AlignCenter,QString::number(value));}
private:
    QString glyph;QIcon icon;int minimum=0,maximum=100,value=0;bool selected=false;
};
QIcon whiteIcon(const QString &path);
QIcon controlIcon(const QString &text){
    const QString label=text.toLower();const QString root="C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/";QString file;
    if(label.contains("import")||label.contains("add image"))file="frame tool.png";
    else if(label.contains("video"))file="video.png";
    else if(label.contains("text"))file="warped text.png";
    else if(label.contains("capture"))file="crop tool.png";
    else if(label.contains("trace"))file="pen tool.png";
    else if(label.contains("export")||label.contains("save"))file="trim_save frame.png";
    else if(label.contains("reset"))file="playback_repeat on.png";
    else if(label.contains("remove")||label.contains("close"))file="delete anchor point tool.png";
    else if(label.contains("play"))file="playback_play.png";
    else if(label.contains("keyframe"))file="trim_save frame.png";
    else if(label.contains("color"))file="gradient tool.png";
    else if(label.contains("settings")||label.contains("path"))file="panel settings.png";
    else if(label.contains("brush")||label.contains("annotate"))file="brush tool.png";
    else if(label.contains("clip"))file="trim_save frame.png";
    return file.isEmpty()?QIcon{}:whiteIcon(root+file);
}
QPushButton *button(const QString &text,QLayout *layout=nullptr,const QString &color={}){auto *b=new QPushButton(text);b->setCursor(Qt::PointingHandCursor);const QIcon icon=controlIcon(text);if(!icon.isNull()){b->setIcon(icon);b->setIconSize(QSize(17,17));}b->setToolTip(text);if(!color.isEmpty())b->setProperty("tone",color);if(layout)layout->addWidget(b);return b;}
QIcon whiteIcon(const QString &path){QImage source(path);if(source.isNull())return {};source=source.convertToFormat(QImage::Format_ARGB32);for(int y=0;y<source.height();++y){QRgb *line=reinterpret_cast<QRgb*>(source.scanLine(y));for(int x=0;x<source.width();++x)line[x]=qRgba(255,255,255,qAlpha(line[x]));}return QIcon(QPixmap::fromImage(source));}
QIcon settingIcon(const QString &name){
    const QString key=name.toLower();const QString root="C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/";QString file;
    if(key=="frame")file="frame tool.png"; else if(key=="canvas")file="icon_canvas.png"; else if(key=="safe")file="icon_safe zone.png"; else if(key=="layer")file="shape layer.png"; else if(key=="mask")file="horizontial mask type tool.png";
    else if(key=="hue")file="icon_color_.png"; else if(key=="saturation")file="editor_saturation.png"; else if(key=="brightness")file="editor_brightness_100.png"; else if(key=="contrast")file="editor_contrast.png"; else if(key=="levels")file="editor_levels.png"; else if(key=="balance")file="editor_light balance_full_.png"; else if(key=="key")file="icon_color keying.png"; else if(key=="auto")file="editor_auto.png";
    else if(key=="subject")file="object selection.png"; else if(key=="brush")file="selection brush.png"; else if(key=="contract")file="rectangle marquee tool.png"; else if(key=="feather")file="editor_feather.png"; else if(key=="edge")file="sharpen tool.png"; else if(key=="hair")file="selection brush.png";
    else if(key=="colors")file="gradient tool.png"; else if(key=="detail")file="add anchor point tool.png"; else if(key=="smooth")file="editor_smooth.png"; else if(key=="export")file="trim_save frame.png";
    else if(key=="blend")file="editor_blend.png"; else if(key=="opacity")file="editor_opacity.png"; else if(key=="stroke")file="editor_stroke.png"; else if(key=="shadow")file="editor_shadow.png"; else if(key=="glow")file="editor_glow.png"; else if(key=="bevel")file="editor_bevel.png";
    else if(key=="start")file="trim_head.png"; else if(key=="end")file="trim_marker.png"; else if(key=="reset")file="playback_repeat on.png";
    else if(key=="sphere")file="icon_textures.png"; else if(key=="maps")file="icon_textures.png"; else if(key=="base"||key=="base color")file="icon_color_.png"; else if(key=="normal")file="editor_normal map.png"; else if(key=="roughness")file="editor_roughness map.png"; else if(key=="metallic")file="editor_metal map.png"; else if(key=="light"||key=="emission")file="editor_brightness_100.png"; else if(key=="ambient occlusion")file="editor_shadow.png"; else if(key=="height / displacement")file="distribute heights.png"; else if(key=="specular"||key=="glossiness"||key=="sheen")file="editor_glow.png"; else if(key=="cavity"||key=="curvature")file="editor_levels.png"; else if(key=="subsurface"||key=="transmission")file="editor_opacity.png"; else if(key=="clearcoat"||key=="clearcoat roughness")file="editor_roughness.png"; else if(key=="anisotropy")file="editor_normal map.png"; else if(key=="orm"||key=="mra"||key=="rma")file="icon_textures.png";
    else if(key=="warp")file="warped text.png"; else if(key=="mirror")file="video editor_flip horizontal.png"; else if(key=="sizes")file="crop tool.png"; else if(key=="images")file="frame tool.png"; else if(key=="videos")file="video.png"; else if(key=="quality")file="anti-aliasing.png"; else if(key=="archive")file="smart object.png";
    return file.isEmpty()?QIcon{}:whiteIcon(root+file);
}
QLabel *label(const QString &text,const QString &name={}){auto *l=new QLabel(text);if(!name.isEmpty())l->setObjectName(name);return l;}
QHBoxLayout *row(QVBoxLayout *parent,int spacing=8){auto *r=new QHBoxLayout;r->setSpacing(spacing);parent->addLayout(r);return r;}
void clearLayoutItems(QLayout *layout){while(auto *item=layout->takeAt(0)){if(auto *nested=item->layout()){clearLayoutItems(nested);delete nested;}else{if(auto *widget=item->widget())delete widget;delete item;}}}
QSpinBox *number(int value,int max=8192){auto *n=new QSpinBox;n->setRange(1,max);n->setValue(value);n->setButtonSymbols(QAbstractSpinBox::NoButtons);return n;}
QDoubleSpinBox *seconds(){auto *n=new QDoubleSpinBox;n->setRange(0,86400);n->setDecimals(3);n->setSingleStep(.1);n->setSuffix(" s");n->setButtonSymbols(QAbstractSpinBox::NoButtons);return n;}
QWidget *scrolled(QWidget *content){auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setWidget(content);return scroll;}
QString clockText(double seconds){int m=int(seconds)/60;return QString("%1:%2").arg(m,2,10,QChar('0')).arg(seconds-m*60,6,'f',3,QChar('0'));}
void rememberRecentDocument(const QString &path){const QFileInfo file(path);if(!file.exists())return;QSettings settings;QStringList documents=settings.value("recentDocuments").toStringList();documents.removeAll(file.absoluteFilePath());documents.prepend(file.absoluteFilePath());while(documents.size()>20)documents.removeLast();settings.setValue("recentDocuments",documents);}
QString recoveryFilePath(){const QString folder=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);QDir().mkpath(folder);return QDir(folder).filePath("Aspectra-X-recovery.aspectra");}
QString appStyle(){return R"(
QWidget {color:#eef2fa;font-family:'Segoe UI Variable','Segoe UI';font-size:12px;}
QWidget#shell {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #171b26,stop:.48 #10131b,stop:1 #0a0c12);border:1px solid #303a50;border-radius:18px;}
QWidget#galleryTopBar {background:#151b26;border:1px solid #2c3a52;border-radius:13px;padding:3px;}
QLabel#galleryTitle {color:#f4f7ff;font-size:13px;font-weight:650;padding:4px 7px;}
QWidget#filmstrip {background:#111722;border:1px solid #293950;border-radius:10px;padding:3px;}
QWidget#toolDock {background:#161d2a;border:1px solid #34445e;border-radius:14px;}
QPushButton[dock="true"] {background:transparent;border:1px solid transparent;border-radius:9px;padding:7px 2px;min-height:20px;color:#aebdd2;font-size:9px;}
QPushButton[dock="true"]:hover {background:#27354a;border-color:#45678e;color:#f2f7ff;}
QPushButton[dock="true"]:checked {background:#245a98;border-color:#74b9ff;color:white;}
QTabWidget#inspectorSheet::pane {border:1px solid #41516c;background:#121925;border-radius:14px;}
QDialog {background:#11151e;}
QLabel {background:transparent;border:none;}
QLabel#muted {color:#8c99ad;font-size:10px;}
QLabel#section {color:#b9d1ff;font-size:10px;font-weight:700;letter-spacing:1.35px;}
QPushButton {background:#202838;border:1px solid #43516a;border-radius:8px;padding:7px 11px;min-height:19px;font-weight:600;}
QPushButton:hover {border:1px solid #7eb3ff;background:#2a3b56;}
QPushButton:pressed,QPushButton:checked {background:#214e83;border-color:#5da5ff;}
QPushButton:disabled {color:#697081;background:#181d27;border-color:#303948;}
QPushButton[tone="blue"] {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #4fa4ff,stop:1 #2765c6);border:1px solid #9bccff;color:white;}
QPushButton[tone="pink"] {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #dc4a91,stop:1 #8e235d);border:1px solid #fc9ccc;color:white;}
QPushButton[tone="gold"] {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffd57a,stop:1 #bf8732);border:1px solid #ffe1a7;color:#21170b;}
QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox {background:#0b0e15;border:1px solid #37445b;border-radius:7px;padding:5px 7px;min-height:19px;selection-background-color:#3d91fb;}
QComboBox::drop-down {border:none;width:16px;}
QComboBox QAbstractItemView {background:#222836;selection-background-color:#345f94;}
QSpinBox:focus,QDoubleSpinBox:focus,QComboBox:focus {border-color:#3d91fb;}
QCheckBox {spacing:6px;}
QCheckBox::indicator,QListView::indicator,QTreeView::indicator,QTableView::indicator {width:16px;height:16px;border:1px solid #69748a;border-radius:4px;background:#141824;}
QCheckBox::indicator:checked,QListView::indicator:checked,QTreeView::indicator:checked,QTableView::indicator:checked {background:#3d91fb;border:1px solid #a1cbff;image:url(:/brand/check.svg);}
QCheckBox::indicator:disabled {background:#292c35;border-color:#454a57;}
QTabWidget::pane {border:1px solid #2c3850;background:#000000;border-radius:12px;top:-1px;}
QTabBar::tab {background:transparent;color:#8492a9;padding:10px 11px;margin:0 1px;border-bottom:2px solid transparent;font-weight:600;}
QTabBar::tab:hover {color:#dceaff;background:#1b2535;}
QTabBar::tab:selected {color:#f5f9ff;border-bottom:2px solid #68adff;background:#202f46;}
PreviewWidget#creativeStage {background:#080b10;border:1px solid #34425a;border-radius:14px;min-height:420px;}
QScrollArea,QWidget#content {border:none;background:transparent;}
QScrollArea>QWidget>QWidget {background:#000000;}
QScrollBar:vertical {width:7px;background:#0c1018;margin:6px 2px 6px 0;border-radius:3px;}
QScrollBar::handle:vertical {background:#4a5d7c;min-height:30px;border-radius:3px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}
QListWidget,QTreeWidget,QTableWidget,QPlainTextEdit {background:#11151d;border:1px solid #3e475b;border-radius:5px;gridline-color:#333d50;outline:none;}
QListWidget::item {padding:4px;margin:2px;border-radius:4px;}
QListWidget::item:selected,QTreeWidget::item:selected,QTableWidget::item:selected {background:#29466c;}
QHeaderView::section {background:#242e40;color:#b9c9e3;border:none;padding:4px;}
QProgressBar {background:#131824;border:1px solid #414e66;border-radius:5px;text-align:center;min-height:15px;}
QProgressBar::chunk {background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #57dd7b,stop:1 #3d91fb);border-radius:4px;}
QSlider::groove:horizontal {height:5px;border-radius:3px;background:#28354a;}
QSlider::sub-page:horizontal {border-radius:3px;background:#4fa4ff;}
QSlider::handle:horizontal {width:16px;height:16px;margin:-6px 0;border-radius:8px;background:#d8ebff;border:2px solid #4fa4ff;}
QMenu {background:#151c29;border:1px solid #3b4d6b;border-radius:10px;padding:7px;color:#edf4ff;}
QMenu::item {padding:8px 28px 8px 14px;border-radius:6px;}
QMenu::item:selected {background:#264a78;}
QToolTip {background:#222a39;color:#edf2fa;border:1px solid #3d91fb;padding:6px;}
/* Aspectra X portrait editor surface: flat, quiet, and Photos-like. */
QWidget#shell {background:#111214;border:none;border-radius:0;}
QWidget#TopBar {background:#111214;border-bottom:1px solid #2b2e33;}
QLabel#TitleLabel {color:#f5f7fa;font-size:14px;font-weight:600;}
QToolButton#BackButton,QToolButton#MoreMenuButton {background:transparent;border:none;border-radius:18px;color:#f2f6ff;font-size:18px;}
QToolButton#BackButton:hover,QToolButton#MoreMenuButton:hover {background:#272a30;}
QPushButton#TopExportButton {background:#9e7bff;border:none;border-radius:18px;color:#111015;font-weight:700;padding:6px 15px;}
QPushButton#TopExportButton:hover {background:#b19aff;}
QWidget#MainPreview {background:#111214;}
PreviewWidget#creativeStage {background:#08090b;border:none;border-radius:18px;min-height:360px;}
QScrollArea#DockScroll {background:transparent;}
QToolButton#DockButton {background:transparent;border:none;border-radius:12px;color:#c4c8d0;font-size:9px;padding:2px;}
QToolButton#DockButton:hover {background:#2b2e35;color:#ffffff;}
QToolButton#DockButton:checked {background:#302748;color:#d9caff;}
QFrame#InspectorPanel {background:#1b1d22;border:none;border-top:1px solid #383b43;border-top-left-radius:24px;border-top-right-radius:24px;}
QTabWidget#InspectorPages::pane {background:#1b1d22;border:none;border-top-left-radius:24px;border-top-right-radius:24px;}
QFrame#FilmstripPanel {background:#191b20;border-top:1px solid #30333a;}
QComboBox#FilmstripSelector {background:#22252b;border:none;border-radius:12px;padding:9px 12px;min-height:28px;}
QPushButton#FilmstripArrow {background:#272a30;border:none;border-radius:14px;font-size:21px;}
QLabel#FilmstripCount {color:#b7bbc4;}
QPushButton[tone="blue"],QPushButton[tone="pink"],QPushButton[tone="gold"] {background:#2a2d34;border:1px solid transparent;color:#f5f7fa;}
QPushButton[tone="blue"]:hover,QPushButton[tone="pink"]:hover,QPushButton[tone="gold"]:hover {background:#343842;border-color:#9e7bff;}
QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox {background:#25282e;border:1px solid transparent;border-radius:10px;padding:8px 10px;min-height:24px;}
QSlider::groove:horizontal {height:8px;border-radius:4px;background:#373a42;}
QSlider::sub-page:horizontal {background:#9e7bff;border-radius:4px;}
QSlider::handle:horizontal {width:22px;height:22px;margin:-7px 0;border-radius:11px;background:#f4f0ff;border:2px solid #b59cff;}
/* Portrait workstation specification: the content area stays black, with a spectral accent only on controls. */
QWidget#shell,QWidget#StageHost {background:#000000;border:none;border-radius:0;}
QWidget#TopBar {background:#000000;border-bottom:1px solid #25252c;}
QLabel#TitleLabel,QLabel#muted,QLabel#section {color:#ffffff;}
QWidget#MainPreview {background:#000000;}
PreviewWidget#creativeStage {background:#000000;border:none;border-radius:0;min-height:0px;}
QSlider#SafeZoneSlider::groove:vertical {width:8px;background:#252530;border-radius:4px;}
QSlider#SafeZoneSlider::sub-page:vertical {background:qlineargradient(x1:0,y1:1,x2:0,y2:0,stop:0 #ff4d4d,stop:.5 #3ddcff,stop:1 #7a4dff);border-radius:4px;}
QSlider#SafeZoneSlider::handle:vertical {height:22px;margin:0 -7px;border-radius:11px;background:#ffffff;border:2px solid #7a4dff;}
QScrollArea#TabsCarousel {background:#000000;border:none;}
QToolButton#CarouselTab {background:#16161d;border:1px solid transparent;border-radius:14px;color:#ffffff;padding:7px 12px;font-weight:600;}
QToolButton#CarouselTab:hover {background:#262431;}
QToolButton#CarouselTab:checked {border-color:#3ddcff;background:#261f3a;color:#ffffff;}
QTabWidget#SettingsScrollArea::pane {background:#000000;border-top:1px solid #25252c;border-radius:0;}
QListWidget#BatchStrip {background:#09090c;border-top:1px solid #25252c;border-radius:0;color:#ffffff;padding:5px;}
QListWidget#BatchStrip::item {border:1px solid transparent;border-radius:10px;padding:2px;color:#ffffff;}
QListWidget#BatchStrip::item:selected {border-color:#3ddcff;background:#241f38;}
/* Final workstation palette: black surfaces only.  Spectral color is reserved for strokes and sliders. */
QWidget,QScrollArea,QWidget#content,QTabWidget,QGroupBox {background:#000000;color:#ffffff;}
QWidget#shell,QWidget#TopBar,QWidget#StageHost,QWidget#MainPreview,QScrollArea#TabsCarousel,QTabWidget#SettingsScrollArea::pane,QListWidget#BatchStrip {background:#000000;}
QPushButton,QToolButton,QComboBox,QLineEdit,QSpinBox,QDoubleSpinBox,QListWidget,QTableWidget,QPlainTextEdit {background:#000000;color:#ffffff;border:1px solid #2d2d35;}
QPushButton:hover,QToolButton:hover {background:#111114;border-color:#ffffff;}
QPushButton:pressed,QPushButton:checked,QToolButton:pressed,QToolButton:checked {background:#101015;border-color:#7a4dff;color:#ffffff;}
QPushButton[tone="blue"],QPushButton[tone="pink"],QPushButton[tone="gold"],QPushButton#TopExportButton {background:#000000;color:#ffffff;border:1px solid #7a4dff;}
QPushButton[tone="blue"]:hover,QPushButton[tone="pink"]:hover,QPushButton[tone="gold"]:hover,QPushButton#TopExportButton:hover {background:#111114;border-color:#3ddcff;}
QToolButton#CarouselTab {background:#000000;border:1px solid #ffffff;border-radius:24px;padding:4px;}
QToolButton#CarouselTab:hover {background:#111114;border:2px solid #ffffff;}
QToolButton#CarouselTab:checked {background:#000000;border:2px solid #7a4dff;}
QListWidget#BatchStrip::item,QListWidget#BatchStrip::item:selected {background:#000000;color:#ffffff;}
QListWidget#BatchStrip::item:selected {border:1px solid #3ddcff;}
QLabel#SliderValue {min-width:26px;min-height:26px;max-width:42px;max-height:42px;background:#ffffff;color:#000000;border-radius:13px;font-weight:700;font-variant-numeric:tabular-nums;padding:1px 3px;}
QWidget#SettingsHost,QScrollArea#TabsCarousel,QScrollArea#SubSettingsRail,QWidget#ActiveSettingCard,QTabWidget#SettingsScrollArea::pane {background:#000000;border:none;}
QToolButton#SubRailIcon {background:#000000;color:#ffffff;border:1px solid #ffffff;border-radius:19px;font-size:10px;font-weight:700;padding:0;}
QToolButton#SubRailIcon:hover {background:#16161b;border:2px solid #ffffff;}
QToolButton#SubRailIcon[selected="true"] {background:#ffffff;color:#000000;border:2px solid #ffffff;}
QTabBar::tab {background:#000000;color:#ffffff;border:none;border-bottom:2px solid transparent;}
QTabBar::tab:hover {background:#111114;color:#ffffff;}
QTabBar::tab:selected {background:#000000;color:#ffffff;border-bottom:2px solid #3ddcff;}
QComboBox QAbstractItemView,QMenu {background:#000000;color:#ffffff;border:1px solid #33333b;}
QMenu::item:selected {background:#141419;color:#ffffff;}
QSlider::groove:horizontal {background:#171a1d;}
QSlider::sub-page:horizontal {background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #57dd7b,stop:.55 #3ddcff,stop:1 #3d91fb);}
QSlider::handle:horizontal {background:#ffffff;border:2px solid #57dd7b;}
QCheckBox::indicator,QListView::indicator,QTreeView::indicator,QTableView::indicator {background:#000000;border:1px solid #ffffff;}
QCheckBox::indicator:checked,QListView::indicator:checked,QTreeView::indicator:checked,QTableView::indicator:checked {background:#000000;border:2px solid #7a4dff;image:none;}
QListWidget::item:selected,QTreeWidget::item:selected,QTableWidget::item:selected {background:#000000;color:#ffffff;border:1px solid #3ddcff;}
QProgressBar,QProgressBar::chunk {background:#000000;border:1px solid #ffffff;color:#ffffff;}
QProgressBar::chunk {border-color:#7a4dff;}
/* Header is a flush control strip: no boxes, dividers, or outlined actions. */
QWidget#TopBar {background:#000000;border:none;}
QToolButton#BackButton,QToolButton#UndoButton,QToolButton#RedoButton,QToolButton#InfoButton,QToolButton#MoreMenuButton,QPushButton#TopExportButton {background:transparent;border:none;border-radius:18px;}
QToolButton#BackButton:hover,QToolButton#UndoButton:hover,QToolButton#RedoButton:hover,QToolButton#InfoButton:hover,QToolButton#MoreMenuButton:hover,QPushButton#TopExportButton:hover {background:#111114;border:none;}
/* Icon rails use quiet circular containers.  A colored stroke belongs only to
   the active tool, category, or setting—not to every dormant icon. */
QToolButton#CarouselTab,QToolButton#SubRailIcon,QToolButton#MainToolButton,QToolButton#NativeToolButton {background:#09090c;border:1px solid transparent;border-radius:20px;}
QToolButton#CarouselTab:hover,QToolButton#SubRailIcon:hover,QToolButton#MainToolButton:hover,QToolButton#NativeToolButton:hover {background:#15151a;border:1px solid transparent;}
QToolButton#CarouselTab:checked,QToolButton#SubRailIcon:checked,QToolButton#MainToolButton:checked,QToolButton#NativeToolButton:checked {background:#14111c;border:2px solid #7a4dff;}
/* Focus canvas: zooming the image removes panel surfaces while leaving the
   icon controls available.  It keeps the portrait editor visually immersive
   without turning the controls into modal overlays. */
QWidget[immersivePreview="true"],QScrollArea[immersivePreview="true"],QWidget#BottomRailsHost {background:transparent;border:none;}
)";}
}
static void psdBytes(QDataStream &stream,const QByteArray &bytes){stream.writeRawData(bytes.constData(),bytes.size());}
static bool writeLayeredPsd(const QString &path,const QStringList &sources,QSize canvas){
    if(sources.isEmpty())return false;QVector<QImage> layers;QStringList names;for(const auto &source:sources){try{QImage image=ImageProcessor::read(source).convertToFormat(QImage::Format_RGBA8888);QImage layer(canvas,QImage::Format_RGBA8888);layer.fill(Qt::transparent);QPainter painter(&layer);painter.drawImage(QRect(QPoint(),image.size().boundedTo(canvas)),image);layers<<layer;names<<QFileInfo(source).completeBaseName();}catch(...){}}
    if(layers.isEmpty())return false;QFile file(path);if(!file.open(QIODevice::WriteOnly))return false;QDataStream out(&file);out.setByteOrder(QDataStream::BigEndian);psdBytes(out,"8BPS");out<<quint16(1);psdBytes(out,QByteArray(6,'\0'));out<<quint16(4)<<quint32(canvas.height())<<quint32(canvas.width())<<quint16(8)<<quint16(3)<<quint32(0)<<quint32(0);
    QByteArray records;QBuffer recordBuffer(&records);recordBuffer.open(QIODevice::WriteOnly);QDataStream record(&recordBuffer);record.setByteOrder(QDataStream::BigEndian);record<<qint16(layers.size());const quint32 plane=quint32(canvas.width()*canvas.height()+2);for(int i=0;i<layers.size();++i){record<<qint32(0)<<qint32(0)<<qint32(canvas.height())<<qint32(canvas.width())<<quint16(4);for(int channel=0;channel<4;++channel)record<<qint16(channel==3?-1:channel)<<plane;psdBytes(record,"8BIMnorm");record<<quint8(255)<<quint8(0)<<quint8(0)<<quint8(0);QByteArray extra;QBuffer extraBuffer(&extra);extraBuffer.open(QIODevice::WriteOnly);QDataStream e(&extraBuffer);e.setByteOrder(QDataStream::BigEndian);e<<quint32(0)<<quint32(0);QByteArray name=names[i].toLatin1().left(255);extra.append(char(name.size()));extra.append(name);while(extra.size()%4)extra.append('\0');record<<quint32(extra.size());psdBytes(record,extra);}for(const auto &image:layers)for(int channel=0;channel<4;++channel){record<<quint16(0);for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){QColor c=image.pixelColor(x,y);char value=channel==0?c.red():channel==1?c.green():channel==2?c.blue():c.alpha();record.writeRawData(&value,1);}}recordBuffer.close();QByteArray layerBlock;QBuffer layerBuffer(&layerBlock);layerBuffer.open(QIODevice::WriteOnly);QDataStream layerStream(&layerBuffer);layerStream.setByteOrder(QDataStream::BigEndian);layerStream<<quint32(records.size());psdBytes(layerStream,records);layerStream<<quint32(0);layerBuffer.close();out<<quint32(layerBlock.size());psdBytes(out,layerBlock);out<<quint16(0);QImage composite=layers.last();for(int channel=0;channel<4;++channel)for(int y=0;y<composite.height();++y)for(int x=0;x<composite.width();++x){QColor c=composite.pixelColor(x,y);char value=channel==0?c.red():channel==1?c.green():channel==2?c.blue():c.alpha();out.writeRawData(&value,1);}return out.status()==QDataStream::Ok;
}
MainWindow::MainWindow(QWidget *parent):AspectraWindow(parent){
    setWindowTitle("Aspectra X");setWindowIcon(QIcon(":/brand/logo.png"));setWindowFlags(Qt::Window|Qt::WindowTitleHint|Qt::WindowSystemMenuHint|Qt::WindowMinimizeButtonHint|Qt::WindowCloseButtonHint);QScreen *screen=QGuiApplication::screenAt(QCursor::pos());if(!screen)screen=QGuiApplication::primaryScreen();const QRect available=screen->availableGeometry();const int initialHeight=qMax(1,available.height()-40);const int initialWidth=qMax(1,qRound(initialHeight*9.0/16.0));setFixedSize(initialWidth,initialHeight);move(available.center()-QPoint(initialWidth/2,initialHeight/2));QTimer::singleShot(0,this,[this]{QScreen *target=QGuiApplication::screenAt(QCursor::pos());if(!target)target=QGuiApplication::primaryScreen();const QRect work=target->availableGeometry();const QSize chrome=frameGeometry().size()-size();const int outerHeight=work.height(),outerWidth=qRound(outerHeight*9.0/16.0);setFixedSize(qMax(1,outerWidth-chrome.width()),qMax(1,outerHeight-chrome.height()));move(work.center()-QPoint(outerWidth/2,outerHeight/2));});setAcceptDrops(true);
    buildUi();qApp->installEventFilter(this);modelRail=new FluidToolButton(tabCarousel->widget());modelRail->setObjectName("CarouselTab");modelRail->setIcon(whiteIcon("C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/smart object.png"));modelRail->setIconSize(QSize(28,28));modelRail->setToolButtonStyle(Qt::ToolButtonIconOnly);modelRail->setToolTip("Model settings");modelRail->setCheckable(true);modelRail->setFixedSize(48,48);modelRail->installEventFilter(this);if(auto *railLayout=qobject_cast<QHBoxLayout*>(tabCarousel->widget()->layout()))railLayout->insertWidget(qMax(0,railLayout->count()-2),modelRail);connect(modelRail,&QToolButton::clicked,this,[this]{for(auto *button:tabCarousel->findChildren<QToolButton*>("CarouselTab"))if(button!=modelRail)button->setChecked(false);modelRail->setChecked(true);tabs->setCurrentIndex(9);tabs->show();if(auto *host=tabs->parentWidget()){host->setMinimumHeight(112);host->setMaximumHeight(QWIDGETSIZE_MAX);}});setStyleSheet(appStyle());move(available.center()-rect().center());
    for(const QString &railName:{QString("AddIconRail"),QString("TabsCarousel"),QString("SubSettingsRail"),QString("MainToolRail"),QString("BottomToolbarRail"),QString("BottomModeRail"),QString("BottomPanelRail")}){
        if(auto *rail=findChild<QWidget*>(railName)){
            fluidizeRailButtons(rail,true);
            const bool needsExpandedViewport=railName=="AddIconRail"||railName=="TabsCarousel"||railName=="SubSettingsRail"||railName=="MainToolRail"||railName=="BottomToolbarRail"||railName=="BottomModeRail"||railName=="BottomPanelRail";
            if(needsExpandedViewport)provideFluidRailRoom(rail);
        }
    }
    player=new QMediaPlayer(this);audio=new QAudioOutput(this);sink=new QVideoSink(this);player->setAudioOutput(audio);player->setVideoSink(sink);audio->setVolume(.7);compositionTimer.setInterval(16);connect(&compositionTimer,&QTimer::timeout,this,[this]{if(!compositionMode||VideoProcessor::isVideo(currentFile))return;compositionPosition+=.016;if(compositionPosition>=media.duration)compositionPosition=0;timeline->setPosition(compositionPosition);timeLabel->setText(clockText(compositionPosition)+" / "+clockText(media.duration));refresh();});buildPowerFeatures();auto *colorTabs=qobject_cast<QTabWidget*>(tabs->widget(1));if(colorTabs&&colorTabs->count()>4){auto *layout=qobject_cast<QVBoxLayout*>(colorTabs->widget(4)->layout());auto extra=[&](QString name,QSlider **slider,int minimum,int maximum,int value,QString tip){auto *line=new QHBoxLayout;line->addWidget(label(name));*slider=new QSlider(Qt::Horizontal);(*slider)->setRange(minimum,maximum);(*slider)->setValue(value);(*slider)->setToolTip(tip);line->addWidget(*slider,1);layout->insertLayout(qMax(0,layout->count()-1),line);connect(*slider,&QSlider::valueChanged,this,&MainWindow::refresh);};extra("Matte choke / expand",&keyMatteBiasSlider,-50,50,0,"Negative expands the keyed-out area; positive protects more edge detail.");extra("Matte clean black",&keyCleanBlackSlider,0,100,0,"Forces very transparent matte pixels fully transparent.");extra("Matte clean white",&keyCleanWhiteSlider,0,100,0,"Forces very opaque matte pixels fully opaque.");}
    auto *pasteImageShortcut=new QShortcut(QKeySequence::Paste,this);connect(pasteImageShortcut,&QShortcut::activated,this,[this]{const int index=trackList?trackList->currentRow():-1;if(index<0||index>=timelineTracks.size()||timelineTracks[index].type!=TimelineTrack::Image)return;const QImage image=QGuiApplication::clipboard()->image();if(image.isNull()){status->setText("Clipboard does not contain an image");return;}auto &track=timelineTracks[index];track.image=image.copy();track.source={};track.name="Clipboard image";track.start=qMax(0.,player?player->position()/1000.:0.);track.end=qMax(track.start+.01,media.duration);status->setText("Clipboard image placed on selected image track");updateTrackPanel();refresh();});
    auto *frameArea=qobject_cast<QScrollArea*>(tabs->widget(0));if(frameArea){auto *frameLayout=qobject_cast<QVBoxLayout*>(frameArea->widget()->layout());perImageOverride=new QCheckBox("Per-image adjustment override");perImageOverride->setToolTip("Stores the current color, keying, crop, and style controls only on this batch item and restores them when you return.");frameLayout->insertWidget(0,perImageOverride);connect(perImageOverride,&QCheckBox::toggled,this,[this](bool on){if(applyingLayerOverride||currentFile.isEmpty())return;auto &layer=canvasLayers[currentFile];layer.hasOverride=on;if(on)layer.overrideAdjustments=adjustments();status->setText(on?"Per-image override enabled":"Using shared batch adjustments");});}
    connect(preview,&PreviewWidget::maskBrushed,this,[this](QPointF point,bool add,int size){if(currentFile.isEmpty()||original.isNull())return;auto &layer=canvasLayers[currentFile];if(layer.mask.size()!=original.size()){layer.mask=QImage(original.size(),QImage::Format_Grayscale8);layer.mask.fill(255);}QPainter painter(&layer.mask);painter.setRenderHint(QPainter::Antialiasing);painter.setBrush(add?Qt::white:Qt::black);painter.setPen(Qt::NoPen);double radius=qMax(1.,double(size)*original.width()/qMax(1,preview->width())/2.);painter.drawEllipse(QPointF(point.x()*original.width(),point.y()*original.height()),radius,radius);preview->setLayerMask(layer.mask);status->setText(add?"Mask paint: add":"Mask paint: remove");});
    connect(sink,&QVideoSink::videoFrameChanged,this,[this](const QVideoFrame &frame){auto image=frame.toImage();if(!image.isNull()){original=image;texturePreviewSource={};refresh();}});
    connect(player,&QMediaPlayer::positionChanged,this,[this](qint64 ms){timeline->setPosition(ms/1000.);timeLabel->setText(clockText(ms/1000.)+" / "+clockText(media.duration));});
    connect(player,&QMediaPlayer::durationChanged,this,[this](qint64 ms){if(ms<=0)return;media.duration=ms/1000.;timeline->setDuration(media.duration);inMarker->setMaximum(media.duration);outMarker->setMaximum(media.duration);if(!draftRanges.contains(currentFile)){QSignalBlocker a(inMarker),b(outMarker);inMarker->setValue(0);outMarker->setValue(media.duration);syncRange(false);}});
    connect(player,&QMediaPlayer::playbackStateChanged,this,[this](QMediaPlayer::PlaybackState s){playButton->setText(s==QMediaPlayer::PlayingState?"Ⅱ Pause":"▶ Play");});
    connect(player,&QMediaPlayer::errorOccurred,this,[this](QMediaPlayer::Error,const QString &message){status->setText("Player: "+message.left(55));logs<<message;});connect(timeline,&TimelineWidget::trackVisibilityChanged,this,[this](int index,bool visible){if(index>=0&&index<timelineTracks.size()){timelineTracks[index].enabled=visible;updateTrackPanel();refresh();}});connect(timeline,&TimelineWidget::trackLockChanged,this,[this](int index,bool locked){if(index>=0&&index<timelineTracks.size()){timelineTracks[index].locked=locked;updateTrackPanel();}});connect(timeline,&TimelineWidget::trackSelected,this,[this](int index){if(trackList&&index>=0&&index<timelineTracks.size())trackList->setCurrentRow(index);});connect(timeline,&TimelineWidget::trackEdited,this,[this](int index,double in,double out){if(index>=0&&index<timelineTracks.size()){timelineTracks[index].start=in;timelineTracks[index].end=out;updateTrackPanel();refresh();}});
    connect(&loadWatcher,&QFutureWatcher<LoadedMedia>::finished,this,[this]{loading=false;auto loaded=loadWatcher.result();if(!loaded.error.isEmpty()){showError(loaded.error);return;}original=loaded.image;texturePreviewSource={};media=loaded.info;if(!original.isNull()&&chromaKeyEnabled){QImage probe=original.scaled(120,120,Qt::IgnoreAspectRatio,Qt::FastTransformation);qint64 rr=0,gg=0,bb=0,n=0;for(int y=0;y<probe.height();++y)for(int x=0;x<probe.width();++x)if(x<10||y<10||x>=110||y>=110){QColor c=probe.pixelColor(x,y);rr+=c.red();gg+=c.green();bb+=c.blue();++n;}if(n&&gg/n>rr/n*1.25&&gg/n>bb/n*1.25){layerStyle.keyColor=QColor(rr/n,gg/n,bb/n);chromaKeyEnabled->setChecked(true);keyToleranceSlider->setValue(20);keySoftnessSlider->setValue(10);status->setText("Green-screen background detected · auto key enabled");}}
        // A newly imported source owns its initial document canvas.  Do not
        // retain the previous 1:1 canvas and merely change its aspect ratio.
        if(followSource&&followSource->isChecked()){
            QSize native=media.size.isValid()?media.size:original.size();
            if(layerStyle.rotation%180)native.transpose();
            lockedRatio=double(native.width())/qMax(1,native.height());
            setDimensions(native.width(),native.height());
        }else if(ratio->currentIndex()==0){lockedRatio=double(original.width())/original.height();setDimensions(widthInput->value(),qRound(widthInput->value()/lockedRatio));}
        traceRefreshTimer.stop();tracePreviewActive=false;pendingTraceSvg.clear();pendingTraceColors=0;if(saveTraceButton)saveTraceButton->setEnabled(false);preview->clearVectorTraceFrame();preview->clearTextureFrame();preview->setFrame(original);refresh();if(inputTitles.contains(currentFile)){const QString source=inputAliases.value(currentFile);int artboard=0,total=0;for(const auto &candidate:batch.files)if(inputTitles.contains(candidate)&&inputAliases.value(candidate)==source){++total;if(candidate==currentFile)artboard=total;}status->setText(QString("Artboard %1 of %2 · %3").arg(artboard).arg(total).arg(inputTitles.value(currentFile)));}else status->setText("Ready · all adjustments are live");thumbnail->setPixmap(QPixmap::fromImage(original).scaled(20,20,Qt::KeepAspectRatio,Qt::SmoothTransformation));info->setText(QString("%1 · %2×%3").arg(QFileInfo(inputAliases.value(currentFile,currentFile)).suffix().toUpper()).arg(media.size.width()).arg(media.size.height()));
        if(VideoProcessor::isVideo(currentFile)){timeline->setDuration(media.duration);QSignalBlocker a(inMarker),b(outMarker);inMarker->setMaximum(media.duration);outMarker->setMaximum(media.duration);auto range=draftRanges.value(currentFile,{0,media.duration});inMarker->setValue(range.first);outMarker->setValue(range.second);syncRange(false);player->setSource(QUrl::fromLocalFile(currentFile));player->setPosition(qRound64(range.first*1000));}
        if(!canvasLayers.contains(currentFile)){CanvasLayer layer;layer.source=currentFile;layer.name=QFileInfo(inputAliases.value(currentFile,currentFile)).completeBaseName();layer.nativeSize=media.size;canvasLayers[currentFile]=layer;}else canvasLayers[currentFile].nativeSize=media.size;preview->setLayerMask(canvasLayers[currentFile].mask);if(canvasX&&canvasY){QSignalBlocker x(canvasX),y(canvasY);canvasX->setValue(qRound(canvasLayers[currentFile].position.x()));canvasY->setValue(qRound(canvasLayers[currentFile].position.y()));}if(VideoProcessor::isVideo(currentFile)&&std::none_of(timelineTracks.cbegin(),timelineTracks.cend(),[this](const TimelineTrack &track){return track.type==TimelineTrack::Video&&track.source==currentFile;})){TimelineTrack video;video.type=TimelineTrack::Video;video.source=currentFile;video.name=QFileInfo(inputAliases.value(currentFile,currentFile)).fileName();video.image=original;video.start=0;video.end=qMax(1.,media.duration);TimelineTrack audioTrack;audioTrack.type=TimelineTrack::Audio;audioTrack.source=currentFile;audioTrack.name="Audio";audioTrack.start=0;audioTrack.end=video.end;TimelineTrack effect;effect.type=TimelineTrack::Effect;effect.name="Effects";effect.start=0;effect.end=video.end;timelineTracks<<video<<audioTrack<<effect;}mediaContext();updateTrackPanel();if(timeline)timeline->setTracks(timelineTracks);updateClipTable();
    });
    connect(&exportWatcher,&QFutureWatcher<ExportResult>::finished,this,[this]{auto result=exportWatcher.result();lastFolder=result.folder;logs+=result.log;exportButton->setEnabled(true);exportCancel->hide();exportDone->setEnabled(true);exportOpen->setEnabled(!lastFolder.isEmpty());exportLog->setPlainText(result.log.join('\n'));
        QString message=result.error.isEmpty()?QString("Complete · %1 outputs saved").arg(result.written):result.cancelled?"Cancelled · completed files kept":"Export failed: "+result.error;exportStatus->setText(message);status->setText(message.left(70));if(closing)close();});
    textureRefreshTimer.setSingleShot(true);textureRefreshTimer.setInterval(4);connect(&textureRefreshTimer,&QTimer::timeout,this,&MainWindow::refreshTexturePreview);connect(preview,&PreviewWidget::sphereOrbited,this,[this](QPoint delta,bool pan){if(texturePreviewSphere){texturePreviewSphere->orbitBy(delta,pan);textureRefreshTimer.start();}});connect(preview,&PreviewWidget::sphereZoomed,this,[this](int delta){if(texturePreviewSphere){texturePreviewSphere->zoomBy(delta);textureRefreshTimer.start();}});
}
MainWindow::~MainWindow(){if(cancelFlag)*cancelFlag=true;exportWatcher.waitForFinished();loadWatcher.waitForFinished();if(player)player->stop();if(recorder){recorder->write("q\n");recorder->waitForFinished(5000);if(recorder->state()!=QProcess::NotRunning)recorder->kill();}}
void MainWindow::buildUi(){
    auto *root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);auto *shell=new QWidget;shell->setObjectName("shell");root->addWidget(shell);auto *layout=new QVBoxLayout(shell);layout->setContentsMargins(16,12,16,0);layout->setSpacing(12);
    auto *topBar=new TopBar(shell);layout->addWidget(topBar,0);filename=topBar->titleLabel;exportButton=topBar->exportButton;exportButton->setEnabled(false);connect(topBar->backButton,&QToolButton::clicked,this,[this]{clearMedia();showWelcomeScreen();});connect(topBar->infoButton,&QToolButton::clicked,this,[this]{QDialog dialog(this);dialog.setWindowTitle("Aspectra X · Media info");auto *v=new QVBoxLayout(&dialog);v->addWidget(label(currentFile.isEmpty()?"No media selected":QFileInfo(inputAliases.value(currentFile,currentFile)).fileName()));v->addWidget(label(info?info->text():"No media","muted"));v->addWidget(label(batchLabel?batchLabel->text():"No batch","muted"));auto *done=button("Done",v);connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);dialog.exec();});auto *menuButton=topBar->moreButton;auto *commandMenu=new QMenu(menuButton);menuButton->setMenu(commandMenu);menuButton->setPopupMode(QToolButton::InstantPopup);connect(topBar->exportButton,&QPushButton::clicked,this,&MainWindow::exportFiles);
    undoButton=topBar->undoButton;redoButton=topBar->redoButton;connect(undoButton,&QToolButton::clicked,this,&MainWindow::undoRaster);connect(redoButton,&QToolButton::clicked,this,&MainWindow::redoRaster);auto *undoShortcut=new QShortcut(QKeySequence("Ctrl+Z"),this);undoShortcut->setContext(Qt::WindowShortcut);connect(undoShortcut,&QShortcut::activated,this,&MainWindow::undoRaster);auto *stepBackShortcut=new QShortcut(QKeySequence("Ctrl+Alt+Z"),this);stepBackShortcut->setContext(Qt::WindowShortcut);connect(stepBackShortcut,&QShortcut::activated,this,&MainWindow::undoRaster);auto *redoShortcut=new QShortcut(QKeySequence("Ctrl+Shift+Z"),this);redoShortcut->setContext(Qt::WindowShortcut);connect(redoShortcut,&QShortcut::activated,this,&MainWindow::redoRaster);updateUndoControls();
    auto *commandHost=new QWidget(shell);commandHost->hide();auto *toolbar=new QVBoxLayout(commandHost);toolbar->setContentsMargins(0,0,0,0);auto *import=button("+ Import",toolbar,"blue");connect(import,&QPushButton::clicked,this,&MainWindow::chooseFiles);auto *reset=button("Reset",toolbar,"pink");reset->setToolTip("Clear the entire current batch and preview. Source files are never deleted.");connect(reset,&QPushButton::clicked,this,&MainWindow::clearMedia);modes=new QButtonGroup(this);int id=0;for(const auto &name:QStringList{"Image","Video","Capture"}){auto *b=button(name,toolbar);b->setCheckable(true);modes->addButton(b,id++);}modes->button(0)->setChecked(true);auto *batch=button("Batch…",toolbar);connect(batch,&QPushButton::clicked,this,&MainWindow::manageBatch);
    auto *saveProject=button("Save Project…",toolbar,"gold");saveProject->setToolTip("Save linked sources, canvas placement, editable masks, and adjustment defaults to an Aspectra project.");connect(saveProject,&QPushButton::clicked,this,[this]{QString path=QFileDialog::getSaveFileName(this,"Save Aspectra project","aspectra-project.aspectra","Aspectra project (*.aspectra)");if(path.isEmpty())return;QJsonObject root;root["version"]=2;root["canvas"]=QJsonObject{{"width",widthInput->value()},{"height",heightInput->value()},{"ratio",ratio->currentText()}};QJsonArray layers;for(const auto &sourcePath:this->batch.files){const auto layer=canvasLayers.value(sourcePath);QJsonObject item{{"source",sourcePath},{"name",layer.name},{"x",layer.position.x()},{"y",layer.position.y()},{"width",layer.nativeSize.width()},{"height",layer.nativeSize.height()},{"visible",layer.visible}};if(!layer.mask.isNull()){QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);layer.mask.save(&buffer,"PNG");item["maskPngBase64"]=QString::fromLatin1(bytes.toBase64());}layers.append(item);}root["layers"]=layers;auto a=adjustments();root["adjustments"]=QJsonObject{{"zoom",a.zoom},{"hue",a.hue},{"saturation",a.saturation},{"brightness",a.brightness},{"contrast",a.contrast},{"keyEnabled",a.chromaKey},{"keyColor",a.keyColor.name(QColor::HexArgb)},{"keyTolerance",a.keyTolerance},{"keySoftness",a.keySoftness},{"keyDespill",a.keyDespill},{"keyLumaProtect",a.keyLumaProtect},{"keyMatteBias",a.keyMatteBias},{"keyCleanBlack",a.keyCleanBlack},{"keyCleanWhite",a.keyCleanWhite}};QFile out(path);if(!out.open(QIODevice::WriteOnly)){showError("Could not save project.");return;}out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));status->setText("Project saved · linked canvas layers and masks are editable on reopen");});
    auto *exportDesign=button("Export Design…",toolbar,"blue");exportDesign->setToolTip("Export the current rendered image as PSD, PSB, SVG, AI, PDF or EPS.");connect(exportDesign,&QPushButton::clicked,this,[this]{if(original.isNull()){showError("Import an image first.");return;}QString path=QFileDialog::getSaveFileName(this,"Export design file","Aspectra-design.psd","Photoshop (*.psd *.psb);;SVG / Illustrator (*.svg *.ai);;PDF (*.pdf);;EPS (*.eps)");if(path.isEmpty())return;QImage image=preview->rendered().convertToFormat(QImage::Format_RGBA8888);QString ext=QFileInfo(path).suffix().toLower();if(ext=="pdf"){QPdfWriter pdf(path);pdf.setPageSize(QPageSize(QSizeF(image.width(),image.height()),QPageSize::Point));QPainter p(&pdf);p.drawImage(QRect(0,0,image.width(),image.height()),image);p.end();}else if(ext=="svg"||ext=="ai"){QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");QFile out(path);if(!out.open(QIODevice::WriteOnly)){showError("Could not write design file.");return;}QTextStream stream(&out);stream<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""<<image.width()<<"\" height=\""<<image.height()<<"\" viewBox=\"0 0 "<<image.width()<<" "<<image.height()<<"\"><image width=\"100%\" height=\"100%\" href=\"data:image/png;base64,"<<png.toBase64()<<"\"/></svg>";}else if(ext=="eps"){QFile out(path);if(!out.open(QIODevice::WriteOnly)){showError("Could not write EPS.");return;}QByteArray rgb;rgb.reserve(image.width()*image.height()*3);for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){QColor c=image.pixelColor(x,y);rgb.append(char(c.red()));rgb.append(char(c.green()));rgb.append(char(c.blue()));}QTextStream stream(&out);stream<<"%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 "<<image.width()<<" "<<image.height()<<"\n/picstr "<<image.width()*3<<" string def\n"<<image.width()<<" "<<image.height()<<" 8 ["<<image.width()<<" 0 0 -"<<image.height()<<" 0 "<<image.height()<<"] {currentfile picstr readhexstring pop} false 3 colorimage\n"<<rgb.toHex()<<"\nshowpage\n%%EOF\n";}else if(ext=="psd"||ext=="psb"){QFile out(path);if(!out.open(QIODevice::WriteOnly)){showError("Could not write Photoshop file.");return;}QDataStream s(&out);s.setByteOrder(QDataStream::BigEndian);s.writeRawData("8BPS",4);s<<quint16(ext=="psb"?2:1);s.writeRawData("\0\0\0\0\0\0",6);s<<quint16(4)<<quint32(image.height())<<quint32(image.width())<<quint16(8)<<quint16(3)<<quint32(0)<<quint32(0);if(ext=="psb")s<<quint64(0);else s<<quint32(0);s<<quint16(0);for(int channel=0;channel<4;++channel)for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){QColor c=image.pixelColor(x,y);char value=channel==0?c.red():channel==1?c.green():channel==2?c.blue():c.alpha();s.writeRawData(&value,1);}}else{showError("Choose PSD, PSB, SVG, AI, PDF or EPS.");return;}status->setText("Design file exported · "+QFileInfo(path).fileName());});
    auto *exportArtboards=button("PSD Artboards → PNG…",toolbar,"blue");exportArtboards->setToolTip("Render every Photoshop artboard in the selected PSD or PSB to its own PNG. Files keep the artboard names.");connect(exportArtboards,&QPushButton::clicked,this,[this]{const QString source=inputAliases.value(currentFile,currentFile);const QString suffix=QFileInfo(source).suffix().toLower();if(suffix!="psd"&&suffix!="psb"){showError("Select a PSD or PSB import first. Aspectra exports the artboards in the current Photoshop document.");return;}const QString folder=QFileDialog::getExistingDirectory(this,"Choose artboard PNG destination",QSettings().value("artboardExportRoot",QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString());if(folder.isEmpty())return;QSettings().setValue("artboardExportRoot",folder);QString error;const int count=PsdImporter::exportArtboards(this,source,folder,&error);if(!count){showError(error);return;}lastFolder=folder;status->setText(QString("Exported %1 artboard PNG%2 · %3").arg(count).arg(count==1?"":"s").arg(QDir(folder).dirName()));});
    auto *layeredPsd=button("Layered PSD…",toolbar,"gold");layeredPsd->setToolTip("Export every imported still image as its own editable Photoshop raster layer.");connect(layeredPsd,&QPushButton::clicked,this,[this]{QString path=QFileDialog::getSaveFileName(this,"Export layered Photoshop document","Aspectra-layers.psd","Photoshop PSD (*.psd)");if(path.isEmpty())return;QStringList stills;for(const auto &source:this->batch.files)if(!VideoProcessor::isVideo(source))stills<<source;if(!writeLayeredPsd(path,stills,QSize(widthInput->value(),heightInput->value())))showError("Could not create layered PSD. Use still images and a valid canvas size.");else status->setText(QString("Layered PSD exported · %1 editable layers").arg(stills.size()));});
    auto addCommand=[commandMenu](const QString &text,QAbstractButton *source){auto *action=commandMenu->addAction(text);QObject::connect(action,&QAction::triggered,source,&QAbstractButton::click);return action;};
    addCommand("Import…",import);addCommand("Reset current batch",reset);commandMenu->addSeparator();auto *imageMode=addCommand("Image mode",modes->button(0));auto *videoMode=addCommand("Video mode",modes->button(1));auto *captureMode=addCommand("Capture mode",modes->button(2));for(auto *action:{imageMode,videoMode,captureMode})action->setCheckable(true);imageMode->setChecked(true);auto *modeActions=new QActionGroup(this);modeActions->setExclusive(true);modeActions->addAction(imageMode);modeActions->addAction(videoMode);modeActions->addAction(captureMode);connect(modes,&QButtonGroup::idClicked,this,[this,imageMode,videoMode,captureMode](int selected){for(auto *action:{imageMode,videoMode,captureMode})action->setChecked(false);if(selected==0)imageMode->setChecked(true);else if(selected==1)videoMode->setChecked(true);else captureMode->setChecked(true);routeMode(selected);});
    commandMenu->addSeparator();auto *openCompositor=commandMenu->addAction(whiteIcon("C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/video.png"),"Open current image in compositor");openCompositor->setToolTip("Creates a five-second composition with this image as the first editable image clip.");connect(openCompositor,&QAction::triggered,this,&MainWindow::openImageInCompositor);addCommand("Save project…",saveProject);addCommand("Export design…",exportDesign);addCommand("Export PSD artboards to PNG…",exportArtboards);addCommand("Export layered PSD…",layeredPsd);commandMenu->addSeparator();auto *logAction=commandMenu->addAction(whiteIcon("C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/panel settings.png"),"Log");connect(logAction,&QAction::triggered,this,&MainWindow::showLog);auto *settingsAction=commandMenu->addAction("Settings…");connect(settingsAction,&QAction::triggered,this,&MainWindow::settingsDialog);auto *minimizeAction=commandMenu->addAction("Minimize");connect(minimizeAction,&QAction::triggered,this,&QWidget::showMinimized);auto *closeAction=commandMenu->addAction("Close Aspectra X");connect(closeAction,&QAction::triggered,this,&QWidget::close);
    auto *filmstrip=new FilmstripPanel(shell);navigation=filmstrip->selector;navLabel=filmstrip->countLabel;connect(filmstrip->previousButton,&QPushButton::clicked,this,[this]{navigate(-1);});connect(navigation,&QComboBox::activated,this,[this](int i){if(i>=0&&i<this->batch.files.size())selectFile(this->batch.files[i]);});connect(filmstrip->nextButton,&QPushButton::clicked,this,[this]{navigate(1);});layout->addWidget(filmstrip);filmstrip->hide();auto *toggleFilmstrip=new QShortcut(QKeySequence("F"),this);connect(toggleFilmstrip,&QShortcut::activated,filmstrip,[filmstrip]{filmstrip->setVisible(!filmstrip->isVisible());});
    captureRow=new QWidget;auto *cap=new QHBoxLayout(captureRow);cap->setContentsMargins(0,0,0,0);captureButton=button("Capture area",cap,"blue");auto *capMode=new QComboBox;capMode->addItems({"Capture image","Record video"});cap->addWidget(capMode);connect(capMode,&QComboBox::currentIndexChanged,this,[this](int i){recordMode=i==1;});connect(captureButton,&QPushButton::clicked,this,&MainWindow::startCapture);layout->addWidget(captureRow);captureRow->hide();
    auto *batchRail=new QWidget(shell);batchRail->setObjectName("BatchNavigationRail");auto *batchRailLayout=new QHBoxLayout(batchRail);batchRailLayout->setContentsMargins(0,0,0,0);batchRailLayout->setSpacing(0);batchRailLayout->addStretch();batchNavigator=new GradientSlider;batchNavigator->setObjectName("BatchNavigationSlider");batchNavigator->setRange(0,0);batchNavigator->setFixedWidth(286);batchNavigator->setToolTip("Batch navigator · drag left or right to move through the imported artboards and media.");batchRailLayout->addWidget(batchNavigator);batchRailLayout->addStretch();connect(batchNavigator,&QSlider::valueChanged,this,[this](int index){if(!loading&&index>=0&&index<this->batch.files.size()&&this->batch.files.value(index)!=currentFile)selectFile(this->batch.files[index]);});layout->addWidget(batchRail);
    auto *addRail=new QWidget(shell);addRail->setObjectName("AddIconRail");auto *addRailLayout=new QHBoxLayout(addRail);addRailLayout->setContentsMargins(0,2,0,2);addRailLayout->setSpacing(12);addRailLayout->addStretch();const QString addIconRoot="C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/";auto addCompositionElement=[this](TimelineTrack::Type type,const QString &name){if(currentFile.isEmpty()||original.isNull()){showError("Import media before adding a composition layer.");return;}TimelineTrack track;track.type=type;track.name=name;track.text=type==TimelineTrack::Text?"Text":QString{};track.image=type==TimelineTrack::Image?QImage(original.size(),QImage::Format_ARGB32):QImage{};if(!track.image.isNull())track.image.fill(Qt::transparent);track.start=0;track.end=qMax(5.,media.duration);timelineTracks<<track;updateTrackPanel();if(type==TimelineTrack::Text&&trackList)trackList->setCurrentRow(timelineTracks.size()-1);status->setText(name+" added to the active composition");};auto addRailButton=[&](const QString &name,const QString &icon,const QString &tip,std::function<void()> action){auto *control=new FluidToolButton(addRail);control->setObjectName("AddRailIcon");control->setIcon(whiteIcon(addIconRoot+icon));control->setIconSize(QSize(25,25));control->setToolButtonStyle(Qt::ToolButtonIconOnly);control->setFixedSize(46,46);control->setToolTip(tip);addRailLayout->addWidget(control);connect(control,&QToolButton::clicked,this,action);};addRailButton("Image","add image.png","Add image to the batch or active composition",[this]{chooseFiles();});addRailButton("Text","add text.png","Add an editable text layer to the composition",[addCompositionElement]{addCompositionElement(TimelineTrack::Text,"Text");});addRailButton("Effects","add effects.png","Add an effects layer to the composition",[addCompositionElement]{addCompositionElement(TimelineTrack::Effect,"Effects");});addRailButton("Layer","add layer.png","Add a transparent image layer to the composition",[addCompositionElement]{addCompositionElement(TimelineTrack::Image,"Layer");});addRailLayout->addStretch();layout->addWidget(addRail);
    addRail->setFixedHeight(40);addRailLayout->setContentsMargins(0,-18,0,0);for(auto *icon:addRail->findChildren<QToolButton*>()){icon->setFixedSize(34,34);icon->setIconSize(QSize(19,19));}
    auto *stageHost=new QWidget(shell);stageHost->setObjectName("StageHost");stageHost->setMinimumHeight(240);stageHost->setMaximumHeight(qMax(240,qRound(height()*.43)));auto *stageColumn=new QVBoxLayout(stageHost);stageColumn->setContentsMargins(0,0,0,0);stageColumn->setSpacing(0);layout->addWidget(stageHost,1);
    stageHost->setMaximumHeight(QWIDGETSIZE_MAX);preview=new PreviewWidget;preview->setObjectName("creativeStage");auto *mainPreview=new MainPreview(preview,stageHost);auto *previewLayer=new QWidget(stageHost);auto *previewStack=new QStackedLayout(previewLayer);previewStack->setContentsMargins(0,0,0,0);previewStack->setStackingMode(QStackedLayout::StackAll);previewStack->addWidget(mainPreview);auto *safeZone=new QSlider(Qt::Vertical,previewLayer);safeZone->setObjectName("SafeZoneSlider");safeZone->setRange(0,45);safeZone->setValue(10);safeZone->setFixedWidth(32);safeZone->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Expanding);safeZone->setToolTip("Safe-zone margin. Drag to apply the same margin to every edge live.");previewStack->addWidget(safeZone);previewStack->setAlignment(safeZone,Qt::AlignLeft);previewStack->setCurrentWidget(safeZone);stageColumn->addWidget(previewLayer,1);connect(safeZone,&QSlider::valueChanged,this,[this](int value){
        for(int edge=0;edge<safeInputs.size();++edge){
            QSignalBlocker block(safeInputs[edge]);
            const double scale=pixelMargins?(edge%2?heightInput->value():widthInput->value())/100.0:1.0;
            safeInputs[edge]->setValue(value*scale);
        }
        if(guides){QSignalBlocker block(guides);guides->setChecked(true);}
        refresh();
    });
    // Keep existing navigation/import controls inside the flexible stage so
    // the shell has a strict TopBar -> Stage -> Rails -> Settings order.
    int stageControlIndex=0;
    for(QWidget *control:{static_cast<QWidget*>(filmstrip),captureRow,batchRail,addRail}){
        layout->removeWidget(control);
        control->setParent(stageHost);
        stageColumn->insertWidget(stageControlIndex++,control,0);
    }
    // Locked portrait composition: the viewer is the dominant, centered
    // surface and the rails sit tightly beneath it without a framed card.
    mainPreview->setMinimumHeight(0);mainPreview->setStyleSheet("background:transparent;border:none;");preview->setStyleSheet("background:transparent;border:none;");
    // The preview consumes the space left after rails and settings are laid out.
    stageHost->setMinimumHeight(0);
    connect(preview,&PreviewWidget::marginsDragged,this,[this](QMarginsF safe,QMarginsF padding){double sv[]{safe.left(),safe.top(),safe.right(),safe.bottom()},pv[]{padding.left(),padding.top(),padding.right(),padding.bottom()};for(int i=0;i<4;++i){QSignalBlocker a(safeInputs[i]),b(paddingInputs[i]);safeInputs[i]->setValue(sv[i]*100);paddingInputs[i]->setValue(pv[i]*100);}refresh();});
    auto *canvasTools=new QWidget;auto *canvasLayout=new QVBoxLayout(canvasTools);canvasLayout->setContentsMargins(12,10,12,4);canvasLayout->setSpacing(7);auto *viewRow=row(canvasLayout,7);viewRow->addWidget(label("PREVIEW ZOOM","muted"));auto *previewZoom=new GradientSlider;previewZoom->setRange(25,1600);previewZoom->setValue(100);previewZoom->setToolTip("Magnifies only the preview viewport. It does not alter crop, output dimensions, or exported pixels.");viewRow->addWidget(previewZoom,1);auto *previewZoomValue=label("100%");previewZoomValue->setFixedWidth(35);viewRow->addWidget(previewZoomValue);connect(previewZoom,&QSlider::valueChanged,this,[this,previewZoomValue](int value){preview->setViewZoom(value/100.);previewZoomValue->setText(QString::number(value)+"%");});
    auto *zoomRow=row(canvasLayout,7);zoomRow->addWidget(label("CROP SCALE","muted"));zoom=new GradientSlider;zoom->setRange(100,400);zoom->setValue(100);zoom->setToolTip("Scales the source inside the output crop. This is separate from Preview Zoom.");zoomRow->addWidget(zoom,1);zoomValue=label("100%");zoomValue->setFixedWidth(35);zoomRow->addWidget(zoomValue);connect(zoom,&QSlider::valueChanged,this,&MainWindow::refresh);
    auto *tracePanel=new QWidget;auto *traceLayout=new QVBoxLayout(tracePanel);traceLayout->setContentsMargins(12,10,12,8);traceLayout->setSpacing(7);traceLayout->addWidget(label("VECTOR TRACE","section"));auto *traceIntro=label("Automatic palette detection and live contour inspection. Yellow paths are preview-only; saved SVGs retain detected colors.","muted");traceIntro->setWordWrap(true);traceLayout->addWidget(traceIntro);auto *traceRow=row(traceLayout,7);traceRow->addWidget(label("IMAGE → SVG TRACE","muted"));auto *traceColors=new QSpinBox;traceColors->setRange(2,64);traceColors->setValue(16);traceColors->setToolTip("Number of dominant colors. Use 16–32 for artwork; more colors preserve gradients but create more paths.");traceRow->addWidget(traceColors);auto *traceDetail=new GradientSlider;traceDetail->setRange(64,1024);traceDetail->setValue(512);traceDetail->setToolTip("Trace sampling resolution. 512 preserves a 512px icon at native detail; 1024 is for finer edges.");traceRow->addWidget(traceDetail,1);auto *trace=button("Trace",traceRow,"gold");trace->setToolTip("Start a live editable SVG contour preview.");saveTraceButton=button("Save SVG…",traceRow,"blue");saveTraceButton->setEnabled(false);saveTraceButton->setToolTip("Save the SVG currently displayed in the trace preview.");auto *showOriginal=button("Original",traceRow);showOriginal->setToolTip("Leave vector trace preview and return to the active image preview.");auto *smoothRow=row(traceLayout,7);smoothRow->addWidget(label("TRACE SMOOTH","muted"));auto *traceSmooth=new GradientSlider;traceSmooth->setRange(0,100);traceSmooth->setValue(28);traceSmooth->setToolTip("0 retains the most edge detail. Higher values use fewer, smoother anchor points.");smoothRow->addWidget(traceSmooth,1);auto *smoothValue=label("28%");smoothValue->setFixedWidth(35);smoothRow->addWidget(smoothValue);
    auto renderTrace=[this,traceColors,traceDetail,traceSmooth]{if(original.isNull()||VideoProcessor::isVideo(currentFile))return;const auto traced=VectorTracer::trace(original,traceColors->value(),traceDetail->value(),traceSmooth->value());if(traced.svg.isEmpty())return;pendingTraceSvg=traced.svg;pendingTraceColors=traced.colorCount;tracePreviewActive=true;preview->setVectorTraceFrame(traced.preview);saveTraceButton->setEnabled(true);status->setText(QString("Live trace preview · %1 colors · save when ready").arg(traced.colorCount));};
    traceRefreshTimer.setSingleShot(true);traceRefreshTimer.setInterval(45);connect(&traceRefreshTimer,&QTimer::timeout,this,renderTrace);auto scheduleTrace=[this]{if(!original.isNull()&&!VideoProcessor::isVideo(currentFile)){tracePreviewActive=true;status->setText("Updating live trace…");traceRefreshTimer.start();}};
    connect(traceColors,qOverload<int>(&QSpinBox::valueChanged),this,[scheduleTrace](int){scheduleTrace();});connect(traceDetail,&QSlider::valueChanged,this,[scheduleTrace](int){scheduleTrace();});connect(traceSmooth,&QSlider::valueChanged,this,[smoothValue,scheduleTrace](int value){smoothValue->setText(QString::number(value)+"%");scheduleTrace();});connect(showOriginal,&QPushButton::clicked,this,[this]{traceRefreshTimer.stop();tracePreviewActive=false;preview->clearVectorTraceFrame();status->setText("Image preview restored");});connect(trace,&QPushButton::clicked,this,[renderTrace]{renderTrace();});connect(saveTraceButton,&QPushButton::clicked,this,[this]{if(pendingTraceSvg.isEmpty()){showError("Generate a trace preview before saving.");return;}const QString suggested=QFileInfo(currentFile).absolutePath()+"/"+QFileInfo(currentFile).completeBaseName()+"-trace.svg";const QString path=QFileDialog::getSaveFileName(this,"Save traced SVG",suggested,"Scalable Vector Graphics (*.svg)");if(path.isEmpty())return;QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)){showError("Could not write traced SVG.");return;}file.write(pendingTraceSvg.toUtf8());status->setText(QString("SVG saved · %1 contour colors · %2").arg(pendingTraceColors).arg(QFileInfo(path).fileName()));});
    auto *autoTraceRow=row(layout,7);auto *autoTraceColors=new QCheckBox("Auto colors");autoTraceColors->setChecked(true);autoTraceColors->setToolTip("Analyzes the image palette and chooses the trace color count automatically. Turn off to use the Colors value above.");autoTraceRow->addWidget(autoTraceColors);autoTraceRow->addWidget(label("Detects dominant colors for every image in a batch","muted"),1);auto *batchTrace=button("Batch SVG → Folder…",autoTraceRow,"blue");batchTrace->setToolTip("Vector-traces all imported still images. Numbered image sequences retain their names and save to an SVG folder.");auto refreshAutoColors=[this,autoTraceColors,traceColors]{if(autoTraceColors->isChecked()&&!original.isNull()){const int detected=VectorTracer::detectColorCount(original);QSignalBlocker block(traceColors);traceColors->setValue(detected);}traceColors->setEnabled(!autoTraceColors->isChecked());};connect(autoTraceColors,&QCheckBox::toggled,this,[refreshAutoColors,trace]{refreshAutoColors();trace->click();});connect(&loadWatcher,&QFutureWatcher<LoadedMedia>::finished,this,refreshAutoColors);connect(batchTrace,&QPushButton::clicked,this,[this,autoTraceColors,traceColors,traceDetail,traceSmooth]{if(this->batch.files.isEmpty()){showError("Import images before batch SVG export.");return;}const QString root=QFileDialog::getExistingDirectory(this,"Choose SVG export destination",QSettings().value("svgExportRoot",QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString());if(root.isEmpty())return;QSettings().setValue("svgExportRoot",root);const QString folder=QDir(root).filePath("SVG");if(!QDir().mkpath(folder)){showError("Could not create the SVG export folder.");return;}QProgressDialog progress("Preparing SVG batch…","Cancel",0,this->batch.files.size(),this);progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(0);QSet<QString> names;int written=0,index=0;for(const auto &input:this->batch.files){if(progress.wasCanceled())break;++index;if(VideoProcessor::isVideo(input)){progress.setValue(index);continue;}const QImage image=ImageProcessor::read(input);if(image.isNull()){progress.setValue(index);continue;}const int colors=autoTraceColors->isChecked()?VectorTracer::detectColorCount(image):traceColors->value();progress.setLabelText(QString("Tracing %1 / %2 · %3").arg(index).arg(this->batch.files.size()).arg(inputTitles.value(input,QFileInfo(input).fileName())));QApplication::processEvents();const auto traced=VectorTracer::trace(image,colors,traceDetail->value(),traceSmooth->value());QString stem=inputTitles.value(input,QFileInfo(input).completeBaseName());for(QChar &character:stem)if(!character.isLetterOrNumber()&&character!=' '&&character!='-'&&character!='_')character='_';QString unique=stem;int suffix=2;while(names.contains(unique.toLower()))unique=stem+"-"+QString::number(suffix++);names.insert(unique.toLower());QFile output(QDir(folder).filePath(unique+".svg"));if(!traced.svg.isEmpty()&&output.open(QIODevice::WriteOnly|QIODevice::Truncate)){output.write(traced.svg.toUtf8());++written;}progress.setValue(index);QApplication::processEvents();}status->setText(progress.wasCanceled()?QString("SVG batch cancelled · %1 files kept").arg(written):QString("SVG batch complete · %1 scalable SVG files · SVG folder").arg(written));});
    layout->removeItem(autoTraceRow);traceLayout->addLayout(autoTraceRow);
    // Loading an image must leave the normal image preview alone.  A trace is
    // created only when the user enters Trace and presses Trace (or changes a
    // trace control); it is never an import-side effect.
    connect(&loadWatcher,&QFutureWatcher<LoadedMedia>::finished,this,[this]{if(tracePreviewActive){traceRefreshTimer.stop();tracePreviewActive=false;pendingTraceSvg.clear();pendingTraceColors=0;if(saveTraceButton)saveTraceButton->setEnabled(false);preview->clearVectorTraceFrame();}});
    videoRow=new QWidget;auto *vl=new QVBoxLayout(videoRow);vl->setContentsMargins(0,0,0,0);vl->setSpacing(2);auto *playRow=row(vl,6);playButton=button("▶ Play",playRow,"blue");connect(playButton,&QPushButton::clicked,this,&MainWindow::togglePlayback);timeLabel=label("00:00.000 / 00:00.000","muted");playRow->addWidget(timeLabel,1);auto *mute=new QCheckBox("Mute");playRow->addWidget(mute);connect(mute,&QCheckBox::toggled,this,[this](bool value){audio->setMuted(value);});timeline=new TimelineWidget;vl->addWidget(timeline);connect(timeline,&TimelineWidget::seek,this,[this](double time){player->setPosition(qRound64(time*1000));});connect(timeline,&TimelineWidget::rangeEdited,this,[this](double a,double b){QSignalBlocker x(inMarker),y(outMarker);inMarker->setValue(a);outMarker->setValue(b);syncRange();});stageColumn->addWidget(videoRow);videoRow->hide();
    tabs=new QTabWidget(shell);tabs->setObjectName("SettingsScrollArea");tabs->tabBar()->hide();tabs->setMinimumHeight(0);tabs->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);auto *canvasPanel=new QWidget;auto *canvasPanelLayout=new QVBoxLayout(canvasPanel);canvasPanelLayout->setContentsMargins(0,0,0,0);canvasPanelLayout->setSpacing(0);canvasPanelLayout->addWidget(canvasTools);canvasPanelLayout->addWidget(framePanel());tabs->addTab(scrolled(canvasPanel),"Canvas");tabs->addTab(scrolled(colorPanel()),"Adjust");tabs->addTab(scrolled(selectRefinePanel()),"Select");tabs->addTab(scrolled(tracePanel),"Trace");tabs->addTab(scrolled(stylesPanel()),"Effects");tabs->addTab(scrolled(clipsPanel()),"Video");tabs->addTab(scrolled(texturePanel()),"Texture");tabs->addTab(scrolled(patternsPanel()),"Pattern");tabs->addTab(scrolled(outputPanel()),"Export");tabs->addTab(scrolled(modelPanel()),"Model");connect(tabs,&QTabWidget::currentChanged,this,[this](int index){if(index!=3&&tracePreviewActive){traceRefreshTimer.stop();tracePreviewActive=false;pendingTraceSvg.clear();pendingTraceColors=0;if(saveTraceButton)saveTraceButton->setEnabled(false);preview->clearVectorTraceFrame();status->setText("Trace preview cleared");}refresh();if(index==7)updatePatternPreview();});
    auto *carouselScroll=new QScrollArea(shell);tabCarousel=carouselScroll;carouselScroll->setObjectName("TabsCarousel");carouselScroll->setFixedHeight(58);carouselScroll->setWidgetResizable(true);carouselScroll->setFrameShape(QFrame::NoFrame);carouselScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);carouselScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);carouselScroll->viewport()->installEventFilter(this);
    auto *carousel=new QWidget;auto *carouselLayout=new QHBoxLayout(carousel);carouselLayout->setContentsMargins(0,3,0,3);carouselLayout->setSpacing(8);carouselScroll->setWidget(carousel);
    auto *tabButtons=new QButtonGroup(carousel);tabButtons->setExclusive(true);const QString iconRoot="C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/";
    const QStringList names{"Canvas","Adjust","Select","Trace","Effects","Video","Texture","Pattern","Export"};const QStringList icons{"artboard tool.png","gradient tool.png","quick selection.png","pen tool.png","brush tool.png","slice tool.png","gradient tool copy.png","pattern stamp.png","move tool.png"};
    for(int i=0;i<names.size();++i){auto *tab=new FluidToolButton(carousel);tab->setObjectName("CarouselTab");tab->setText(names[i]);tab->setIcon(whiteIcon(iconRoot+icons[i]));tab->setIconSize(QSize(28,28));tab->setToolButtonStyle(Qt::ToolButtonIconOnly);tab->setCheckable(true);tab->setFixedSize(48,48);tab->setToolTip(names[i]+" settings");tab->installEventFilter(this);carouselLayout->addWidget(tab);tabButtons->addButton(tab,i);}
    auto *moreTab=new FluidToolButton(carousel);moreTab->setObjectName("CarouselTab");moreTab->setText("More");moreTab->setIcon(whiteIcon(iconRoot+"panel settings.png"));moreTab->setIconSize(QSize(28,28));moreTab->setToolButtonStyle(Qt::ToolButtonIconOnly);moreTab->setFixedSize(48,48);moreTab->setToolTip("More actions");moreTab->setMenu(commandMenu);moreTab->setPopupMode(QToolButton::InstantPopup);moreTab->installEventFilter(this);carouselLayout->addWidget(moreTab);carouselLayout->addStretch();tabButtons->button(0)->setChecked(true);connect(tabButtons,&QButtonGroup::idClicked,tabs,&QTabWidget::setCurrentIndex);layout->addWidget(carouselScroll);layout->addWidget(tabs,1);
    carouselContent=tabCarousel->widget();tabCarousel->setWidgetResizable(false);carouselContent->setFixedWidth(576);carouselLeftFade=new QWidget(tabCarousel->viewport());carouselRightFade=new QWidget(tabCarousel->viewport());for(auto *fade:{carouselLeftFade,carouselRightFade}){fade->setObjectName("RailEdgeFade");fade->setAttribute(Qt::WA_TransparentForMouseEvents);fade->raise();}carouselLeftFade->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #000000,stop:.55 rgba(0,0,0,210),stop:1 rgba(0,0,0,0));");carouselRightFade->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 rgba(0,0,0,0),stop:.45 rgba(0,0,0,210),stop:1 #000000);");auto updateCarouselFeather=[this]{if(!tabCarousel||!carouselLeftFade||!carouselRightFade)return;auto *view=tabCarousel->viewport();carouselLeftFade->setGeometry(0,0,44,view->height());carouselRightFade->setGeometry(view->width()-44,0,44,view->height());auto *bar=tabCarousel->horizontalScrollBar();carouselLeftFade->setVisible(!carouselLeftFade->property("immersiveHidden").toBool()&&bar->value()>0);carouselRightFade->setVisible(!carouselRightFade->property("immersiveHidden").toBool()&&bar->value()<bar->maximum());carouselLeftFade->raise();carouselRightFade->raise();};connect(tabCarousel->horizontalScrollBar(),&QScrollBar::valueChanged,this,[updateCarouselFeather](int){updateCarouselFeather();});QTimer::singleShot(0,this,updateCarouselFeather);
    layout->removeWidget(carouselScroll);
    layout->removeWidget(tabs);
    auto *settingsHost=new QWidget(shell);
    settingsHost->setObjectName("SettingsHost");
    auto *settingsLayout=new QVBoxLayout(settingsHost);
    settingsLayout->setContentsMargins(0,0,0,0);
    settingsLayout->setSpacing(0);
    settingsLayout->addWidget(carouselScroll);
    settingsLayout->addWidget(tabs,1);
    stageHost->setMinimumHeight(0);
    stageHost->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    mainPreview->setMinimumHeight(0);
    preview->setMinimumHeight(0);
    preview->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    settingsHost->setMinimumHeight(0);
    settingsHost->setMaximumHeight(420);
    settingsHost->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    tabs->hide();
    settingsHost->hide();
    auto settingsExpanded=std::make_shared<bool>(false);
    auto showSettings=[this,settingsHost,previewZoom,settingsExpanded]{
        preview->setViewZoom(1.0);
        previewZoom->setValue(100);
        if(!*settingsExpanded){
            *settingsExpanded=true;
            settingsHost->setProperty("settingsOpen",true);
            tabs->show();
            settingsHost->show();
            settingsHost->updateGeometry();
        }
    };
    connect(tabButtons,&QButtonGroup::idClicked,this,[this,showSettings](int index){tabs->setCurrentIndex(index);showSettings();});
    tabs->setMinimumWidth(0);
    settingsHost->setMinimumWidth(0);
    carouselLayout->insertStretch(0);
    auto *subRail=new QScrollArea(settingsHost);
    subRail->setObjectName("SubSettingsRail");
    subRail->setFixedHeight(54);
    subRail->setFrameShape(QFrame::NoFrame);
    subRail->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    subRail->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    subRail->setWidgetResizable(true);
    auto *subHost=new QWidget(subRail);
    auto *subLayout=new QHBoxLayout(subHost);
    subLayout->setContentsMargins(0,3,0,3);
    subLayout->setSpacing(8);
    subRail->setWidget(subHost);
    settingsLayout->insertWidget(1,subRail);
    auto *subButtons=new QButtonGroup(subHost);
    subButtons->setExclusive(true);
    const QVector<QStringList> subNames{{"Frame","Canvas","Safe","Layer","Mask"},{"Hue","Saturation","Brightness","Contrast","Levels","Balance","Key","Auto"},{"Subject","Brush","Contract","Feather","Edge","Hair"},{"Colors","Detail","Smooth","Export"},{"Blend","Opacity","Stroke","Shadow","Glow","Bevel"},{"Start","End","Reset"},{"Base Color","Normal","Roughness","Metallic","Ambient Occlusion","Height / Displacement","Opacity","Emission","Specular","Glossiness","Cavity","Curvature","Subsurface","Clearcoat","Clearcoat Roughness","Sheen","Sheen Roughness","Anisotropy","Transmission","ORM","MRA","RMA","Sphere","Export"},{"Blend","Warp","Mirror","Export"},{"Sizes","Images","Videos","Quality","Archive"}};
    auto rebuildSubRail=[subLayout,subButtons,subNames](int category){
        for(auto *button:subButtons->buttons()){subButtons->removeButton(button);button->deleteLater();}
        while(auto *item=subLayout->takeAt(0))delete item;
        subLayout->addStretch();
        const auto &names=subNames.value(category);
        for(int i=0;i<names.size();++i){auto *button=new SettingRailButton(names[i]);button->setObjectName("SubRailIcon");button->setToolTip(names[i]);button->setCheckable(true);subLayout->addWidget(button);subButtons->addButton(button,i);}
        subLayout->addStretch();
        if(auto *first=static_cast<SettingRailButton*>(subButtons->button(0))){first->setChecked(true);first->setSelected(true);}
    };
    rebuildSubRail(0);
    connect(subButtons,&QButtonGroup::idClicked,this,[subButtons](int index){for(auto *button:subButtons->buttons())static_cast<SettingRailButton*>(button)->setSelected(button==subButtons->button(index));});
    connect(tabs,&QTabWidget::currentChanged,this,[rebuildSubRail](int category){rebuildSubRail(category);});
    auto *settingCard=new QWidget(settingsHost);
    settingCard->setObjectName("ActiveSettingCard");
    auto *settingCardLayout=new QVBoxLayout(settingCard);
    settingCardLayout->setContentsMargins(12,0,12,0);
    settingCardLayout->setSpacing(6);
    settingsLayout->insertWidget(2,settingCard);
    settingCard->hide();
    connect(subButtons,&QButtonGroup::idClicked,this,[this,settingsHost,settingCard,settingCardLayout,subNames](int index){
        QTimer::singleShot(0,this,[this,settingsHost,settingCard,settingCardLayout,subNames,index]{
            const QString name=subNames.value(tabs->currentIndex()).value(index,"Setting");

            // =================================================================
            // EXPLICIT LAYER CARD OVERRIDE WITH ABSOLUTE EARLY RETURN
            // =================================================================
           if (name == "Layer") {
                clearLayoutItems(settingCardLayout);
                settingCardLayout->setContentsMargins(8, 4, 8, 6);
                settingCardLayout->setSpacing(6);

                // =============================================================
                // 1. TOP SECTION: SLIM HEADER (Locks, Blend Mode, Slim Sliders)
                // =============================================================
                auto *topContainer = new QWidget;
                auto *topLayout = new QVBoxLayout(topContainer);
                topLayout->setContentsMargins(0, 0, 0, 0);
                topLayout->setSpacing(4);

                auto *row1 = new QHBoxLayout;
                row1->setSpacing(6);

                auto *lockPixels = new QToolButton; lockPixels->setIcon(settingIcon("brush"));
                auto *lockPos = new QToolButton; lockPos->setIcon(whiteIcon("C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/move tool.png"));
                auto *lockAll = new QToolButton; lockAll->setIcon(settingIcon("safe"));
                for (auto *b : {lockPixels, lockPos, lockAll}) {
                    b->setCheckable(true);
                    b->setFixedSize(26, 26);
                    b->setIconSize(QSize(14, 14));
                    b->setStyleSheet("QToolButton { background: #000000; border: 1px solid #2d2d35; border-radius: 6px; }"
                                     "QToolButton:checked { background: #101015; border: 2px solid #7a4dff; }");
                    row1->addWidget(b);
                }
                row1->addStretch();

                auto *blendCombo = new QComboBox;
                blendCombo->addItems({"Pass Through", "Normal", "Multiply", "Screen", "Overlay", "Color Dodge"});
                blendCombo->setFixedHeight(26);
                blendCombo->setStyleSheet("QComboBox { background: #000000; border: 1px solid #2d2d35; border-radius: 8px; color: #ffffff; padding-left: 8px; font-size: 11px; }");
                row1->addWidget(blendCombo);
                topLayout->addLayout(row1);

                auto createGradientSliderRow = [](const QString &title, int defaultVal, GradientSlider **output) {
                    auto *sliderRow = new QHBoxLayout;
                    sliderRow->setSpacing(8);
                    auto *lbl = new QLabel(title);
                    lbl->setFixedWidth(52);
                    lbl->setStyleSheet("color: #ffffff; font-size: 10px; font-weight: 700;");

                    auto *slider = new GradientSlider(Qt::Horizontal);
                    slider->setRange(0, 100);
                    slider->setValue(defaultVal);
                    slider->setFixedHeight(22);
                    *output = slider;

                    auto *valLbl = new QLabel(QString::number(defaultVal) + "%");
                    valLbl->setFixedWidth(34);
                    valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    valLbl->setStyleSheet("min-width:26px; min-height:26px; max-width:42px; max-height:42px; background:#ffffff; color:#000000; border-radius:13px; font-weight:700; font-variant-numeric:tabular-nums; padding:1px 3px; font-size:10px;");

                    QObject::connect(slider, &QSlider::valueChanged, [valLbl](int val) {
                        valLbl->setText(QString::number(val) + "%");
                    });

                    sliderRow->addWidget(lbl);
                    sliderRow->addWidget(slider, 1);
                    sliderRow->addWidget(valLbl);
                    return sliderRow;
                };

                GradientSlider *opacitySlider = nullptr, *fillSlider = nullptr;
                topLayout->addLayout(createGradientSliderRow("Opacity", 100, &opacitySlider));
                topLayout->addLayout(createGradientSliderRow("Fill", 100, &fillSlider));
                settingCardLayout->addWidget(topContainer);

                // =============================================================
                // 2. MIDDLE SECTION: NAVIGATION & VIEW SWITCHER
                // =============================================================
                auto *navRow = new QHBoxLayout;
                navRow->setSpacing(6);
                auto *backBtn = new QToolButton;
                backBtn->setText("‹ Back to Artboards");
                backBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
                backBtn->setStyleSheet("QToolButton { color: #3ddcff; font-size: 11px; font-weight: bold; background: transparent; border: none; padding: 0; }"
                                       "QToolButton:hover { color: #ffffff; }");
                backBtn->hide();

                auto *crumbLabel = new QLabel("Artboards");
                crumbLabel->setStyleSheet("color: #ffffff; font-size: 10px; font-weight: 700; letter-spacing: 1px;");

                navRow->addWidget(backBtn);
                navRow->addWidget(crumbLabel, 1);
                settingCardLayout->addLayout(navRow);

                auto *stackWidget = new QStackedWidget;
                settingCardLayout->addWidget(stackWidget, 1);

                // Page 0: Horizontal Artboard Rail (LOCKED to 240px width)
                auto *railWidget = new QListWidget;
                // Force the Artboards card to behave as one horizontal row.
                railWidget->setViewMode(QListView::IconMode);
                railWidget->setFlow(QListView::LeftToRight);
                railWidget->setWrapping(false);
                railWidget->setResizeMode(QListView::Adjust);
                railWidget->setMovement(QListView::Snap);
                railWidget->setGridSize(QSize(248, 72));
                railWidget->setIconSize(QSize(40, 40));
                railWidget->setFixedHeight(96);
                railWidget->setSpacing(8);
                railWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                railWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                railWidget->setDragEnabled(true);
                railWidget->setAcceptDrops(true);
                railWidget->setDropIndicatorShown(true);
                railWidget->setDragDropMode(QAbstractItemView::InternalMove);
                railWidget->setStyleSheet(
                    "QListWidget { background: #000000; border: 1px solid #2d2d35; border-radius: 10px; outline: none; padding: 4px; }"
                    "QListWidget::item { width: 240px; height: 64px; background: #000000; border: 1px solid #2d2d35; border-radius: 8px; color: #ffffff; font-size: 10px; font-weight: 600; padding: 4px; }"
                    "QListWidget::item:selected { border: 2px solid #7a4dff; background: #101015; }"
                );
                stackWidget->addWidget(railWidget);

                // Page 1: Detailed Layer Rows
                auto *detailRowList = new QListWidget;
                detailRowList->setStyleSheet(
                    "QListWidget { background: #000000; border: 1px solid #2d2d35; border-radius: 10px; outline: none; padding: 4px; }"
                    "QListWidget::item { height: 38px; border-bottom: 1px solid #1c1c24; color: #ffffff; font-size: 11px; padding: 2px 6px; }"
                    "QListWidget::item:selected { background: #101015; border: 1px solid #3ddcff; border-radius: 6px; }"
                );
                stackWidget->addWidget(detailRowList);
                auto selectedTrack = [this, stackWidget, detailRowList]() -> int {
                    if (stackWidget->currentIndex() != 1 || !detailRowList->currentItem() || detailRowList->currentItem()->data(Qt::UserRole).toString() != "track") return -1;
                    const int index = detailRowList->currentItem()->data(Qt::UserRole + 2).toInt();
                    return index >= 0 && index < timelineTracks.size() ? index : -1;
                };
                auto syncLayerControls = [this, selectedTrack, blendCombo, opacitySlider, fillSlider, lockPixels, lockPos, lockAll]() {
                    const int index = selectedTrack();
                    const CanvasLayer layer = canvasLayers.value(currentFile);
                    const bool trackSelected = index >= 0;
                    const QString blend = trackSelected ? timelineTracks[index].blendMode : layer.blendMode;
                    QSignalBlocker blendBlock(blendCombo), opacityBlock(opacitySlider), fillBlock(fillSlider);
                    QSignalBlocker pixelsBlock(lockPixels), posBlock(lockPos), allBlock(lockAll);
                    blendCombo->setCurrentText(blend.isEmpty() ? (trackSelected ? "Normal" : "Pass Through") : blend);
                    opacitySlider->setValue(qRound((trackSelected ? timelineTracks[index].opacity : layer.opacity) * 100));
                    fillSlider->setValue(qRound((trackSelected ? timelineTracks[index].fill : layer.fill) * 100));
                    lockPixels->setChecked(trackSelected ? timelineTracks[index].lockPixels : layer.lockPixels);
                    lockPos->setChecked(trackSelected ? timelineTracks[index].lockPosition : layer.lockPosition);
                    lockAll->setChecked(trackSelected ? timelineTracks[index].locked : layer.lockAll);
                };
                QObject::connect(detailRowList, &QListWidget::currentItemChanged, detailRowList, [syncLayerControls](QListWidgetItem *, QListWidgetItem *) { syncLayerControls(); });
                QObject::connect(blendCombo, &QComboBox::currentTextChanged, detailRowList, [this, selectedTrack](const QString &value) { const int i=selectedTrack(); if(i>=0)timelineTracks[i].blendMode=value;else if(canvasLayers.contains(currentFile))canvasLayers[currentFile].blendMode=value;refresh(); });
                QObject::connect(opacitySlider, &QSlider::valueChanged, detailRowList, [this, selectedTrack](int value) { const int i=selectedTrack(); if(i>=0)timelineTracks[i].opacity=value/100.;else if(canvasLayers.contains(currentFile))canvasLayers[currentFile].opacity=value/100.;refresh(); });
                QObject::connect(fillSlider, &QSlider::valueChanged, detailRowList, [this, selectedTrack](int value) { const int i=selectedTrack(); if(i>=0)timelineTracks[i].fill=value/100.;else if(canvasLayers.contains(currentFile))canvasLayers[currentFile].fill=value/100.;refresh(); });
                QObject::connect(lockPixels, &QToolButton::toggled, detailRowList, [this, selectedTrack](bool value) { const int i=selectedTrack(); if(i>=0)timelineTracks[i].lockPixels=value;else if(canvasLayers.contains(currentFile))canvasLayers[currentFile].lockPixels=value; });
                QObject::connect(lockPos, &QToolButton::toggled, detailRowList, [this, selectedTrack](bool value) { const int i=selectedTrack(); if(i>=0)timelineTracks[i].lockPosition=value;else if(canvasLayers.contains(currentFile))canvasLayers[currentFile].lockPosition=value; });
                QObject::connect(lockAll, &QToolButton::toggled, detailRowList, [this, selectedTrack](bool value) { const int i=selectedTrack(); if(i>=0)timelineTracks[i].locked=value;else if(canvasLayers.contains(currentFile))canvasLayers[currentFile].lockAll=value; });

                // --- Shared Lambdas using shared_ptr to safely break scope dead-ends ---
                auto populateRailPtr = std::make_shared<std::function<void()>>();
                auto populateLayersPtr = std::make_shared<std::function<void(const QString&)>>();
                auto thumbnailIcon = [](const QImage &image, int side) {
                    QPixmap tile(side, side);
                    tile.fill(QColor("#161a22"));
                    QPainter painter(&tile);
                    for (int y = 0; y < side; y += 8) for (int x = 0; x < side; x += 8)
                        painter.fillRect(x, y, 8, 8, ((x + y) / 8) % 2 ? QColor("#252b30") : QColor("#161b20"));
                    if (!image.isNull()) {
                        const QImage fitted = image.scaled(side - 4, side - 4, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        painter.drawImage((side - fitted.width()) / 2, (side - fitted.height()) / 2, fitted);
                    }
                    return QIcon(tile);
                };
                auto showArtboardWorkspace = [this]() {
                    QVector<PreviewArtboard> artboards;
                    qreal nextX = 0;
                    for (const QString &path : this->batch.files) {
                        const CanvasLayer layer = canvasLayers.value(path);
                        const QSize size = layer.nativeSize.isValid() ? layer.nativeSize : QSize(widthInput->value(), heightInput->value());
                        PreviewArtboard board;
                        board.id = path;
                        board.name = layer.name.isEmpty() ? inputTitles.value(path, QFileInfo(path).completeBaseName()) : layer.name;
                        board.position = QPointF(nextX, 0);
                        board.size = size;
                        board.active = path == currentFile;
                        board.visible = layer.visible;
                        board.image = layer.artboardImage;
                        if (board.image.isNull() && path == currentFile) board.image = original;
                        else if (!path.startsWith(QLatin1String("aspectra://"))) {
                            QImageReader reader(path);
                            const QSize sourceSize = reader.size();
                            if (sourceSize.isValid()) reader.setScaledSize(sourceSize.scaled(1200, 1200, Qt::KeepAspectRatio));
                            board.image = reader.read();
                        }
                        artboards.append(board);
                        nextX += size.width() + 48;
                    }
                    if (artboards.size() > 1) preview->setArtboards(artboards);
                    else preview->clearArtboards();
                };

                *populateRailPtr = [this, railWidget, backBtn, crumbLabel, stackWidget, blendCombo, showArtboardWorkspace, syncLayerControls, thumbnailIcon]() {
                    railWidget->clear();
                    
                    if (this->batch.files.isEmpty()) {
                        auto *item = new QListWidgetItem("Blank Canvas");
                        item->setData(Qt::UserRole, QString());
                        railWidget->addItem(item);
                    } else {
                        // Strictly iterates batch.files so creation order matches the left-to-right visual order perfectly
                        for (const QString &path : this->batch.files) {
                            const auto &layer = canvasLayers[path];
                            QString displayName = layer.name.isEmpty() ? QFileInfo(path).completeBaseName() : layer.name;
                            if (displayName.isEmpty()) displayName = "Artboard";
                            QImage image = layer.artboardImage;
                            if (image.isNull() && path == currentFile) image = original;
                            if (image.isNull() && !path.startsWith(QLatin1String("aspectra://"))) {
                                QImageReader reader(path);
                                reader.setScaledSize(QSize(64, 64));
                                image = reader.read();
                            }
                            auto *item = new QListWidgetItem(thumbnailIcon(image, 40), displayName);
                            item->setData(Qt::UserRole, path);
                            item->setSizeHint(QSize(240, 64)); // Explicit enforcement
                            railWidget->addItem(item);
                            if (path == currentFile) {
                                item->setSelected(true);
                                railWidget->setCurrentItem(item);
                            }
                        }
                    }
                    stackWidget->setCurrentIndex(0);
                    backBtn->hide();
                    crumbLabel->setText("Artboards");
                    blendCombo->setCurrentText("Pass Through");
                    syncLayerControls();
                    showArtboardWorkspace();
                };

                *populateLayersPtr = [this, detailRowList, backBtn, crumbLabel, stackWidget, blendCombo, syncLayerControls, thumbnailIcon](const QString &sourcePath) {
                    if (sourcePath.isEmpty()) return;
                    if (sourcePath != currentFile) selectFile(sourcePath);
                    detailRowList->clear();

                    auto createLayerRow = [this, sourcePath, thumbnailIcon](const QString &layerName, const QImage &image, const QImage &mask, bool baseRow, bool isVisible, bool hasFx, bool isLinked, std::function<void(bool)> onToggleVisibility) {
                        auto *rowItemWidget = new QWidget;
                        auto *rowLayout = new QHBoxLayout(rowItemWidget);
                        rowLayout->setContentsMargins(4, 2, 4, 2);
                        rowLayout->setSpacing(8);

                        auto *eyeBtn = new QToolButton; eyeBtn->setCheckable(true); eyeBtn->setChecked(isVisible); eyeBtn->setText(isVisible ? "👁" : "○");
                        eyeBtn->setFixedSize(22, 22); eyeBtn->setStyleSheet("QToolButton { background: transparent; border: none; color: #3ddcff; font-size: 13px; } QToolButton:checked { color: #3ddcff; } QToolButton:not(:checked) { color: #555562; }");
                        QObject::connect(eyeBtn, &QToolButton::toggled, [eyeBtn, onToggleVisibility](bool checked) {
                            eyeBtn->setText(checked ? "👁" : "○");
                            if (onToggleVisibility) onToggleVisibility(checked);
                        });
                        rowLayout->addWidget(eyeBtn);

                        auto *imageButton = new QToolButton(rowItemWidget);
                        imageButton->setFixedSize(28, 28);imageButton->setIconSize(QSize(24, 24));imageButton->setIcon(thumbnailIcon(image, 24));
                        imageButton->setToolTip("Image thumbnail · click to return to artwork");
                        imageButton->setStyleSheet(QString("QToolButton{background:#11151a;border:2px solid %1;border-radius:4px;}").arg(baseRow&&!preview->isMaskEditMode()?"#3ddcff":"#333b46"));
                        rowLayout->addWidget(imageButton);
                        if (baseRow) QObject::connect(imageButton, &QToolButton::clicked, rowItemWidget, [this, imageButton, rowItemWidget]() {
                            preview->setMaskEditMode(false);
                            imageButton->setStyleSheet("QToolButton{background:#11151a;border:2px solid #3ddcff;border-radius:4px;}");
                            for (auto *button : rowItemWidget->findChildren<QToolButton*>("MaskThumbnail")) button->setStyleSheet("QToolButton{background:#11151a;border:2px solid #333b46;border-radius:4px;}");
                            status->setText("Editing artwork thumbnail");
                        });
                        if (baseRow && !mask.isNull()) {
                            auto *maskButton = new QToolButton(rowItemWidget);
                            maskButton->setObjectName("MaskThumbnail");maskButton->setFixedSize(28, 28);maskButton->setIconSize(QSize(24, 24));maskButton->setIcon(thumbnailIcon(mask, 24));
                            maskButton->setToolTip("Alt+click to view and paint this mask; Shift+drag paints black");
                            maskButton->setStyleSheet(QString("QToolButton{background:#11151a;border:2px solid %1;border-radius:4px;}").arg(preview->isMaskEditMode()?"#3ddcff":"#333b46"));
                            rowLayout->addWidget(maskButton);
                            QObject::connect(maskButton, &QToolButton::clicked, rowItemWidget, [this, sourcePath, imageButton, maskButton]() {
                                if (!(QApplication::keyboardModifiers() & Qt::AltModifier)) { status->setText("Alt+click the mask thumbnail to edit it"); return; }
                                if (currentFile != sourcePath) selectFile(sourcePath);
                                preview->setLayerMask(canvasLayers[sourcePath].mask);
                                preview->setMaskEditMode(true);
                                imageButton->setStyleSheet("QToolButton{background:#11151a;border:2px solid #333b46;border-radius:4px;}");
                                maskButton->setStyleSheet("QToolButton{background:#11151a;border:2px solid #3ddcff;border-radius:4px;}");
                                status->setText("Editing mask · drag to reveal · Shift+drag to hide · click artwork thumbnail to exit");
                            });
                            QObject::connect(preview, &PreviewWidget::maskBrushed, maskButton, [this, sourcePath, maskButton, thumbnailIcon](QPointF, bool, int) {
                                if (sourcePath == currentFile) maskButton->setIcon(thumbnailIcon(canvasLayers[sourcePath].mask, 24));
                            });
                        }

                        auto *nameLbl = new QLabel(layerName); nameLbl->setStyleSheet("color: #ffffff; font-size: 11px; font-weight: 600; background: transparent;"); rowLayout->addWidget(nameLbl);
                        rowLayout->addStretch(1);

                        if (hasFx) { auto *fxLbl = new QLabel("fx"); fxLbl->setStyleSheet("color: #f39c12; font-size: 9px; font-weight: bold; background: #000000; border: 1px solid #4a3b1c; border-radius: 4px; padding: 2px 5px;"); rowLayout->addWidget(fxLbl); }
                        if (isLinked) { auto *linkBtn = new QToolButton; linkBtn->setText("🔗"); linkBtn->setFixedSize(20, 20); linkBtn->setStyleSheet("QToolButton { background: transparent; border: none; color: #8c99ad; font-size: 10px; }"); rowLayout->addWidget(linkBtn); }

                        return rowItemWidget;
                    };

                    auto *baseItem = new QListWidgetItem(detailRowList);
                    baseItem->setSizeHint(QSize(0, 40));
                    bool baseVisible = canvasLayers.contains(sourcePath) ? canvasLayers[sourcePath].visible : true;
                    QString baseName = canvasLayers.contains(sourcePath) && !canvasLayers[sourcePath].name.isEmpty() ? canvasLayers[sourcePath].name : (sourcePath.isEmpty() ? "Canvas" : QFileInfo(sourcePath).completeBaseName());
                    QImage baseImage = canvasLayers.value(sourcePath).artboardImage;
                    if (baseImage.isNull() && sourcePath == currentFile) baseImage = original;
                    if (baseImage.isNull() && !sourcePath.startsWith(QLatin1String("aspectra://"))) { QImageReader reader(sourcePath);reader.setScaledSize(QSize(64,64));baseImage=reader.read(); }
                    auto *baseWidget = createLayerRow(baseName, baseImage, canvasLayers.value(sourcePath).mask, true, baseVisible, true, true, [this, sourcePath](bool visible) {
                        if (canvasLayers.contains(sourcePath)) { canvasLayers[sourcePath].visible = visible; refresh(); }
                    });
                    detailRowList->setItemWidget(baseItem, baseWidget);
                    baseItem->setData(Qt::UserRole, "base");
                    baseItem->setData(Qt::UserRole + 1, sourcePath);

                    for (int i = 0; i < timelineTracks.size(); ++i) {
                        const auto &track = timelineTracks[i];
                        if (track.artboardSource != sourcePath && (sourcePath.startsWith(QLatin1String("aspectra://")) || !track.artboardSource.isEmpty())) continue;

                        QString tName = track.name.isEmpty() ? QString("Layer %1").arg(i + 1) : track.name;
                        auto *item = new QListWidgetItem(detailRowList);
                        item->setSizeHint(QSize(0, 40));
                        auto *trackWidget = createLayerRow(tName, track.image, {}, false, track.enabled, false, false, [this, i](bool visible) {
                            if (i >= 0 && i < timelineTracks.size()) { timelineTracks[i].enabled = visible; updateTrackPanel(); refresh(); }
                        });
                        detailRowList->setItemWidget(item, trackWidget);
                        item->setData(Qt::UserRole, "track");
                        item->setData(Qt::UserRole + 2, i);
                    }

                    stackWidget->setCurrentIndex(1);
                    preview->clearArtboards();
                    backBtn->show();
                    crumbLabel->setText("Artboards / " + baseName);
                    blendCombo->setCurrentText("Normal");
                    detailRowList->setCurrentRow(0);
                    syncLayerControls();
                };

                QObject::connect(railWidget, &QListWidget::itemDoubleClicked, [populateLayersPtr](QListWidgetItem *item) {
                    if (item && populateLayersPtr) {
                        QString path = item->data(Qt::UserRole).toString();
                        if (!path.isEmpty()) (*populateLayersPtr)(path);
                    }
                });
                QObject::connect(railWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
                    const QString path = item ? item->data(Qt::UserRole).toString() : QString();
                    if (!path.isEmpty() && path != currentFile) selectFile(path);
                });
                QObject::connect(preview, &PreviewWidget::artboardSelected, railWidget, [this, railWidget](const QString &path) {
                    if (!this->batch.files.contains(path)) return;
                    selectFile(path);
                    for (int i = 0; i < railWidget->count(); ++i)
                        if (railWidget->item(i)->data(Qt::UserRole).toString() == path) { railWidget->setCurrentRow(i); break; }
                });
                QObject::connect(batchNavigator, &QSlider::valueChanged, railWidget, [this, railWidget](int index) {
                    if (index < 0 || index >= railWidget->count()) return;
                    railWidget->setCurrentRow(index);
                    railWidget->scrollToItem(railWidget->item(index), QAbstractItemView::EnsureVisible);
                });

                QObject::connect(backBtn, &QToolButton::clicked, [populateRailPtr]() {
                    if (populateRailPtr) (*populateRailPtr)();
                });

                // Call once to initialize
                (*populateRailPtr)();

                // =============================================================
                // 3. ACTION STRIP
                // =============================================================
                auto *actionRow = new QHBoxLayout;
                actionRow->setSpacing(8);
                actionRow->setContentsMargins(0, 4, 0, 0);
                actionRow->addStretch();

                auto *maskBtn = new QToolButton; maskBtn->setIcon(settingIcon("mask")); maskBtn->setToolTip("Add Layer Mask");
                auto *adjBtn = new QToolButton; adjBtn->setIcon(whiteIcon("C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/editor_adjustments.png")); adjBtn->setToolTip("New Adjustment");
                auto *grpBtn = new QToolButton; grpBtn->setIcon(settingIcon("archive")); grpBtn->setToolTip("New Folder/Group");
                auto *newLayerBtn = new QToolButton; newLayerBtn->setIcon(whiteIcon("C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/add layer.png")); newLayerBtn->setToolTip("New Layer / Artboard");
                auto *delBtn = new QToolButton; delBtn->setIcon(whiteIcon("C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/delete anchor point tool.png")); delBtn->setToolTip("Delete");

                for (auto *btn : {maskBtn, adjBtn, grpBtn, newLayerBtn, delBtn}) {
                    btn->setFixedSize(28, 28);
                    btn->setIconSize(QSize(15, 15));
                    btn->setStyleSheet("QToolButton { background: #000000; border: 1px solid #2d2d35; border-radius: 6px; } QToolButton:hover { background: #111114; border-color: #3ddcff; }");
                    actionRow->addWidget(btn);
                }
                settingCardLayout->addLayout(actionRow);

                QObject::connect(newLayerBtn, &QToolButton::clicked, this, [this, stackWidget, railWidget, detailRowList, populateRailPtr, populateLayersPtr]() {
                    if (stackWidget->currentIndex() == 0) {
                        // Artboards Mode: Create Artboard & ensure rightward appending
                        QString artboardId = "aspectra://artboard-" + QUuid::createUuid().toString(QUuid::Id128).left(8);
                        CanvasLayer layer;
                        layer.source = artboardId;
                        layer.name = QString("Artboard %1").arg(this->batch.files.size() + 1);
                        layer.nativeSize = QSize(widthInput->value(), heightInput->value());
                        canvasLayers[artboardId] = layer;
                        
                        this->batch.files.append(artboardId);
                        updateBatchLabel();
                        
                        if (populateRailPtr) (*populateRailPtr)();
                        selectFile(artboardId);
                        
                        if (railWidget->count() > 0) {
                            railWidget->setCurrentRow(railWidget->count() - 1);
                            railWidget->scrollToItem(railWidget->item(railWidget->count() - 1), QAbstractItemView::EnsureVisible);
                        }
                        status->setText("New artboard added");
                        autosaveProject();
                    } else {
                        // Layers Mode: Create timeline track owned by current artboard
                        if (currentFile.isEmpty()) return;
                        TimelineTrack track;
                        track.type = TimelineTrack::Image;
                        track.artboardSource = currentFile; 
                        track.name = QString("Layer %1").arg(timelineTracks.size() + 1);
                        track.image = QImage(widthInput->value(), heightInput->value(), QImage::Format_ARGB32);
                        track.image.fill(Qt::transparent);
                        track.start = 0;
                        track.end = qMax(5., media.duration);
                        timelineTracks.append(track);
                        updateTrackPanel();
                        refresh();
                        if (populateLayersPtr) (*populateLayersPtr)(currentFile);
                        detailRowList->setCurrentRow(detailRowList->count() - 1);
                        status->setText("New layer added");
                    }
                });

                QObject::connect(delBtn, &QToolButton::clicked, this, [this, stackWidget, detailRowList, railWidget, populateRailPtr, populateLayersPtr]() {
                    if (stackWidget->currentIndex() == 1) {
                        // Delete Layer Track
                        auto *selected = detailRowList->currentItem();
                        if (selected && selected->data(Qt::UserRole).toString() == "track") {
                            int idx = selected->data(Qt::UserRole + 2).toInt();
                            if (idx >= 0 && idx < timelineTracks.size()) {
                                timelineTracks.removeAt(idx);
                                updateTrackPanel();
                                refresh();
                                if (populateLayersPtr) (*populateLayersPtr)(currentFile);
                                status->setText("Layer deleted");
                            }
                        }
                    } else {
                        // Delete Artboard
                        auto *selected = railWidget->currentItem();
                        if (selected) {
                            QString path = selected->data(Qt::UserRole).toString();
                            if (!path.isEmpty()) {
                                int removedIndex = this->batch.files.indexOf(path);
                                this->batch.files.removeOne(path);
                                canvasLayers.remove(path);
                                timelineTracks.erase(std::remove_if(timelineTracks.begin(), timelineTracks.end(), [&path](const TimelineTrack &track) { return track.artboardSource == path; }), timelineTracks.end());
                                updateBatchLabel();
                                updateTrackPanel();
                                
                                if (currentFile == path) {
                                    if (!this->batch.files.isEmpty()) {
                                        int nextIndex = qBound(0, removedIndex, int(this->batch.files.size()) - 1);
                                        selectFile(this->batch.files[nextIndex]);
                                    } else {
                                        clearMedia();
                                    }
                                }
                            }
                            if (populateRailPtr) (*populateRailPtr)();
                            status->setText("Artboard removed");
                        }
                    }
                });

                QObject::connect(maskBtn, &QToolButton::clicked, this, [this, stackWidget, populateLayersPtr]() {
                    if (currentFile.isEmpty() || original.isNull()) return;
                    auto &layer = canvasLayers[currentFile];
                    QSize sz = layer.nativeSize.isValid() ? layer.nativeSize : QSize(widthInput->value(), heightInput->value());
                    layer.mask = QImage(sz, QImage::Format_Grayscale8);
                    layer.mask.fill(255);
                    preview->setLayerMask(layer.mask);
                    if (stackWidget->currentIndex() == 1 && populateLayersPtr) (*populateLayersPtr)(currentFile);
                    status->setText("Layer mask created");
                });

                QObject::connect(adjBtn, &QToolButton::clicked, this, [this]() {
                    if (currentFile.isEmpty() || !canvasLayers.contains(currentFile)) return;
                    canvasLayers[currentFile].hasOverride = true;
                    canvasLayers[currentFile].overrideAdjustments = adjustments();
                    if (perImageOverride) perImageOverride->setChecked(true);
                    status->setText("Artboard adjustments enabled");
                });

                QObject::connect(grpBtn, &QToolButton::clicked, this, [this, stackWidget, populateLayersPtr]() {
                    if (currentFile.isEmpty()) return;
                    TimelineTrack track;
                    track.type = TimelineTrack::Effect;
                    track.isGroup = true;
                    track.artboardSource = currentFile;
                    track.name = QString("Group %1").arg(timelineTracks.size() + 1);
                    timelineTracks.append(track);
                    updateTrackPanel();
                    refresh();
                    if (stackWidget->currentIndex() == 1 && populateLayersPtr) (*populateLayersPtr)(currentFile);
                    status->setText("New group folder created");
                });

                settingCard->show();
                tabs->hide();
                settingsHost->setMinimumHeight(qMin(340, settingsHost->layout()->sizeHint().height()));
                settingsHost->updateGeometry();
                if (auto *outer = qobject_cast<QVBoxLayout*>(settingsHost->parentWidget()->layout())) {
                    outer->invalidate();
                    outer->activate();
                }
                return;
            }

            // =================================================================
            // STANDARD SLIDER CARD FALLBACK FOR OTHER BUTTONS
            // =================================================================
            preview->clearArtboards();
            clearLayoutItems(settingCardLayout);
            QSlider *target=qobject_cast<QSlider*>(QApplication::focusWidget());
            if(!target||!tabs->isAncestorOf(target)){
                const auto controls=tabs->currentWidget()->findChildren<QSlider*>();
                target=controls.value(index%qMax(1,controls.size()),nullptr);
            }
            if(target){
                auto *slider=new GradientSlider;
                slider->setRange(target->minimum(),target->maximum());
                slider->setValue(target->value());
                slider->setTitle(name);
                slider->setIcon(settingIcon(name));
                slider->setToolTip(target->toolTip());
                slider->setObjectName("ActiveSettingSlider");
                slider->setFixedHeight(48);
                settingCardLayout->addWidget(slider,1);
                connect(slider,&QSlider::valueChanged,target,&QSlider::setValue);
                connect(target,&QSlider::valueChanged,slider,[slider](int v){QSignalBlocker block(slider);slider->setValue(v);});
                settingCard->show();
                tabs->hide();
                slider->setFocus(Qt::OtherFocusReason);
            }else{
                settingCard->hide();
                tabs->show();
            }

            settingsHost->setMinimumHeight(qMin(320,settingsHost->layout()->sizeHint().height()));
            settingsHost->updateGeometry();
            if(auto *outer=qobject_cast<QVBoxLayout*>(settingsHost->parentWidget()->layout())){outer->invalidate();outer->activate();}
        });
    });
    connect(tabs,&QTabWidget::currentChanged,this,[subButtons](int){QTimer::singleShot(0,subButtons,[subButtons]{if(auto *first=subButtons->button(0))first->click();});});
    connect(subButtons,&QButtonGroup::idClicked,this,[this,settingsHost,settingCard,settingCardLayout,subNames](int index){
        if(!tabs||tabs->currentIndex()!=6||subNames.value(6).value(index)!="Sphere")return;
        QTimer::singleShot(1,this,[this,settingsHost,settingCard,settingCardLayout]{
            clearLayoutItems(settingCardLayout);
            auto addSphereLight=[settingCardLayout](const QString &name,QSlider *target){
                if(!target)return;
                auto *control=new GradientSlider;
                control->setObjectName("ActiveSettingSlider");
                control->setRange(target->minimum(),target->maximum());
                control->setValue(target->value());
                control->setTitle(name);
                control->setIcon(settingIcon("light"));
                control->setFixedHeight(48);
                control->setToolTip(target->toolTip());
                settingCardLayout->addWidget(control);
                connect(control,&QSlider::valueChanged,target,&QSlider::setValue);
                connect(target,&QSlider::valueChanged,control,[control](int value){QSignalBlocker block(control);control->setValue(value);});
            };
            addSphereLight("Azimuth",lightAzimuth);
            addSphereLight("Elevation",lightElevation);
            addSphereLight("Intensity",lightIntensity);
            settingCard->show();
            settingsHost->setMinimumHeight(qMin(320,settingsHost->layout()->sizeHint().height()));
            settingsHost->updateGeometry();
        });
    });
    connect(subButtons,&QButtonGroup::idClicked,this,[this,subNames,settingCardLayout](int index){if(!tabs||tabs->currentIndex()!=6)return;const QString name=subNames.value(6).value(index);if(auto *control=textureMapSliders.value(name,nullptr)){control->setFocus(Qt::TabFocusReason);control->ensurePolished();if(textureMapPreview)textureMapPreview->setCurrentText(name);textureRefreshTimer.start();}});
    connect(subButtons,&QButtonGroup::idClicked,this,[subButtons](int index){QTimer::singleShot(0,subButtons,[subButtons,index]{auto *icon=static_cast<SettingRailButton*>(subButtons->button(index));auto *target=qobject_cast<QSlider*>(QApplication::focusWidget());if(!icon||!target)return;icon->setLiveRange(target->minimum(),target->maximum());icon->setLiveValue(target->value());QObject::connect(target,&QSlider::valueChanged,icon,[icon](int value){icon->setLiveValue(value);});});});
    connect(tabs,&QTabWidget::currentChanged,this,[subRail,subHost,subButtons](int category){
        const int count=subButtons->buttons().size();
        const int contentWidth=16+count*38+qMax(0,count-1)*8;
        subHost->setMinimumWidth(contentWidth);
        subHost->resize(qMax(contentWidth,subRail->viewport()->width()),subRail->viewport()->height());
        for(auto *button:subButtons->buttons())button->setProperty("textureMode",category==6);
    });
    subRail->hide();
    connect(tabButtons,&QButtonGroup::idClicked,this,[subRail,subButtons]{
        subRail->show();
        QTimer::singleShot(0,subButtons,[subButtons]{if(auto *first=subButtons->button(0))first->click();});
    });
    auto collapseSettings=[this,settingsHost,settingsExpanded,subRail,settingCard]{
        if(!*settingsExpanded)return;
        *settingsExpanded=false;
        settingsHost->setProperty("settingsOpen",false);
        tabs->hide();
        subRail->hide();
        settingCard->hide();
        settingsHost->setMinimumHeight(0);
        settingsHost->hide();
        if(!preview->parentWidget()||preview->parentWidget()->objectName()!="shell")preview->setViewZoom(1.0);
    };
    connect(preview,&PreviewWidget::canvasTapped,this,collapseSettings);
    connect(preview,&PreviewWidget::importRequested,this,&MainWindow::chooseFiles);
    auto *tapRelay=new TapRelay(collapseSettings,this);
    mainPreview->installEventFilter(tapRelay);
    stageHost->installEventFilter(tapRelay);
    carouselScroll->viewport()->installEventFilter(tapRelay);
    settingsHost->installEventFilter(tapRelay);
    auto bindTool=[this,tabButtons,showSettings](const QKeySequence &shortcut,int index){auto *key=new QShortcut(shortcut,this);connect(key,&QShortcut::activated,this,[this,tabButtons,showSettings,index]{tabButtons->button(index)->setChecked(true);tabs->setCurrentIndex(index);showSettings();});};bindTool(QKeySequence("C"),0);bindTool(QKeySequence("A"),1);bindTool(QKeySequence("S"),2);bindTool(QKeySequence("T"),3);bindTool(QKeySequence("Ctrl+E"),1);auto *previousKey=new QShortcut(QKeySequence(Qt::Key_Left),this);connect(previousKey,&QShortcut::activated,this,[this]{navigate(-1);});auto *nextKey=new QShortcut(QKeySequence(Qt::Key_Right),this);connect(nextKey,&QShortcut::activated,this,[this]{navigate(1);});
    // Internal visual-regression state. Normal launches ignore these flags; they make it
    // possible to capture the same native UI states used in the specification document.
    const QString shotArg=QCoreApplication::arguments().filter(QRegularExpression("^--ui-shot=")).value(0);const QString shotName=shotArg.section('=',1);const QStringList shotPages{"canvas","adjust","select","trace","effects","video","texture","pattern","export"};const int shotPage=shotPages.indexOf(shotName);if(shotPage>=0)QTimer::singleShot(0,this,[this,shotPage]{tabs->setCurrentIndex(shotPage);});
    // The bottom icon rails have no card or panel surface beneath them.
    bottomDock=new QWidget(shell);
    bottomDock->setObjectName("BottomRailsHost");
    bottomDock->setStyleSheet("QWidget#BottomRailsHost{background:transparent;border:none;}");
    bottomDock->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    auto *bottomLayout=new QVBoxLayout(bottomDock);
    bottomLayout->setContentsMargins(0,0,0,0);
    bottomLayout->setSpacing(1);
    batchLabel=label("No media imported","muted");batchLabel->setParent(bottomDock);batchLabel->hide();
    status=label("Ready · all adjustments are live","muted");status->setParent(bottomDock);status->hide();
    thumbnail=new QLabel(bottomDock);thumbnail->hide();
    info=label("No media","muted");info->setParent(bottomDock);info->hide();
    const QString panelIconRoot="C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/";
    auto buildBottomRail=[&](const QString &objectName){auto *rail=new QScrollArea(bottomDock);rail->setObjectName(objectName);rail->setFixedHeight(objectName=="BottomToolbarRail"?42:50);rail->setFrameShape(QFrame::NoFrame);rail->setWidgetResizable(false);rail->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);rail->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);rail->setStyleSheet("QScrollArea{background:transparent;border:0;}");auto *host=new QWidget(rail);host->setObjectName(objectName+"Content");auto *railLayout=new QHBoxLayout(host);railLayout->setContentsMargins(0,0,0,0);railLayout->setSpacing(8);rail->setWidget(host);rail->viewport()->installEventFilter(this);host->installEventFilter(this);return qMakePair(rail,railLayout);};auto bottomTools=buildBottomRail("BottomToolbarRail");auto bottomModes=buildBottomRail("BottomModeRail");auto addBottomRailButton=[&](QHBoxLayout *railLayout,const QString &icon,const QString &tip){auto *control=new FluidToolButton(railLayout->parentWidget());control->setIcon(whiteIcon(panelIconRoot+icon));control->setIconSize(QSize(19,19));control->setFixedSize(38,38);control->setCheckable(true);control->setToolTip(tip);control->installEventFilter(this);railLayout->addWidget(control);return control;};const QList<QPair<CanvasTool,QPair<QString,QString>>> bottomToolDefs{{CanvasTool::Move,{"move tool.png","Move"}},{CanvasTool::ObjectSelect,{"object selection.png","Object Selection"}},{CanvasTool::RectangularMarquee,{"rectangle marquee tool.png","Rectangular Marquee"}},{CanvasTool::EllipticalMarquee,{"circle marquee tool.png","Elliptical Marquee"}},{CanvasTool::Lasso,{"lasso tool.png","Lasso"}},{CanvasTool::MagicWand,{"magic selection.png","Magic Wand"}},{CanvasTool::Crop,{"crop tool.png","Crop"}},{CanvasTool::SpotHeal,{"spot healing tool_.png","Spot Healing"}},{CanvasTool::HealingBrush,{"healing tool.png","Healing Brush"}},{CanvasTool::Patch,{"patch tool.png","Patch"}},{CanvasTool::CloneStamp,{"stamp tool.png","Clone Stamp · Alt-click samples"}},{CanvasTool::Brush,{"brush tool.png","Brush"}},{CanvasTool::Pencil,{"pencil tool.png","Pencil"}},{CanvasTool::Eraser,{"eraser tool.png","Eraser"}},{CanvasTool::Gradient,{"gradient tool.png","Gradient"}},{CanvasTool::Blur,{"blur tool.png","Blur"}},{CanvasTool::Sharpen,{"sharpen tool.png","Sharpen"}},{CanvasTool::Smudge,{"mixer brush tool.png","Smudge"}},{CanvasTool::Dodge,{"dodge tool.png","Dodge"}},{CanvasTool::Burn,{"burn tool.png","Burn"}},{CanvasTool::Sponge,{"sponge tool.png","Sponge"}},{CanvasTool::Pen,{"pen tool.png","Pen"}},{CanvasTool::Type,{"horizontal type tool.png","Type"}}};for(const auto &tool:bottomToolDefs){auto *control=addBottomRailButton(bottomTools.second,tool.second.first,tool.second.second);connect(control,&QToolButton::clicked,this,[this,tool,bottomTools,control]{for(auto *peer:bottomTools.first->findChildren<QToolButton*>())peer->setChecked(peer==control);if(tool.first==CanvasTool::Type){if(currentFile.isEmpty()||original.isNull()){showError("Import media before placing text.");return;}preview->setCanvasTool(CanvasTool::Type);status->setText("Type tool active · click the preview to place text");return;}preview->setCanvasTool(tool.first,maskBrushSize?maskBrushSize->value():48);status->setText(tool.second.second+" active");});}const QList<QPair<QString,QString>> bottomModeDefs{{"icon_canvas.png","Canvas"},{"editor_adjustments.png","Adjust"},{"quick selection.png","Select"},{"editor_trace.png","Trace"},{"editor_effects.png","Effects"},{"video.png","Video"},{"icon_textures.png","Texture"},{"icon_patterns.png","Pattern"},{"export.png","Export"}};for(const auto &mode:bottomModeDefs){addBottomRailButton(bottomModes.second,mode.first,mode.second);}
    for(auto *modeButton:bottomModes.first->findChildren<QToolButton*>()){
        const QString modeName=modeButton->toolTip();
        connect(modeButton,&QToolButton::clicked,this,[this,modeName,bottomModes]{
            for(auto *peer:bottomModes.first->findChildren<QToolButton*>())peer->setChecked(peer->toolTip()==modeName);
            for(auto *carouselButton:tabCarousel->findChildren<QToolButton*>("CarouselTab"))
                if(carouselButton->toolTip()==modeName+" settings"){carouselButton->click();return;}
            status->setText(modeName+" panel is unavailable");
        });
    }
    auto *panelRail=new QWidget(bottomDock);
    panelRail->setObjectName("BottomPanelRail");
    auto *panelLayout=new QHBoxLayout(panelRail);
    panelLayout->setContentsMargins(0,0,0,0);
    panelLayout->setSpacing(10);
    panelLayout->addStretch();

    for(const auto &entry:QList<QPair<QString,QString>>{
        {"shape layer.png", "Layers"},
        {"icon_color_.png", "Color"},
        {"horizontal type tool.png", "Character"},
        {"panel settings.png", "Properties"},
        {"editor_adjustments.png", "Adjustments"}
    }){
        auto *panel=new FluidToolButton(panelRail);
        panel->setIcon(whiteIcon(panelIconRoot+entry.first));
        panel->setIconSize(QSize(19,19));
        panel->setFixedSize(38,38);
        panel->setToolTip(entry.second);
        panelLayout->addWidget(panel);
        
        connect(panel,&QToolButton::clicked,this,[this,name=entry.second,showSettings,subButtons]{
            if(name == "Layers") {
                tabs->setCurrentIndex(0);
                showSettings();
                QTimer::singleShot(1,this,[subButtons]{if(auto *layer=subButtons->button(3))layer->click();});
            } else {
                const QString target = name=="Color"||name=="Adjustments" ? "Adjust" : "Canvas";
                for(auto *tab : tabCarousel->findChildren<QToolButton*>("CarouselTab"))
                    if(tab->toolTip() == target + " settings") { tab->click(); break; }
            }
        });
    }
    panelLayout->addStretch();
    bottomTools.first->setFixedHeight(BottomRailMetrics::toolbarHeight);
    bottomModes.first->setFixedHeight(BottomRailMetrics::modesHeight);
    for(auto *rail:{bottomTools.first,bottomModes.first}){
        rail->widget()->adjustSize();
        bottomLayout->addWidget(rail);
    }
    panelRail->setFixedHeight(BottomRailMetrics::panelsHeight);
    bottomLayout->addWidget(panelRail);
    bottomDock->setFixedHeight(BottomRailMetrics::toolbarHeight+BottomRailMetrics::modesHeight+BottomRailMetrics::panelsHeight+2*BottomRailMetrics::gap);
    layout->addWidget(bottomDock,0);
    layout->addWidget(settingsHost,0);
    layout->setContentsMargins(16,12,16,12);
    layout->setStretch(layout->indexOf(stageHost),1);
    preview->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    stageHost->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    if(auto *legacy=findChild<QScrollArea*>("MainToolRail"))legacy->hide();
    tabCarousel->hide();
    if(auto *sub=findChild<QScrollArea*>("SubSettingsRail"))sub->hide();
}
QWidget *MainWindow::framePanel(){
    auto *page=new QWidget;auto *v=new QVBoxLayout(page);v->setContentsMargins(12,12,12,12);v->setSpacing(8);auto *r=row(v);r->addWidget(label("ASPECT & DIMENSIONS","section"));r->addStretch();lock=new QCheckBox("Lock ratio");lock->setChecked(true);r->addWidget(lock);ratio=new QComboBox;ratio->addItems({"Source","1:1","16:9","9:16","4:3","Custom…"});ratio->setCurrentIndex(1);r->addWidget(ratio);connect(ratio,&QComboBox::currentIndexChanged,this,&MainWindow::aspectChanged);connect(lock,&QCheckBox::toggled,this,[this](bool on){if(on)lockedRatio=double(widthInput->value())/heightInput->value();});
    r=row(v);r->addWidget(label("Width"));widthInput=number(512);r->addWidget(widthInput);r->addWidget(label("×"));heightInput=number(512);r->addWidget(heightInput);r->addWidget(label("px","muted"));
    connect(widthInput,&QSpinBox::valueChanged,this,[this](int n){if(changing)return;changing=true;if(lock->isChecked())heightInput->setValue(qRound(n/lockedRatio));changing=false;refresh();});connect(heightInput,&QSpinBox::valueChanged,this,[this](int n){if(changing)return;changing=true;if(lock->isChecked())widthInput->setValue(qRound(n*lockedRatio));changing=false;refresh();});
    r=row(v,5);for(int n:{128,256,512,1024,2048}){auto *b=button(QString::number(n),r);connect(b,&QPushButton::clicked,this,[this,n]{setDimensions(n,lock->isChecked()?qRound(n/lockedRatio):n);});}
    r=row(v,7);r->addWidget(label("CANVAS SCALE","muted"));auto *canvasScale=new GradientSlider;canvasScale->setRange(10,400);canvasScale->setValue(100);canvasScale->setToolTip("Resizes the output canvas live. The current canvas dimensions become the 100% reference when you start dragging.");r->addWidget(canvasScale,1);auto *canvasScaleValue=label("100%");canvasScaleValue->setFixedWidth(40);r->addWidget(canvasScaleValue);connect(canvasScale,&QSlider::sliderPressed,this,[this,canvasScale]{canvasScale->setProperty("baseWidth",widthInput->value());canvasScale->setProperty("baseHeight",heightInput->value());});connect(canvasScale,&QSlider::valueChanged,this,[this,canvasScale,canvasScaleValue](int value){canvasScaleValue->setText(QString::number(value)+"%");const int baseWidth=canvasScale->property("baseWidth").toInt();const int baseHeight=canvasScale->property("baseHeight").toInt();if(baseWidth>0&&baseHeight>0)setDimensions(qMax(1,qRound(baseWidth*value/100.0)),qMax(1,qRound(baseHeight*value/100.0)));});
    r=row(v);r->addWidget(label("SAFE REGION & PADDING","section"));r->addStretch();guides=new QCheckBox("Show guides");guides->setChecked(true);r->addWidget(guides);connect(guides,&QCheckBox::toggled,this,&MainWindow::refresh);
    auto *grid=new QGridLayout;v->addLayout(grid);grid->setHorizontalSpacing(5);QStringList sides{"Left","Top","Right","Bottom"};for(int i=0;i<4;++i)grid->addWidget(label(sides[i],"muted"),0,i+1);
    for(int kind=0;kind<2;++kind){grid->addWidget(label(kind?"Padding":"Safe margin"),kind+1,0);for(int i=0;i<4;++i){auto *n=new QDoubleSpinBox;n->setRange(0,45);n->setDecimals(1);n->setSuffix("%");n->setButtonSymbols(QAbstractSpinBox::NoButtons);n->setValue(kind?0:10);grid->addWidget(n,kind+1,i+1);(kind?paddingInputs:safeInputs)<<n;connect(n,&QDoubleSpinBox::valueChanged,this,&MainWindow::refresh);}}
    auto *placement=new QHBoxLayout;placement->addWidget(label("LAYER POSITION ON CANVAS","section"));placement->addStretch();placement->addWidget(label("X"));canvasX=new QSpinBox;canvasX->setRange(-8192,8192);placement->addWidget(canvasX);placement->addWidget(label("Y"));canvasY=new QSpinBox;canvasY->setRange(-8192,8192);placement->addWidget(canvasY);v->addLayout(placement);auto savePosition=[this]{if(!currentFile.isEmpty()&&canvasLayers.contains(currentFile)){canvasLayers[currentFile].position=QPointF(double(canvasX->value()),double(canvasY->value()));status->setText("Layer placement saved");}};connect(canvasX,qOverload<int>(&QSpinBox::valueChanged),this,[savePosition](int){savePosition();});connect(canvasY,qOverload<int>(&QSpinBox::valueChanged),this,[savePosition](int){savePosition();});auto *maskRow=new QHBoxLayout;maskRow->addWidget(label("LAYER MASK","section"));auto *makeMask=new QPushButton("Create from key");auto *paintAdd=new QPushButton("Paint add");auto *paintRemove=new QPushButton("Paint remove");auto *clearMask=new QPushButton("Clear mask");maskRow->addWidget(makeMask);maskRow->addWidget(paintAdd);maskRow->addWidget(paintRemove);maskRow->addWidget(clearMask);maskRow->addWidget(label("Brush"));maskBrushSize=new QSpinBox;maskBrushSize->setRange(1,500);maskBrushSize->setValue(48);maskBrushSize->setToolTip("Mask brush diameter in preview pixels.");maskRow->addWidget(maskBrushSize);v->addLayout(maskRow);connect(makeMask,&QPushButton::clicked,this,[this]{if(currentFile.isEmpty()||original.isNull())return;auto &layer=canvasLayers[currentFile];layer.mask=QImage(original.size(),QImage::Format_Grayscale8);QColor key=layerStyle.keyColor;for(int y=0;y<original.height();++y)for(int x=0;x<original.width();++x){QColor p=original.pixelColor(x,y);double d=std::sqrt(std::pow((p.red()-key.red())/255.,2)+std::pow((p.green()-key.green())/255.,2)+std::pow((p.blue()-key.blue())/255.,2));layer.mask.setPixel(x,y,qBound(0,qRound((d-.18)/.08*255),255));}preview->setLayerMask(layer.mask);status->setText("Editable layer mask created from key");});connect(paintAdd,&QPushButton::clicked,this,[this]{preview->setMaskBrush(true,maskBrushSize->value());status->setText("Mask paint add · drag on the main preview");});connect(paintRemove,&QPushButton::clicked,this,[this]{preview->setMaskBrush(false,maskBrushSize->value());status->setText("Mask paint remove · drag on the main preview");});connect(clearMask,&QPushButton::clicked,this,[this]{if(canvasLayers.contains(currentFile)){canvasLayers[currentFile].mask={};preview->setLayerMask({});status->setText("Layer mask cleared");}});auto *note=label("Green = safe · Red = outside safe. Drag green edges to adjust.\nMask paint works directly on the main preview. Padding exports; guide markers do not.","muted");note->setWordWrap(true);v->addWidget(note);v->addStretch();return page;
}
QWidget *MainWindow::makeAdjustTab(const QStringList &names,const QStringList &keys,const QVector<int>&minimum,const QVector<int>&maximum,const QVector<int>&values){
    auto *page=new QWidget;auto *v=new QVBoxLayout(page);v->setContentsMargins(12,10,12,10);v->setSpacing(7);
    for(int i=0;i<names.size();++i){auto *r=row(v,6);auto *name=label(names[i]);name->setFixedWidth(104);r->addWidget(name);auto *slider=new GradientSlider;slider->setRange(minimum[i],maximum[i]);slider->setValue(values[i]);r->addWidget(slider,1);sliders[keys[i]]=slider;auto *value=label(QString::number(values[i]));value->setObjectName("SliderValue");value->setFixedWidth(52);value->setAlignment(Qt::AlignRight|Qt::AlignVCenter);r->addWidget(value);connect(slider,&QSlider::valueChanged,value,[value](int n){value->setText(QString::number(n));});connect(slider,&QSlider::valueChanged,this,&MainWindow::refresh);}
    v->addStretch();return scrolled(page);
}
QWidget *MainWindow::colorPanel(){
    auto *panel=new QTabWidget;panel->addTab(makeAdjustTab({"Hue °","Saturation %","Brightness %","Contrast %","Exposure ×100"},{"hue","saturation","brightness","contrast","exposure"},{-180,0,-100,0,-400},{180,200,100,200,400},{0,100,0,100,0}),"Basic");
    panel->addTab(makeAdjustTab({"Black point","Gamma ×100","White point"},{"blacks","gamma","whites"},{0,10,1},{254,300,255},{0,100,255}),"Levels");
    panel->addTab(makeAdjustTab({"Cyan ↔ Red","Magenta ↔ Green","Yellow ↔ Blue"},{"red","green","blue"},{-100,-100,-100},{100,100,100},{0,0,0}),"Balance");
    auto *page=new QWidget;auto *v=new QVBoxLayout(page);flat=new QCheckBox("Flat color / posterize");v->addWidget(flat);connect(flat,&QCheckBox::toggled,this,&MainWindow::refresh);
    for(int i=0;i<2;++i){auto *r=row(v);auto *enabled=new QCheckBox(presets[i].name+" export variant");r->addWidget(enabled);r->addStretch();auto *edit=button("Edit…",r);connect(enabled,&QCheckBox::toggled,this,[this,i](bool on){presets[i].enabled=on;refresh();});connect(edit,&QPushButton::clicked,this,[this,i]{editPreset(i);});}
    auto *r=row(v);r->addWidget(label("Preview variant"));previewVariant=new QComboBox;previewVariant->addItems({"Original","White","Black"});r->addWidget(previewVariant);connect(previewVariant,&QComboBox::currentIndexChanged,this,&MainWindow::refresh);v->addWidget(label("Original + checked variants are exported. Raster output.","muted"));v->addStretch();panel->addTab(page,"Recolor");auto *keyPage=new QWidget;auto *kv=new QVBoxLayout(keyPage);chromaKeyEnabled=new QCheckBox("Enable chroma / color key");kv->addWidget(chromaKeyEnabled);auto *autoKey=new QPushButton("Auto-detect background key");kv->addWidget(autoKey);auto *pick=new QPushButton("Pick key color…");kv->addWidget(pick);auto addKey=[&](QString name,QSlider **slider,int value){auto *r=new QHBoxLayout;r->addWidget(label(name));*slider=new QSlider(Qt::Horizontal);(*slider)->setRange(0,100);(*slider)->setValue(value);r->addWidget(*slider,1);kv->addLayout(r);connect(*slider,&QSlider::valueChanged,this,&MainWindow::refresh);};addKey("Color distance",&keyToleranceSlider,18);addKey("Edge feather",&keySoftnessSlider,8);kv->addWidget(label("Auto-detect samples the frame border (where the subject normally is not) and sets a safe starting key. Fine-tune with the controls after inspecting the matte.","muted"));kv->addStretch();connect(chromaKeyEnabled,&QCheckBox::toggled,this,&MainWindow::refresh);connect(autoKey,&QPushButton::clicked,this,[this]{if(original.isNull())return;QImage image=original.scaled(160,160,Qt::IgnoreAspectRatio,Qt::FastTransformation);qint64 r=0,g=0,b=0,n=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(x<12||y<12||x>=image.width()-12||y>=image.height()-12){QColor c=image.pixelColor(x,y);r+=c.red();g+=c.green();b+=c.blue();++n;}if(n){layerStyle.keyColor=QColor(r/n,g/n,b/n);chromaKeyEnabled->setChecked(true);keyToleranceSlider->setValue(20);keySoftnessSlider->setValue(10);status->setText("Auto key analyzed the image border");refresh();}});connect(pick,&QPushButton::clicked,this,[this]{QColor color=QColorDialog::getColor(layerStyle.keyColor,this,"Pick background color");if(color.isValid()){layerStyle.keyColor=color;refresh();}});panel->addTab(keyPage,"Keying");auto *autoPage=new QWidget;auto *av=new QVBoxLayout(autoPage);auto *autoFix=new QPushButton("Auto color · balance · brightness · contrast · levels");av->addWidget(autoFix);av->addWidget(label("Analyzes the current frame and sets a conservative neutral starting grade. You can still refine every slider manually.","muted"));av->addStretch();connect(autoFix,&QPushButton::clicked,this,[this]{if(original.isNull())return;QImage gray=original.scaled(256,256,Qt::KeepAspectRatio,Qt::FastTransformation).convertToFormat(QImage::Format_Grayscale8);int lo=255,hi=0;double sum=0;for(int y=0;y<gray.height();++y){const uchar *p=gray.constScanLine(y);for(int x=0;x<gray.width();++x){lo=qMin(lo,int(p[x]));hi=qMax(hi,int(p[x]));sum+=p[x];}}double mean=sum/qMax(1,gray.width()*gray.height());sliders["blacks"]->setValue(lo);sliders["whites"]->setValue(qMax(lo+1,hi));sliders["brightness"]->setValue(qBound(-25,qRound((128-mean)/5),25));sliders["contrast"]->setValue(qBound(80,qRound(100+(.55-(hi-lo)/255.)*45),140));sliders["saturation"]->setValue(100);status->setText("Auto color and levels applied");refresh();});panel->addTab(autoPage,"Auto");return panel;
}
QWidget *MainWindow::selectRefinePanel(){
    auto *page=new QWidget;auto *v=new QVBoxLayout(page);v->setContentsMargins(14,14,14,14);v->setSpacing(9);v->addWidget(label("SELECT SUBJECT & REFINE EDGE","section"));auto *intro=label("Create a live subject matte, inspect the marching-ants edge in the stage, then refine once and export every imported asset.","muted");intro->setWordWrap(true);v->addWidget(intro);
    auto makeMask=[](const QImage &source,QColor *background){if(source.isNull())return QImage{};QImage input=source.convertToFormat(QImage::Format_RGBA8888);qint64 r=0,g=0,b=0,n=0;const int band=qMax(2,qMin(input.width(),input.height())/24);for(int y=0;y<input.height();++y)for(int x=0;x<input.width();++x)if(x<band||y<band||x>=input.width()-band||y>=input.height()-band){QColor c=input.pixelColor(x,y);r+=c.red();g+=c.green();b+=c.blue();++n;}QColor key(n?r/n:0,n?g/n:255,n?b/n:0);if(background)*background=key;QImage mask(input.size(),QImage::Format_Grayscale8);for(int y=0;y<input.height();++y)for(int x=0;x<input.width();++x){QColor c=input.pixelColor(x,y);double d=std::sqrt(std::pow((c.red()-key.red())/255.,2)+std::pow((c.green()-key.green())/255.,2)+std::pow((c.blue()-key.blue())/255.,2));mask.setPixel(x,y,qBound(0,qRound((d-.10)/.18*255),255));}return mask;};
    auto *actions=row(v);auto *autoCurrent=button("Auto select subject",actions,"blue");auto *autoAll=button("Auto select all",actions);actions->addStretch();connect(autoCurrent,&QPushButton::clicked,this,[this,makeMask]{if(original.isNull()||currentFile.isEmpty())return;QColor background;auto mask=makeMask(original,&background);canvasLayers[currentFile].mask=mask;preview->setLayerMask(mask);layerStyle.keyColor=background;chromaKeyEnabled->setChecked(true);status->setText("Subject matte created · inspect marching ants and refine edges");refresh();});connect(autoAll,&QPushButton::clicked,this,[this,makeMask]{int selected=0;for(const auto &path:batch.files){if(VideoProcessor::isVideo(path))continue;QColor background;QImage mask=makeMask(ImageProcessor::read(path),&background);if(mask.isNull())continue;canvasLayers[path].mask=mask;++selected;}if(selected&&canvasLayers.contains(currentFile))preview->setLayerMask(canvasLayers[currentFile].mask);status->setText(QString("Auto subject mattes created for %1 still images").arg(selected));});
    auto *brush=row(v);auto *add=button("Brush add",brush);auto *subtract=button("Brush subtract",brush);brush->addWidget(label("Brush size"));auto *brushSize=new QSpinBox;brushSize->setRange(1,500);brushSize->setValue(48);brush->addWidget(brushSize);connect(add,&QPushButton::clicked,this,[this,brushSize]{preview->setMaskBrush(true,brushSize->value());status->setText("Refine selection: brush add");});connect(subtract,&QPushButton::clicked,this,[this,brushSize]{preview->setMaskBrush(false,brushSize->value());status->setText("Refine selection: brush subtract");});
    v->addWidget(label("MATTE & EDGE REFINEMENT","section"));auto control=[this,v](const QString &name,int minimum,int maximum,int value,const QString &tip,std::function<void(int)> apply){auto *r=new QHBoxLayout;r->addWidget(label(name));auto *slider=new GradientSlider;slider->setRange(minimum,maximum);slider->setValue(value);slider->setToolTip(tip);r->addWidget(slider,1);auto *number=new QLabel(QString::number(value));number->setFixedWidth(36);r->addWidget(number);v->addLayout(r);connect(slider,&QSlider::valueChanged,this,[this,number,apply](int n){number->setText(QString::number(n));apply(n);refresh();});};
    control("Contract / expand",-50,50,0,"Negative expands the subject matte; positive contracts it.",[this](int n){layerStyle.keyContract=n/100.;});control("Smooth",0,100,0,"Softens small edge variations before feathering.",[this](int n){layerStyle.keySmooth=n/100.;});control("Feather",0,100,8,"Softens the matte transition.",[this](int n){layerStyle.keySoftness=n/100.;});control("Edge contrast",-100,100,0,"Sharpens or relaxes the alpha transition.",[this](int n){layerStyle.keyEdgeContrast=n/100.;});control("Edge radius",0,100,0,"Extends the analysis radius around the key edge.",[this](int n){layerStyle.keyRadius=n/100.;});control("Density",0,200,100,"Controls overall matte opacity.",[this](int n){layerStyle.keyDensity=n/100.;});control("Shift edge",-100,100,0,"Moves the transparent edge inward or outward.",[this](int n){layerStyle.keyShiftEdge=n/100.;});control("Smart radius",0,100,0,"Additional transition radius for detailed edges such as hair.",[this](int n){layerStyle.keySmartRadius=n/100.;});
    auto *note=label("Automatic selection is a fast border/chroma matte. It is reliable for clean green/blue or flat backgrounds; it is not a semantic AI hair model. Brush refinement is available directly on the stage.","muted");note->setWordWrap(true);v->addWidget(note);v->addStretch();return page;
}
QWidget *MainWindow::stylesPanel(){
    auto *page=new QWidget;auto *v=new QVBoxLayout(page);v->setSpacing(7);v->addWidget(label("LIVE LAYER STYLES · APPLIED TO OUTPUT","section"));auto *r=row(v);r->addWidget(label("Blend mode"));auto *blend=new QComboBox;blend->addItems({"Normal","Multiply","Screen","Overlay"});r->addWidget(blend,1);connect(blend,&QComboBox::currentIndexChanged,this,[this](int mode){layerStyle.blendMode=mode;refresh();});
    auto numeric=[&](QString name,double minimum,double maximum,double value,std::function<void(double)> change){auto *line=row(v);line->addWidget(label(name));auto *n=new QDoubleSpinBox;n->setRange(minimum,maximum);n->setValue(value);n->setSuffix(" %");n->setSingleStep(1);line->addWidget(n);connect(n,&QDoubleSpinBox::valueChanged,this,[this,change](double value){change(value/100.);refresh();});};
    numeric("Layer opacity",0,100,100,[this](double v){layerStyle.opacity=v;});numeric("Effect opacity",0,100,75,[this](double v){layerStyle.effectOpacity=v;});
    const QStringList keys{"overlay","gradient","stroke","shadow","glow","innerShadow","bevel"},names{"Color overlay","Gradient overlay","Outside stroke","Drop shadow","Outer glow","Inner shadow","Bevel / emboss"};
    for(int i=0;i<keys.size();++i){auto *line=row(v);auto *check=new QCheckBox(names[i]);styleChecks[keys[i]]=check;line->addWidget(check,1);connect(check,&QCheckBox::toggled,this,&MainWindow::refresh);
        if(i<5){auto *color=button(i==1?"End color…":"Color…",line);connect(color,&QPushButton::clicked,this,[this,i]{QColor *target=i==0?&layerStyle.overlayColor:i==1?&layerStyle.gradientEnd:i==2?&layerStyle.strokeColor:i==3?&layerStyle.shadowColor:&layerStyle.glowColor;QColor chosen=QColorDialog::getColor(*target,this);if(chosen.isValid()){*target=chosen;refresh();}});}}
    numeric("Stroke width",.1,15,1,[this](double v){layerStyle.strokeSize=v;});numeric("Shadow softness",.1,20,2.5,[this](double v){layerStyle.shadowSize=v;});numeric("Shadow distance",0,20,2.5,[this](double v){layerStyle.shadowDistance=v;});numeric("Glow radius",.1,20,2.5,[this](double v){layerStyle.glowSize=v;});numeric("Bevel size",.1,10,1,[this](double v){layerStyle.bevelSize=v;});v->addWidget(label("Sizes are relative to the canvas. Add padding for outside effects.\nBlend mode applies to color / gradient overlays.","muted"));return page;
}
QWidget *MainWindow::clipsPanel(){
    auto *page=new QWidget;auto *v=new QVBoxLayout(page);v->setContentsMargins(14,14,14,14);v->setSpacing(10);v->addWidget(label("VIDEO WORKSPACE · CLIP REGIONS","section"));auto *note=label("The player timeline beneath the preview controls the active region. Set the in/out points, then add named regions to build an editable export list. Image, texture, pattern, color, mask, and effects tools remain available on every video frame.","muted");note->setWordWrap(true);v->addWidget(note);
    inMarker=seconds();outMarker=seconds();outMarker->setValue(1);inMarker->hide();outMarker->hide();auto *range=row(v,8);auto *inValue=label("Start 00:00.000","SliderValue");auto *markIn=button("Set start",range,"gold");auto *outValue=label("End 00:01.000","SliderValue");auto *markOut=button("Set end",range,"pink");range->addWidget(inValue,1);range->addWidget(markIn);range->addWidget(outValue,1);range->addWidget(markOut);
    auto updateLabels=[this,inValue,outValue]{inValue->setText("Start "+clockText(inMarker->value()));outValue->setText("End "+clockText(outMarker->value()));syncRange();};connect(inMarker,&QDoubleSpinBox::valueChanged,this,updateLabels);connect(outMarker,&QDoubleSpinBox::valueChanged,this,updateLabels);connect(markIn,&QPushButton::clicked,this,[this]{inMarker->setValue(player->position()/1000.);});connect(markOut,&QPushButton::clicked,this,[this]{outMarker->setValue(player->position()/1000.);});
    auto *actions=row(v);auto *add=button("Add clip region",actions,"blue");auto *remove=button("Remove selected",actions);auto *reset=button("Full video",actions);connect(add,&QPushButton::clicked,this,&MainWindow::addClip);connect(remove,&QPushButton::clicked,this,[this]{if(currentFile.isEmpty()||!clipTable->currentIndex().isValid())return;clips[currentFile].removeAt(clipTable->currentRow());updateClipTable();});connect(reset,&QPushButton::clicked,this,[this]{inMarker->setValue(0);outMarker->setValue(media.duration);});
    clipTable=new QTableWidget(0,4,page);clipTable->setHorizontalHeaderLabels({"On","Region","Start","End"});clipTable->horizontalHeader()->setStretchLastSection(true);clipTable->verticalHeader()->hide();clipTable->setSelectionBehavior(QAbstractItemView::SelectRows);clipTable->setMinimumHeight(142);clipTable->setToolTip("Double-click Region to rename it. Active regions are sent to video export.");v->addWidget(clipTable);connect(clipTable,&QTableWidget::cellChanged,this,[this](int row,int column){if(updatingClips||currentFile.isEmpty()||row<0||row>=clips[currentFile].size())return;auto &clip=clips[currentFile][row];if(column==0)clip.enabled=clipTable->item(row,0)->checkState()==Qt::Checked;else if(column==1)clip.name=clipTable->item(row,1)->text();else if(column==2)clip.start=qBound(0.,clipTable->item(row,2)->text().toDouble(),media.duration);else if(column==3)clip.end=qBound(clip.start,clipTable->item(row,3)->text().toDouble(),media.duration);timeline->setClips(clips[currentFile]);});
    v->addWidget(label("COMPOSITION TRACKS","section"));trackList=new QListWidget(page);trackList->setMinimumHeight(112);trackList->setToolTip("Stack order: lower tracks render over higher tracks. Select a track to edit its transform and animate it with keyframes.");v->addWidget(trackList);auto *trackActions=row(v);auto *addText=button("+ Text",trackActions,"blue");auto *addImage=button("+ Image",trackActions);auto *removeTrack=button("Remove",trackActions);auto *addKey=button("◇ Keyframe",trackActions,"gold");auto *transform=row(v);transform->addWidget(label("X"));trackX=number(24,8192);trackX->setMinimum(-8192);transform->addWidget(trackX);transform->addWidget(label("Y"));trackY=number(56,8192);trackY->setMinimum(-8192);transform->addWidget(trackY);transform->addWidget(label("Opacity"));trackOpacity=number(100,100);transform->addWidget(trackOpacity);auto updateTrack=[this]{int index=trackList?trackList->currentRow():-1;if(index<0||index>=timelineTracks.size())return;auto &track=timelineTracks[index];track.position={double(trackX->value()),double(trackY->value())};track.opacity=trackOpacity->value()/100.;refresh();};connect(trackX,qOverload<int>(&QSpinBox::valueChanged),this,[updateTrack](int){updateTrack();});connect(trackY,qOverload<int>(&QSpinBox::valueChanged),this,[updateTrack](int){updateTrack();});connect(trackOpacity,qOverload<int>(&QSpinBox::valueChanged),this,[updateTrack](int){updateTrack();});connect(trackList,&QListWidget::currentRowChanged,this,[this](int){updateTrackPanel();});connect(addText,&QPushButton::clicked,this,[this]{bool ok=false;QString text=QInputDialog::getText(this,"Add text track","Text",QLineEdit::Normal,"Text",&ok);if(!ok||text.isEmpty())return;TimelineTrack track;track.type=TimelineTrack::Text;track.name="Text · "+text.left(20);track.text=text;track.start=0;track.end=qMax(1.,media.duration);timelineTracks<<track;updateTrackPanel();refresh();});connect(addImage,&QPushButton::clicked,this,[this]{QString path=QFileDialog::getOpenFileName(this,"Add image track",{},"Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)");if(path.isEmpty())return;TimelineTrack track;track.type=TimelineTrack::Image;track.name="Image · "+QFileInfo(path).fileName();track.source=path;track.image=ImageProcessor::read(path);track.start=0;track.end=qMax(1.,media.duration);timelineTracks<<track;updateTrackPanel();refresh();});connect(removeTrack,&QPushButton::clicked,this,[this]{int index=trackList?trackList->currentRow():-1;if(index<0||index>=timelineTracks.size())return;timelineTracks.removeAt(index);updateTrackPanel();refresh();});connect(addKey,&QPushButton::clicked,this,[this]{int index=trackList?trackList->currentRow():-1;if(index<0||index>=timelineTracks.size())return;auto &track=timelineTracks[index];double time=player?qMax(0.,player->position()/1000.):0.;track.keyframes<<TimelineKeyframe{time,track.position,track.opacity};std::sort(track.keyframes.begin(),track.keyframes.end(),[](const TimelineKeyframe &a,const TimelineKeyframe &b){return a.time<b.time;});status->setText("Keyframe saved · "+track.name);});v->addStretch();return page;
}
QWidget *MainWindow::patternsPanel(){
    auto *page=new QWidget;auto *v=new QVBoxLayout(page);v->addWidget(label("SEAMLESS TEXTURE · MAIN PREVIEW CHANNEL","section"));auto *note=label("The preview above is the one preview window. This tab sends the live seamless-tile channel there; image arrows still change the source.","muted");note->setWordWrap(true);v->addWidget(note);auto *line=new QHBoxLayout;line->addWidget(label("Edge blend"));patternBlend=new QSlider(Qt::Horizontal);patternBlend->setRange(0,50);patternBlend->setValue(12);line->addWidget(patternBlend,1);patternMirror=new QCheckBox("Mirror alternate tiles");line->addWidget(patternMirror);v->addLayout(line);line=new QHBoxLayout;line->addWidget(label("Local warp"));patternVariation=new QSlider(Qt::Horizontal);patternVariation->setRange(0,35);patternVariation->setValue(12);patternVariation->setToolTip("Changes the amount and size of localized texture displacement per quad.");line->addWidget(patternVariation,1);v->addLayout(line);auto *exportTile=new QPushButton("Export seamless texture…");v->addWidget(exportTile);connect(patternBlend,&QSlider::valueChanged,this,&MainWindow::updatePatternPreview);connect(patternVariation,&QSlider::valueChanged,this,&MainWindow::updatePatternPreview);connect(patternMirror,&QCheckBox::toggled,this,&MainWindow::updatePatternPreview);connect(exportTile,&QPushButton::clicked,this,[this]{if(original.isNull()){showError("Import an image first.");return;}QString path=QFileDialog::getSaveFileName(this,"Export seamless texture","seamless.png","PNG (*.png)");if(!path.isEmpty())original.save(path,"PNG");});v->addStretch();return page;
}
void MainWindow::updatePatternPreview(){if(!preview||original.isNull()||!patternBlend)return;QImage tile=original.scaled(300,300,Qt::KeepAspectRatio,Qt::FastTransformation).convertToFormat(QImage::Format_RGBA8888);int band=qMin(qMin(tile.width(),tile.height())/2,patternBlend->value()*qMin(tile.width(),tile.height())/100);for(int y=0;y<tile.height();++y)for(int x=0;x<tile.width();++x){double edge=qMin(qMin(x,tile.width()-1-x),qMin(y,tile.height()-1-y));if(edge<band&&band){double t=(band-edge)/double(band);QRgb q=tile.pixel(tile.width()-1-x,tile.height()-1-y),p=tile.pixel(x,y);tile.setPixel(x,y,qRgba(qRound(qRed(p)*(1-t*.5)+qRed(q)*t*.5),qRound(qGreen(p)*(1-t*.5)+qGreen(q)*t*.5),qRound(qBlue(p)*(1-t*.5)+qBlue(q)*t*.5),qAlpha(p)));}}QImage grid(tile.width()*2,tile.height()*2,QImage::Format_RGBA8888);QPainter painter(&grid);int amount=patternVariation?patternVariation->value():0;for(int gy=0;gy<2;++gy)for(int gx=0;gx<2;++gx){int index=gy*2+gx;QImage draw(tile.size(),QImage::Format_RGBA8888);for(int py=0;py<tile.height();++py)for(int px=0;px<tile.width();++px){double sx=px,sy=py;for(int patch=0;patch<3;++patch){double dx=px-(45+(index*47+patch*61)%210),dy=py-(40+(index*71+patch*43)%210),f=std::exp(-(dx*dx+dy*dy)/1600.);sx+=f*amount*(patch-1)*.35;sy+=f*amount*((index+patch)%3-1)*.35;}draw.setPixel(px,py,tile.pixel(qBound(0,qRound(sx),tile.width()-1),qBound(0,qRound(sy),tile.height()-1)));}if(patternMirror&&patternMirror->isChecked()&&((gx+gy)&1))draw=draw.mirrored(gx&1,gy&1);painter.drawImage(gx*tile.width(),gy*tile.height(),draw);}preview->setTextureFrame(grid);}
QWidget *MainWindow::outputPanel(){
    auto *page=new QWidget;auto *v=new QVBoxLayout(page);v->setSpacing(8);v->addWidget(label("EXPORT ALL IMPORTED MEDIA","section"));auto *workflow=label("1. Select sizes  ·  2. Select formats  ·  3. Export all", "muted");workflow->setWordWrap(true);v->addWidget(workflow);auto *r=row(v);r->addWidget(label("OUTPUT WIDTHS · HEIGHT FOLLOWS CANVAS","section"));r->addStretch();auto *add=button("+ Size",r,"gold");connect(add,&QPushButton::clicked,this,[this]{addSize();});sizes=new QListWidget;sizes->setFlow(QListView::LeftToRight);sizes->setWrapping(false);sizes->setFixedHeight(42);v->addWidget(sizes);for(int n:{128,256,512})addSize(n);
    v->addWidget(label("EXPORT SEVERAL FORMATS AT ONCE","section"));r=row(v);r->addWidget(label("Images","muted"));for(const auto &format:QStringList{"PNG","JPG","WEBP","TIFF","BMP"}){auto *check=new QCheckBox(format);check->setChecked(format=="PNG");check->setToolTip(format=="WEBP"?"Modern compressed raster with alpha support.":format=="TIFF"?"Lossless high-bit-depth compatible raster.":"Export this raster format.");imageFormats[format]=check;r->addWidget(check);}r->addStretch();r=row(v);r->addWidget(label("Videos","muted"));for(const auto &format:QStringList{"MP4","MOV","WEBM","GIF"}){auto *check=new QCheckBox(format);check->setChecked(format=="MP4");videoFormats[format]=check;r->addWidget(check);}r->addStretch();
    r=row(v);r->addWidget(label("JPEG quality"));quality=number(90,100);r->addWidget(quality);r->addWidget(label("Video CRF"));crf=number(23,51);crf->setMinimum(0);r->addWidget(crf);
    v->addWidget(label("ARCHIVES · OPTIONAL, BESIDE OUTPUT FOLDER","section"));r=row(v);zip=new QCheckBox("ZIP");rar=new QCheckBox("RAR");rar->setEnabled(!VideoProcessor::findRar().isEmpty());rar->setToolTip("RAR requires Rar.exe, configured in Settings.");r->addWidget(zip);r->addWidget(rar);archiveMode=new QComboBox;archiveMode->addItems({"Fast compression","Store only","Maximum compression"});r->addWidget(archiveMode,1);
    r=row(v);r->addWidget(label("Parallel jobs"));workerCount=new QComboBox;workerCount->addItems({"1","2","3","4"});workerCount->setCurrentIndex(1);r->addWidget(workerCount);auto *paths=button("FFmpeg / RAR paths…",r);connect(paths,&QPushButton::clicked,this,&MainWindow::settingsDialog);
    auto *note=label("Folders: output/128/ · output/256/ · output/512/\nClips keep separate names. Archives are off by default for speed.","muted");note->setWordWrap(true);v->addWidget(note);return page;
}

static double textureLevel(QRgb pixel,double black,double white,double gamma){
    double value=qBound(0.,(qGray(pixel)-black)/qMax(1.,white-black),1.);
    return std::pow(value,1./qMax(.1,gamma));
}
static QImage renderTextureMap(const QImage &source,const QString &map,const QMap<QString,int> &strengths,const QMap<QString,QVector<int>> &advanced,double black,double white,double gamma){
    if(source.isNull())return {};
    const QImage input=source.convertToFormat(QImage::Format_RGBA8888);
    const int width=input.width(),height=input.height();
    const double strength=strengths.value(map,100)/100.;
    auto toneAt=[&](int x,int y){return textureLevel(input.pixel(qBound(0,x,width-1),qBound(0,y,height-1)),black,white,gamma);};
    QImage output(input.size(),QImage::Format_RGBA8888);const QVector<int> control=advanced.value(map,{0,100,0,100,0,255,100});
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const QRgb pixel=input.pixel(x,y); const int alpha=qAlpha(pixel);
        const double tone=toneAt(x,y), left=toneAt(x-1,y), right=toneAt(x+1,y), up=toneAt(x,y-1), down=toneAt(x,y+1);
        const int gray=qBound(0,qRound(tone*strength*255),255);
        QRgb result=qRgba(gray,gray,gray,alpha);
        if(map=="Base Color"){
            const double lift=.65+.35*strength;
            result=qRgba(qBound(0,qRound(qRed(pixel)*lift),255),qBound(0,qRound(qGreen(pixel)*lift),255),qBound(0,qRound(qBlue(pixel)*lift),255),alpha);
        }else if(map=="Normal"){
            const double scale=2.2*strength,dx=(right-left)*scale,dy=(down-up)*scale,z=1.; const double length=std::sqrt(dx*dx+dy*dy+z*z);
            result=qRgba(qBound(0,qRound((.5-dx/(2*length))*255),255),qBound(0,qRound((.5-dy/(2*length))*255),255),qBound(0,qRound((.5+z/(2*length))*255),255),alpha);
        }else if(map=="Ambient Occlusion"||map=="Cavity"){
            const double detail=qBound(0.,1.-qAbs((left+right+up+down)/4.-tone)*6.*strength,1.); int c=qRound(detail*255);result=qRgba(c,c,c,alpha);
        }else if(map=="Curvature"){
            int c=qBound(0,qRound((.5+((left+right+up+down)/4.-tone)*8.*strength)*255),255);result=qRgba(c,c,c,alpha);
        }else if(map=="Emission"){
            const double glow=std::pow(tone,qMax(.15,1.8/strength));result=qRgba(qBound(0,qRound(qRed(pixel)*glow),255),qBound(0,qRound(qGreen(pixel)*glow),255),qBound(0,qRound(qBlue(pixel)*glow),255),alpha);
        }else if(map=="Opacity"){
            result=qRgba(gray,gray,gray,qBound(0,qRound(tone*strength*255),255));
        }else if(map=="Glossiness"){
            int c=qBound(0,qRound((1.-tone)*strength*255),255);result=qRgba(c,c,c,alpha);
        }else if(map=="Metallic"||map=="Specular"||map=="Clearcoat"||map=="Sheen"||map=="Anisotropy"||map=="Transmission"||map=="Subsurface"){
            int c=qBound(0,qRound(tone*strength*255),255);result=qRgba(c,c,c,alpha);
        }else if(map=="Clearcoat Roughness"||map=="Sheen Roughness"||map=="Roughness"||map=="Height / Displacement"){
            result=qRgba(gray,gray,gray,alpha);
        }else if(map=="ORM"||map=="MRA"||map=="RMA"){
            const int ao=qBound(0,qRound((1.-qAbs((left+right+up+down)/4.-tone)*6.)*255),255),rough=qBound(0,qRound(tone*255),255),metal=qBound(0,qRound(tone*strength*255),255);
            result=map=="ORM"?qRgba(ao,rough,metal,alpha):map=="MRA"?qRgba(metal,rough,ao,alpha):qRgba(rough,metal,ao,alpha);
        }
        QColor adjusted=QColor::fromRgba(result);double range=qMax(1,control[5]-control[4]);auto adjustChannel=[&](int channel){double value=qBound(0.,(channel-control[4])/range,1.);value=std::pow(value,100./qMax(10,control[6]));value=(value-.5)*(control[3]/100.)+.5+control[2]/100.;return qBound(0,qRound(value*255),255);};
        adjusted.setRed(adjustChannel(adjusted.red()));adjusted.setGreen(adjustChannel(adjusted.green()));adjusted.setBlue(adjustChannel(adjusted.blue()));if(map=="Base Color"){int h=adjusted.hslHue();if(h>=0)adjusted.setHsl((h+control[0]+360)%360,qBound(0,qRound(adjusted.hslSaturation()*control[1]/100.),255),adjusted.lightness(),adjusted.alpha());}output.setPixelColor(x,y,adjusted);
    }
    return output;
}
static QImage renderPbrPreview(const QImage &source,const QMap<QString,int> &strengths,const QMap<QString,QVector<int>> &advanced,double black,double white,double gamma){
    QImage color=renderTextureMap(source,"Base Color",strengths,advanced,black,white,gamma),normal=renderTextureMap(source,"Normal",strengths,advanced,black,white,gamma),roughness=renderTextureMap(source,"Roughness",strengths,advanced,black,white,gamma);
    for(int y=0;y<color.height();++y)for(int x=0;x<color.width();++x){QRgb c=color.pixel(x,y),n=normal.pixel(x,y),r=roughness.pixel(x,y);double light=qBound(.2,(qRed(n)/255.-.5)*.55+(qGreen(n)/255.-.5)*-.35+(qBlue(n)/255.-.5)*.8+.45,1.);double shine=(1.-qRed(r)/255.)*.22;color.setPixel(x,y,qRgba(qBound(0,qRound(qRed(c)*(light+shine)),255),qBound(0,qRound(qGreen(c)*(light+shine)),255),qBound(0,qRound(qBlue(c)*(light+shine)),255),qAlpha(c)));}
    return color;
}
static int writeTextureMaps(const QImage &source,const QString &folder,const QString &stem,const QStringList &maps,const QMap<QString,int> &strengths,const QMap<QString,QVector<int>> &advanced,const QString &format,double black,double white,double gamma,bool materialOnly){
    QDir().mkpath(folder+"/maps");
    QString ext=format.toLower();if(ext=="exr")ext="tiff";QStringList paths;int count=0;for(const auto &map:maps){QString safe=map.toLower().replace(QRegularExpression("[^a-z0-9]+"),"_");QString rel="maps/"+stem+"_"+safe+"."+ext;paths<<rel;if(!materialOnly){QImage out=renderTextureMap(source,map,strengths,advanced,black,white,gamma);if(out.save(QDir(folder).filePath(rel),ext.toLatin1().constData(),95))++count;}}
    if(!materialOnly){QFile manifest(QDir(folder).filePath(stem+".pbr"));if(manifest.open(QIODevice::WriteOnly|QIODevice::Text)){QTextStream t(&manifest);t<<"{\n  \"material\": \""<<stem<<"\",\n  \"maps\": [\n";for(int i=0;i<paths.size();++i)t<<"    { \"name\": \""<<maps[i]<<"\", \"path\": \""<<paths[i]<<"\" }"<<(i+1<paths.size()?",":"")<<"\n";t<<"  ]\n}\n";}}
    return count;
}
static int writeTextureMaps(const QImage &source,const QString &folder,const QString &stem,const QStringList &maps,const QMap<QString,int> &strengths,const QString &format,double black,double white,double gamma,bool materialOnly){return writeTextureMaps(source,folder,stem,maps,strengths,{},format,black,white,gamma,materialOnly);}
QWidget *MainWindow::texturePanel(){
    auto *page=new QWidget;auto *v=new QVBoxLayout(page);v->setContentsMargins(12,10,12,12);v->setSpacing(7);
    v->addWidget(label("PBR TEXTURE MAPS · LIVE SPHERE","section"));
    auto *previewRow=new QHBoxLayout;textureMapPreview=new QComboBox;textureMapPreview->addItems({"Full PBR shaded","Base Color","Normal","Roughness","Metallic","Ambient Occlusion","Height / Displacement","Opacity","Emission","Specular","Glossiness","Cavity","Curvature","Subsurface","Clearcoat","Clearcoat Roughness","Sheen","Sheen Roughness","Anisotropy","Transmission","ORM","MRA","RMA"});textureLighting=new QComboBox;textureLighting->addItems({"Studio","Outdoor","HDRI","Night"});previewRow->addWidget(textureMapPreview,1);previewRow->addWidget(textureLighting);v->addLayout(previewRow);
    textureMapPreview->setToolTip("Choose the generated map used by the live PBR sphere.");textureLighting->setToolTip("Lighting is used by the live PBR sphere.");auto *positionRow=new QHBoxLayout;positionRow->addWidget(label("Image X","muted"));textureOffsetX=new GradientSlider;textureOffsetX->setRange(-100,100);textureOffsetX->setValue(0);textureOffsetX->setToolTip("Move the source image horizontally within the generated texture.");positionRow->addWidget(textureOffsetX,1);positionRow->addWidget(label("Y","muted"));textureOffsetY=new GradientSlider;textureOffsetY->setRange(-100,100);textureOffsetY->setValue(0);textureOffsetY->setToolTip("Move the source image vertically within the generated texture.");positionRow->addWidget(textureOffsetY,1);v->addLayout(positionRow);auto *lightingRow=new QHBoxLayout;auto addMainLight=[this,lightingRow](const QString &name,QSlider **target,int minimum,int maximum,int value){lightingRow->addWidget(label(name,"muted"));*target=new GradientSlider;(*target)->setRange(minimum,maximum);(*target)->setValue(value);(*target)->setToolTip(name+" for the live PBR sphere in the main preview.");lightingRow->addWidget(*target,1);};addMainLight("Azimuth",&lightAzimuth,-180,180,35);addMainLight("Elevation",&lightElevation,-80,80,45);addMainLight("Intensity",&lightIntensity,0,200,100);v->addLayout(lightingRow);
    textureMaps.clear();textureMapSliders.clear();textureMapAdvanced.clear();auto addMaps=[&](const QString &title,const QStringList &names){auto *box=new QGroupBox(title);auto *grid=new QGridLayout(box);grid->setContentsMargins(8,4,8,4);int row=0;for(const auto &name:names){auto *check=new QCheckBox(name);check->setChecked(title=="CORE MAPS"||name=="Height / Displacement");check->setToolTip("Export toggle: checked maps are written by Export all maps. This does not change the preview selection.");auto *slider=new GradientSlider;slider->setRange(0,200);slider->setValue(100);slider->setToolTip("Adjust "+name+" strength for both the live preview and export. 100% is the neutral setting.");auto *value=new QSpinBox;value->setRange(0,200);value->setValue(100);value->setButtonSymbols(QAbstractSpinBox::NoButtons);value->setFixedWidth(45);value->setToolTip(slider->toolTip());auto *tune=new QPushButton("Tune…");tune->setToolTip("Open per-map HSL, levels, brightness, contrast and gamma controls.");textureMaps[name]=check;textureMapSliders[name]=slider;textureMapAdvanced[name]={0,100,0,100,0,255,100};grid->addWidget(check,row,0);grid->addWidget(slider,row,1);grid->addWidget(value,row,2);grid->addWidget(tune,row,3);connect(slider,&QSlider::valueChanged,value,&QSpinBox::setValue);connect(value,qOverload<int>(&QSpinBox::valueChanged),slider,&QSlider::setValue);connect(slider,&QSlider::valueChanged,this,[this,name]{if(textureMapPreview)textureMapPreview->setCurrentText(name);if(tabs&&tabs->currentIndex()==4)textureRefreshTimer.start();});connect(tune,&QPushButton::clicked,this,[this,name]{QDialog dialog(this);dialog.setWindowTitle(name+" map tuning");auto *form=new QFormLayout(&dialog);QStringList labels={"Hue","Saturation","Brightness","Contrast","Black level","White level","Gamma"};QVector<QPair<int,int>> ranges={{-180,180},{0,200},{-100,100},{0,200},{0,254},{1,255},{10,500}};for(int i=0;i<labels.size();++i){auto *control=new QSlider(Qt::Horizontal);control->setRange(ranges[i].first,ranges[i].second);control->setValue(textureMapAdvanced[name][i]);auto *number=new QSpinBox;number->setRange(ranges[i].first,ranges[i].second);number->setValue(control->value());auto *line=new QHBoxLayout;line->addWidget(control,1);line->addWidget(number);form->addRow(labels[i],line);connect(control,&QSlider::valueChanged,number,&QSpinBox::setValue);connect(number,qOverload<int>(&QSpinBox::valueChanged),control,&QSlider::setValue);connect(control,&QSlider::valueChanged,this,[this,name,i](int setting){textureMapAdvanced[name][i]=setting;if(tabs&&tabs->currentIndex()==4)textureRefreshTimer.start();});}auto *done=new QPushButton("Done");form->addRow(done);connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);dialog.exec();});++row;}v->addWidget(box);};
    addMaps("CORE MAPS",{"Base Color","Normal","Roughness","Metallic","Ambient Occlusion"});addMaps("EXTENDED MAPS",{"Height / Displacement","Opacity","Emission","Specular","Glossiness","Cavity","Curvature"});addMaps("ADVANCED MAPS",{"Subsurface","Clearcoat","Clearcoat Roughness","Sheen","Sheen Roughness","Anisotropy","Transmission"});addMaps("PACKED MAPS",{"ORM","MRA","RMA"});for(auto *control:textureMapSliders)connect(control,&QSlider::valueChanged,this,[this](int){textureRefreshTimer.start();});
    v->addWidget(label("GLOBAL BGW LEVELS · LIVE","section"));auto *levels=new QGridLayout;textureBgw.clear();QStringList levelNames={"Black","Gray","White","Gamma"};QStringList levelTips={"Sets the darkest input value mapped to black.","Midpoint reference for level balancing.","Sets the brightest input value mapped to white.","Adjusts midtone response without moving black or white points."};for(int i=0;i<4;++i){levels->addWidget(label(levelNames[i],"muted"),0,i);auto *n=new QDoubleSpinBox;n->setRange(i==3?.1:0,i==3?5:255);n->setValue(i==1?128:i==2?255:i==3?1:0);n->setButtonSymbols(QAbstractSpinBox::NoButtons);n->setToolTip(levelTips[i]);auto *slider=new GradientSlider;slider->setRange(i==3?10:0,i==3?500:255);slider->setValue(i==3?100:n->value());slider->setToolTip(levelTips[i]);textureBgw<<n;levels->addWidget(n,1,i);levels->addWidget(slider,2,i);connect(slider,&QSlider::valueChanged,this,[n,i](int value){QSignalBlocker block(n);n->setValue(i==3?value/100.:value);});connect(n,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[slider,i](double value){QSignalBlocker block(slider);slider->setValue(i==3?qRound(value*100):qRound(value));});connect(n,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(tabs&&tabs->currentIndex()==4)textureRefreshTimer.start();});}v->addLayout(levels);
    auto *flags=new QHBoxLayout;textureAutoNormalize=new QCheckBox("Auto normalize");textureAutoNormalize->setToolTip("Compute Black/White from this image's actual range instead of the Global BGW Levels values, live in the preview and on export.");textureHistogram=new QCheckBox("Histogram");textureHistogram->setToolTip("Overlay a live luminance histogram of the rendered map in the preview corner. Preview only, not exported.");textureShowClipping=new QCheckBox("Show clipping");textureShowClipping->setToolTip("Tint pixels at pure white red and pure black blue in the preview so clipped detail is visible. Preview only, not exported.");for(auto *check:{textureAutoNormalize,textureHistogram,textureShowClipping}){flags->addWidget(check);connect(check,&QCheckBox::toggled,this,&MainWindow::refresh);}flags->addStretch();v->addLayout(flags);
    v->addWidget(label("EXPORT MAPS + MATERIAL","section"));textureFormat=new QComboBox;textureFormat->addItems({"PNG","JPG","EXR","TIFF"});auto *formatRow=new QHBoxLayout;formatRow->addWidget(label("Format","muted"));formatRow->addWidget(textureFormat);auto *exportMaps=button("Export all maps",formatRow,"blue");auto *exportMaterial=button("Export material",formatRow,"gold");v->addLayout(formatRow);auto *archiveRow=new QHBoxLayout;textureZip=new QCheckBox("ZIP");textureZip->setToolTip("Also create a .zip of each texture's output folder after export.");textureRar=new QCheckBox("RAR");textureRar->setEnabled(!VideoProcessor::findRar().isEmpty());textureRar->setToolTip("Also create a .rar of each texture's output folder after export. Requires Rar.exe, configured in Settings.");archiveRow->addWidget(textureZip);archiveRow->addWidget(textureRar);archiveRow->addWidget(label("Each texture gets its own folder","muted"));archiveRow->addStretch();v->addLayout(archiveRow);auto exportTexture=[this,exportMaps,exportMaterial](bool materialOnly){if(batch.files.isEmpty()){showError("Select an image before exporting textures.");return;}QString root=QFileDialog::getExistingDirectory(this,"Choose texture output folder");if(root.isEmpty())return;QStringList selected;QMap<QString,int> strengths;for(auto it=textureMaps.begin();it!=textureMaps.end();++it){strengths[it.key()]=textureMapSliders.value(it.key())->value();if(it.value()->isChecked())selected<<it.key();}if(selected.isEmpty()){showError("Select at least one texture map.");return;}QDialog queue(this);queue.setWindowTitle("Aspectra · Texture Export");queue.setFixedSize(390,420);auto *qv=new QVBoxLayout(&queue);qv->addWidget(label("TEXTURE EXPORT QUEUE","section"));auto *list=new QListWidget;for(const auto &m:selected)list->addItem("[x] "+m);qv->addWidget(list,1);auto *progress=new QProgressBar;progress->setRange(0,100);progress->setValue(0);qv->addWidget(progress);auto *message=label("Rendering…","muted");qv->addWidget(message);queue.show();QApplication::processEvents();std::atomic_bool archiveCancel{false};int count=0;for(const auto &path:batch.files){if(VideoProcessor::isVideo(path))continue;QImage source=ImageProcessor::read(path);if(source.isNull())continue;QString stem=QFileInfo(inputAliases.value(path,path)).completeBaseName();QString folder=QDir(root).filePath(stem);count+=writeTextureMaps(source,folder,stem,selected,strengths,textureFormat->currentText(),textureBgw[0]->value(),textureBgw[2]->value(),textureBgw[3]->value(),materialOnly);if(textureZip&&textureZip->isChecked())ExportManager::zip(folder,folder+".zip",archiveCancel,1);if(textureRar&&textureRar->isChecked()&&!VideoProcessor::findRar().isEmpty()){QProcess rarProcess;rarProcess.setWorkingDirectory(folder);rarProcess.start(VideoProcessor::findRar(),{"a","-r","-idq","-m1",folder+".rar","."});if(rarProcess.waitForStarted())rarProcess.waitForFinished(-1);}}progress->setValue(100);message->setText(QString("Complete · %1 map files written").arg(count));QApplication::processEvents();QThread::msleep(250);queue.close();status->setText(QString("Texture export complete · %1 files").arg(count));};connect(exportMaps,&QPushButton::clicked,this,[exportTexture]{exportTexture(false);});connect(exportMaterial,&QPushButton::clicked,this,[exportTexture]{exportTexture(true);});
    auto *note=label("Select a map above to view it in the main preview. Every slider is live; safe-zone visibility remains controlled from Frame.","muted");note->setWordWrap(true);v->addWidget(note);connect(textureMapPreview,&QComboBox::currentTextChanged,this,[this](const QString &map){status->setText("Texture preview · "+map);refresh();});connect(textureLighting,&QComboBox::currentTextChanged,this,[this]{refresh();});for(auto *control:page->findChildren<QSlider*>())connect(control,&QSlider::valueChanged,this,[this](int){textureRefreshTimer.start();});v->addStretch();return page;
}
QWidget *MainWindow::modelPanel(){auto *page=new QWidget;auto *v=new QVBoxLayout(page);v->setContentsMargins(12,10,12,12);v->setSpacing(8);v->addWidget(label("MODEL MATERIAL WORKSPACE","section"));auto *summary=label("Blender-style material slots: model → material → texture input.","muted");summary->setWordWrap(true);v->addWidget(summary);auto *load=button("Load 3D model…",v,"blue");modelTextureList=new QListWidget;modelTextureList->setObjectName("ModelTextureLayers");modelTextureList->setToolTip("Texture inputs are grouped by their material slot. The first editable texture opens automatically in the live mesh preview.");v->addWidget(modelTextureList,1);auto *transform=new QFormLayout;auto addModelSlider=[this,transform](const QString &name,QSlider **target,int minimum,int maximum,int value,const QString &tip){*target=new GradientSlider;(*target)->setRange(minimum,maximum);(*target)->setValue(value);(*target)->setToolTip(tip);transform->addRow(name,*target);};addModelSlider("Model scale",&modelScale,10,400,100,"Scale the live model preview from 10% to 400%.");addModelSlider("UV scale",&modelUvScale,1,400,100,"Tile the selected material texture from 1% to 400% on the live mesh preview.");addModelSlider("UV X",&modelUvX,-100,100,0,"Offset the selected material texture horizontally on the live mesh preview.");addModelSlider("UV Y",&modelUvY,-100,100,0,"Offset the selected material texture vertically on the live mesh preview.");v->addLayout(transform);auto *actions=new QHBoxLayout;auto *replace=button("Replace texture…",actions,"blue");auto *extract=button("Extract textures…",actions,"gold");v->addLayout(actions);for(auto *control:{modelScale,modelUvScale,modelUvX,modelUvY})connect(control,&QSlider::valueChanged,this,[this](int){if(!modelAsset.sourcePath.isEmpty())refreshTexturePreview();});connect(load,&QPushButton::clicked,this,[this,summary]{const QString path=QFileDialog::getOpenFileName(this,"Load 3D model",QSettings().value("modelFolder",lastFolder).toString(),"3D models (*.obj *.gltf *.glb *.fbx)");if(path.isEmpty())return;QSettings().setValue("modelFolder",QFileInfo(path).absolutePath());QString error;if(!ModelImporter::load(path,modelAsset,error)){showError(error);return;}modelTextureList->clear();for(int i=0;i<modelAsset.textures.size();++i){const auto &texture=modelAsset.textures[i];auto *item=new QListWidgetItem(texture.material+"  ·  "+texture.slot+"\n"+QFileInfo(texture.path).fileName(),modelTextureList);item->setData(Qt::UserRole,i);item->setToolTip(texture.path);}summary->setText(QString("%1 · %2 vertices · %3 faces · %4 material texture inputs").arg(QFileInfo(path).fileName()).arg(modelAsset.vertexCount).arg(modelAsset.faceCount).arg(modelAsset.textures.size()));status->setText("Model loaded · first material texture is opening in the live preview");if(modelTextureList->count())modelTextureList->setCurrentRow(0);});connect(modelTextureList,&QListWidget::currentRowChanged,this,[this](int row){if(row<0||row>=modelAsset.textures.size())return;const QImage texture=ImageProcessor::read(modelAsset.textures[row].path);if(texture.isNull()){status->setText("Texture is missing: "+modelAsset.textures[row].path);return;}original=texture;texturePreviewSource={};tabs->setCurrentIndex(6);refreshTexturePreview();status->setText("Editing model texture · "+modelAsset.textures[row].material+" · "+modelAsset.textures[row].slot);});connect(replace,&QPushButton::clicked,this,[this]{const int row=modelTextureList?modelTextureList->currentRow():-1;if(row<0||row>=modelAsset.textures.size()){showError("Select a model texture input first.");return;}const QString replacement=QFileDialog::getOpenFileName(this,"Replace model texture",QFileInfo(modelAsset.textures[row].path).absolutePath(),"Images (*.png *.jpg *.jpeg *.tga *.webp *.bmp *.tif *.tiff)");if(replacement.isEmpty())return;modelAsset.textures[row].path=replacement;modelTextureList->item(row)->setText(modelAsset.textures[row].material+"  ·  "+modelAsset.textures[row].slot+"\n"+QFileInfo(replacement).fileName());modelTextureList->setCurrentRow(row);status->setText("Replacement texture staged for the selected material input");});connect(extract,&QPushButton::clicked,this,[this]{if(modelAsset.textures.isEmpty()){showError("Load a model with textures first.");return;}const QString folder=QFileDialog::getExistingDirectory(this,"Extract model textures");if(folder.isEmpty())return;QString error;const int count=ModelImporter::extractTextures(modelAsset,folder,error);if(!count){showError(error);return;}lastFolder=folder;status->setText(QString("Extracted %1 model textures").arg(count));});return page;}

Adjustments MainWindow::adjustments() const {
    Adjustments a=layerStyle;a.zoom=zoom->value()/100.;a.hue=sliders["hue"]->value();a.saturation=sliders["saturation"]->value()/100.;a.brightness=sliders["brightness"]->value()/100.;a.contrast=sliders["contrast"]->value()/100.;a.exposure=sliders["exposure"]->value()/100.;a.blacks=sliders["blacks"]->value()/255.;a.gamma=sliders["gamma"]->value()/100.;a.whites=sliders["whites"]->value()/255.;a.red=sliders["red"]->value()/100.;a.green=sliders["green"]->value()/100.;a.blue=sliders["blue"]->value()/100.;a.flat=flat->isChecked();a.chromaKey=chromaKeyEnabled&&chromaKeyEnabled->isChecked();a.keyTolerance=(keyToleranceSlider?keyToleranceSlider->value():18)/100.;a.keySoftness=(keySoftnessSlider?keySoftnessSlider->value():8)/100.;a.keyDespill=(keyDespillSlider?keyDespillSlider->value():0)/100.;a.keyLumaProtect=(keyLumaProtectSlider?keyLumaProtectSlider->value():0)/100.;a.keyMatteBias=(keyMatteBiasSlider?keyMatteBiasSlider->value():0)/100.;a.keyCleanBlack=(keyCleanBlackSlider?keyCleanBlackSlider->value():0)/100.;a.keyCleanWhite=(keyCleanWhiteSlider?keyCleanWhiteSlider->value():0)/100.;
    // Guides and padding are expressed in canvas space.  The UI can switch
    // between percent and output-pixel units, so never assume percent here.
    a.padding=normalizedMargins(paddingInputs);
    a.overlay=styleChecks["overlay"]->isChecked();a.gradient=styleChecks["gradient"]->isChecked();a.stroke=styleChecks["stroke"]->isChecked();a.shadow=styleChecks["shadow"]->isChecked();a.glow=styleChecks["glow"]->isChecked();a.innerShadow=styleChecks["innerShadow"]->isChecked();a.bevel=styleChecks["bevel"]->isChecked();return a;
}
void MainWindow::refresh(){
    if(!previewVariant||styleChecks.size()<7)return;
    if(!applyingLayerOverride&&perImageOverride&&perImageOverride->isChecked()&&!currentFile.isEmpty()&&canvasLayers.contains(currentFile)){canvasLayers[currentFile].hasOverride=true;canvasLayers[currentFile].overrideAdjustments=adjustments();}
    if(sliders["blacks"]->value()>=sliders["whites"]->value())sliders["whites"]->setValue(sliders["blacks"]->value()+1);
    zoomValue->setText(QString::number(zoom->value())+"%");
    if(!currentFile.isEmpty()&&!original.isNull()){
        QImage composed=original;
        if(canvasLayers.contains(currentFile)){
            const auto &layer=canvasLayers[currentFile];
            if(!layer.visible||layer.opacity<1.||layer.fill<1.||layer.position!=QPointF()){
                QImage canvas(QSize(widthInput->value(),heightInput->value()),QImage::Format_ARGB32);
                canvas.fill(Qt::transparent);
                QPainter painter(&canvas);
                painter.setOpacity(layer.visible?std::clamp(layer.opacity*layer.fill,0.,1.):0.);
                painter.drawImage(layer.position,original);
                composed=canvas;
            }
        }
        preview->setFrame(compositeTimelineTracks(composed));
    }
    int variant=previewVariant->currentIndex();preview->setAdjustments(adjustments(),QSize(widthInput->value(),heightInput->value()),variant?&presets[variant-1]:nullptr);
    if(tabs&&tabs->currentIndex()==6&&!original.isNull()&&textureMapPreview&&!textureBgw.isEmpty())textureRefreshTimer.start();else if(tabs&&tabs->currentIndex()==7&&!original.isNull()){preview->setTextureSphereInteractive(false);updatePatternPreview();}else{preview->setTextureSphereInteractive(false);preview->clearTextureFrame();}
    preview->setGuides(guides->isChecked(),normalizedMargins(safeInputs));
}
void MainWindow::refreshTexturePreview(){
    if(!tabs||tabs->currentIndex()!=6||original.isNull()||!textureMapPreview||textureBgw.isEmpty())return;
    QMap<QString,int> strengths;for(auto it=textureMapSliders.cbegin();it!=textureMapSliders.cend();++it)strengths[it.key()]=it.value()->value();
    QString map=textureMapPreview->currentText();if(texturePreviewSource.isNull())texturePreviewSource=original.scaled(QSize(320,320),Qt::KeepAspectRatio,Qt::FastTransformation);const QImage &previewSource=texturePreviewSource;QImage positioned(previewSource.size(),QImage::Format_RGBA8888);positioned.fill(Qt::transparent);{QPainter painter(&positioned);const int x=textureOffsetX?qRound(textureOffsetX->value()*previewSource.width()/100.):0,y=textureOffsetY?qRound(textureOffsetY->value()*previewSource.height()/100.):0;painter.drawImage(x,y,previewSource);}
    double black=textureBgw[0]->value(),white=textureBgw[2]->value(),gamma=textureBgw[3]->value();
    if(textureAutoNormalize&&textureAutoNormalize->isChecked()){
        QImage gray=positioned.convertToFormat(QImage::Format_Grayscale8);int lo=255,hi=0;
        for(int y=0;y<gray.height();++y){const uchar *scan=gray.constScanLine(y);for(int x=0;x<gray.width();++x){lo=qMin(lo,int(scan[x]));hi=qMax(hi,int(scan[x]));}}
        if(hi>lo){black=lo;white=hi;}
    }
    QImage material=renderPbrPreview(positioned,strengths,textureMapAdvanced,black,white,gamma);if(!texturePreviewSphere){texturePreviewSphere=new MaterialViewport(this);texturePreviewSphere->hide();}texturePreviewSphere->setMesh(modelAsset.vertices.isEmpty()?nullptr:&modelAsset);texturePreviewSphere->setMaterial(material);texturePreviewSphere->setLighting(lightAzimuth?lightAzimuth->value():35,lightElevation?lightElevation->value():45,lightIntensity?lightIntensity->value():100,lightAmbient?lightAmbient->value():20);texturePreviewSphere->setModelTransform(modelScale?modelScale->value()/100.:1.,modelUvScale?modelUvScale->value()/100.:1.,modelUvX?modelUvX->value()/100.:0.,modelUvY?modelUvY->value()/100.:0.);QImage out=texturePreviewSphere->renderPreview(QSize(512,512));
    if(textureShowClipping&&textureShowClipping->isChecked())for(int y=0;y<out.height();++y)for(int x=0;x<out.width();++x){QRgb px=out.pixel(x,y);if(qRed(px)>=253&&qGreen(px)>=253&&qBlue(px)>=253)out.setPixel(x,y,qRgba(255,40,40,qAlpha(px)));else if(qRed(px)<=2&&qGreen(px)<=2&&qBlue(px)<=2)out.setPixel(x,y,qRgba(40,90,255,qAlpha(px)));}
    if(textureHistogram&&textureHistogram->isChecked()){
        QVector<int> bins(64,0);int peak=1;
        for(int y=0;y<out.height();y+=2)for(int x=0;x<out.width();x+=2){int bin=qBound(0,qGray(out.pixel(x,y))*64/256,63);++bins[bin];}
        for(int count:bins)peak=qMax(peak,count);
        int panelWidth=qMin(220,out.width()-16),panelHeight=64,ox=8,oy=out.height()-panelHeight-8;
        QPainter hist(&out);hist.setPen(Qt::NoPen);hist.setBrush(QColor(10,14,16,190));hist.drawRect(ox,oy,panelWidth,panelHeight);
        hist.setBrush(QColor("#3d91fb"));for(int b=0;b<64;++b){int barHeight=qRound(double(bins[b])/peak*(panelHeight-6));hist.drawRect(ox+b*panelWidth/64,oy+panelHeight-3-barHeight,qMax(1,panelWidth/64),barHeight);}
    }
    preview->setTextureSphereInteractive(true);preview->setTextureFrame(out);
    if(materialPreviewDialog&&materialPreviewDialog->isVisible())showMaterialPreview();
}
void MainWindow::restoreLayerOverride(){
    if(!perImageOverride||currentFile.isEmpty()||!canvasLayers.contains(currentFile))return;const auto &layer=canvasLayers[currentFile];applyingLayerOverride=true;{QSignalBlocker flag(perImageOverride);perImageOverride->setChecked(layer.hasOverride);}if(layer.hasOverride){const auto &a=layer.overrideAdjustments;layerStyle=a;QSignalBlocker z(zoom),h(sliders["hue"]),s(sliders["saturation"]),b(sliders["brightness"]),c(sliders["contrast"]),e(sliders["exposure"]),bl(sliders["blacks"]),g(sliders["gamma"]),w(sliders["whites"]),r(sliders["red"]),gr(sliders["green"]),bu(sliders["blue"]),ck(chromaKeyEnabled),kt(keyToleranceSlider),ks(keySoftnessSlider);zoom->setValue(qRound(a.zoom*100));sliders["hue"]->setValue(qRound(a.hue));sliders["saturation"]->setValue(qRound(a.saturation*100));sliders["brightness"]->setValue(qRound(a.brightness*100));sliders["contrast"]->setValue(qRound(a.contrast*100));sliders["exposure"]->setValue(qRound(a.exposure*100));sliders["blacks"]->setValue(qRound(a.blacks*255));sliders["gamma"]->setValue(qRound(a.gamma*100));sliders["whites"]->setValue(qRound(a.whites*255));sliders["red"]->setValue(qRound(a.red*100));sliders["green"]->setValue(qRound(a.green*100));sliders["blue"]->setValue(qRound(a.blue*100));chromaKeyEnabled->setChecked(a.chromaKey);keyToleranceSlider->setValue(qRound(a.keyTolerance*100));keySoftnessSlider->setValue(qRound(a.keySoftness*100));}applyingLayerOverride=false;
}
void MainWindow::showMaterialPreview(){
    if(lightAzimuth){lightAzimuth->setFocus(Qt::TabFocusReason);textureRefreshTimer.start();return;}
    if(original.isNull()){showError("Import an image before opening the material preview.");return;}
    if(!materialPreviewDialog){materialPreviewDialog=new QDialog(this);materialPreviewDialog->setWindowTitle("Aspectra · Live 3D PBR environment");auto *v=new QVBoxLayout(materialPreviewDialog);v->addWidget(label("LIVE PBR MATERIAL SPHERE","section"));materialPreviewView=new MaterialViewport;v->addWidget(materialPreviewView);auto *lights=new QFormLayout;auto addLight=[&](QString name,QSlider **target,int min,int max,int value){*target=new QSlider(Qt::Horizontal);(*target)->setRange(min,max);(*target)->setValue(value);lights->addRow(name,*target);connect(*target,&QSlider::valueChanged,this,[this]{if(materialPreviewDialog&&materialPreviewDialog->isVisible())showMaterialPreview();});};addLight("Light azimuth",&lightAzimuth,-180,180,35);addLight("Light elevation",&lightElevation,-80,80,45);addLight("Light intensity",&lightIntensity,0,200,100);addLight("Ambient light",&lightAmbient,0,100,20);v->addLayout(lights);}
    QImage source=texturePreviewSource.isNull()?original.scaled(QSize(320,320),Qt::KeepAspectRatio,Qt::FastTransformation):texturePreviewSource;QMap<QString,int> strengths;for(auto it=textureMapSliders.cbegin();it!=textureMapSliders.cend();++it)strengths[it.key()]=(textureMaps.value(it.key())->isChecked()?it.value()->value():0);QImage material=renderPbrPreview(source,strengths,textureMapAdvanced,textureBgw[0]->value(),textureBgw[2]->value(),textureBgw[3]->value());materialPreviewView->setMaterial(material);materialPreviewView->setLighting(lightAzimuth->value(),lightElevation->value(),lightIntensity->value(),lightAmbient->value());materialPreviewDialog->show();materialPreviewDialog->raise();
}
void MainWindow::setDimensions(int w,int h){changing=true;widthInput->setValue(w);heightInput->setValue(h);changing=false;refresh();}
void MainWindow::aspectChanged(){
    int index=ratio->currentIndex();const QVector<double> ratios{original.isNull()?1.:double(original.width())/original.height(),1,16./9,9./16,4./3};double value=1;
    if(index==5){bool ok=false;QString text=QInputDialog::getText(this,"Custom aspect ratio","Width:height",QLineEdit::Normal,"3:2",&ok);auto parts=text.split(':');bool a=false,b=false;double w=parts.value(0).toDouble(&a),h=parts.value(1).toDouble(&b);if(!ok||!a||!b||w<=0||h<=0||w/h<.05||w/h>20){QSignalBlocker block(ratio);ratio->setCurrentIndex(0);return;}value=w/h;}else value=ratios[index];
    lockedRatio=value;{QSignalBlocker block(lock);lock->setChecked(true);}setDimensions(widthInput->value(),qRound(widthInput->value()/value));
}
void MainWindow::chooseFiles(){const QString folder=QSettings().value("lastImportFolder",QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();const QStringList files=QFileDialog::getOpenFileNames(this,"Import media or 3D model",folder,"Aspectra media and 3D models (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff *.psd *.psb *.mp4 *.mov *.avi *.mkv *.webm *.gif *.obj *.fbx *.gltf *.glb);;All files (*)");if(files.isEmpty())return;QSettings().setValue("lastImportFolder",QFileInfo(files.first()).absolutePath());QStringList mediaFiles;for(const QString &path:files){const QString suffix=QFileInfo(path).suffix().toLower();if(suffix=="obj"||suffix=="fbx"||suffix=="gltf"||suffix=="glb")loadModelFile(path);else mediaFiles<<path;}if(!mediaFiles.isEmpty())loadFiles(mediaFiles);}
void MainWindow::loadModelFile(const QString &path){
    QString error;if(!ModelImporter::load(path,modelAsset,error)){showError(error);return;}
    QSettings().setValue("modelFolder",QFileInfo(path).absolutePath());lastFolder=QFileInfo(path).absolutePath();
    if(modelTextureList){modelTextureList->clear();for(int i=0;i<modelAsset.textures.size();++i){const auto &texture=modelAsset.textures[i];auto *item=new QListWidgetItem(texture.material+"  ·  "+texture.slot+"\n"+QFileInfo(texture.path).fileName(),modelTextureList);item->setData(Qt::UserRole,i);item->setToolTip(texture.path);}if(modelTextureList->count())modelTextureList->setCurrentRow(0);}
    if(modelRail)modelRail->click();
    if(modelSummary)modelSummary->setText(QString("%1 · %2 vertices · %3 faces · %4 material texture inputs").arg(QFileInfo(path).fileName()).arg(modelAsset.vertexCount).arg(modelAsset.faceCount).arg(modelAsset.textures.size()));
    status->setText(modelAsset.textures.isEmpty()?"Model loaded · no editable image textures were found":QString("Model loaded · %1 material texture inputs are ready to edit").arg(modelAsset.textures.size()));
}
void MainWindow::loadProject(){
    QString path=QFileDialog::getOpenFileName(this,"Open Aspectra project",{},"Aspectra project (*.aspectra)");if(path.isEmpty())return;
    QFile in(path);if(!in.open(QIODevice::ReadOnly)){showError("Could not open project.");return;}auto document=QJsonDocument::fromJson(in.readAll());if(!document.isObject()){showError("This is not a valid Aspectra project.");return;}auto root=document.object();auto canvas=root.value("canvas").toObject();
    clearMedia();if(canvas.contains("width")&&canvas.contains("height"))setDimensions(canvas["width"].toInt(1024),canvas["height"].toInt(1024));
    QStringList sources;for(auto value:root.value("layers").toArray()){if(!value.isObject())continue;auto item=value.toObject();QString source=item.value("source").toString();if(source.isEmpty()||!QFileInfo::exists(source))continue;sources<<source;CanvasLayer layer;layer.source=source;layer.name=item.value("name").toString(QFileInfo(source).completeBaseName());layer.position={item.value("x").toDouble(),item.value("y").toDouble()};layer.nativeSize={item.value("width").toInt(),item.value("height").toInt()};layer.visible=item.value("visible").toBool(true);QByteArray mask=QByteArray::fromBase64(item.value("maskPngBase64").toString().toLatin1());if(!mask.isEmpty())layer.mask.loadFromData(mask,"PNG");canvasLayers[source]=layer;}
    if(sources.isEmpty()){showError("No linked project media was found. The source files may have moved.");return;}batch.add(sources);updateBatchLabel();selectFile(sources.first());status->setText(QString("Project opened · %1 linked layers").arg(sources.size()));
}
void MainWindow::createNewProject(int width,int height,int resolution,bool artboard,const QString &colorMode,const QString &profile,const QColor &background){
    if(galleryScreen)galleryScreen->hide();
    batch.files.clear();canvasLayers.clear();inputAliases.clear();inputTitles.clear();timelineTracks.clear();currentFile="aspectra://untitled";batch.files.append(currentFile);original=QImage(qMax(1,width),qMax(1,height),QImage::Format_ARGB32);original.fill(background.alpha()==0?Qt::transparent:background);media={};media.size=original.size();setDimensions(width,height);CanvasLayer layer;layer.source=currentFile;layer.name=artboard?"Artboard 1":"Canvas";layer.nativeSize=original.size();canvasLayers[currentFile]=layer;preview->clearVectorTraceFrame();preview->clearTextureFrame();preview->setLayerMask({});preview->setFrame(original);refresh();updateBatchLabel();if(exportButton)exportButton->setEnabled(true);QSettings settings;settings.setValue("newProjectResolution",resolution);settings.setValue("newProjectColorMode",colorMode);settings.setValue("newProjectProfile",profile);status->setText(QString("New %1 · %2 × %3 · %4 dpi · %5").arg(artboard?"artboard":"transparent canvas").arg(width).arg(height).arg(resolution).arg(colorMode));autosaveProject();
}
void MainWindow::autosaveProject(){
    if(!widthInput||!heightInput)return;QJsonObject document;document["version"]=2;document["recovery"]=true;document["savedAt"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);document["canvas"]=QJsonObject{{"width",widthInput->value()},{"height",heightInput->value()},{"ratio",ratio?ratio->currentText():"Custom"}};QJsonArray layers;for(const auto &source:batch.files){const auto layer=canvasLayers.value(source);layers.append(QJsonObject{{"source",source},{"name",layer.name},{"x",layer.position.x()},{"y",layer.position.y()},{"width",layer.nativeSize.width()},{"height",layer.nativeSize.height()},{"visible",layer.visible}});}document["layers"]=layers;QSaveFile out(recoveryFilePath());if(!out.open(QIODevice::WriteOnly))return;out.write(QJsonDocument(document).toJson(QJsonDocument::Compact));out.commit();
}
void MainWindow::openProjectFile(const QString &path){
    if(galleryScreen)galleryScreen->hide();
    QFile in(path);if(!in.open(QIODevice::ReadOnly)){showError("Could not open project.");return;}const QJsonDocument document=QJsonDocument::fromJson(in.readAll());if(!document.isObject()){showError("This is not a valid Aspectra project.");return;}const QJsonObject root=document.object(),canvas=root.value("canvas").toObject();const int width=canvas.value("width").toInt(1024),height=canvas.value("height").toInt(1024);QStringList sources;canvasLayers.clear();for(const auto &value:root.value("layers").toArray()){const QJsonObject item=value.toObject();const QString source=item.value("source").toString();if(source.isEmpty())continue;if(!source.startsWith(QLatin1String("aspectra://"))&&!QFileInfo::exists(source))continue;sources<<source;CanvasLayer layer;layer.source=source;layer.name=item.value("name").toString(QFileInfo(source).completeBaseName());layer.position={item.value("x").toDouble(),item.value("y").toDouble()};layer.nativeSize={item.value("width").toInt(),item.value("height").toInt()};layer.visible=item.value("visible").toBool(true);canvasLayers[source]=layer;}setDimensions(width,height);if(sources.isEmpty()){createNewProject(width,height,QSettings().value("newProjectResolution",300).toInt(),true,QSettings().value("newProjectColorMode","RGB").toString(),QSettings().value("newProjectProfile","sRGB IEC61966-2.1").toString(),Qt::transparent);status->setText("Recovered blank canvas");}else{batch.files=sources;updateBatchLabel();selectFile(batch.files.first());status->setText(QString("Project opened · %1 linked artboards").arg(batch.files.size()));}if(!path.endsWith("Aspectra-X-recovery.aspectra"))rememberRecentDocument(path);
}
#if 0
void MainWindow::showWelcomeScreen(){
    {
        autosaveTimer.setInterval(30000);if(!autosaveTimer.isActive()){connect(&autosaveTimer,&QTimer::timeout,this,&MainWindow::autosaveProject);autosaveTimer.start();}
        QDialog dialog(this);dialog.setWindowTitle("Aspectra Script · Projects");dialog.setModal(true);dialog.setFixedSize(560,960);
        dialog.setStyleSheet("QDialog,QDialog>QWidget{background:#000000;} QScrollArea#ProjectGridRail,QScrollArea#ProjectGridRail>QWidget>QWidget{background:#000000;border:none;} QToolButton#ProjectTile{border:1px solid #32323b;border-radius:14px;color:#fff;padding:7px;font-weight:600;} QToolButton#ProjectTile:hover{border:2px solid #eb4790;}");
        auto *root=new QVBoxLayout(&dialog);root->setContentsMargins(24,18,24,20);root->setSpacing(12);
        auto *hero=new QWidget(&dialog);auto *heroLayout=new QVBoxLayout(hero);heroLayout->setContentsMargins(0,0,0,4);heroLayout->setSpacing(0);auto *wordmark=new QLabel(hero);wordmark->setPixmap(QPixmap(":/brand/wordmark.png").scaled(240,48,Qt::KeepAspectRatio,Qt::SmoothTransformation));wordmark->setAlignment(Qt::AlignCenter);auto *script=label("SCRIPT · PROJECTS","section");script->setAlignment(Qt::AlignCenter);script->setStyleSheet("color:#ffffff;font-size:12px;font-weight:800;letter-spacing:4px;padding:2px;");heroLayout->addWidget(wordmark);heroLayout->addWidget(script);root->addWidget(hero);
        auto *tabLayout=new QHBoxLayout;tabLayout->setSpacing(7);auto *tabGroup=new QButtonGroup(&dialog);tabGroup->setExclusive(true);const QStringList tabs{"Recent","Saved","Photo","Print","Art","Web","Mobile","Film","3D Model"};for(int i=0;i<tabs.size();++i){auto *tab=new QPushButton(tabs[i],&dialog);tab->setCheckable(true);tab->setToolTip(tabs[i]);tabLayout->addWidget(tab);tabGroup->addButton(tab,i);}tabLayout->addStretch();tabGroup->button(0)->setChecked(true);root->addLayout(tabLayout);
        auto *recentLabel=label("PROJECTS · 5 × 7","section");root->addWidget(recentLabel);auto *scroll=new QScrollArea(&dialog);scroll->setObjectName("ProjectGridRail");scroll->setWidgetResizable(false);scroll->setAlignment(Qt::AlignLeft|Qt::AlignTop);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);auto *host=new QWidget(scroll);auto *grid=new QGridLayout(host);grid->setContentsMargins(4,4,4,4);grid->setHorizontalSpacing(5);grid->setVerticalSpacing(5);grid->setSizeConstraint(QLayout::SetFixedSize);scroll->setWidget(host);root->addWidget(scroll,1);
        auto *projectRail=new QHBoxLayout;auto *newProject=button("New Project",projectRail,"blue");auto *loadProjectButton=button("Load Project",projectRail);projectRail->addStretch();root->addLayout(projectRail);
        auto *settingsPanel=new QWidget(&dialog);settingsPanel->setMaximumHeight(0);settingsPanel->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);auto *settings=new QGridLayout(settingsPanel);settings->setContentsMargins(14,14,14,14);settings->setHorizontalSpacing(14);settings->setVerticalSpacing(8);
        auto *back=button("‹ Back to projects");auto *create=button("Create Project",nullptr,"blue");auto *projectWidth=number(1920,16384),*projectHeight=number(1080,16384),*projectResolution=number(300,2400);auto *unit=new QComboBox;unit->addItems({"px","in","mm"});auto *orientation=new QComboBox;orientation->addItems({"Landscape","Portrait"});auto *artboard=new QCheckBox("Artboard");artboard->setChecked(true);auto *mode=new QComboBox;mode->addItems({"RGB","CMYK","Grayscale"});auto *bit=new QComboBox;bit->addItems({"8 bit","16 bit","32 bit"});auto *profile=new QComboBox;profile->addItems({"sRGB IEC61966-2.1","Display P3","Adobe RGB (1998)","CMYK Coated FOGRA39"});auto *background=button("Background: Transparent");background->setProperty("color",QColor(Qt::transparent));
        settings->addWidget(label("NEW PROJECT","section"),0,0,1,2);settings->addWidget(back,0,2,1,2);settings->addWidget(label("Width"),1,0);settings->addWidget(projectWidth,1,1);settings->addWidget(label("Height"),1,2);settings->addWidget(projectHeight,1,3);settings->addWidget(label("Unit"),2,0);settings->addWidget(unit,2,1);settings->addWidget(label("Resolution"),2,2);settings->addWidget(projectResolution,2,3);settings->addWidget(label("Orientation"),3,0);settings->addWidget(orientation,3,1);settings->addWidget(label("Artboard"),3,2);settings->addWidget(artboard,3,3);settings->addWidget(label("Color mode"),4,0);settings->addWidget(mode,4,1);settings->addWidget(label("Bit depth"),4,2);settings->addWidget(bit,4,3);settings->addWidget(label("Color profile"),5,0);settings->addWidget(profile,5,1);settings->addWidget(background,5,2,1,2);settings->addWidget(create,6,2,1,2);root->addWidget(settingsPanel);
        bool expanded=false;std::function<void()> rebuild=[this,grid,host,scroll,tabGroup,recentLabel,&dialog,&expanded]{while(auto *item=grid->takeAt(0)){if(item->widget())item->widget()->deleteLater();delete item;}const int tab=tabGroup->checkedId(),visibleColumns=expanded?3:7;QList<QStringList> categories(4);for(const QString &path:QSettings().value("recentDocuments").toStringList()){const QFileInfo file(path);if(!file.exists())continue;const QString suffix=file.suffix().toLower();const bool show=tab==0||(tab==1&&suffix=="aspectra")||(tab==2&&QStringList{"png","jpg","jpeg","webp","tif","tiff"}.contains(suffix))||(tab==3&&QStringList{"psd","psb","pdf"}.contains(suffix))||(tab==4&&QStringList{"svg","eps","ai"}.contains(suffix))||(tab==7&&QStringList{"mp4","mov","webm","mkv"}.contains(suffix))||(tab==8&&QStringList{"obj","fbx","gltf","glb"}.contains(suffix));if(!show)continue;const int category=suffix=="aspectra"?0:(suffix=="psd"||suffix=="psb"?1:(suffix=="ai"?2:(suffix=="xd"?3:0)));categories[category]<<path;}const QStringList colors{"transparent","#0759a7","#d88b0b","#c52c70"};const QStringList types{"ASPECTRA","PHOTOSHOP","ILLUSTRATOR","ADOBE XD"};int column=0,placed=0;for(int category=0;category<categories.size();++category){for(int i=0;i<categories[category].size();++i){const QFileInfo file(categories[category][i]);auto *tile=new QToolButton(host);tile->setObjectName("ProjectTile");tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);tile->setFixedSize(118,144);tile->setIconSize(QSize(86,86));tile->setToolTip(types[category]+" · "+file.absoluteFilePath());QPixmap thumb(86,86);thumb.fill(QColor(colors[category]));QImage image(file.absoluteFilePath());QPainter painter(&thumb);if(!image.isNull()){const QImage fitted=image.scaled(82,82,Qt::KeepAspectRatio,Qt::SmoothTransformation);painter.drawImage((86-fitted.width())/2,(86-fitted.height())/2,fitted);}else{painter.setPen(QColor(255,255,255,180));painter.setFont(QFont("Segoe UI",10,QFont::Bold));painter.drawText(thumb.rect(),Qt::AlignCenter,file.suffix().toUpper());}painter.end();const QDateTime created=file.birthTime().isValid()?file.birthTime():file.lastModified();tile->setIcon(QIcon(thumb));tile->setText(file.completeBaseName()+"\n"+created.toLocalTime().toString("MMM d · h:mm ap"));tile->setStyleSheet(category==0?"QToolButton#ProjectTile{background:transparent;border-color:transparent;} QToolButton#ProjectTile:hover{background:transparent;border:2px solid #eb4790;}":QString("QToolButton#ProjectTile{background:%1;border-color:%1;} QToolButton#ProjectTile:hover{background:%1;border:2px solid #eb4790;}").arg(colors[category]));grid->addWidget(tile,i%5,column+i/5);const QString path=file.absoluteFilePath();connect(tile,&QToolButton::clicked,this,[this,path,&dialog]{const QString suffix=QFileInfo(path).suffix().toLower();if(suffix=="aspectra")openProjectFile(path);else if(suffix=="obj"||suffix=="fbx"||suffix=="gltf"||suffix=="glb")loadModelFile(path);else loadFiles({path});dialog.accept();});++placed;}column+=(categories[category].size()+4)/5;}host->setFixedSize(qMax(1,column)*123+8,5*149+8);recentLabel->setText(placed?QString("PROJECTS · 5 × %1 · %2").arg(visibleColumns).arg(placed):"NO RECENT PROJECTS");};
        auto slidePanel=[&](bool open){if(expanded==open)return;expanded=open;settingsPanel->show();auto *animation=new QPropertyAnimation(settingsPanel,"maximumHeight",settingsPanel);animation->setDuration(220);animation->setEasingCurve(QEasingCurve::OutCubic);animation->setStartValue(settingsPanel->maximumHeight());animation->setEndValue(open?250:0);connect(animation,&QPropertyAnimation::finished,settingsPanel,[settingsPanel,open]{if(!open)settingsPanel->hide();});animation->start(QAbstractAnimation::DeleteWhenStopped);rebuild();};
        connect(tabGroup,&QButtonGroup::idClicked,&dialog,[&](int){rebuild();});connect(newProject,&QPushButton::clicked,&dialog,[&]{slidePanel(true);});connect(back,&QPushButton::clicked,&dialog,[&]{slidePanel(false);});connect(orientation,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[projectWidth,projectHeight](int){const int width=projectWidth->value();projectWidth->setValue(projectHeight->value());projectHeight->setValue(width);});connect(background,&QPushButton::clicked,&dialog,[background,&dialog]{QColor color=QColorDialog::getColor(background->property("color").value<QColor>(),&dialog,"Canvas background",QColorDialog::ShowAlphaChannel);if(color.isValid()){background->setProperty("color",color);background->setText(color.alpha()==0?"Background: Transparent":"Background: "+color.name(QColor::HexArgb));}});connect(create,&QPushButton::clicked,&dialog,[this,projectWidth,projectHeight,projectResolution,unit,artboard,mode,profile,background,&dialog]{const double scale=unit->currentText()=="in"?projectResolution->value():(unit->currentText()=="mm"?projectResolution->value()/25.4:1.);createNewProject(qMax(1,qRound(projectWidth->value()*scale)),qMax(1,qRound(projectHeight->value()*scale)),projectResolution->value(),artboard->isChecked(),mode->currentText(),profile->currentText(),background->property("color").value<QColor>());dialog.accept();});connect(loadProjectButton,&QPushButton::clicked,&dialog,[this,&dialog]{const QString path=QFileDialog::getOpenFileName(&dialog,"Load project or Adobe document",QSettings().value("lastImportFolder").toString(),"Aspectra / Adobe (*.aspectra *.psd *.psb *.ai *.xd);;All supported media (*.*)");if(path.isEmpty())return;if(QFileInfo(path).suffix().compare("aspectra",Qt::CaseInsensitive)==0)openProjectFile(path);else loadFiles({path});dialog.accept();});rebuild();dialog.exec();return;
    }
}
#if 0
    autosaveTimer.setInterval(30000);if(!autosaveTimer.isActive()){connect(&autosaveTimer,&QTimer::timeout,this,&MainWindow::autosaveProject);autosaveTimer.start();}
    QDialog dialog(this);dialog.setWindowTitle("Aspectra X");dialog.setModal(true);dialog.setMinimumSize(560,680);auto *root=new QVBoxLayout(&dialog);root->setContentsMargins(18,18,18,18);root->setSpacing(12);auto *brand=label("ASPECTRA X","section");brand->setAlignment(Qt::AlignCenter);root->addWidget(brand);auto *tabsRow=new QHBoxLayout;root->addLayout(tabsRow);auto *tabsGroup=new QButtonGroup(&dialog);tabsGroup->setExclusive(true);const QStringList tabNames{"Recent","Saved","Photo","Print","Art","Web","Mobile","Film","3D Model"};for(int i=0;i<tabNames.size();++i){auto *tab=new QPushButton(tabNames[i],&dialog);tab->setCheckable(true);tab->setToolTip(tabNames[i]+" projects");tabsRow->addWidget(tab);tabsGroup->addButton(tab,i);}tabsGroup->button(0)->setChecked(true);
    auto *projectRail=new QHBoxLayout;auto *newProject=button("New Project",projectRail,"blue");auto *loadProjectButton=button("Load Project",projectRail);projectRail->addStretch();root->addLayout(projectRail);auto *settingsPanel=new QWidget(&dialog);auto *settings=new QGridLayout(settingsPanel);settings->setContentsMargins(0,0,0,0);auto *projectWidth=number(1920,16384),*projectHeight=number(1080,16384),*projectResolution=number(300,2400);auto *unit=new QComboBox;unit->addItems({"px","in","mm"});auto *orientation=new QComboBox;orientation->addItems({"Landscape","Portrait"});auto *artboard=new QCheckBox("Artboard");artboard->setChecked(true);auto *mode=new QComboBox;mode->addItems({"RGB","CMYK","Grayscale"});auto *bit=new QComboBox;bit->addItems({"8 bit","16 bit","32 bit"});auto *profile=new QComboBox;profile->addItems({"sRGB IEC61966-2.1","Display P3","Adobe RGB (1998)","CMYK Coated FOGRA39"});auto *pixelAspect=new QComboBox;pixelAspect->addItems({"Square 1.0","DV 1.067","HDV 1.333"});auto *background=button("Background: Transparent",nullptr);background->setProperty("color",QColor(Qt::transparent));settings->addWidget(label("Width"),0,0);settings->addWidget(projectWidth,0,1);settings->addWidget(label("Height"),0,2);settings->addWidget(projectHeight,0,3);settings->addWidget(unit,0,4);settings->addWidget(label("Resolution"),1,0);settings->addWidget(projectResolution,1,1);settings->addWidget(orientation,1,2);settings->addWidget(artboard,1,3,1,2);settings->addWidget(label("Color mode"),2,0);settings->addWidget(mode,2,1);settings->addWidget(bit,2,2);settings->addWidget(background,2,3,1,2);settings->addWidget(label("Color profile"),3,0);settings->addWidget(profile,3,1,1,2);settings->addWidget(pixelAspect,3,3,1,2);auto *create=button("Create Project",settings,"blue");settings->addWidget(create,4,3,1,2);settingsPanel->hide();root->addWidget(settingsPanel);auto *recentLabel=label("RECENT PROJECTS","section");root->addWidget(recentLabel);auto *scroll=new QScrollArea(&dialog);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto *gridHost=new QWidget(scroll);auto *grid=new QGridLayout(gridHost);grid->setContentsMargins(0,0,0,0);grid->setSpacing(8);scroll->setWidget(gridHost);root->addWidget(scroll,1);
    auto rebuild=[this,grid,tabsGroup,recentLabel]{while(auto *item=grid->takeAt(0)){if(item->widget())item->widget()->deleteLater();delete item;}const int tab=tabsGroup->checkedId();const QStringList all=QSettings().value("recentDocuments").toStringList();int placed=0;for(const QString &path:all){const QFileInfo file(path);if(!file.exists())continue;const QString suffix=file.suffix().toLower();bool show=tab==0||(tab==1&&suffix=="aspectra")||(tab==2&&QStringList{"png","jpg","jpeg","webp","tif","tiff"}.contains(suffix))||(tab==3&&QStringList{"psd","psb","pdf"}.contains(suffix))||(tab==4&&QStringList{"svg","eps","ai"}.contains(suffix))||(tab==7&&QStringList{"mp4","mov","webm","mkv"}.contains(suffix))||(tab==8&&QStringList{"obj","fbx","gltf","glb"}.contains(suffix));if(!show)continue;auto *tile=new QPushButton(file.completeBaseName()+"\n"+suffix.toUpper(),gridHost);tile->setMinimumSize(96,78);tile->setToolTip(file.absoluteFilePath());grid->addWidget(tile,placed/5,placed%5);connect(tile,&QPushButton::clicked,this,[this,path,&dialog]{const QString suffix=QFileInfo(path).suffix().toLower();if(suffix=="aspectra")openProjectFile(path);else if(suffix=="obj"||suffix=="fbx"||suffix=="gltf"||suffix=="glb")loadModelFile(path);else loadFiles({path});dialog.accept();});if(++placed==20)break;}recentLabel->setText(placed?"RECENT PROJECTS":"NO RECENT PROJECTS");};connect(tabsGroup,&QButtonGroup::idClicked,&dialog,[rebuild](int){rebuild();});connect(newProject,&QPushButton::clicked,&dialog,[settingsPanel]{settingsPanel->setVisible(!settingsPanel->isVisible());});connect(orientation,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[projectWidth,projectHeight](int index){if(index==0&&projectHeight->value()>projectWidth->value())std::swap(*projectWidth,*projectHeight);if(index==1&&projectWidth->value()>projectHeight->value())std::swap(*projectWidth,*projectHeight);});connect(background,&QPushButton::clicked,&dialog,[background,&dialog]{QColor color=QColorDialog::getColor(background->property("color").value<QColor>(),&dialog,"Canvas background",QColorDialog::ShowAlphaChannel);if(color.isValid()){background->setProperty("color",color);background->setText(color.alpha()==0?"Background: Transparent":"Background: "+color.name(QColor::HexArgb));}});connect(create,&QPushButton::clicked,&dialog,[this,projectWidth,projectHeight,projectResolution,unit,artboard,mode,profile,background,&dialog]{double scale=unit->currentText()=="in"?projectResolution->value():(unit->currentText()=="mm"?projectResolution->value()/25.4:1.);createNewProject(qMax(1,qRound(projectWidth->value()*scale)),qMax(1,qRound(projectHeight->value()*scale)),projectResolution->value(),artboard->isChecked(),mode->currentText(),profile->currentText(),background->property("color").value<QColor>());dialog.accept();});connect(loadProjectButton,&QPushButton::clicked,&dialog,[this,&dialog]{const QString path=QFileDialog::getOpenFileName(&dialog,"Load project or Photoshop document",QSettings().value("lastImportFolder").toString(),"Aspectra / Photoshop (*.aspectra *.psd *.psb);;All supported media (*.*)");if(path.isEmpty())return;if(QFileInfo(path).suffix().compare("aspectra",Qt::CaseInsensitive)==0)openProjectFile(path);else loadFiles({path});dialog.accept();});if(QFileInfo::exists(recoveryFilePath())){auto *recover=button("Recover last autosave",projectRail,"gold");connect(recover,&QPushButton::clicked,&dialog,[this,&dialog]{openProjectFile(recoveryFilePath());dialog.accept();});}rebuild();dialog.exec();
}
#endif
#endif

void MainWindow::showWelcomeScreen(){
    if(!currentFile.isEmpty()||loading)return;
    autosaveTimer.setInterval(30000);
    if(!autosaveTimer.isActive()){
        connect(&autosaveTimer,&QTimer::timeout,this,&MainWindow::autosaveProject);
        autosaveTimer.start();
    }
    if(galleryScreen)galleryScreen->hide();
    QDialog dialog(this);
    dialog.setWindowTitle("Aspectra Script · Projects");
    dialog.setModal(true);
    dialog.setFixedSize(560,960);
    dialog.setStyleSheet("QDialog,QDialog>QWidget{background:#000000;}QPushButton{background:#0d1017;border:1px solid #303847;border-radius:10px;padding:7px 11px;color:#fff;font-weight:650;}QPushButton:hover{border-color:#35c3f6;background:#151e2a;}QPushButton:checked{border:2px solid #eb4790;}QLabel#WelcomeSection{color:#d8e5f5;font-size:10px;font-weight:800;letter-spacing:2px;}");
    auto *root=new QVBoxLayout(&dialog);
    root->setContentsMargins(24,18,24,20);
    root->setSpacing(12);
    auto *hero=new QWidget(&dialog);
    auto *heroLayout=new QVBoxLayout(hero);
    heroLayout->setContentsMargins(0,0,0,0);
    heroLayout->setSpacing(2);
    auto *wordmark=new QLabel(hero);
    wordmark->setPixmap(QPixmap(":/brand/wordmark.png").scaled(240,48,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    wordmark->setAlignment(Qt::AlignCenter);
    auto *heading=new QLabel("SCRIPT · PROJECTS",hero);
    heading->setObjectName("WelcomeSection");
    heading->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(wordmark);
    heroLayout->addWidget(heading);
    root->addWidget(hero);

    auto *tabsRail=new QScrollArea(&dialog);
    tabsRail->setFrameShape(QFrame::NoFrame);
    tabsRail->setWidgetResizable(false);
    tabsRail->setFixedHeight(43);
    tabsRail->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabsRail->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *tabHost=new QWidget(tabsRail);
    auto *tabsLayout=new QHBoxLayout(tabHost);
    tabsLayout->setContentsMargins(0,0,0,0);
    tabsLayout->setSpacing(7);
    auto *tabGroup=new QButtonGroup(&dialog);
    tabGroup->setExclusive(true);
    const QStringList tabs{"Recent","Saved","Photo","Print","Art","Web","Mobile","Film","3D Model"};
    for(int i=0;i<tabs.size();++i){auto *tab=new QPushButton(tabs[i],tabHost);tab->setCheckable(true);tab->setToolTip(tabs[i]+" projects");tabsLayout->addWidget(tab);tabGroup->addButton(tab,i);}
    tabGroup->button(0)->setChecked(true);
    tabHost->adjustSize();
    tabsRail->setWidget(tabHost);
    root->addWidget(tabsRail);

    auto *selectorLabel=new QLabel("PROJECT SELECTOR",&dialog);
    selectorLabel->setObjectName("WelcomeSection");
    root->addWidget(selectorLabel);
    auto *galleryStage=new QWidget(&dialog);
    auto *galleryStageLayout=new QGridLayout(galleryStage);
    galleryStageLayout->setContentsMargins(0,0,0,0);
    galleryStageLayout->setSpacing(0);
    auto *gallery=new AspectraGalleryScreen(AspectraGalleryScreen::Presentation::ProjectSelector,galleryStage);
    gallery->setMinimumHeight(0);
    auto *galleryOverlay=new QWidget(galleryStage);
    galleryOverlay->setObjectName("WelcomeGalleryOverlay");
    galleryOverlay->setStyleSheet("QWidget#WelcomeGalleryOverlay{background:rgba(5,6,8,210);}");
    galleryOverlay->setAttribute(Qt::WA_TransparentForMouseEvents,false);
    auto *overlayEffect=new QGraphicsOpacityEffect(galleryOverlay);
    overlayEffect->setOpacity(0.0);
    galleryOverlay->setGraphicsEffect(overlayEffect);
    galleryStageLayout->addWidget(gallery,0,0);
    galleryStageLayout->addWidget(galleryOverlay,0,0);
    galleryOverlay->hide();
    root->addWidget(galleryStage,1);

    auto *projectRail=new QHBoxLayout;
    auto *newProject=button("New Project",projectRail,"blue");
    auto *loadProject=button("Load Project",projectRail);
    projectRail->addStretch(1);
    root->addLayout(projectRail);
    auto *settingsPanel=new QWidget(&dialog);
    settingsPanel->setMaximumHeight(0);
    settingsPanel->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    auto *settings=new QVBoxLayout(settingsPanel);
    settings->setContentsMargins(14,12,14,12);settings->setSpacing(10);
    auto *topRow=new QHBoxLayout;topRow->setContentsMargins(0,0,0,0);topRow->setSpacing(10);
    auto *back=button("‹ Back to projects",topRow);
    topRow->addStretch(1);
    auto *create=button("Create Project",topRow,"blue");
    settings->addLayout(topRow);
    auto *projectWidth=number(1920,16384),*projectHeight=number(1080,16384),*projectResolution=number(300,2400);
    auto *projectName=new QLineEdit("Untitled-1",settingsPanel);
    projectName->setPlaceholderText("Project Name");
    projectName->setClearButtonEnabled(true);
    auto *unit=new QComboBox(settingsPanel);unit->addItems({"px","in","mm"});
    auto *mode=new QComboBox(settingsPanel);mode->addItems({"RGB","CMYK","Grayscale"});
    auto *artboard=new QCheckBox("Artboard",settingsPanel);artboard->setChecked(true);
    auto *mainSplit=new QHBoxLayout;mainSplit->setContentsMargins(0,0,0,0);mainSplit->setSpacing(16);
    auto *leftCol=new QVBoxLayout;leftCol->setContentsMargins(0,0,0,0);leftCol->setSpacing(8);
    auto *aspectPreview=new CanvasAspectPreview(settingsPanel);
    leftCol->addWidget(aspectPreview,1);
    auto *presetCaption=label("DOCUMENT PRESETS","WelcomeSection");leftCol->addWidget(presetCaption);
    auto *presetRail=new QScrollArea(settingsPanel);presetRail->setObjectName("DocumentPresetRail");presetRail->setFrameShape(QFrame::NoFrame);presetRail->setWidgetResizable(true);presetRail->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);presetRail->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);presetRail->setFixedHeight(132);
    auto *presetHost=new QWidget(settingsPanel);presetHost->setObjectName("DocumentPresetGrid");
    auto *presetLayout=new QGridLayout(presetHost);presetLayout->setContentsMargins(0,0,0,0);presetLayout->setHorizontalSpacing(7);presetLayout->setVerticalSpacing(7);presetLayout->setAlignment(Qt::AlignTop);
    presetRail->setWidget(presetHost);leftCol->addWidget(presetRail);
    auto makeDimensionSlider=[](QSpinBox *backing){
        auto *control=new QWidget(backing->parentWidget());
        auto *line=new QHBoxLayout(control);line->setContentsMargins(0,0,0,0);line->setSpacing(8);
        auto *slider=new GradientSlider(control);slider->setRange(backing->minimum(),backing->maximum());slider->setValue(backing->value());
        backing->setObjectName("DimensionNumberField");backing->setFixedWidth(72);backing->setButtonSymbols(QAbstractSpinBox::NoButtons);
        if(auto *editor=backing->findChild<QLineEdit*>())editor->setAlignment(Qt::AlignCenter);
        line->addWidget(slider,1);line->addWidget(backing);
        QObject::connect(slider,&QSlider::valueChanged,control,[backing](int value){QSignalBlocker guard(backing);backing->setValue(value);});
        QObject::connect(backing,qOverload<int>(&QSpinBox::valueChanged),control,[slider](int value){QSignalBlocker guard(slider);slider->setValue(qBound(slider->minimum(),value,slider->maximum()));});
        return control;
    };
    auto *widthControl=makeDimensionSlider(projectWidth);
    auto *heightControl=makeDimensionSlider(projectHeight);
    auto *resolutionControl=makeDimensionSlider(projectResolution);
    for(auto *control:{widthControl,heightControl,resolutionControl})control->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    auto *rightCol=new QFormLayout;rightCol->setContentsMargins(0,0,0,0);rightCol->setHorizontalSpacing(10);rightCol->setVerticalSpacing(8);rightCol->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    rightCol->addRow("Project Name",projectName);rightCol->addRow("Width",widthControl);rightCol->addRow("Height",heightControl);rightCol->addRow("Resolution",resolutionControl);
    auto *canvasFields=new QWidget(settingsPanel);auto *canvasFieldsLayout=new QHBoxLayout(canvasFields);canvasFieldsLayout->setContentsMargins(0,0,0,0);canvasFieldsLayout->setSpacing(8);canvasFieldsLayout->addWidget(unit,1);canvasFieldsLayout->addWidget(mode,1);
    rightCol->addRow("Canvas",canvasFields);rightCol->addRow(QString{},artboard);
    mainSplit->addLayout(leftCol,1);mainSplit->addLayout(rightCol,0);
    settings->addLayout(mainSplit,1);
    root->addWidget(settingsPanel);

    struct CanvasPreset { QString name; int width; int height; };
    const QMap<int,QVector<CanvasPreset>> presetsByTab{
        {0,{{"HD Frame",1920,1080},{"Square",1080,1080},{"4K UHD",3840,2160}}},
        {2,{{"4 × 6 Photo",1800,1200},{"8 × 10 Photo",2400,3000},{"Instagram",1080,1080}}},
        {3,{{"US Letter",2550,3300},{"A4",2480,3508},{"Tabloid",3300,5100}}},
        {4,{{"Poster",3600,5400},{"Art Print",3000,3000},{"Canvas",4800,3600}}},
        {5,{{"Full HD",1920,1080},{"Desktop",1366,768},{"Widescreen",1440,900}}},
        {6,{{"iPhone",1170,2532},{"Android",1080,2400},{"Mobile Square",1080,1080}}},
        {7,{{"HD Film",1920,1080},{"4K Film",3840,2160},{"Vertical Film",1080,1920}}}
    };
    auto updateAspect=[projectWidth,projectHeight,projectResolution,unit,aspectPreview]{aspectPreview->updateAspect(projectWidth->value(),projectHeight->value(),projectResolution->value(),unit->currentText());};
    connect(projectWidth,qOverload<int>(&QSpinBox::valueChanged),aspectPreview,[updateAspect](int){updateAspect();});
    connect(projectHeight,qOverload<int>(&QSpinBox::valueChanged),aspectPreview,[updateAspect](int){updateAspect();});
    connect(projectResolution,qOverload<int>(&QSpinBox::valueChanged),aspectPreview,[updateAspect](int){updateAspect();});
    connect(unit,&QComboBox::currentTextChanged,aspectPreview,[updateAspect](const QString &){updateAspect();});
    for(auto *slider:{widthControl->findChild<QSlider*>(),heightControl->findChild<QSlider*>(),resolutionControl->findChild<QSlider*>()})if(slider)connect(slider,&QSlider::valueChanged,aspectPreview,[updateAspect](int){updateAspect();});
    auto rebuildPresets=[presetLayout,presetHost,projectWidth,projectHeight,updateAspect,presetsByTab](int tabId){
        clearLayoutItems(presetLayout);
        const QVector<CanvasPreset> presets=presetsByTab.value(tabId,presetsByTab.value(0));
        for(int index=0;index<presets.size();++index){
            const CanvasPreset &preset=presets[index];auto *card=new QToolButton(presetHost);
            card->setText(QString("%1\n%2 × %3").arg(preset.name).arg(preset.width).arg(preset.height));card->setToolButtonStyle(Qt::ToolButtonTextOnly);card->setMinimumSize(118,58);card->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            card->setStyleSheet("QToolButton{background:#0d1017;border:1px solid #303847;border-radius:9px;color:#dfeaff;font-size:10px;font-weight:700;}QToolButton:hover{border-color:#35c3f6;background:#151e2a;}");
            presetLayout->addWidget(card,index/2,index%2);
            connect(card,&QToolButton::clicked,presetHost,[projectWidth,projectHeight,updateAspect,preset]{projectWidth->setValue(preset.width);projectHeight->setValue(preset.height);updateAspect();});
        }
        for(int column=0;column<2;++column)presetLayout->setColumnStretch(column,1);
        presetHost->updateGeometry();
    };
    updateAspect();

    auto filteredPaths=[tabGroup]{
        QStringList selected;
        const int tab=tabGroup->checkedId();
        for(const QString &path:QSettings().value("recentDocuments").toStringList()){
            const QString suffix=QFileInfo(path).suffix().toLower();
            const bool visible=tab==0||(tab==1&&suffix=="aspectra")||(tab==2&&QStringList{"png","jpg","jpeg","webp","tif","tiff"}.contains(suffix))||(tab==3&&QStringList{"psd","psb","pdf"}.contains(suffix))||(tab==4&&QStringList{"svg","eps","ai"}.contains(suffix))||(tab==7&&QStringList{"mp4","mov","webm","mkv"}.contains(suffix))||(tab==8&&QStringList{"obj","fbx","gltf","glb"}.contains(suffix));
            if(visible)selected<<path;
        }
        return selected;
    };
    auto rebuild=[gallery,selectorLabel,filteredPaths,tabGroup,rebuildPresets]{const QStringList paths=filteredPaths();gallery->setProjectPaths(paths);selectorLabel->setText(paths.isEmpty()?"PROJECT SELECTOR · NO RECENT PROJECTS":QString("PROJECT SELECTOR · %1 PROJECT%2").arg(paths.size()).arg(paths.size()==1?"":"S"));rebuildPresets(tabGroup->checkedId());};
    bool expanded=false;
    auto slidePanel=[&](bool open){
        if(expanded==open)return;
        expanded=open;
        if(!open)settingsPanel->setMinimumHeight(0);
        settingsPanel->show();
        auto *animation=new QPropertyAnimation(settingsPanel,"maximumHeight",settingsPanel);
        animation->setDuration(260);animation->setEasingCurve(QEasingCurve::OutCubic);
        animation->setStartValue(settingsPanel->maximumHeight());animation->setEndValue(open?420:0);
        connect(animation,&QPropertyAnimation::finished,settingsPanel,[settingsPanel,open]{if(open)settingsPanel->setMinimumHeight(420);else settingsPanel->hide();});
        animation->start(QAbstractAnimation::DeleteWhenStopped);
        if(open){galleryOverlay->show();galleryOverlay->raise();}
        auto *overlayAnimation=new QPropertyAnimation(overlayEffect,"opacity",galleryOverlay);
        overlayAnimation->setDuration(260);overlayAnimation->setEasingCurve(QEasingCurve::OutCubic);
        overlayAnimation->setStartValue(overlayEffect->opacity());overlayAnimation->setEndValue(open?1.0:0.0);
        connect(overlayAnimation,&QPropertyAnimation::finished,galleryOverlay,[galleryOverlay,open]{if(!open)galleryOverlay->hide();});
        overlayAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    };
    connect(tabGroup,&QButtonGroup::idClicked,&dialog,[rebuild](int){rebuild();});
    connect(newProject,&QPushButton::clicked,&dialog,[&]{slidePanel(true);});
    connect(back,&QPushButton::clicked,&dialog,[&]{slidePanel(false);});
    connect(create,&QPushButton::clicked,&dialog,[this,projectName,projectWidth,projectHeight,projectResolution,unit,artboard,mode,&dialog]{const double scale=unit->currentText()=="in"?projectResolution->value():(unit->currentText()=="mm"?projectResolution->value()/25.4:1.0);createNewProject(qMax(1,qRound(projectWidth->value()*scale)),qMax(1,qRound(projectHeight->value()*scale)),projectResolution->value(),artboard->isChecked(),mode->currentText(),"sRGB IEC61966-2.1",Qt::transparent);const QString name=projectName->text().trimmed();if(!name.isEmpty()&&canvasLayers.contains(currentFile))canvasLayers[currentFile].name=name;dialog.accept();});
    auto openPath=[this,&dialog](const QString &path){const QString suffix=QFileInfo(path).suffix().toLower();if(suffix=="aspectra")openProjectFile(path);else if(suffix=="obj"||suffix=="fbx"||suffix=="gltf"||suffix=="glb")loadModelFile(path);else loadFiles({path});dialog.accept();};
    connect(gallery,&AspectraGalleryScreen::openProjectRequested,&dialog,openPath);
    connect(loadProject,&QPushButton::clicked,&dialog,[&]{const QString path=QFileDialog::getOpenFileName(&dialog,"Load project or media",QSettings().value("lastImportFolder").toString(),"Aspectra and media (*.aspectra *.psd *.psb *.ai *.xd *.png *.jpg *.jpeg *.webp *.tif *.tiff *.mp4 *.mov *.webm *.mkv *.obj *.fbx *.gltf *.glb);;All files (*.*)");if(!path.isEmpty())openPath(path);});
    rebuild();
    dialog.exec();
}

#if 0
void MainWindow::showWelcomeScreen(){
    autosaveTimer.setInterval(30000);if(!autosaveTimer.isActive()){connect(&autosaveTimer,&QTimer::timeout,this,&MainWindow::autosaveProject);autosaveTimer.start();}
    QDialog dialog(this);dialog.setWindowTitle("Aspectra X · Welcome");dialog.setModal(true);dialog.setMinimumSize(560,680);auto *root=new QVBoxLayout(&dialog);root->setContentsMargins(18,18,18,18);root->setSpacing(10);
    auto *brand=label("ASPECTRA X","section");brand->setAlignment(Qt::AlignCenter);root->addWidget(brand);
    auto *tabLayout=new QHBoxLayout;auto *tabGroup=new QButtonGroup(&dialog);tabGroup->setExclusive(true);const QStringList tabs{"Recent","Saved","Photo","Print","Art","Web","Mobile","Film","3D Model"};for(int i=0;i<tabs.size();++i){auto *tab=new QPushButton(tabs[i],&dialog);tab->setCheckable(true);tab->setToolTip(tabs[i]);tabLayout->addWidget(tab);tabGroup->addButton(tab,i);}tabGroup->button(0)->setChecked(true);root->addLayout(tabLayout);
    auto *projectRail=new QHBoxLayout;auto *newProject=button("New Project",projectRail,"blue");auto *loadProjectButton=button("Load Project",projectRail);projectRail->addStretch();root->addLayout(projectRail);
    auto *settingsPanel=new QWidget(&dialog);auto *settings=new QGridLayout(settingsPanel);settings->setContentsMargins(0,0,0,0);auto *projectWidth=number(1920,16384),*projectHeight=number(1080,16384),*projectResolution=number(300,2400);auto *unit=new QComboBox;unit->addItems({"px","in","mm"});auto *orientation=new QComboBox;orientation->addItems({"Landscape","Portrait"});auto *artboard=new QCheckBox("Artboard");artboard->setChecked(true);auto *mode=new QComboBox;mode->addItems({"RGB","CMYK","Grayscale"});auto *bit=new QComboBox;bit->addItems({"8 bit","16 bit","32 bit"});auto *profile=new QComboBox;profile->addItems({"sRGB IEC61966-2.1","Display P3","Adobe RGB (1998)","CMYK Coated FOGRA39"});auto *pixelAspect=new QComboBox;pixelAspect->addItems({"Square 1.0","DV 1.067","HDV 1.333"});auto *background=button("Background: Transparent");background->setProperty("color",QColor(Qt::transparent));settings->addWidget(label("Width"),0,0);settings->addWidget(projectWidth,0,1);settings->addWidget(label("Height"),0,2);settings->addWidget(projectHeight,0,3);settings->addWidget(unit,0,4);settings->addWidget(label("Resolution"),1,0);settings->addWidget(projectResolution,1,1);settings->addWidget(orientation,1,2);settings->addWidget(artboard,1,3,1,2);settings->addWidget(label("Color Mode"),2,0);settings->addWidget(mode,2,1);settings->addWidget(bit,2,2);settings->addWidget(background,2,3,1,2);settings->addWidget(label("Color Profile"),3,0);settings->addWidget(profile,3,1,1,2);settings->addWidget(pixelAspect,3,3,1,2);auto *create=button("Create Project",settings,"blue");settings->addWidget(create,4,3,1,2);settingsPanel->hide();root->addWidget(settingsPanel);
    auto makeDimensionSlider=[](QSpinBox *backing){auto *control=new QWidget;auto *line=new QHBoxLayout(control);line->setContentsMargins(0,0,0,0);line->setSpacing(8);auto *slider=new GradientSlider;slider->setRange(64,8192);slider->setValue(backing->value());backing->setObjectName("DimensionNumberField");backing->setFixedWidth(58);if(auto *editor=backing->findChild<QLineEdit*>())editor->setAlignment(Qt::AlignCenter);line->addWidget(slider,1);line->addWidget(backing);QObject::connect(slider,&QSlider::valueChanged,backing,[backing](int n){backing->setValue(n);});QObject::connect(backing,qOverload<int>(&QSpinBox::valueChanged),control,[slider](int n){QSignalBlocker block(slider);slider->setValue(qBound(slider->minimum(),n,slider->maximum()));});return control;};settings->removeWidget(projectWidth);settings->removeWidget(projectHeight);for(auto *oldLabel:settingsPanel->findChildren<QLabel*>()){settings->removeWidget(oldLabel);oldLabel->hide();oldLabel->deleteLater();}auto *widthControl=makeDimensionSlider(projectWidth);auto *heightControl=makeDimensionSlider(projectHeight);auto *dimensionsSeparator=label("×");dimensionsSeparator->setAlignment(Qt::AlignCenter);dimensionsSeparator->setObjectName("DimensionsSeparator");settings->setHorizontalSpacing(16);settings->setVerticalSpacing(10);settings->setColumnStretch(1,1);settings->setColumnStretch(4,1);settings->setColumnMinimumWidth(2,22);settings->addWidget(label("Width"),0,0);settings->addWidget(widthControl,0,1);settings->addWidget(dimensionsSeparator,0,2);settings->addWidget(label("Height"),0,3);settings->addWidget(heightControl,0,4);settings->addWidget(label("Unit"),1,0);settings->addWidget(unit,1,1);settings->addWidget(label("Resolution"),1,3);settings->addWidget(projectResolution,1,4);settings->addWidget(label("Orientation"),2,0);settings->addWidget(orientation,2,1);settings->addWidget(label("Artboard"),2,3);settings->addWidget(artboard,2,4);settings->addWidget(label("Color mode"),3,0);settings->addWidget(mode,3,1);settings->addWidget(label("Bit depth"),3,3);settings->addWidget(bit,3,4);settings->addWidget(label("Color profile"),4,0);settings->addWidget(profile,4,1);settings->addWidget(label("Pixel aspect"),4,3);settings->addWidget(pixelAspect,4,4);settings->addWidget(label("Background"),5,0);settings->addWidget(background,5,1);settings->addWidget(create,5,3,1,2);
    auto *recentLabel=label("RECENT PROJECTS","section");root->addWidget(recentLabel);auto *scroll=new QScrollArea(&dialog);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto *host=new QWidget(scroll);auto *grid=new QGridLayout(host);grid->setContentsMargins(0,0,0,0);grid->setSpacing(8);scroll->setWidget(host);root->addWidget(scroll,1);root->removeWidget(brand);brand->hide();root->removeItem(projectRail);root->removeWidget(settingsPanel);root->removeWidget(recentLabel);root->removeWidget(scroll);root->addWidget(recentLabel);root->addWidget(scroll,1);root->addLayout(projectRail);root->addWidget(settingsPanel);
    auto rebuild=[this,grid,host,tabGroup,recentLabel,&dialog]{while(auto *item=grid->takeAt(0)){if(item->widget())item->widget()->deleteLater();delete item;}int placed=0;const int tab=tabGroup->checkedId();for(const QString &path:QSettings().value("recentDocuments").toStringList()){const QFileInfo file(path);if(!file.exists())continue;const QString suffix=file.suffix().toLower();const bool show=tab==0||(tab==1&&suffix=="aspectra")||(tab==2&&QStringList{"png","jpg","jpeg","webp","tif","tiff"}.contains(suffix))||(tab==3&&QStringList{"psd","psb","pdf"}.contains(suffix))||(tab==4&&QStringList{"svg","eps","ai"}.contains(suffix))||(tab==7&&QStringList{"mp4","mov","webm","mkv"}.contains(suffix))||(tab==8&&QStringList{"obj","fbx","gltf","glb"}.contains(suffix));if(!show)continue;auto *tile=new QPushButton(file.completeBaseName()+"\n"+suffix.toUpper(),host);tile->setMinimumSize(96,78);tile->setToolTip(file.absoluteFilePath());grid->addWidget(tile,placed/5,placed%5);connect(tile,&QPushButton::clicked,this,[this,path,&dialog]{const QString suffix=QFileInfo(path).suffix().toLower();if(suffix=="aspectra")openProjectFile(path);else if(suffix=="obj"||suffix=="fbx"||suffix=="gltf"||suffix=="glb")loadModelFile(path);else loadFiles({path});dialog.accept();});if(++placed==20)break;}recentLabel->setText(placed?"RECENT PROJECTS":"NO RECENT PROJECTS");};
    connect(tabGroup,&QButtonGroup::idClicked,&dialog,[rebuild](int){rebuild();});connect(newProject,&QPushButton::clicked,&dialog,[settingsPanel]{settingsPanel->setVisible(!settingsPanel->isVisible());});connect(orientation,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[projectWidth,projectHeight](int){const int width=projectWidth->value();projectWidth->setValue(projectHeight->value());projectHeight->setValue(width);});connect(background,&QPushButton::clicked,&dialog,[background,&dialog]{QColor color=QColorDialog::getColor(background->property("color").value<QColor>(),&dialog,"Canvas background",QColorDialog::ShowAlphaChannel);if(color.isValid()){background->setProperty("color",color);background->setText(color.alpha()==0?"Background: Transparent":"Background: "+color.name(QColor::HexArgb));}});connect(create,&QPushButton::clicked,&dialog,[this,projectWidth,projectHeight,projectResolution,unit,artboard,mode,profile,background,&dialog]{const double scale=unit->currentText()=="in"?projectResolution->value():(unit->currentText()=="mm"?projectResolution->value()/25.4:1.);createNewProject(qMax(1,qRound(projectWidth->value()*scale)),qMax(1,qRound(projectHeight->value()*scale)),projectResolution->value(),artboard->isChecked(),mode->currentText(),profile->currentText(),background->property("color").value<QColor>());dialog.accept();});connect(loadProjectButton,&QPushButton::clicked,&dialog,[this,&dialog]{const QString path=QFileDialog::getOpenFileName(&dialog,"Load project or Photoshop document",QSettings().value("lastImportFolder").toString(),"Aspectra / Photoshop (*.aspectra *.psd *.psb);;All supported media (*.*)");if(path.isEmpty())return;if(QFileInfo(path).suffix().compare("aspectra",Qt::CaseInsensitive)==0)openProjectFile(path);else loadFiles({path});dialog.accept();});if(QFileInfo::exists(recoveryFilePath())){auto *recover=button("Recover last autosave",projectRail,"gold");connect(recover,&QPushButton::clicked,&dialog,[this,&dialog]{openProjectFile(recoveryFilePath());dialog.accept();});}rebuild();dialog.exec();
}
#endif
void MainWindow::loadFiles(const QStringList &paths){
    if(paths.isEmpty())return;if(galleryScreen)galleryScreen->hide();if(loading){status->setText("Wait for the current file to load.");return;}for(const QString &path:paths)rememberRecentDocument(path);compositionMode=false;compositionPlaying=false;compositionTimer.stop();QStringList importPaths=paths,imported;
    if(QFileInfo(paths.first()).exists())QSettings().setValue("lastImportFolder",QFileInfo(paths.first()).absolutePath());
    // Selecting one numbered still (for example 01.png) automatically imports
    // its same-folder sequence so batch SVG names remain in sequence.
    if(paths.size()==1){const QFileInfo first(paths.first());const QString suffix=first.suffix().toLower();const auto match=QRegularExpression(R"(^(.*?)(\d{2,})$)").match(first.completeBaseName());if(match.hasMatch()&&!VideoProcessor::isVideo(first.absoluteFilePath())&&suffix!="psd"&&suffix!="psb"){QStringList sequence;const QString prefix=match.captured(1),digits=match.captured(2);for(const auto &name:QDir(first.absolutePath()).entryList({"*."+suffix},QDir::Files,QDir::Name)){const QFileInfo candidate(QDir(first.absolutePath()).filePath(name));const auto candidateMatch=QRegularExpression(R"(^(.*?)(\d{2,})$)").match(candidate.completeBaseName());if(candidateMatch.hasMatch()&&candidateMatch.captured(1)==prefix&&candidateMatch.captured(2).size()==digits.size())sequence<<candidate.absoluteFilePath();}if(sequence.size()>1){importPaths=sequence;status->setText(QString("Detected %1-image sequence").arg(sequence.size()));}}}
    for(const auto &path:importPaths){if(!BatchManager::supported(path)||!QFileInfo::exists(path))continue;QString suffix=QFileInfo(path).suffix().toLower();if(suffix=="psd"||suffix=="psb"){
        const QString source=QFileInfo(path).absoluteFilePath();
        const QString cache=captures.filePath("psd-"+QUuid::createUuid().toString(QUuid::Id128));
        QString error;
        QStringList artboards=PsdImporter::extractArtboards(this,source,cache,&error);
        // The bridge records the exact files it has rendered. Read that record as
        // the authoritative hand-off, so a completed PSD artboard render can never
        // fall through to the one-image composite import.
        QMap<QString,QString> artboardNames;
        QFile artboardManifest(QDir(cache).filePath("aspectra-artboards.json"));
        if(artboardManifest.open(QIODevice::ReadOnly)){
            const QJsonArray records=QJsonDocument::fromJson(artboardManifest.readAll()).object().value("artboards").toArray();
            for(const auto &value:records){const QJsonObject record=value.toObject();const QString image=QDir(cache).filePath(record.value("file").toString());if(QFileInfo::exists(image)){if(!artboards.contains(image,Qt::CaseInsensitive))artboards<<image;artboardNames[image]=record.value("name").toString(QFileInfo(image).completeBaseName());}}
        }
        if(!artboards.isEmpty()){for(const auto &image:artboards){inputAliases[image]=source;inputTitles[image]=artboardNames.value(image,QFileInfo(image).completeBaseName());imported<<image;}status->setText(QString("Imported %1 PSD artboards as separate images").arg(artboards.size()));continue;}
        QString image=PsdImporter::importFile(this,source,cache);if(image.isEmpty())continue;inputAliases[image]=source;imported<<image;
    }else imported<<QFileInfo(path).absoluteFilePath();}
    batch.add(imported);updateBatchLabel();if(!imported.isEmpty())selectFile(imported.first());
}
void MainWindow::selectFile(const QString &path){
      if(batchStrip){batchStrip->show();if(auto *dock=batchStrip->parentWidget())dock->setFixedHeight(164);}if(exportButton)exportButton->setEnabled(true);
      if(loading)return;if(path!=currentFile)preview->setMaskEditMode(false);stopPlayback();player->setSource({});currentFile=path;const bool isArtboard=path.startsWith(QLatin1String("aspectra://"));if(!canvasLayers.contains(path)){CanvasLayer layer;layer.source=path;layer.name=isArtboard?QString("Artboard %1").arg(batch.files.indexOf(path)+1):QFileInfo(inputTitles.value(path,inputAliases.value(path,path))).completeBaseName();if(isArtboard)layer.nativeSize=QSize(widthInput->value(),heightInput->value());canvasLayers[path]=layer;}restoreLayerOverride();bool video=!isArtboard&&VideoProcessor::isVideo(path);modes->button(video?1:0)->setChecked(true);captureRow->hide();videoRow->setVisible(video);inMarker->setEnabled(video);outMarker->setEnabled(video);
    QString name=isArtboard?canvasLayers.value(path).name:inputTitles.value(path,QFileInfo(inputAliases.value(path,path)).fileName());filename->setText(fontMetrics().elidedText(name,Qt::ElideMiddle,qMax(160,width()-190)));filename->setToolTip(isArtboard?name:inputAliases.value(path,path));{QSignalBlocker block(navigation);navigation->setCurrentIndex(batch.files.indexOf(path));}if(batchNavigator){QSignalBlocker block(batchNavigator);batchNavigator->setValue(batch.files.indexOf(path));}preview->setActiveArtboard(path);navLabel->setText(QString("%1 / %2").arg(batch.files.indexOf(path)+1).arg(batch.files.size()));
    if(isArtboard){
        // Artboards are synthetic canvases with no file on disk (identified by
        // an aspectra:// URI). Routing them through the async file/video loader
        // below would always fail (no such file), silently pop an error and
        // leave the previous artboard's image on screen. Build a blank
        // transparent frame from the stored layer size instead.
        loading=false;const auto &layer=canvasLayers[path];QSize size=layer.nativeSize.isValid()?layer.nativeSize:QSize(widthInput->value(),heightInput->value());
        original=layer.artboardImage.isNull()?QImage(size,QImage::Format_ARGB32):layer.artboardImage;if(layer.artboardImage.isNull())original.fill(Qt::transparent);texturePreviewSource={};media.size=size;preview->setLayerMask(layer.mask);preview->clearVectorTraceFrame();preview->clearTextureFrame();
        refresh();status->setText(QString("Artboard \"%1\" ready").arg(layer.name));
        return;
    }
    status->setText("Loading…");loading=true;QString ffmpeg=VideoProcessor::findFfmpeg();loadWatcher.setFuture(QtConcurrent::run([path,video,ffmpeg]{LoadedMedia result;try{if(video){result.info=VideoProcessor::inspect(ffmpeg,path);result.image=VideoProcessor::firstFrame(ffmpeg,path);}else{result.image=ImageProcessor::read(path);result.info.size=result.image.size();}}catch(const std::exception &e){result.error=QString::fromUtf8(e.what());}return result;}));
}
void MainWindow::navigate(int delta){if(batch.files.isEmpty()||loading)return;int index=batch.files.indexOf(currentFile);index=(index+delta+batch.files.size())%batch.files.size();selectFile(batch.files[index]);}
void MainWindow::updateBatchLabel(){
    qint64 sourceBytes=0;for(const auto &path:batch.files)sourceBytes+=QFileInfo(path).size();int formats=0;for(auto it=imageFormats.cbegin();it!=imageFormats.cend();++it)if(it.value()->isChecked())++formats;for(auto it=videoFormats.cbegin();it!=videoFormats.cend();++it)if(it.value()->isChecked())++formats;double estimate=sourceBytes*(formats?qMax(.1,formats*(quality?quality->value()/100.:.9)):1.);auto formatBytes=[](double bytes){return bytes<1024*1024?QString::number(qRound(bytes/1024))+" KB":QString::number(bytes/(1024*1024),'f',1)+" MB";};batchLabel->setText(QString("%1 selected · source %2 · estimated export %3").arg(batch.files.size()).arg(formatBytes(sourceBytes)).arg(formatBytes(estimate)));batchLabel->setToolTip("Estimate uses selected formats and quality. Actual video and compressed-image output can vary.");QSignalBlocker block(navigation);navigation->clear();for(const auto &path:batch.files)navigation->addItem(canvasLayers.value(path).name.isEmpty()?inputTitles.value(path,QFileInfo(inputAliases.value(path,path)).fileName()):canvasLayers.value(path).name,path);navigation->setCurrentIndex(batch.files.indexOf(currentFile));if(batchNavigator){QSignalBlocker sliderBlock(batchNavigator);batchNavigator->setRange(0,qMax(0,int(batch.files.size())-1));batchNavigator->setValue(qMax(0,batch.files.indexOf(currentFile)));}navLabel->setText(QString("%1 / %2").arg(qMax(0,batch.files.indexOf(currentFile)+1)).arg(batch.files.size()));
}
void MainWindow::manageBatch(){
    QDialog dialog(this);dialog.setWindowTitle("Selected images & videos");dialog.resize(490,380);auto *v=new QVBoxLayout(&dialog);auto *list=new QListWidget;list->setSelectionMode(QAbstractItemView::ExtendedSelection);v->addWidget(list,1);
    auto populate=[&]{list->clear();for(const auto &path:batch.files){auto *item=new QListWidgetItem(inputTitles.value(path,QFileInfo(inputAliases.value(path,path)).fileName()),list);item->setData(Qt::UserRole,path);item->setToolTip(inputAliases.value(path,path));}};populate();auto *r=row(v);auto *add=button("Add…",r,"blue"),*remove=button("Remove",r,"pink"),*view=button("View",r);connect(add,&QPushButton::clicked,&dialog,[&]{chooseFiles();populate();});connect(remove,&QPushButton::clicked,&dialog,[&]{for(auto *item:list->selectedItems()){batch.files.removeAll(item->data(Qt::UserRole).toString());delete item;}updateBatchLabel();});connect(view,&QPushButton::clicked,&dialog,[&]{if(list->currentItem()){selectFile(list->currentItem()->data(Qt::UserRole).toString());dialog.accept();}});auto *done=button("Done",v);connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);dialog.exec();
}
void MainWindow::addSize(int size){
    if(!size){bool ok=false;size=QInputDialog::getInt(this,"Add output width","Width in pixels",1024,1,8192,1,&ok);if(!ok)return;}for(int i=0;i<sizes->count();++i)if(sizes->item(i)->data(Qt::UserRole).toInt()==size){sizes->item(i)->setCheckState(Qt::Checked);return;}auto *item=new QListWidgetItem(QString::number(size),sizes);item->setData(Qt::UserRole,size);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(Qt::Checked);
}
void MainWindow::syncRange(bool save){
    if(!timeline)return;double a=inMarker->value(),b=outMarker->value();timeline->setRange(a,b);if(save&&!currentFile.isEmpty()&&VideoProcessor::isVideo(currentFile))draftRanges[currentFile]={a,b};
}
void MainWindow::addClip(){
    if(!VideoProcessor::isVideo(currentFile)){showError("Select a video to add clip ranges.");return;}if(outMarker->value()<=inMarker->value()){showError("The Out marker must be after In.");return;}
    auto &ranges=clips[currentFile];ranges<<ClipRange{QString("Clip %1").arg(ranges.size()+1),inMarker->value(),outMarker->value(),true};updateClipTable();
}
void MainWindow::updateClipTable(){
    if(!clipTable)return;updatingClips=true;auto ranges=clips.value(currentFile);clipTable->setRowCount(ranges.size());for(int i=0;i<ranges.size();++i){auto *enabled=new QTableWidgetItem;enabled->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable);enabled->setCheckState(ranges[i].enabled?Qt::Checked:Qt::Unchecked);clipTable->setItem(i,0,enabled);clipTable->setItem(i,1,new QTableWidgetItem(ranges[i].name));for(int col=2;col<4;++col)clipTable->setItem(i,col,new QTableWidgetItem(QString::number(col==2?ranges[i].start:ranges[i].end,'f',3)));}updatingClips=false;timeline->setClips(ranges);
}
void MainWindow::updateTrackPanel(){
    if(!trackList)return;
    const int selected=qBound(0,trackList->currentRow(),qMax(0,timelineTracks.size()-1));
    QSignalBlocker block(trackList);trackList->clear();
    for(const auto &track:timelineTracks){auto *item=new QListWidgetItem(QString("%1  %2  %3–%4 s").arg(track.enabled?"●":"○",track.name).arg(track.start,0,'f',2).arg(track.end,0,'f',2),trackList);item->setToolTip(track.type==TimelineTrack::Text?track.text:track.source);}
    if(!timelineTracks.isEmpty())trackList->setCurrentRow(selected);
    if(selected>=0&&selected<timelineTracks.size()){
        const auto &track=timelineTracks[selected];
        QSignalBlocker x(trackX),y(trackY),scale(trackScale),rotation(trackRotation),opacity(trackOpacity),blend(trackBlend),transition(trackTransition);
        trackX->setValue(qRound(track.position.x()));trackY->setValue(qRound(track.position.y()));trackScale->setValue(qRound(track.scale.x()*100));trackRotation->setValue(qRound(track.rotation));trackOpacity->setValue(qRound(track.opacity*100));trackBlend->setCurrentText(track.blendMode);trackTransition->setCurrentText(track.transition);
    }
    const bool have=!timelineTracks.isEmpty();
    for(auto *control:{static_cast<QWidget*>(trackX),static_cast<QWidget*>(trackY),static_cast<QWidget*>(trackScale),static_cast<QWidget*>(trackRotation),static_cast<QWidget*>(trackOpacity),static_cast<QWidget*>(trackBlend),static_cast<QWidget*>(trackTransition)})if(control)control->setEnabled(have);
    if(timeline)timeline->setTracks(timelineTracks);
}
QImage MainWindow::compositeTimelineTracks(const QImage &source) const{
    const double time=compositionMode&&!VideoProcessor::isVideo(currentFile)?compositionPosition:(player?qMax(0.,player->position()/1000.):0.);
    QVector<TimelineTrack> visibleTracks;
    visibleTracks.reserve(timelineTracks.size());
    for (const auto &track : timelineTracks)
        if (track.artboardSource == currentFile || (track.artboardSource.isEmpty() && !currentFile.startsWith(QLatin1String("aspectra://"))))
            visibleTracks.append(track);
    return ImageProcessor::compositeTimeline(source,visibleTracks,time);
}
void MainWindow::togglePlayback(){if(original.isNull())return;if(compositionMode&&!VideoProcessor::isVideo(currentFile)){compositionPlaying=!compositionPlaying;if(compositionPlaying){compositionTimer.start();playButton->setText("Ⅱ Pause");}else{compositionTimer.stop();playButton->setText("▶ Play");}return;}if(!VideoProcessor::isVideo(currentFile))return;if(player->playbackState()==QMediaPlayer::PlayingState)player->pause();else player->play();}
void MainWindow::stopPlayback(){if(player)player->pause();compositionPlaying=false;compositionTimer.stop();if(playButton)playButton->setText("▶ Play");}
void MainWindow::showExportWindow(){
    if(!exportDialog){exportDialog=new QDialog(this);exportDialog->setWindowTitle("Aspectra · Export");exportDialog->setWindowModality(Qt::WindowModal);exportDialog->setFixedSize(462,370);auto *v=new QVBoxLayout(exportDialog);v->setContentsMargins(20,20,20,20);v->addWidget(label("EXPORT QUEUE","section"));exportStatus=label("Preparing outputs…");exportStatus->setWordWrap(true);v->addWidget(exportStatus);exportProgress=new QProgressBar;v->addWidget(exportProgress);exportLog=new QPlainTextEdit;exportLog->setReadOnly(true);v->addWidget(exportLog,1);auto *r=row(v);exportOpen=button("Open folder",r,"blue");exportCancel=button("Cancel export",r,"pink");exportDone=button("Close",r);connect(exportOpen,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(lastFolder));});connect(exportCancel,&QPushButton::clicked,this,[this]{if(cancelFlag)*cancelFlag=true;exportStatus->setText("Cancelling…");});connect(exportDone,&QPushButton::clicked,exportDialog,&QDialog::hide);}
    exportCancel->show();exportDone->setEnabled(false);exportOpen->setEnabled(false);exportProgress->setValue(0);exportLog->clear();exportStatus->setText("Preparing outputs…");exportDialog->move(frameGeometry().center()-exportDialog->rect().center());exportDialog->show();exportDialog->raise();
}
void MainWindow::exportFiles(){
    if(exportWatcher.isRunning()){exportDialog->show();exportDialog->raise();return;}if(batch.files.isEmpty()){showError("Import images or videos first.");return;}
    ExportJob job;job.files=batch.files;job.inputAliases=inputAliases;job.adjustments=adjustments();job.presets=presets;job.clips=clips;job.timelineTracks=timelineTracks;for(const auto &path:batch.files)if(!canvasLayers.value(path).mask.isNull())job.masks[path]=canvasLayers.value(path).mask;job.workers=workerCount->currentText().toInt();job.pngCompression=1;job.archiveLevel=archiveMode->currentIndex()==1?0:archiveMode->currentIndex()==2?9:1;
    for(auto it=imageFormats.begin();it!=imageFormats.end();++it)if(it.value()->isChecked())job.imageFormats<<it.key();for(auto it=videoFormats.begin();it!=videoFormats.end();++it)if(it.value()->isChecked())job.videoFormats<<it.key();
    for(const auto &path:job.files){bool video=VideoProcessor::isVideo(path);if((video?job.videoFormats:job.imageFormats).isEmpty()){showError(video?"Check at least one video format in Output.":"Check at least one image format in Output.");return;}if(video&&!job.clips.contains(path)&&draftRanges.contains(path)){auto r=draftRanges[path];if(r.second<=r.first){showError("Invalid In / Out markers for "+QFileInfo(path).fileName());return;}job.clips[path]={ClipRange{"Range",r.first,r.second,true}};}}
    for(int i=0;i<sizes->count();++i)if(sizes->item(i)->checkState()==Qt::Checked){int w=sizes->item(i)->data(Qt::UserRole).toInt(),h=qMax(1,qRound(double(w)*heightInput->value()/widthInput->value()));if(h>8192){showError("An output height exceeds 8192 pixels.");return;}job.sizes<<QSize(w,h);}if(job.sizes.isEmpty()){showError("Check an output width in Output.");return;}
    job.ffmpeg=VideoProcessor::findFfmpeg();job.rar=VideoProcessor::findRar();job.zip=zip->isChecked();job.createRar=rar->isChecked();job.quality=quality->value();job.crf=crf->value();
    for(const auto &path:job.files)if(VideoProcessor::isVideo(path)&&job.ffmpeg.isEmpty()){showError("Select ffmpeg.exe in Settings.");return;}
    job.outputRoot=QFileDialog::getExistingDirectory(this,"Choose export destination",QSettings().value("outputRoot",QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString());if(job.outputRoot.isEmpty())return;QSettings().setValue("outputRoot",job.outputRoot);stopPlayback();cancelFlag=std::make_shared<std::atomic_bool>(false);auto cancel=cancelFlag;exportButton->setEnabled(false);showExportWindow();
    exportWatcher.setFuture(QtConcurrent::run([this,job,cancel]{return ExportManager::run(job,*cancel,[this](int n,int total,QString text){QMetaObject::invokeMethod(this,[this,n,total,text]{exportProgress->setMaximum(total);exportProgress->setValue(n);exportStatus->setText(text);if(text.startsWith("Rendered"))exportLog->appendPlainText(text);status->setText(QString("Exporting %1 / %2").arg(n).arg(total));},Qt::QueuedConnection);});}));
}
void MainWindow::settingsDialog(){
    QDialog dialog(this);dialog.setWindowTitle("Aspectra settings");dialog.setMinimumWidth(440);auto *v=new QVBoxLayout(&dialog);v->addWidget(label("EXTERNAL ENCODERS","section"));for(const auto &key:QStringList{"ffmpeg","rar"}){auto *r=row(v);r->addWidget(label(key.toUpper()));auto *path=new QLineEdit(key=="ffmpeg"?VideoProcessor::findFfmpeg():VideoProcessor::findRar());r->addWidget(path,1);auto *browse=button("…",r);connect(browse,&QPushButton::clicked,&dialog,[&,path,key]{auto file=QFileDialog::getOpenFileName(&dialog,"Select "+key+".exe",{},"Executable (*.exe)");if(!file.isEmpty())path->setText(file);});connect(path,&QLineEdit::textChanged,&dialog,[this,key](QString value){QSettings().setValue(key,value);rar->setEnabled(!VideoProcessor::findRar().isEmpty());if(!rar->isEnabled())rar->setChecked(false);if(textureRar){textureRar->setEnabled(!VideoProcessor::findRar().isEmpty());if(!textureRar->isEnabled())textureRar->setChecked(false);}});}
    auto *godMode=button("God Mode · inspect & export customization…",v,"blue");godMode->setToolTip("Opens Aspectra's portable customization document. It contains the exact active canvas, adjustment, keying, export, and media settings.");connect(godMode,&QPushButton::clicked,this,[this]{auto buildDocument=[this]{QJsonObject root{{"schema","aspectra-x/customization"},{"version",1},{"profile","God Mode"},{"createdUtc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};QJsonObject canvas{{"width",widthInput->value()},{"height",heightInput->value()},{"cropScale",zoom->value()},{"lockAspect",lock->isChecked()}};root.insert("canvas",canvas);QJsonObject adjust;for(auto it=sliders.cbegin();it!=sliders.cend();++it)adjust.insert(it.key(),it.value()->value());root.insert("adjustments",adjust);auto value=[](QSlider *control){return control?control->value():0;};root.insert("keying",QJsonObject{{"enabled",chromaKeyEnabled->isChecked()},{"keyColor",layerStyle.keyColor.name(QColor::HexArgb)},{"colorDistance",value(keyToleranceSlider)},{"edgeFeather",value(keySoftnessSlider)},{"despill",value(keyDespillSlider)},{"protectDark",value(keyLumaProtectSlider)},{"matteChoke",value(keyMatteBiasSlider)},{"cleanBlack",value(keyCleanBlackSlider)},{"cleanWhite",value(keyCleanWhiteSlider)}});QJsonArray image;for(auto it=imageFormats.cbegin();it!=imageFormats.cend();++it)if(it.value()->isChecked())image.append(it.key());QJsonArray video;for(auto it=videoFormats.cbegin();it!=videoFormats.cend();++it)if(it.value()->isChecked())video.append(it.key());QJsonArray outputSizes;for(int i=0;i<sizes->count();++i)if(sizes->item(i)->checkState()==Qt::Checked)outputSizes.append(sizes->item(i)->data(Qt::UserRole).toInt());root.insert("export",QJsonObject{{"imageFormats",image},{"videoFormats",video},{"widths",outputSizes},{"quality",quality->value()},{"crf",crf->value()},{"zip",zip->isChecked()},{"rar",rar->isChecked()}});QJsonArray sources;for(const auto &path:batch.files)sources.append(QJsonObject{{"name",inputTitles.value(path,QFileInfo(path).fileName())},{"path",path}});root.insert("media",QJsonObject{{"active",currentFile},{"sources",sources}});root.insert("ui",QJsonObject{{"layout","portrait-9:16"},{"sliderTreatment","green-blue-fluid-sweep"},{"sliderSweepSeconds",4.6}});return QJsonDocument(root).toJson(QJsonDocument::Indented);};QDialog god(this);god.setWindowTitle("Aspectra X · God Mode");god.resize(620,660);auto *layout=new QVBoxLayout(&god);auto *on=new QCheckBox("God Mode enabled",&god);on->setChecked(QSettings().value("godMode",true).toBool());layout->addWidget(on);auto *note=label("This is the canonical portable customization format used by Aspectra. It is editable JSON for inspection, versioning, and sharing.","muted");note->setWordWrap(true);layout->addWidget(note);auto *json=new QPlainTextEdit(QString::fromUtf8(buildDocument()),&god);json->setReadOnly(true);layout->addWidget(json,1);auto *actions=row(layout);auto *copy=button("Copy JSON",actions);auto *save=button("Export customization…",actions,"blue");auto *done=button("Done",actions);connect(on,&QCheckBox::toggled,&god,[](bool enabled){QSettings().setValue("godMode",enabled);});connect(copy,&QPushButton::clicked,&god,[json]{QGuiApplication::clipboard()->setText(json->toPlainText());});connect(save,&QPushButton::clicked,&god,[this,json]{const QString path=QFileDialog::getSaveFileName(this,"Export Aspectra customization","aspectra-customization.aspectra.json","Aspectra customization (*.aspectra.json);;JSON (*.json)");if(path.isEmpty())return;QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(json->toPlainText().toUtf8())<0||!file.commit()){showError("Could not export the customization document.");return;}status->setText("God Mode customization exported · "+QFileInfo(path).fileName());});connect(done,&QPushButton::clicked,&god,&QDialog::accept);god.exec();});
    auto *reset=button("Reset color, crop and layer styles",v,"gold");connect(reset,&QPushButton::clicked,this,[this]{zoom->setValue(100);for(auto it=sliders.begin();it!=sliders.end();++it)it.value()->setValue(it.key()=="gamma"||it.key()=="contrast"||it.key()=="saturation"?100:it.key()=="whites"?255:0);for(auto *c:styleChecks)c->setChecked(false);flat->setChecked(false);previewVariant->setCurrentIndex(0);for(auto *n:paddingInputs)n->setValue(0);refresh();});v->addWidget(label("PSD reader is bundled. No Photoshop or separate Python install needed.","muted"));auto *done=button("Done",v,"blue");connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);dialog.exec();
}
void MainWindow::editPreset(int index){
    QDialog dialog(this);dialog.setWindowTitle("Edit "+presets[index].name+" variant");auto *v=new QVBoxLayout(&dialog);auto *color=button("Choose color…",v,"blue");connect(color,&QPushButton::clicked,this,[this,index,&dialog]{QColor selected=QColorDialog::getColor(presets[index].color,&dialog);if(selected.isValid()){presets[index].color=selected;refresh();}});auto *form=new QFormLayout;v->addLayout(form);auto *threshold=new QDoubleSpinBox;threshold->setRange(0,1);threshold->setSingleStep(.05);threshold->setValue(presets[index].threshold);form->addRow("Hide below luminance",threshold);auto *gamma=new QDoubleSpinBox;gamma->setRange(.1,3);gamma->setSingleStep(.1);gamma->setValue(presets[index].gamma);form->addRow("Alpha midtones",gamma);connect(threshold,&QDoubleSpinBox::valueChanged,this,[this,index](double value){presets[index].threshold=value;refresh();});connect(gamma,&QDoubleSpinBox::valueChanged,this,[this,index](double value){presets[index].gamma=value;refresh();});previewVariant->setCurrentIndex(index+1);auto *done=button("Done",v);connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);dialog.exec();
}
void MainWindow::showError(const QString &text){logs<<text;status->setText("Error · see Log");QMessageBox::warning(this,"Aspectra",text);}
void MainWindow::showLog(){QDialog dialog(this);dialog.setWindowTitle("Activity log");dialog.resize(490,350);auto *v=new QVBoxLayout(&dialog);auto *text=new QPlainTextEdit(logs.isEmpty()?"No operations yet.":logs.join('\n'));text->setReadOnly(true);v->addWidget(text);auto *done=button("Close",v);connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);dialog.exec();}
void MainWindow::dragEnterEvent(QDragEnterEvent *event){if(event->mimeData()->hasUrls())event->acceptProposedAction();}
void MainWindow::dropEvent(QDropEvent *event){QStringList paths;for(const auto &url:event->mimeData()->urls())if(url.isLocalFile())paths<<url.toLocalFile();loadFiles(paths);event->acceptProposedAction();}
void MainWindow::mousePressEvent(QMouseEvent *event){if(event->button()==Qt::LeftButton&&event->position().y()<72)dragOffset=event->globalPosition().toPoint()-frameGeometry().topLeft();else dragOffset={};}
void MainWindow::mouseMoveEvent(QMouseEvent *event){if((event->buttons()&Qt::LeftButton)&&!dragOffset.isNull())move(event->globalPosition().toPoint()-dragOffset);}
void MainWindow::keyPressEvent(QKeyEvent *event){
    const bool focusCanvas=preview&&preview->parentWidget()&&preview->parentWidget()->objectName()=="shell";
    if(focusCanvas){
        if(event->key()==Qt::Key_Escape||event->key()==Qt::Key_0){preview->setViewZoom(1.0);event->accept();return;}
        if(event->key()==Qt::Key_Minus){preview->adjustViewZoomFromInput(-120,0,event->modifiers());event->accept();return;}
        if(event->key()==Qt::Key_Plus||event->key()==Qt::Key_Equal){preview->adjustViewZoomFromInput(120,0,event->modifiers());event->accept();return;}
    }
    AspectraWindow::keyPressEvent(event);
}
bool MainWindow::eventFilter(QObject *watched,QEvent *event){
    // A focused active slider can be adjusted directly from the canvas. This
    // consumes that drag only; ordinary painting, panning, and clicks remain.
    if(watched==preview && (event->type()==QEvent::MouseButtonPress ||
                            event->type()==QEvent::MouseMove ||
                            event->type()==QEvent::MouseButtonRelease)){
        auto *mouse=static_cast<QMouseEvent*>(event);
        if(event->type()==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton){
            if(auto *slider=dynamic_cast<GradientSlider*>(QApplication::focusWidget());slider && slider->objectName()=="ActiveSettingSlider" && slider->isVisible() && slider->isEnabled() && preview->cursor().shape()==Qt::ArrowCursor){
                blindSlider=slider;
                blindDragOrigin=mouse->globalPosition();
                blindValueOrigin=slider->value();
                slider->setCanvasEmphasis(true);
                return true;
            }
        }else if(event->type()==QEvent::MouseMove && blindSlider && (mouse->buttons()&Qt::LeftButton)){
            if(auto *slider=dynamic_cast<GradientSlider*>(blindSlider.data())){
                const qreal delta=mouse->globalPosition().x()-blindDragOrigin.x();
                const int range=slider->maximum()-slider->minimum();
                slider->setValueWithResistance(blindValueOrigin+qRound(delta*range/qMax(120,preview->width())));
            }
            return true;
        }else if(event->type()==QEvent::MouseButtonRelease && mouse->button()==Qt::LeftButton && blindSlider){
            if(auto *slider=dynamic_cast<GradientSlider*>(blindSlider.data()))slider->setCanvasEmphasis(false);
            blindSlider.clear();
            return true;
        }
    }
    // Focus canvas owns every wheel event that reaches a rail or the empty
    // shell, so zoom-out cannot be trapped by a transparent overlay.
    const bool focusCanvas=preview&&preview->parentWidget()&&preview->parentWidget()->objectName()=="shell";
    QScrollArea *inputRail=nullptr;for(QObject *ancestor=watched;ancestor&&!inputRail;ancestor=ancestor->parent())inputRail=qobject_cast<QScrollArea*>(ancestor);
    const bool wheelOverInteractiveRail=inputRail&&(inputRail->objectName()=="ArtboardRail"||inputRail->objectName()=="SubSettingsRail"||inputRail->objectName()=="MainToolRail"||inputRail->objectName()=="BottomToolbarRail"||inputRail->objectName()=="BottomModeRail");
    if(focusCanvas&&!wheelOverInteractiveRail&&event->type()==QEvent::Wheel){
        auto *wheel=static_cast<QWheelEvent*>(event);
        preview->adjustViewZoomFromInput(wheel->angleDelta().y(),wheel->pixelDelta().y(),wheel->modifiers());
        return true;
    }
    // The focus preview is visually below transparent header/rail surfaces.
    // Capture body drags at the application level so those surfaces can never
    // swallow the actual image-navigation gesture.
    if(focusCanvas&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseMove||event->type()==QEvent::MouseButtonRelease)&&watched!=preview){
        bool isControl=false;
        for(QObject *parent=watched;parent;parent=parent->parent()){
            if(qobject_cast<QAbstractButton*>(parent)||qobject_cast<QSlider*>(parent)||qobject_cast<QComboBox*>(parent)){
                isControl=true;
                break;
            }
        }
        if(!isControl){
            auto *mouse=static_cast<QMouseEvent*>(event);
            const QPoint local=preview->mapFromGlobal(mouse->globalPosition().toPoint());
            QMouseEvent forwarded(event->type(),QPointF(local),mouse->globalPosition(),mouse->button(),mouse->buttons(),mouse->modifiers());
            QCoreApplication::sendEvent(preview,&forwarded);
            return true;
        }
    }
    QScrollArea *subRail=inputRail;if(subRail&&(subRail->objectName()=="SubSettingsRail"||subRail->objectName()=="ArtboardRail"||subRail->objectName()=="BottomToolbarRail"||subRail->objectName()=="BottomModeRail")){auto *bar=subRail->horizontalScrollBar();if(event->type()==QEvent::Wheel){auto *wheel=static_cast<QWheelEvent*>(event);const int amount=wheel->angleDelta().x()!=0?wheel->angleDelta().x():wheel->angleDelta().y();bar->setValue(qBound(0,bar->value()-amount,bar->maximum()));return true;}if(event->type()==QEvent::MouseButtonPress){auto *mouse=static_cast<QMouseEvent*>(event);if(mouse->button()==Qt::LeftButton){watched->setProperty("subRailDragStart",mouse->position().toPoint());watched->setProperty("subRailScrollStart",bar->value());watched->setProperty("subRailDragging",false);}}if(event->type()==QEvent::MouseMove){auto *mouse=static_cast<QMouseEvent*>(event);if(mouse->buttons()&Qt::LeftButton){const int delta=mouse->position().toPoint().x()-watched->property("subRailDragStart").toPoint().x();if(qAbs(delta)>=4){watched->setProperty("subRailDragging",true);bar->setValue(qBound(0,watched->property("subRailScrollStart").toInt()-delta,bar->maximum()));return true;}}}if(event->type()==QEvent::MouseButtonRelease&&watched->property("subRailDragging").toBool())return true;}
    QScrollArea *mainToolRail=nullptr;for(QObject *ancestor=watched;ancestor&&!mainToolRail;ancestor=ancestor->parent())mainToolRail=qobject_cast<QScrollArea*>(ancestor);if(mainToolRail&&mainToolRail->objectName()=="MainToolRail"){auto *bar=mainToolRail->horizontalScrollBar();if(event->type()==QEvent::Wheel){auto *wheel=static_cast<QWheelEvent*>(event);const int amount=wheel->angleDelta().x()!=0?wheel->angleDelta().x():wheel->angleDelta().y();bar->setValue(qBound(0,bar->value()-amount,bar->maximum()));return true;}if(event->type()==QEvent::MouseButtonPress){auto *mouse=static_cast<QMouseEvent*>(event);if(mouse->button()==Qt::LeftButton){watched->setProperty("toolRailDragStart",mouse->position().toPoint());watched->setProperty("toolRailScrollStart",bar->value());watched->setProperty("toolRailDragging",false);}}if(event->type()==QEvent::MouseMove){auto *mouse=static_cast<QMouseEvent*>(event);if(mouse->buttons()&Qt::LeftButton){const int delta=mouse->position().toPoint().x()-watched->property("toolRailDragStart").toPoint().x();if(qAbs(delta)>=4){watched->setProperty("toolRailDragging",true);bar->setValue(qBound(0,watched->property("toolRailScrollStart").toInt()-delta,bar->maximum()));return true;}}}if(event->type()==QEvent::MouseButtonRelease&&watched->property("toolRailDragging").toBool())return true;}
    auto *tool=qobject_cast<QToolButton*>(watched);const bool carouselSurface=tabCarousel&&(watched==tabCarousel->viewport()||(tool&&tool->objectName()=="CarouselTab"));
    if(carouselSurface){
        auto *bar=tabCarousel->horizontalScrollBar();
        if(event->type()==QEvent::Resize){if(carouselLeftFade&&carouselRightFade){carouselLeftFade->setGeometry(0,0,44,tabCarousel->viewport()->height());carouselRightFade->setGeometry(tabCarousel->viewport()->width()-44,0,44,tabCarousel->viewport()->height());}return false;}
        if(event->type()==QEvent::Wheel){auto *wheel=static_cast<QWheelEvent*>(event);const int amount=wheel->angleDelta().x()!=0?wheel->angleDelta().x():wheel->angleDelta().y();bar->setValue(qBound(0,bar->value()-amount,bar->maximum()));return true;}
        if(event->type()==QEvent::MouseButtonPress){auto *mouse=static_cast<QMouseEvent*>(event);if(mouse->button()==Qt::LeftButton){carouselDragStart=mouse->position().toPoint();carouselScrollStart=bar->value();carouselRestPosition=carouselContent?carouselContent->pos():QPoint{};carouselDragging=false;carouselOverscrolling=false;return watched==tabCarousel->viewport();}}
        if(event->type()==QEvent::MouseMove&&carouselContent){auto *mouse=static_cast<QMouseEvent*>(event);if(!(mouse->buttons()&Qt::LeftButton))return false;const int drag=mouse->position().toPoint().x()-carouselDragStart.x();if(!carouselDragging&&qAbs(drag)<4)return false;carouselDragging=true;const int target=carouselScrollStart-drag;const int clamped=qBound(0,target,bar->maximum());bar->setValue(clamped);carouselRestPosition=QPoint(-clamped,0);if(target!=clamped){carouselOverscrolling=true;const int excess=qAbs(target-clamped);const int stretch=qMin(54,qRound(excess*.34));carouselContent->move(carouselRestPosition+QPoint(target<0?stretch:-stretch,0));}else carouselOverscrolling=false;return true;}
        if(event->type()==QEvent::MouseButtonRelease&&carouselDragging){if(carouselOverscrolling&&carouselContent){auto *animation=new QPropertyAnimation(carouselContent,"pos",this);carouselBounce=animation;animation->setDuration(180);animation->setEasingCurve(QEasingCurve::OutCubic);animation->setStartValue(carouselContent->pos());animation->setEndValue(carouselRestPosition);animation->start(QAbstractAnimation::DeleteWhenStopped);}carouselDragging=false;carouselOverscrolling=false;return true;}
    }
    if(tool&&tool->objectName()=="CarouselTab"&&(event->type()==QEvent::Enter||event->type()==QEvent::Leave)){auto *animation=new QPropertyAnimation(tool,"iconSize",tool);animation->setDuration(120);animation->setEasingCurve(QEasingCurve::OutCubic);animation->setStartValue(tool->iconSize());animation->setEndValue(event->type()==QEvent::Enter?QSize(31,31):QSize(28,28));animation->start(QAbstractAnimation::DeleteWhenStopped);}
    return AspectraWindow::eventFilter(watched,event);
}
void MainWindow::resizeEvent(QResizeEvent *event){
    AspectraWindow::resizeEvent(event);
    if(galleryScreen&&galleryScreen->isVisible())galleryScreen->setGeometry(contentsRect());
}
void MainWindow::closeEvent(QCloseEvent *event){if(exportWatcher.isRunning()){closing=true;*cancelFlag=true;event->ignore();return;}if(recorder&&recorder->state()!=QProcess::NotRunning){recorder->write("q\n");status->setText("Finishing recording. Close after it loads.");event->ignore();return;}autosaveProject();event->accept();}
void MainWindow::recordUndoState(){
    if(original.isNull())return;
    undoHistory.push(original.copy());
    while(undoHistory.size()>40)undoHistory.remove(0);
    redoHistory.clear();
    updateUndoControls();
}
void MainWindow::updateUndoControls(){
    if(undoButton)undoButton->setEnabled(!undoHistory.isEmpty());
    if(redoButton)redoButton->setEnabled(!redoHistory.isEmpty());
}
void MainWindow::undoRaster(){
    if(undoHistory.isEmpty()||original.isNull())return;
    redoHistory.push(original.copy());original=undoHistory.pop();media.size=original.size();
    if(!currentFile.isEmpty()&&canvasLayers.contains(currentFile))canvasLayers[currentFile].nativeSize=original.size();
    refresh();updateUndoControls();status->setText("Undo");
}
void MainWindow::redoRaster(){
    if(redoHistory.isEmpty()||original.isNull())return;
    undoHistory.push(original.copy());original=redoHistory.pop();media.size=original.size();
    if(!currentFile.isEmpty()&&canvasLayers.contains(currentFile))canvasLayers[currentFile].nativeSize=original.size();
    refresh();updateUndoControls();status->setText("Redo");
}
void MainWindow::savePreview(const QString &path){grab().save(path);}
