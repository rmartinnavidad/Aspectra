#pragma once
#include <QtWidgets>
class PsdImporter {
public:
    static QString importFile(QWidget *parent,const QString &file,const QString &cache);
    // Extracts native Photoshop artboards to PNGs and returns their rendered paths.
    // An empty result means the document has no readable artboards or the operation failed.
    static QStringList extractArtboards(QWidget *parent,const QString &file,const QString &destination,QString *error=nullptr);
    // Renders each Photoshop artboard to a standalone PNG without launching Photoshop.
    // Returns the number of files written; the optional error receives a user-facing reason.
    static int exportArtboards(QWidget *parent,const QString &file,const QString &destination,QString *error=nullptr);
};
