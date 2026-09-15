#include "AnnotationEditor.h"
#include <cmath>
namespace {
class Canvas:public QWidget {
public:
    QImage image;QVector<QImage> history;int tool=0;QColor color{"#ca267a"};int weight=4;QPointF anchor,last;bool drawing=false;
    explicit Canvas(QImage source):image(source.convertToFormat(QImage::Format_ARGB32)){setMinimumSize(420,300);setMouseTracking(true);}
    QRectF box() const {QSize s=image.size().scaled(size(),Qt::KeepAspectRatio);return QRectF(QPointF((width()-s.width())/2.,(height()-s.height())/2.),s);}
    QPointF point(QPointF p)const {auto b=box();return {(p.x()-b.x())*image.width()/b.width(),(p.y()-b.y())*image.height()/b.height()};}
    void stash(){if(history.size()>=10)history.removeFirst();history<<image;image=image.copy();}
    void undo(){if(!history.isEmpty()){image=history.takeLast();update();}}
    void shape(QPainter &p,QPointF end){p.setPen(QPen(color,weight,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);if(tool==1){p.drawLine(anchor,end);double a=std::atan2(end.y()-anchor.y(),end.x()-anchor.x());double d=weight*4.;p.drawLine(end,end-QPointF(std::cos(a-.5)*d,std::sin(a-.5)*d));p.drawLine(end,end-QPointF(std::cos(a+.5)*d,std::sin(a+.5)*d));}else if(tool==2)p.drawRect(QRectF(anchor,end).normalized());else if(tool==4)p.fillRect(QRectF(anchor,end).normalized(),Qt::black);}
    void paintEvent(QPaintEvent *)override{QPainter p(this);p.fillRect(rect(),QColor("#0b0c10"));p.drawImage(box(),image);if(drawing&&tool!=0){p.save();auto b=box();p.translate(b.topLeft());p.scale(b.width()/image.width(),b.height()/image.height());shape(p,last);p.restore();}}
    void mousePressEvent(QMouseEvent *e)override{if(e->button()!=Qt::LeftButton||!box().contains(e->position()))return;anchor=last=point(e->position());if(tool==3){bool ok=false;auto text=QInputDialog::getText(this,"Add text","Text",QLineEdit::Normal,{},&ok);if(ok&&!text.isEmpty()){stash();QPainter p(&image);p.setPen(color);QFont font("Segoe UI");font.setPixelSize(std::max(12,weight*6));p.setFont(font);p.drawText(anchor,text);update();}return;}stash();drawing=true;}
    void mouseMoveEvent(QMouseEvent *e)override{if(!drawing)return;QPointF next=point(e->position());if(tool==0){QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(color,weight,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawLine(last,next);}last=next;update();}
    void mouseReleaseEvent(QMouseEvent *e)override{if(!drawing)return;if(tool!=0){QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);shape(p,point(e->position()));}drawing=false;update();}
};
}
QImage AnnotationEditor::edit(QWidget *parent,const QImage &source){
    if(source.isNull())return {};QDialog dialog(parent);dialog.setWindowTitle("Annotate / redact");dialog.resize(880,680);auto *v=new QVBoxLayout(&dialog);auto *bar=new QHBoxLayout;v->addLayout(bar);auto *canvas=new Canvas(source);auto *tools=new QComboBox;tools->addItems({"Pen","Arrow","Rectangle","Text","Solid redaction"});bar->addWidget(tools);QObject::connect(tools,&QComboBox::currentIndexChanged,&dialog,[=](int tool){canvas->tool=tool;});auto *color=new QPushButton("Color");bar->addWidget(color);QObject::connect(color,&QPushButton::clicked,&dialog,[&]{auto c=QColorDialog::getColor(canvas->color,&dialog);if(c.isValid())canvas->color=c;});auto *width=new QSpinBox;width->setRange(1,100);width->setValue(4);bar->addWidget(new QLabel("Size (px)"));bar->addWidget(width);QObject::connect(width,&QSpinBox::valueChanged,&dialog,[=](int n){canvas->weight=n;});auto *undo=new QPushButton("Undo");bar->addWidget(undo);QObject::connect(undo,&QPushButton::clicked,&dialog,[=]{canvas->undo();});v->addWidget(canvas,1);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);v->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);return dialog.exec()==QDialog::Accepted?canvas->image:QImage();
}
