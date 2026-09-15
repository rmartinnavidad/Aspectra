#include "PsdImporter.h"
QString PsdImporter::importFile(QWidget *parent,const QString &file,const QString &cache){
    QString root=QCoreApplication::applicationDirPath(),python=root+"/psd-runtime/python.exe",script=root+"/psd_bridge.py";
    if(!QFile::exists(python)){QMessageBox::warning(parent,"PSD import","The bundled PSD reader is missing. Keep the psd-runtime folder beside Aspectra.exe.");return {};}
    QDir().mkpath(cache);
    auto run=[&](QStringList args)->bool{QProcess process;QProgressDialog progress("Reading PSD layers…","Cancel",0,0,parent);progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(0);QEventLoop loop;
        QObject::connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&loop,&QEventLoop::quit);
        QObject::connect(&process,&QProcess::errorOccurred,&loop,&QEventLoop::quit);QObject::connect(&progress,&QProgressDialog::canceled,&process,&QProcess::kill);
        args.prepend(script);process.start(python,args);loop.exec();progress.close();if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0){if(!progress.wasCanceled())QMessageBox::warning(parent,"PSD import",QString::fromUtf8(process.readAllStandardError()).right(1800));return false;}return true;};
    if(!run({"inspect",file,cache}))return {};
    QFile manifest(cache+"/manifest.json");if(!manifest.open(QIODevice::ReadOnly))return {};auto data=QJsonDocument::fromJson(manifest.readAll()).object();
    QDialog dialog(parent);dialog.setWindowTitle("PSD layers · "+QFileInfo(file).fileName());dialog.resize(680,590);auto *layout=new QVBoxLayout(&dialog);
    auto *note=new QLabel("Choose layers to composite. Photoshop is not required.\nUnsupported Photoshop effects may differ from the stored preview.");note->setWordWrap(true);layout->addWidget(note);
    auto *row=new QHBoxLayout;layout->addLayout(row,1);auto *list=new QTreeWidget;list->setHeaderLabels({"Layers","Type"});row->addWidget(list,1);auto *preview=new QLabel;preview->setFixedSize(300,350);preview->setAlignment(Qt::AlignCenter);row->addWidget(preview);
    QVector<QTreeWidgetItem*> ancestors;for(const auto &value:data["layers"].toArray()){auto layer=value.toObject();int depth=layer["depth"].toInt();auto *item=new QTreeWidgetItem;item->setText(0,layer["name"].toString());item->setText(1,layer["kind"].toString());item->setData(0,Qt::UserRole,layer["id"].toString());item->setData(0,Qt::UserRole+1,layer["group"].toBool());item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(0,layer["visible"].toBool()?Qt::Checked:Qt::Unchecked);
        if(depth>0&&depth<=ancestors.size())ancestors[depth-1]->addChild(item);else list->addTopLevelItem(item);if(ancestors.size()<=depth)ancestors.resize(depth+1);ancestors[depth]=item;}
    list->expandAll();list->resizeColumnToContents(0);
    auto showPreview=[&]{preview->setPixmap(QPixmap(cache+"/preview.png").scaled(preview->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));};showPreview();
    auto *message=new QLabel("Stored composite preview");layout->addWidget(message);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText("Import selected layers");layout->addWidget(buttons);
    QProcess renderer;QTimer debounce;debounce.setSingleShot(true);debounce.setInterval(180);bool pending=false,ready=QFileInfo::exists(cache+"/composite.png");
    auto render=[&]{if(renderer.state()!=QProcess::NotRunning){pending=true;return;}QJsonArray selected;QTreeWidgetItemIterator it(list);while(*it){if((*it)->checkState(0)==Qt::Checked)selected.append((*it)->data(0,Qt::UserRole).toString());++it;}QFile selection(cache+"/selection.json");if(!selection.open(QIODevice::WriteOnly))return;selection.write(QJsonDocument(selected).toJson());selection.close();ready=false;buttons->button(QDialogButtonBox::Ok)->setEnabled(false);message->setText("Updating layer composite…");renderer.start(python,{script,"render",file,cache,cache+"/selection.json"});};
    QObject::connect(&debounce,&QTimer::timeout,&dialog,render);
    QObject::connect(list,&QTreeWidget::itemChanged,&dialog,[&](QTreeWidgetItem *item,int){if(item->childCount()){QSignalBlocker block(list);std::function<void(QTreeWidgetItem*)> check=[&](QTreeWidgetItem *p){for(int i=0;i<p->childCount();++i){p->child(i)->setCheckState(0,item->checkState(0));check(p->child(i));}};check(item);}ready=false;buttons->button(QDialogButtonBox::Ok)->setEnabled(false);if(renderer.state()!=QProcess::NotRunning)pending=true;debounce.start();});
    QObject::connect(&renderer,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&dialog,[&](int code,QProcess::ExitStatus){if(pending){pending=false;render();return;}ready=code==0;buttons->button(QDialogButtonBox::Ok)->setEnabled(ready);if(ready){showPreview();message->setText("Live composite · selected layers");}else message->setText(QString::fromUtf8(renderer.readAllStandardError()).right(300));});
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(ready);int result=dialog.exec();renderer.kill();renderer.waitForFinished(2000);
    return result==QDialog::Accepted&&ready?cache+"/composite.png":QString();
}

