#pragma once

#include <QImage>
#include <QString>

struct VectorTraceResult {
    QString svg;
    QImage preview;
    int colorCount{};
};

class VectorTracer {
public:
    // Detail controls sampling resolution; smoothing controls how aggressively
    // sampled edges are reduced into editable anchor points.
    static VectorTraceResult trace(const QImage &source, int colors = 16, int detail = 512, int smoothing = 28);
    static int detectColorCount(const QImage &source, int maximum = 64);
};
