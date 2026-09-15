#include "LayerEffects.h"
#include <algorithm>
#include <cmath>
#include <deque>
namespace {
QVector<float> blur(QVector<float> a,int w,int h,int radius) {
    radius=std::clamp(radius,1,256); QVector<float> tmp(a.size());
    for(int pass=0;pass<2;++pass) {
        for(int y=0;y<h;++y) { double sum=0; for(int x=-radius;x<=radius;++x)if(x>=0&&x<w)sum+=a[y*w+x];
            for(int x=0;x<w;++x){tmp[y*w+x]=float(sum/(2*radius+1));if(x-radius>=0)sum-=a[y*w+x-radius];if(x+radius+1<w)sum+=a[y*w+x+radius+1];}}
        for(int x=0;x<w;++x){double sum=0;for(int y=-radius;y<=radius;++y)if(y>=0&&y<h)sum+=tmp[y*w+x];
            for(int y=0;y<h;++y){a[y*w+x]=float(sum/(2*radius+1));if(y-radius>=0)sum-=tmp[(y-radius)*w+x];if(y+radius+1<h)sum+=tmp[(y+radius+1)*w+x];}}
    } return a;
}
QImage tint(const QVector<float>& mask,QSize size,QColor color,double opacity) {
    QImage result(size,QImage::Format_ARGB32);for(int y=0;y<size.height();++y){auto *line=(QRgb*)result.scanLine(y);for(int x=0;x<size.width();++x)line[x]=qRgba(color.red(),color.green(),color.blue(),qRound(std::clamp(double(mask[y*size.width()+x])*opacity,0.,1.)*255));}return result;
}
double blend(double base,double color,int mode) {
    if(mode==1)return base*color; if(mode==2)return 1-(1-base)*(1-color); if(mode==3)return base<.5?2*base*color:1-2*(1-base)*(1-color);return color;
}
}
QImage applyLayerEffects(const QImage &input,const Adjustments &s) {
    if(!s.hasStyles())return input;
    const int w=input.width(),h=input.height(),scale=std::min(w,h);QImage image=input.convertToFormat(QImage::Format_ARGB32);
    QVector<float> alpha(w*h);for(int y=0;y<h;++y){auto *p=(const QRgb*)image.constScanLine(y);for(int x=0;x<w;++x)alpha[y*w+x]=qAlpha(p[x])/255.f;}
    if(s.overlay||s.gradient)for(int y=0;y<h;++y){auto *p=(QRgb*)image.scanLine(y);double t=s.gradient?double(y)/std::max(1,h-1):0;
        QColor color=QColor::fromRgbF(s.overlayColor.redF()*(1-t)+s.gradientEnd.redF()*t,s.overlayColor.greenF()*(1-t)+s.gradientEnd.greenF()*t,s.overlayColor.blueF()*(1-t)+s.gradientEnd.blueF()*t);
        for(int x=0;x<w;++x){QColor b=QColor::fromRgba(p[x]);auto channel=[&](double v,double c){return qRound(std::clamp(v*(1-s.effectOpacity)+blend(v,c,s.blendMode)*s.effectOpacity,0.,1.)*255);};p[x]=qRgba(channel(b.redF(),color.redF()),channel(b.greenF(),color.greenF()),channel(b.blueF(),color.blueF()),b.alpha());}}
    QImage output(input.size(),QImage::Format_ARGB32);output.fill(Qt::transparent);QPainter painter(&output);
    if(s.shadow){auto blurred=blur(alpha,w,h,qRound(s.shadowSize*scale));auto shadow=tint(blurred,input.size(),s.shadowColor,s.effectOpacity);int offset=qRound(s.shadowDistance*scale);painter.drawImage(offset,offset,shadow);}
    if(s.glow){auto blurred=blur(alpha,w,h,qRound(s.glowSize*scale));painter.drawImage(0,0,tint(blurred,input.size(),s.glowColor,s.effectOpacity));}
    if(s.stroke){ // Two linear-time sliding maximum passes produce an outside stroke.
        int r=std::max(1,qRound(s.strokeSize*scale));QVector<float> horizontal(alpha.size()),expanded(alpha.size());
        auto maximum=[&](int count,auto get,auto put){std::deque<int> q;for(int i=0;i<count+r;++i){if(i<count){while(!q.empty()&&get(q.back())<=get(i))q.pop_back();q.push_back(i);}int center=i-r;if(center>=0){while(!q.empty()&&q.front()<center-r)q.pop_front();put(center,q.empty()?0.f:get(q.front()));}}};
        for(int y=0;y<h;++y)maximum(w,[&](int x){return alpha[y*w+x];},[&](int x,float v){horizontal[y*w+x]=v;});
        for(int x=0;x<w;++x)maximum(h,[&](int y){return horizontal[y*w+x];},[&](int y,float v){expanded[y*w+x]=v;});
        painter.drawImage(0,0,tint(expanded,input.size(),s.strokeColor,1));
    }
    if(s.innerShadow||s.bevel){auto softened=blur(alpha,w,h,std::max(1,qRound((s.bevel?s.bevelSize:s.shadowSize)*scale)));
        for(int y=0;y<h;++y){auto *p=(QRgb*)image.scanLine(y);for(int x=0;x<w;++x){QColor c=QColor::fromRgba(p[x]);double factor=1;
            if(s.innerShadow){int dx=std::max(0,x-std::max(1,qRound(s.shadowDistance*scale))),dy=std::max(0,y-std::max(1,qRound(s.shadowDistance*scale)));factor-=s.effectOpacity*(1-softened[dy*w+dx]);}
            double light=0;if(s.bevel){int a=std::max(0,x-1),b=std::min(w-1,x+1),u=std::max(0,y-1),d=std::min(h-1,y+1);light=(softened[y*w+a]-softened[y*w+b]+softened[u*w+x]-softened[d*w+x])*s.effectOpacity*3;}
            auto ch=[&](int v){return qRound(std::clamp(v*factor+light*255,0.,255.));};p[x]=qRgba(ch(c.red()),ch(c.green()),ch(c.blue()),c.alpha());}}
    }
    painter.drawImage(0,0,image);painter.end();
    if(s.opacity!=1)for(int y=0;y<h;++y){auto *p=(QRgb*)output.scanLine(y);for(int x=0;x<w;++x)p[x]=qRgba(qRed(p[x]),qGreen(p[x]),qBlue(p[x]),qRound(qAlpha(p[x])*s.opacity));}
    return output;
}
