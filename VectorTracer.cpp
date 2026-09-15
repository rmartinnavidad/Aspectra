#include "VectorTracer.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
struct PaletteEntry { QColor color; int count{}; qint64 red{}; qint64 green{}; qint64 blue{}; };
struct ColorSum { qint64 red{}; qint64 green{}; qint64 blue{}; int count{}; };

int bucketKey(const QColor &color) { return ((color.red() >> 5) << 6) | ((color.green() >> 5) << 3) | (color.blue() >> 5); }
int distanceSquared(const QColor &lhs, const QColor &rhs) { const int r=lhs.red()-rhs.red(),g=lhs.green()-rhs.green(),b=lhs.blue()-rhs.blue(); return r*r+g*g+b*b; }
quint64 pointKey(const QPoint &point) { return (quint64(quint32(point.x())) << 32) | quint32(point.y()); }
QPoint pointFromKey(quint64 key) { return {qint32(key >> 32),qint32(key & 0xffffffff)}; }
QString svgNumber(qreal value) { return QString::number(value,'f',3).remove(QRegularExpression("\\.?0+$")); }

QVector<QPoint> simplifyAxisPath(QVector<QPoint> points) {
    if(points.size()<3)return points;bool changed=true;
    while(changed&&points.size()>3){changed=false;for(int i=0;i<points.size();++i){const QPoint before=points[(i-1+points.size())%points.size()],current=points[i],after=points[(i+1)%points.size()];if((before.x()==current.x()&&current.x()==after.x())||(before.y()==current.y()&&current.y()==after.y())){points.removeAt(i);changed=true;break;}}}
    return points;
}

qreal distanceToSegment(const QPoint &point,const QPoint &start,const QPoint &end) {
    const qreal dx=end.x()-start.x(),dy=end.y()-start.y();
    if(qFuzzyIsNull(dx)&&qFuzzyIsNull(dy))return std::hypot(point.x()-start.x(),point.y()-start.y());
    const qreal t=std::clamp(((point.x()-start.x())*dx+(point.y()-start.y())*dy)/(dx*dx+dy*dy),qreal(0),qreal(1));
    return std::hypot(point.x()-(start.x()+t*dx),point.y()-(start.y()+t*dy));
}

QVector<QPoint> simplifyOpenPath(const QVector<QPoint> &points,qreal tolerance) {
    if(points.size()<3)return points;QVector<bool> keep(points.size(),false);keep[0]=keep.back()=true;
    std::function<void(int,int)> reduce=[&](int first,int last){qreal maximum=0;int candidate=-1;for(int i=first+1;i<last;++i){const qreal distance=distanceToSegment(points[i],points[first],points[last]);if(distance>maximum){maximum=distance;candidate=i;}}if(candidate>=0&&maximum>tolerance){keep[candidate]=true;reduce(first,candidate);reduce(candidate,last);}};
    reduce(0,points.size()-1);QVector<QPoint> result;for(int i=0;i<points.size();++i)if(keep[i])result<<points[i];return result;
}

QVector<QPoint> simplifyClosedPath(const QVector<QPoint> &points,qreal tolerance) {
    if(points.size()<5)return points;const int pivot=points.size()/2;QVector<QPoint> first,second;for(int i=0;i<=pivot;++i)first<<points[i];for(int i=pivot;i<points.size();++i)second<<points[i];second<<points.first();first=simplifyOpenPath(first,tolerance);second=simplifyOpenPath(second,tolerance);QVector<QPoint> result=first;for(int i=1;i+1<second.size();++i)result<<second[i];return result;
}

QVector<QVector<QPoint>> contourLoops(const QVector<int> &indexed,int width,int height,int color,qreal tolerance) {
    QHash<quint64,QVector<QPoint>> edges;
    auto at=[&](int x,int y){return x>=0&&x<width&&y>=0&&y<height&&indexed[y*width+x]==color;};
    auto add=[&](QPoint from,QPoint to){edges[pointKey(from)].append(to);};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){if(!at(x,y))continue;if(!at(x,y-1))add({x,y},{x+1,y});if(!at(x+1,y))add({x+1,y},{x+1,y+1});if(!at(x,y+1))add({x+1,y+1},{x,y+1});if(!at(x-1,y))add({x,y+1},{x,y});}
    QVector<QVector<QPoint>> loops;
    while(!edges.isEmpty()){const QPoint start=pointFromKey(edges.constBegin().key());QPoint current=start;QVector<QPoint> loop;int safety=width*height*4;do{loop<<current;auto it=edges.find(pointKey(current));if(it==edges.end()||it->isEmpty())break;const QPoint next=it->takeLast();if(it->isEmpty())edges.erase(it);current=next;}while(current!=start&&--safety>0);if(current==start&&loop.size()>=3)loops<<simplifyClosedPath(simplifyAxisPath(loop),tolerance);}
    return loops;
}
}

