#pragma once
#include <QtCore>
#include <QtGui>

struct ModelTexture {
    QString material;
    QString slot;
    QString path;
};

struct ModelVertex {
    QVector3D position;
    QVector2D uv;
};

struct ModelTriangle {
    int first=0;
    int second=0;
    int third=0;
    int material=-1;
};

struct ModelAsset {
    QString sourcePath;
    quint64 revision=0;
    int vertexCount=0;
    int faceCount=0;
    QStringList materials;
    QVector<ModelTexture> textures;
    QVector<ModelVertex> vertices;
    QVector<ModelTriangle> triangles;
};

class ModelImporter {
public:
    static bool load(const QString &path,ModelAsset &asset,QString &error);
    static int extractTextures(const ModelAsset &asset,const QString &folder,QString &error);
};