QStringList PsdImporter::extractArtboards(QWidget *parent,const QString &file,const QString &destination,QString *error){
    const QString root=QCoreApplication::applicationDirPath();
    const QString python=root+"/psd-runtime/python.exe";
    const QString script=root+"/psd_bridge.py";
    auto fail=[&](const QString &reason){if(error)*error=reason;return QStringList{};};
    if(!QFile::exists(python)||!QFile::exists(script))return fail("The bundled PSD reader is missing. Keep psd-runtime and psd_bridge.py beside Aspectra.exe.");
    if(!QDir().mkpath(destination))return fail("Aspectra could not create the selected export folder.");

    QProgressDialog progress("Rendering PSD artboards…","Cancel",0,0,parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QProcess process;
    QEventLoop loop;
    QByteArray outputBuffer;
    QObject::connect(&process,&QProcess::readyReadStandardOutput,&progress,[&]{
        outputBuffer+=process.readAllStandardOutput();
        while(true){
            const int newline=outputBuffer.indexOf('\n');
            if(newline<0)break;
            const QByteArray line=outputBuffer.left(newline).trimmed();
            outputBuffer.remove(0,newline+1);
            const QJsonObject event=QJsonDocument::fromJson(line).object();
            const QString type=event.value("event").toString();
            const int total=event.value("total").toInt();
            if(type=="begin"&&total>0){progress.setRange(0,total);progress.setValue(0);progress.setLabelText(QString("Preparing %1 PSD artboards…").arg(total));}
            if(type=="progress"&&total>0){const int completed=event.value("completed").toInt();progress.setRange(0,total);progress.setValue(completed);progress.setLabelText(QString("Rendering artboard %1 of %2 · %3").arg(completed).arg(total).arg(event.value("name").toString()));}
        }
    });
    QObject::connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&loop,&QEventLoop::quit);
    QObject::connect(&process,&QProcess::errorOccurred,&loop,&QEventLoop::quit);
    QObject::connect(&progress,&QProgressDialog::canceled,&process,&QProcess::kill);
    process.start(python,{script,"export-artboards",file,destination});
    loop.exec();
    progress.close();
    if(progress.wasCanceled())return fail("Artboard export was cancelled.");
    if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0){
        QString detail=QString::fromUtf8(process.readAllStandardError()).trimmed();
        return fail(detail.isEmpty()?"The PSD did not contain readable artboards.":detail);
    }
    QFile manifest(QDir(destination).filePath("aspectra-artboards.json"));
    if(!manifest.open(QIODevice::ReadOnly))return fail("Artboard export completed but its manifest could not be read.");
    const QJsonObject report=QJsonDocument::fromJson(manifest.readAll()).object();
    const QJsonArray records=report.value("artboards").toArray();
    QStringList result;
    for(const auto &value:records){
        const QString rendered=QDir(destination).filePath(value.toObject().value("file").toString());
        if(QFileInfo::exists(rendered))result<<rendered;
    }
    if(result.isEmpty())return fail("No Photoshop artboards were found in this PSD or PSB.");
    return result;
}

int PsdImporter::exportArtboards(QWidget *parent,const QString &file,const QString &destination,QString *error){
    return extractArtboards(parent,file,destination,error).size();
}
