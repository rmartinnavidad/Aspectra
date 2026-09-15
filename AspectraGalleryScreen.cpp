#include "AspectraGalleryScreen.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr int TileUnit=138;
QDateTime projectTime(const QString &path){const QFileInfo info(path);return info.birthTime().isValid()?info.birthTime():info.lastModified();}
}

AspectraGalleryScreen::AspectraGalleryScreen(Presentation presentation,QWidget *parent):QWidget(parent),m_presentation(presentation){
    setObjectName("AspectraGalleryScreen");
    setStyleSheet("QWidget#AspectraGalleryScreen{background:#050608;}QScrollArea#LivingGalleryRail{background:transparent;border:0;}QScrollBar:horizontal{height:10px;background:transparent;margin:2px 20px 2px 20px;}QScrollBar::handle:horizontal{background:#36465d;border-radius:4px;min-width:42px;}QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}QPushButton#GalleryImport{background:#111721;border:1px solid #42516a;border-radius:14px;padding:8px 13px;color:#fff;font-weight:700;}QPushButton#GalleryImport:hover{border-color:#35c3f6;background:#182433;}");
    auto *root=new QVBoxLayout(this);root->setContentsMargins(presentation==Presentation::FullWelcome?QMargins(24,18,24,18):QMargins());root->setSpacing(presentation==Presentation::FullWelcome?12:0);
    if(presentation==Presentation::FullWelcome){
        auto *header=new QWidget(this);auto *headerLayout=new QHBoxLayout(header);headerLayout->setContentsMargins(0,0,0,0);headerLayout->setSpacing(10);
        auto *brand=new QLabel(header);QPixmap wordmark(":/brand/wordmark.png");if(!wordmark.isNull())brand->setPixmap(wordmark.scaled(220,44,Qt::KeepAspectRatio,Qt::SmoothTransformation));else brand->setText("ASPECTRA");brand->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
        auto *subhead=new QLabel("LIVING ART GALLERY",header);subhead->setStyleSheet("color:#d8e1ee;font-size:11px;font-weight:800;letter-spacing:3px;");
        auto *importButton=new QPushButton("Import project",header);importButton->setObjectName("GalleryImport");headerLayout->addWidget(brand);headerLayout->addWidget(subhead);headerLayout->addStretch(1);headerLayout->addWidget(importButton);root->addWidget(header);
        auto *caption=new QLabel("Every launch reshuffles your project history into a living mosaic. Hover to explore each project.",this);caption->setStyleSheet("color:#8f9caf;font-size:11px;");root->addWidget(caption);
        connect(importButton,&QPushButton::clicked,this,&AspectraGalleryScreen::importProjectRequested);
    }
    m_scrollArea=new QScrollArea(this);m_scrollArea->setObjectName("LivingGalleryRail");m_scrollArea->setFrameShape(QFrame::NoFrame);m_scrollArea->setWidgetResizable(false);m_scrollArea->setAlignment(Qt::AlignLeft|Qt::AlignTop);m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mosaicCanvas=new QWidget(m_scrollArea);m_mosaicCanvas->setObjectName("LivingGalleryCanvas");m_mosaicCanvas->setStyleSheet("QWidget#LivingGalleryCanvas{background:#050608;}");m_scrollArea->setWidget(m_mosaicCanvas);root->addWidget(m_scrollArea,1);
    m_emptyLabel=new QLabel("No recent projects yet\nImport a project to start your gallery.",m_mosaicCanvas);m_emptyLabel->setAlignment(Qt::AlignCenter);m_emptyLabel->setStyleSheet("color:#8d9cad;font-size:15px;line-height:1.5;");m_emptyLabel->hide();
}

void AspectraGalleryScreen::setProjectPaths(const QStringList &paths){
    QStringList valid;for(const QString &path:paths){const QString canonical=QFileInfo(path).absoluteFilePath();if(QFileInfo::exists(canonical)&&!valid.contains(canonical))valid.push_back(canonical);}std::sort(valid.begin(),valid.end(),[](const QString &left,const QString &right){return projectTime(left)>projectTime(right);});m_projectPaths=valid;m_hoveredTile=nullptr;for(GalleryTile *tile:m_tiles)tile->deleteLater();m_tiles.clear();m_spans.clear();
    for(const QString &path:m_projectPaths){auto *tile=new GalleryTile(path,m_mosaicCanvas);m_tiles.push_back(tile);connect(tile,&GalleryTile::hoverStarted,this,[this](GalleryTile *hovered){applyRadialRepulsion(hovered);});connect(tile,&GalleryTile::hoverEnded,this,[this](GalleryTile *leaving){clearRadialRepulsion(leaving);});connect(tile,&GalleryTile::openProjectRequested,this,&AspectraGalleryScreen::openProjectRequested);}scheduleMosaicLayout();
}

