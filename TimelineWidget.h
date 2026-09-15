#pragma once
#include <QtWidgets>
#include "Processing.h"
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget *parent=nullptr):QWidget(parent){setFixedHeight(214);setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);}
    void setDuration(double v){duration=std::max(.001,v);viewStart=0;viewEnd=duration;update();}
    void setPosition(double v){position=v;update();}
    void setRange(double a,double b){start=a;end=b;update();}
    void setClips(QVector<ClipRange> value){clips=value;update();}
    void setTracks(QVector<TimelineTrack> value){tracks=std::move(value);laneTracks.clear();laneTypes.clear();for(int type=0;type<=4;++type){bool found=false;for(int index=0;index<tracks.size();++index)if(int(tracks[index].type)==type){laneTracks<<index;laneTypes<<type;found=true;}if(!found){laneTracks<<-1;laneTypes<<type;}}updateHeight();update();}
signals:
    void seek(double);
    void rangeEdited(double,double);
    void trackVisibilityChanged(int,bool);
    void trackLockChanged(int,bool);
    void trackSelected(int);
    void trackEdited(int,double,double);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override{drag=0;editedTrack=-1;trackDrag=0;}
    void wheelEvent(QWheelEvent *) override;
private:
    double duration=1,position=0,start=0,end=1,viewStart=0,viewEnd=1;
    QVector<ClipRange> clips;QVector<TimelineTrack> tracks;QVector<int> laneTracks,laneTypes;
    int drag=0,editedTrack=-1,trackDrag=0,trackHeight=31;
    double dragOffset=0;
    double viewSpan() const{return qMax(.001,viewEnd-viewStart);}
    void updateHeight(){setFixedHeight(qMax(214,43+laneTracks.size()*trackHeight+10));}
    double x(double time) const{return 9+std::clamp((time-viewStart)/viewSpan(),0.,1.)*(width()-18);}
    double time(double pixel) const{return viewStart+std::clamp((pixel-9)/(width()-18),0.,1.)*viewSpan();}
    int laneAt(double y) const{return qFloor((y-43)/double(trackHeight));}
    int trackIndexForLane(int lane) const{return lane>=0&&lane<laneTracks.size()?laneTracks[lane]:-1;}
};
