#pragma once
#include "Processing.h"
class Compressor {
public:
    static void image(const QImage &,const QString &format,const QString &output,const ExportJob &,std::atomic_bool &);
    static void video(const QString &output,const ExportJob &,std::atomic_bool &,const std::function<void(QString)> &);
    static void audio(const QString &source,const QString &output,const QString &format,const ExportJob &,std::atomic_bool &);
};
