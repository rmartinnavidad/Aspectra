#include "TimelineWidget.h"
void TimelineWidget::paintEvent(QPaintEvent *){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),Qt::black);
    const QStringList roles{"VIDEO","AUDIO","IMAGE","TEXT","EFFECT"};
    p.setPen(QPen(QColor("#262a30"),1));
    for(int lane=0;lane<laneTracks.size();++lane){
        const int index=trackIndexForLane(lane),type=laneTypes.value(lane);const TimelineTrack *track=index>=0?&tracks[index]:nullptr;const double y=43+lane*trackHeight;const double clipHeight=qMax(16.,double(trackHeight)-11.);
        p.setPen(QColor("#ffffff"));p.drawText(QRectF(64,y,82,trackHeight-4),Qt::AlignVCenter|Qt::AlignLeft,(track?track->name:roles.value(type)+" · +").left(14));
        p.setPen(QColor("#24282e"));p.drawLine(QPointF(146,y+trackHeight-2),QPointF(width()-8,y+trackHeight-2));
        p.setPen(track&&track->enabled?QColor("#57dd7b"):QColor("#777b84"));p.drawText(QRectF(6,y,28,trackHeight-4),Qt::AlignCenter,track&&track->enabled?"◉":"○");
        p.setPen(track&&track->locked?QColor("#fac665"):QColor("#a8acb4"));p.drawText(QRectF(35,y,24,trackHeight-4),Qt::AlignCenter,track&&track->locked?"▣":"□");
        if(!track)continue;
        const QRectF clip(x(track->start),y+5,qMax(3.,x(track->end)-x(track->start)),clipHeight);p.setPen(Qt::NoPen);p.setBrush(track->enabled?QColor("#193d47"):QColor("#18191b"));p.drawRoundedRect(clip,5,5);
        p.save();p.setClipRect(clip.adjusted(2,2,-2,-2));
        if(track->type==TimelineTrack::Video||track->type==TimelineTrack::Image){if(!track->image.isNull())for(qreal left=clip.left();left<clip.right();left+=42)p.drawImage(QRectF(left,clip.top(),40,clip.height()),track->image);}
        else if(track->type==TimelineTrack::Audio){p.setPen(QPen(QColor("#3ddcff"),1));QPainterPath wave;for(qreal px=clip.left();px<clip.right();px+=2){const qreal height=(std::sin((px-clip.left())*.47)+std::sin((px-clip.left())*.13))*4+6;wave.moveTo(px,clip.center().y()-height);wave.lineTo(px,clip.center().y()+height);}p.drawPath(wave);}
        else if(track->type==TimelineTrack::Text){p.setPen(Qt::white);p.drawText(clip.adjusted(5,0,-5,0),Qt::AlignVCenter|Qt::AlignLeft,track->text.isEmpty()?"Text":track->text.left(18));}
        else {p.setPen(QColor("#ff4d4d"));p.drawText(clip,Qt::AlignCenter,"FX");}p.restore();
        if(track->transitionIn>0||track->transitionOut>0){p.setBrush(QColor("#7a4dff"));if(track->transitionIn>0)p.drawPolygon(QPolygonF{{clip.left(),clip.top()},{clip.left()+8,clip.center().y()},{clip.left(),clip.bottom()}});if(track->transitionOut>0)p.drawPolygon(QPolygonF{{clip.right(),clip.top()},{clip.right()-8,clip.center().y()},{clip.right(),clip.bottom()}});}
        for(const auto &key:track->keyframes){const double px=x(key.time),py=y+15;p.setPen(Qt::NoPen);p.setBrush(QColor("#ffffff"));p.drawPolygon(QPolygonF{{px,py-5},{px+5,py},{px,py+5},{px-5,py}});}
    }
    p.setPen(Qt::NoPen);p.setBrush(QColor(61,145,251,90));p.drawRect(QRectF(x(start),28,qMax(1.,x(end)-x(start)),8));p.setBrush(QColor("#57dd7b"));p.drawRoundedRect(QRectF(x(start)-3,17,6,26),2,2);p.setBrush(QColor("#3d91fb"));p.drawRoundedRect(QRectF(x(end)-3,17,6,26),2,2);p.setPen(QPen(QColor("#3ddcff"),2));p.drawLine(QPointF(x(position),8),QPointF(x(position),height()-5));p.setBrush(QColor("#3ddcff"));p.drawEllipse(QPointF(x(position),10),3,3);
}
void TimelineWidget::mousePressEvent(QMouseEvent *e){
    if(e->button()!=Qt::LeftButton)return;const int lane=laneAt(e->position().y()),index=trackIndexForLane(lane);const double px=e->position().x();
    if(qAbs(px-x(position))<=9){drag=3;mouseMoveEvent(e);return;}
    if(index>=0){
        emit trackSelected(index);
        if(px<36){tracks[index].enabled=!tracks[index].enabled;emit trackVisibilityChanged(index,tracks[index].enabled);update();return;}
        if(px<62){tracks[index].locked=!tracks[index].locked;emit trackLockChanged(index,tracks[index].locked);update();return;}
        const auto &track=tracks[index];const QRectF clip(x(track.start),43+lane*trackHeight+5,qMax(3.,x(track.end)-x(track.start)),qMax(16.,double(trackHeight)-11.));
        if(px>=clip.left()-8&&px<=clip.right()+8){editedTrack=index;trackDrag=qAbs(px-clip.left())<8?1:qAbs(px-clip.right())<8?2:3;dragOffset=time(px)-track.start;return;}
    }
    drag=qAbs(px-x(start))<8?1:qAbs(px-x(end))<8?2:3;mouseMoveEvent(e);
}
void TimelineWidget::wheelEvent(QWheelEvent *e){
    const int delta=e->angleDelta().y()!=0?e->angleDelta().y():e->pixelDelta().y();
    if(e->modifiers()&Qt::ShiftModifier){if(delta!=0){trackHeight=qBound(24,trackHeight+(delta>0?3:-3),76);updateHeight();update();}e->accept();return;}
    const int horizontal=e->angleDelta().x()!=0?e->angleDelta().x():e->pixelDelta().x();
    if(horizontal!=0||e->modifiers()&Qt::ControlModifier){const int motion=horizontal!=0?horizontal:delta;const double span=viewSpan();viewStart=std::clamp(viewStart-motion/120.0*span*.16,0.,qMax(0.,duration-span));viewEnd=viewStart+span;update();e->accept();return;}
    if(delta!=0){const double before=time(e->position().x());const double oldSpan=viewSpan();const double nextSpan=std::clamp(oldSpan*(delta>0?.8:1.25),qMin(.05,duration),duration);const double fraction=(before-viewStart)/oldSpan;viewStart=std::clamp(before-fraction*nextSpan,0.,qMax(0.,duration-nextSpan));viewEnd=viewStart+nextSpan;update();}e->accept();
}
void TimelineWidget::mouseMoveEvent(QMouseEvent *e){
    const double t=time(e->position().x());
    if(editedTrack>=0&&trackDrag){auto &track=tracks[editedTrack];if(track.locked)return;if(trackDrag==1)track.start=std::min(t,track.end-.01);else if(trackDrag==2)track.end=std::max(t,track.start+.01);else{const double length=track.end-track.start;track.start=std::clamp(t-dragOffset,0.,std::max(0.,duration-length));track.end=track.start+length;}emit trackEdited(editedTrack,track.start,track.end);update();return;}
    if(!drag)return;if(drag==1){start=std::min(t,end-.01);emit rangeEdited(start,end);}else if(drag==2){end=std::max(t,start+.01);emit rangeEdited(start,end);}else{position=t;emit seek(t);}update();
}