void AspectraGalleryScreen::scheduleMosaicLayout(){if(m_repackQueued)return;m_repackQueued=true;QTimer::singleShot(0,this,[this]{m_repackQueued=false;layoutMosaic();});}

AspectraGalleryScreen::Span AspectraGalleryScreen::randomSpan(bool hero,int availableColumns) const {
    if(hero)return availableColumns>=4?Span{4,4}:Span{qMin(3,availableColumns),2};
    static const QVector<Span> choices{{1,1},{2,1},{1,2},{2,2},{3,2},{4,4}};static const std::vector<double> weights{44.0,23.0,16.0,11.0,5.0,1.0};static thread_local std::mt19937 generator(std::random_device{}());std::discrete_distribution<int> distribution(weights.begin(),weights.end());
    for(int attempts=0;attempts<12;++attempts){const Span span=choices.value(distribution(generator));if(span.columns<=availableColumns)return span;}return {1,1};
}

void AspectraGalleryScreen::layoutMosaic(){
    const int availableWidth=qMax(360,m_scrollArea->viewport()->width());const int columns=qMax(3,availableWidth/TileUnit);
    if(m_tiles.isEmpty()){m_mosaicCanvas->setFixedSize(availableWidth,qMax(260,m_scrollArea->viewport()->height()));m_emptyLabel->setGeometry(m_mosaicCanvas->rect());m_emptyLabel->show();return;}
    m_emptyLabel->hide();QVector<QVector<bool>> occupied;auto ensureRows=[&](int count){while(occupied.size()<count)occupied.push_back(QVector<bool>(columns,false));};auto fits=[&](int x,int y,const Span &span){ensureRows(y+span.rows);if(x+span.columns>columns)return false;for(int row=y;row<y+span.rows;++row)for(int column=x;column<x+span.columns;++column)if(occupied[row][column])return false;return true;};auto reserve=[&](int x,int y,const Span &span){for(int row=y;row<y+span.rows;++row)for(int column=x;column<x+span.columns;++column)occupied[row][column]=true;};
    for(int index=0;index<m_tiles.size();++index){GalleryTile *tile=m_tiles[index];Span span=m_spans.value(tile->projectPath());if(span.columns<=0){span=randomSpan(index==0,columns);m_spans.insert(tile->projectPath(),span);}int placedX=0,placedY=0;bool placed=false;for(int row=0;!placed;++row){ensureRows(row+span.rows);for(int column=0;column<columns;++column){if(!fits(column,row,span))continue;placedX=column;placedY=row;reserve(column,row,span);placed=true;break;}}tile->setBaseGeometry(QRect(placedX*TileUnit,placedY*TileUnit,span.columns*TileUnit,span.rows*TileUnit),false);tile->show();}
    m_mosaicCanvas->setFixedSize(columns*TileUnit,qMax(1,occupied.size())*TileUnit);
}

void AspectraGalleryScreen::applyRadialRepulsion(GalleryTile *hovered){
    if(!hovered)return;m_hoveredTile=hovered;const QPointF focus=hovered->baseGeometry().center();for(GalleryTile *tile:m_tiles){const QRect base=tile->baseGeometry();if(tile==hovered){const int expandX=qMax(10,qRound(base.width()*.075));const int expandY=qMax(3,qRound(base.height()*.025));tile->animateTo(base.adjusted(-expandX,-expandY,expandX,expandY),QEasingCurve(QEasingCurve::OutBack));continue;}const QPointF delta=QPointF(base.center())-focus;const qreal distance=qMax<qreal>(1.0,std::hypot(delta.x(),delta.y()));const qreal magnitude=distance>460.0?0.0:qBound<qreal>(0.0,14.0,1350.0/distance);const QPointF force=(delta/distance)*magnitude;tile->animateTo(base.translated(qRound(force.x()),qRound(force.y())),QEasingCurve(QEasingCurve::OutQuad));}
}

void AspectraGalleryScreen::clearRadialRepulsion(GalleryTile *leavingTile){if(m_hoveredTile!=leavingTile)return;m_hoveredTile=nullptr;for(GalleryTile *tile:m_tiles)tile->animateTo(tile->baseGeometry(),QEasingCurve(QEasingCurve::OutBack));}
void AspectraGalleryScreen::resizeEvent(QResizeEvent *event){QWidget::resizeEvent(event);scheduleMosaicLayout();}
void AspectraGalleryScreen::showEvent(QShowEvent *event){QWidget::showEvent(event);scheduleMosaicLayout();}
