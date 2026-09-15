#pragma once

#include <QtCore>
#include <QtGui>

// Native raster operations shared by the desktop canvas and the in-game host.
// Coordinates passed to this API are normalized to the editable image bounds.
enum class CanvasTool : quint8 {
    None, Move, RectangularMarquee, EllipticalMarquee, Lasso, ObjectSelect,
    MagicWand, Crop, Eyedropper, SpotHeal, HealingBrush, Patch, CloneStamp,
    Brush, Pencil, Eraser, Gradient, Blur, Sharpen, Smudge, Dodge, Burn,
    Sponge, Pen, PathSelection, Type, Shape, Hand, Zoom
};

class RasterToolEngine final {
public:
    static bool isSelectionTool(CanvasTool tool);
    static bool isStrokeTool(CanvasTool tool);
    static QImage rectangularSelection(QSize size, QPointF first, QPointF last, bool ellipse);
    static QImage lassoSelection(QSize size, const QVector<QPointF> &points);
    static QImage floodSelection(const QImage &source, QPointF point, int tolerance);
    static void stroke(QImage &image, CanvasTool tool, QPointF point, int diameter,
                       const QColor &color, QPointF cloneSource={}, bool hasCloneSource=false);
    static void gradient(QImage &image, QPointF first, QPointF last, const QColor &color);
    static void crop(QImage &image, QPointF first, QPointF last);
private:
    static QPoint pixelPoint(QSize size, QPointF normalized);
    static void blurRegion(QImage &image, QPoint center, int radius);
    static void sharpenRegion(QImage &image, QPoint center, int radius);
};