VectorTraceResult VectorTracer::trace(const QImage &source,int colors,int detail,int smoothing) {
    VectorTraceResult result;if(source.isNull())return result;
    colors=std::clamp(colors,2,64);detail=std::clamp(detail,64,1024);smoothing=std::clamp(smoothing,0,100);
    const QImage input=source.convertToFormat(QImage::Format_RGBA8888);const QSize workingSize=input.size().scaled(detail,detail,Qt::KeepAspectRatio);const QImage working=input.scaled(workingSize,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    QHash<int,PaletteEntry> buckets;
    for(int y=0;y<working.height();++y)for(int x=0;x<working.width();++x){const QColor pixel=working.pixelColor(x,y);if(pixel.alpha()<16)continue;auto &entry=buckets[bucketKey(pixel)];++entry.count;entry.red+=pixel.red();entry.green+=pixel.green();entry.blue+=pixel.blue();}
    QList<PaletteEntry> entries=buckets.values();for(auto &entry:entries)if(entry.count)entry.color=QColor(entry.red/entry.count,entry.green/entry.count,entry.blue/entry.count);std::sort(entries.begin(),entries.end(),[](const PaletteEntry &a,const PaletteEntry &b){return a.count>b.count;});if(entries.size()>colors)entries.erase(entries.begin()+colors,entries.end());if(entries.isEmpty())return result;
    QVector<QColor> palette;for(const auto &entry:entries)palette<<entry.color;
    // Refine colors against actual pixels instead of retaining coarse histogram buckets.
    const int refinementStep=qMax(1,int(std::sqrt(double(working.width()*working.height())/160000.0)));for(int iteration=0;iteration<4;++iteration){QVector<ColorSum> sums(palette.size());for(int y=0;y<working.height();y+=refinementStep)for(int x=0;x<working.width();x+=refinementStep){const QColor pixel=working.pixelColor(x,y);if(pixel.alpha()<16)continue;int chosen=0,best=distanceSquared(pixel,palette.first());for(int i=1;i<palette.size();++i){const int candidate=distanceSquared(pixel,palette[i]);if(candidate<best){best=candidate;chosen=i;}}auto &sum=sums[chosen];++sum.count;sum.red+=pixel.red();sum.green+=pixel.green();sum.blue+=pixel.blue();}for(int i=0;i<palette.size();++i)if(sums[i].count)palette[i]=QColor(sums[i].red/sums[i].count,sums[i].green/sums[i].count,sums[i].blue/sums[i].count);}
    QVector<int> indexed(working.width()*working.height(),-1);
    for(int y=0;y<working.height();++y)for(int x=0;x<working.width();++x){const QColor pixel=working.pixelColor(x,y);if(pixel.alpha()<16)continue;int chosen=0,best=distanceSquared(pixel,palette.first());for(int i=1;i<palette.size();++i){const int candidate=distanceSquared(pixel,palette[i]);if(candidate<best){best=candidate;chosen=i;}}indexed[y*working.width()+x]=chosen;}
    const qreal sx=qreal(input.width())/working.width(),sy=qreal(input.height())/working.height();QImage preview(working.size(),QImage::Format_RGBA8888);preview.fill(Qt::transparent);QPainter painter(&preview);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);QString svg;QTextStream out(&svg);out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""<<input.width()<<"\" height=\""<<input.height()<<"\" viewBox=\"0 0 "<<input.width()<<' '<<input.height()<<"\">\n";
    const qreal tolerance=.25+(qreal(smoothing)/100.0)*4.75;for(int color=0;color<palette.size();++color){const auto loops=contourLoops(indexed,working.width(),working.height(),color,tolerance);if(loops.isEmpty())continue;QPainterPath previewPath;previewPath.setFillRule(Qt::OddEvenFill);QString path;for(const auto &loop:loops){if(loop.size()<3)continue;QPolygonF polygon;for(const QPoint &point:loop)polygon<<QPointF(point);previewPath.addPolygon(polygon);path+='M'+svgNumber(loop.first().x()*sx)+' '+svgNumber(loop.first().y()*sy);for(int i=1;i<loop.size();++i)path+='L'+svgNumber(loop[i].x()*sx)+' '+svgNumber(loop[i].y()*sy);path+='Z';}painter.fillPath(previewPath,QColor(255,214,46,48));painter.strokePath(previewPath,QPen(QColor(255,224,74),qMax<qreal>(1.0,qreal(working.width())/512.0),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));if(!path.isEmpty())out<<"  <path fill=\""<<palette[color].name()<<"\" fill-rule=\"evenodd\" d=\""<<path<<"\"/>\n";}
    painter.end();out<<"</svg>\n";result.svg=svg;result.preview=preview.scaled(input.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);result.colorCount=palette.size();return result;
}

int VectorTracer::detectColorCount(const QImage &source,int maximum) {
    if(source.isNull())return 2;maximum=std::clamp(maximum,2,64);const QImage sample=source.convertToFormat(QImage::Format_RGBA8888).scaled(QSize(160,160),Qt::KeepAspectRatio,Qt::FastTransformation);QHash<int,int> histogram;int pixels=0;for(int y=0;y<sample.height();++y)for(int x=0;x<sample.width();++x){const QColor pixel=sample.pixelColor(x,y);if(pixel.alpha()<16)continue;++histogram[bucketKey(pixel)];++pixels;}if(!pixels)return 2;QList<int> counts=histogram.values();std::sort(counts.begin(),counts.end(),std::greater<int>());const int floor=qMax(3,pixels/1200);int colors=0;for(int count:counts){if(count<floor)break;++colors;if(colors>=maximum)break;}return std::clamp(colors,2,maximum);
}
