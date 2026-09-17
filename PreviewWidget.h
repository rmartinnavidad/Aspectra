#pragma once
#include <optional>
#include <QtWidgets>
#include "Processing.h"
#include "RasterToolEngine.h"

struct PreviewArtboard {
    QString id;
    QString name;
    QPointF position;
    QSize size;
    QImage image;
    bool active=false;
    bool visible=true;
};

class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget *parent=nullptr);
    void setFrame(const QImage &image);
    void setArtboards(const QVector<PreviewArtboard> &items);
    void clearArtboards();
    void setActiveArtboard(const QString &id);
    QVector<PreviewArtboard> artboards() const { return workspaceArtboards; }
    void setTextureFrame(const QImage &image);
    void clearTextureFrame();
    void setTextureSphereInteractive(bool on){sphereInteractive=on;if(!on)sphereDragging=false;}
    void setVectorTraceFrame(const QImage &image);
    void clearVectorTraceFrame();
    void setViewZoom(double value);
    double viewZoomValue() const { return viewZoom; }
    void adjustViewZoomFromInput(int angleDelta,int pixelDelta,Qt::KeyboardModifiers modifiers=Qt::NoModifier);
    void setLayerMask(const QImage &image);
    void setMaskEditMode(bool enabled);
    bool isMaskEditMode() const { return maskEditMode; }
    void setSelectionMask(const QImage &image);
    void clearSelectionMask();
    void setCanvasTool(CanvasTool value,int diameter=48){canvasTool=value;toolDiameter=qMax(1,diameter);toolPath.clear();maskPainting=false;eyedropper=false;setCursor(value==CanvasTool::None?Qt::ArrowCursor:Qt::CrossCursor);}
    void setEyedropper(bool on){eyedropper=on;setCursor(on?Qt::CrossCursor:Qt::ArrowCursor);}
    void setMaskBrush(bool add,int size){maskPainting=true;maskAdd=add;maskSize=size;setCursor(Qt::CrossCursor);}
    void setAdjustments(const Adjustments &adjustments,QSize size,const RecolorPreset *preset=nullptr);
    QImage rendered() const { return processed; }
    QRectF displayedFrameRect() const { return frameBox(); }
    void setGuides(bool visible,QMarginsF safe);
signals:
    void fullScreenRequested();
    void marginsDragged(QMarginsF safe,QMarginsF padding);
    void colorSampled(QColor color);
    void maskBrushed(QPointF normalized,bool add,int size);
    void toolStroke(QPointF normalized,CanvasTool tool,int diameter,Qt::KeyboardModifiers modifiers);
    void toolFinished(QVector<QPointF> path,CanvasTool tool,Qt::KeyboardModifiers modifiers);
    void canvasTapped();
    void importRequested();
    void sphereOrbited(QPoint delta,bool pan);
    void sphereZoomed(int delta);
    void viewZoomChanged(double value);
    void artboardSelected(QString id);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    QImage frame,textureFrame,vectorTraceFrame,layerMask,selectionMask,processed;
    QVector<PreviewArtboard> workspaceArtboards;
    QString activeArtboardId;
    QRectF artboardScreenRect(const PreviewArtboard &artboard) const;
    QRectF workspaceBounds() const;
    bool textureActive=false,vectorTraceActive=false,sphereInteractive=false,sphereDragging=false,maskEditMode=false;
    double viewZoom=1.0;
    Adjustments settings;
    QSize target{512,512};
    std::optional<RecolorPreset> recolor;
    QTimer refresh,selectionAnimation,zoomSettleRefresh;
    QVariantAnimation artboardSlide;
    QMarginsF safeMargins{.1,.1,.1,.1};
    bool guides=true,draggingPadding=false,eyedropper=false,maskPainting=false,maskAdd=true,paintingMask=false,panning=false,toolInteracting=false;int maskSize=48,toolDiameter=48;
    CanvasTool canvasTool=CanvasTool::None;QVector<QPointF> toolPath;
    int dragEdge=-1;
    QPointF viewPan;QPointF panAnchor,tapStart,sphereAnchor;bool tapCandidate=false;
    QRectF frameBox() const;
    void rebuild();
};
