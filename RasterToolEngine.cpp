#include "RasterToolEngine.h"

#include <algorithm>
#include <cmath>

namespace {
QRect brushRect(const QImage &image, QPoint center, int diameter) {
    const int radius=qMax(1,diameter/2);
    return QRect(center-QPoint(radius,radius),QSize(radius*2+1,radius*2+1)).intersected(image.rect());
}
bool insideBrush(QPoint pixel,QPoint center,int diameter) {
    const double radius=qMax(1.,diameter/2.);
    const QPoint delta=pixel-center;
    return double(delta.x()*delta.x()+delta.y()*delta.y())<=radius*radius;
}
QColor mix(const QColor &a,const QColor &b,double amount) {
    amount=std::clamp(amount,0.,1.);
    return QColor(qRound(a.red()+(b.red()-a.red())*amount),qRound(a.green()+(b.green()-a.green())*amount),qRound(a.blue()+(b.blue()-a.blue())*amount),qRound(a.alpha()+(b.alpha()-a.alpha())*amount));
}
}

bool RasterToolEngine::isSelectionTool(CanvasTool tool) {
    return tool==CanvasTool::RectangularMarquee||tool==CanvasTool::EllipticalMarquee||
        tool==CanvasTool::Lasso||tool==CanvasTool::ObjectSelect||tool==CanvasTool::MagicWand;
}
bool RasterToolEngine::isStrokeTool(CanvasTool tool) {
    switch(tool) {
    case CanvasTool::SpotHeal: case CanvasTool::HealingBrush: case CanvasTool::Patch:
    case CanvasTool::CloneStamp: case CanvasTool::Brush: case CanvasTool::Pencil:
    case CanvasTool::Eraser: case CanvasTool::Blur: case CanvasTool::Sharpen:
    case CanvasTool::Smudge: case CanvasTool::Dodge: case CanvasTool::Burn:
    case CanvasTool::Sponge: return true;
    default: return false;
    }
}
QPoint RasterToolEngine::pixelPoint(QSize size,QPointF normalized) {
    return {qBound(0,qRound(normalized.x()*(size.width()-1)),qMax(0,size.width()-1)),qBound(0,qRound(normalized.y()*(size.height()-1)),qMax(0,size.height()-1))};
}
QImage RasterToolEngine::rectangularSelection(QSize size,QPointF first,QPointF last,bool ellipse) {
    QImage mask(size,QImage::Format_Grayscale8);mask.fill(0);
    if(size.isEmpty())return mask;
    const QRect rect=QRect(pixelPoint(size,first),pixelPoint(size,last)).normalized();
    QPainter painter(&mask);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(Qt::white);
    if(ellipse)painter.drawEllipse(rect);else painter.drawRect(rect);
    return mask;
}
QImage RasterToolEngine::lassoSelection(QSize size,const QVector<QPointF> &points) {
    QImage mask(size,QImage::Format_Grayscale8);mask.fill(0);
    if(points.size()<3||size.isEmpty())return mask;
    QPolygon polygon;polygon.reserve(points.size());for(const auto point:points)polygon<<pixelPoint(size,point);
    QPainter painter(&mask);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(Qt::white);painter.drawPolygon(polygon);
    return mask;
}
QImage RasterToolEngine::floodSelection(const QImage &source,QPointF normalized,int tolerance) {
    if(source.isNull())return {};
    const QImage rgba=source.convertToFormat(QImage::Format_ARGB32);QImage mask(source.size(),QImage::Format_Grayscale8);mask.fill(0);
    const QPoint start=pixelPoint(source.size(),normalized);const QColor seed=rgba.pixelColor(start);
    QQueue<QPoint> queue;queue.enqueue(start);mask.setPixel(start,255);const int limit=qBound(1,tolerance,255);
    auto distance=[](const QColor &a,const QColor &b){return qAbs(a.red()-b.red())+qAbs(a.green()-b.green())+qAbs(a.blue()-b.blue());};
    constexpr QPoint offsets[]{QPoint(1,0),QPoint(-1,0),QPoint(0,1),QPoint(0,-1)};
    while(!queue.isEmpty()) { const QPoint current=queue.dequeue();for(const QPoint delta:offsets){const QPoint next=current+delta;if(!mask.rect().contains(next)||mask.pixelColor(next).value()>0||distance(seed,rgba.pixelColor(next))>limit*3)continue;mask.setPixel(next,255);queue.enqueue(next);} }
    return mask;
}
void RasterToolEngine::blurRegion(QImage &image,QPoint center,int radius) {
    QImage source=image.copy();const QRect bounds=brushRect(image,center,radius*2+1);
    for(int y=bounds.top();y<=bounds.bottom();++y)for(int x=bounds.left();x<=bounds.right();++x){int red=0,green=0,blue=0,alpha=0,count=0;for(int oy=-radius;oy<=radius;++oy)for(int ox=-radius;ox<=radius;++ox){const QPoint sample(qBound(0,x+ox,image.width()-1),qBound(0,y+oy,image.height()-1));const QColor p=source.pixelColor(sample);red+=p.red();green+=p.green();blue+=p.blue();alpha+=p.alpha();++count;}image.setPixelColor(x,y,QColor(red/count,green/count,blue/count,alpha/count));}
}
void RasterToolEngine::sharpenRegion(QImage &image,QPoint center,int radius) {
    QImage source=image.copy();blurRegion(source,center,qMax(1,radius));const QRect bounds=brushRect(image,center,radius*2+1);
    for(int y=bounds.top();y<=bounds.bottom();++y)for(int x=bounds.left();x<=bounds.right();++x){const QColor base=image.pixelColor(x,y),soft=source.pixelColor(x,y);image.setPixelColor(x,y,QColor(qBound(0,base.red()*2-soft.red(),255),qBound(0,base.green()*2-soft.green(),255),qBound(0,base.blue()*2-soft.blue(),255),base.alpha()));}
}
void RasterToolEngine::stroke(QImage &image,CanvasTool tool,QPointF normalized,int diameter,const QColor &color,QPointF cloneSource,bool hasCloneSource) {
    if(image.isNull())return;image=image.convertToFormat(QImage::Format_ARGB32);const QPoint center=pixelPoint(image.size(),normalized);diameter=qBound(1,diameter,qMax(image.width(),image.height()));
    if(tool==CanvasTool::Blur||tool==CanvasTool::Smudge||tool==CanvasTool::SpotHeal||tool==CanvasTool::HealingBrush||tool==CanvasTool::Patch){blurRegion(image,center,qMax(1,diameter/8));return;}
    if(tool==CanvasTool::Sharpen){sharpenRegion(image,center,qMax(1,diameter/10));return;}
    const QRect bounds=brushRect(image,center,diameter);const QPoint clone=pixelPoint(image.size(),cloneSource);
    for(int y=bounds.top();y<=bounds.bottom();++y)for(int x=bounds.left();x<=bounds.right();++x){if(!insideBrush({x,y},center,diameter))continue;QColor pixel=image.pixelColor(x,y);
        if(tool==CanvasTool::Eraser){pixel.setAlpha(0);}
        else if(tool==CanvasTool::CloneStamp&&hasCloneSource){const QPoint sample(qBound(0,clone.x()+x-center.x(),image.width()-1),qBound(0,clone.y()+y-center.y(),image.height()-1));pixel=image.pixelColor(sample);}
        else if(tool==CanvasTool::Brush||tool==CanvasTool::Pencil)pixel=mix(pixel,color,tool==CanvasTool::Pencil?1.:.72);
        else if(tool==CanvasTool::Dodge)pixel=QColor(qMin(255,pixel.red()+22),qMin(255,pixel.green()+22),qMin(255,pixel.blue()+22),pixel.alpha());
        else if(tool==CanvasTool::Burn)pixel=QColor(qMax(0,pixel.red()-22),qMax(0,pixel.green()-22),qMax(0,pixel.blue()-22),pixel.alpha());
        else if(tool==CanvasTool::Sponge){QColor hsv=pixel.toHsv();hsv.setHsv(hsv.hue(),qMin(255,hsv.saturation()+28),hsv.value(),hsv.alpha());pixel=hsv;}
        image.setPixelColor(x,y,pixel);
    }
}
void RasterToolEngine::gradient(QImage &image,QPointF first,QPointF last,const QColor &color) {
    if(image.isNull())return;image=image.convertToFormat(QImage::Format_ARGB32);const QPoint from=pixelPoint(image.size(),first),to=pixelPoint(image.size(),last);const QPointF direction=QPointF(to-from);const double length=qMax(1.,QPointF::dotProduct(direction,direction));
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const QPointF relative=QPointF(x,y)-from;const double amount=std::clamp(QPointF::dotProduct(relative,direction)/length,0.,1.);image.setPixelColor(x,y,mix(image.pixelColor(x,y),color,amount*.72));}
}
void RasterToolEngine::crop(QImage &image,QPointF first,QPointF last) {
    if(image.isNull())return;const QRect rect=QRect(pixelPoint(image.size(),first),pixelPoint(image.size(),last)).normalized().intersected(image.rect());if(rect.width()>1&&rect.height()>1)image=image.copy(rect);
}
