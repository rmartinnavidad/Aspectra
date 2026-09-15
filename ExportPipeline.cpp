#include "Processing.h"
#include "Compressor.h"
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>
#include <stdexcept>
namespace {
void fail(const QString &s){throw std::runtime_error(s.toUtf8().constData());}
void cancelled(std::atomic_bool &c){if(c)fail("Cancelled");}
void applySelectionMask(QImage &image,const QImage &mask){if(image.isNull()||mask.isNull())return;const QImage alpha=mask.scaled(image.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);image=image.convertToFormat(QImage::Format_ARGB32);for(int y=0;y<image.height();++y){QRgb *pixels=reinterpret_cast<QRgb*>(image.scanLine(y));const uchar *values=alpha.constScanLine(y);for(int x=0;x<image.width();++x)pixels[x]=qRgba(qRed(pixels[x]),qGreen(pixels[x]),qBlue(pixels[x]),qAlpha(pixels[x])*values[x]/255);}}

}
ExportResult ExportManager::run(const ExportJob &job,std::atomic_bool &cancel,const std::function<void(int,int,QString)> &progress){
    ExportResult result;std::mutex mutex;std::atomic_int written{0},next{0};std::atomic_bool failed{false};
    struct Task {QString input,stem;QVector<ClipRange> clips;};QVector<Task> tasks;
    try{
        if(job.files.isEmpty()||job.sizes.isEmpty())fail("Choose files and output sizes.");
        if(job.createRar&&job.rar.isEmpty())fail("Select Rar.exe in Settings to create RAR archives.");
        result.folder=QDir(job.outputRoot).filePath("Aspectra-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(6));
        if(!QDir().mkpath(result.folder))fail("Cannot create export directory.");
        QVector<RecolorPreset> variants{{"Original",Qt::white,0,1,true}};for(const auto &p:job.presets)if(p.enabled)variants<<p;
        const QStringList imageFormats=job.imageFormats.isEmpty()&&!job.explicitFormats?QStringList{job.imageFormat}:job.imageFormats;
        const QStringList videoFormats=job.videoFormats.isEmpty()&&!job.explicitFormats?QStringList{job.videoFormat}:job.videoFormats;
        QSet<QString> names;int total=0;
        for(const auto &input:job.files){bool video=VideoProcessor::isVideo(input);QString stem=QFileInfo(job.inputAliases.value(input,input)).completeBaseName(),name=stem;int duplicate=2;while(names.contains(name.toLower()))name=stem+"_"+QString::number(duplicate++);names.insert(name.toLower());
            QVector<ClipRange> clips;for(const auto &clip:job.clips.value(input))if(clip.enabled){if(clip.start<0||clip.end<=clip.start)fail("Invalid clip range: "+clip.name);clips<<clip;}
            if(video&&job.clips.contains(input)&&clips.isEmpty())continue;
            if(clips.isEmpty())clips<<ClipRange{"",job.start,job.duration>0?job.start+job.duration:0,true};
            if(video){for(int i=0;i<clips.size();++i){QString suffix=clips[i].name.isEmpty()?"":QString("_clip%1_%2").arg(i+1,2,10,QChar('0')).arg(clips[i].name).replace(QRegularExpression("[^a-zA-Z0-9_-]"),"_");tasks<<Task{input,name+suffix,{clips[i]}};}}
            else tasks<<Task{input,name,clips};
            total+=int((job.sizeMode==1?1:job.sizes.size())*variants.size()*(video?videoFormats.size()*clips.size():imageFormats.size()));if(video)total+=int(job.audioFormats.size()*clips.size());
        }
        if(!total)fail("No enabled clips or formats to export.");
        auto notify=[&](QString message){std::lock_guard<std::mutex> guard(mutex);progress(written,total,message);};
        auto work=[&]{
            while(!failed&&!cancel){int index=next++;if(index>=tasks.size())break;const auto task=tasks[index];
                try{
                    bool video=VideoProcessor::isVideo(task.input);QImage source;if(!video)source=ImageProcessor::read(task.input);
                    QVector<QSize> outputSizes=job.sizes;if(job.sizeMode==1){QSize native=video?VideoProcessor::inspect(job.ffmpeg,task.input).size:source.size();if(job.adjustments.rotation%180)native.transpose();outputSizes={native};}
                    for(const auto &size:outputSizes)if(size.width()>8192||size.height()>8192)fail("Output dimensions exceed 8192 pixels. Choose smaller dimensions.");
                    for(const QSize &size:outputSizes)for(int v=0;v<variants.size();++v){cancelled(cancel);if(failed)return;
                        QString folder=QDir(result.folder).filePath(QString::number(size.width()));if(!QDir().mkpath(folder))fail("Cannot create size directory.");
                        QString suffix=v?"_"+variants[v].name.toLower().replace(QRegularExpression("[^a-z0-9_-]"),"_"):"";
                        QImage rendered;if(!video){rendered=ImageProcessor::compositeTimeline(ImageProcessor::render(source,size,job.adjustments,v?&variants[v]:nullptr),job.timelineTracks,0);applySelectionMask(rendered,job.masks.value(task.input));}
                        for(const auto &format:(video?videoFormats:imageFormats)){cancelled(cancel);QString output=QDir(folder).filePath(task.stem+suffix+"."+format.toLower());
                            if(video){ExportJob item=job;item.videoFormat=format;item.start=task.clips[0].start;item.duration=task.clips[0].end>item.start?task.clips[0].end-item.start:0;VideoProcessor::render(item,task.input,output,size,v?&variants[v]:nullptr,cancel,[&](QString m){notify(task.stem+" · "+m);});}
                            else Compressor::image(rendered,format,output,job,cancel);
                            if(video&&job.targetBytes){try{Compressor::video(output,job,cancel,notify);}catch(...){QFile::remove(output);throw;}}
                            {std::lock_guard<std::mutex> guard(mutex);++written;result.log<<"Saved "+QDir(result.folder).relativeFilePath(output)+QString(" · %1 bytes").arg(QFileInfo(output).size());progress(written,total,QString("Rendered %1 / %2 outputs").arg(written.load()).arg(total));}
                        }
                    }
                    if(video)for(const auto &format:job.audioFormats){cancelled(cancel);QString folder=QDir(result.folder).filePath("audio");QDir().mkpath(folder);QString output=QDir(folder).filePath(task.stem+"."+format.toLower());ExportJob item=job;item.start=task.clips[0].start;item.duration=task.clips[0].end>item.start?task.clips[0].end-item.start:0;Compressor::audio(task.input,output,format,item,cancel);{std::lock_guard<std::mutex> guard(mutex);++written;result.log<<"Saved audio/"+QFileInfo(output).fileName()+QString(" · %1 bytes").arg(QFileInfo(output).size());progress(written,total,QString("Rendered %1 / %2 outputs").arg(written.load()).arg(total));}}
                }catch(const std::exception &e){std::lock_guard<std::mutex> guard(mutex);failed=true;result.error=QString::fromUtf8(e.what());result.log<<result.error;}
            }
        };
        std::vector<std::thread> workers;for(int i=0;i<std::min(int(tasks.size()),std::clamp(job.workers,1,4));++i)workers.emplace_back(work);for(auto &t:workers)t.join();
        cancelled(cancel);if(failed)fail(result.error);
        // Archive the completed log along with the files.
        {QFile log(result.folder+"/export-log.txt");if(log.open(QIODevice::WriteOnly))log.write(result.log.join('\n').toUtf8());}
        if(job.zip){notify("Creating ZIP archive…");zip(result.folder,result.folder+".zip",cancel,job.archiveLevel);result.log<<"Created "+result.folder+".zip";}
        if(job.createRar){notify("Creating RAR archive…");QProcess p;p.setWorkingDirectory(result.folder);p.start(job.rar,{"a","-r","-idq",QString("-m%1").arg(job.archiveLevel==0?0:job.archiveLevel<=3?1:5),result.folder+".rar","."});if(!p.waitForStarted())fail(p.errorString());
            while(p.state()!=QProcess::NotRunning){if(cancel){p.kill();p.waitForFinished();QFile::remove(result.folder+".rar");cancelled(cancel);}p.waitForFinished(100);}if(p.exitCode()!=0)fail("RAR failed: "+QString::fromUtf8(p.readAllStandardError()));result.log<<"Created "+result.folder+".rar";}
    }catch(const std::exception &e){result.error=QString::fromUtf8(e.what());result.log<<result.error;}
    result.cancelled=cancel;result.written=written;
    if(!result.folder.isEmpty()){QFile log(result.folder+"/export-log.txt");if(log.open(QIODevice::WriteOnly))log.write(result.log.join('\n').toUtf8());}
    return result;
}
