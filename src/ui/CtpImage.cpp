#include "ui/CtpImage.h"

#include "viewer/MultiViewportPanel.h"

#include <QDateTime>
#include <QDir>

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

QVector3D ToQt(const geometry::Vec3& v) {
    return QVector3D(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

QVector3D AxisVector(int axis, double sign) {
    QVector3D v;
    v[axis] = static_cast<float>(sign);
    return v;
}

} // namespace

QImage RenderCtpPlan(viewer::MultiViewportPanel* panel, geometry::ModelHandle handle, const rule::CtpCapturePlan& plan) {
    if (!panel || !plan.ok) {
        return QImage();
    }
    const auto& v = plan.view;
    viewer::CtpRenderView view;
    view.center = ToQt(v.center);
    view.viewDir = AxisVector(v.viewAxis, v.viewSign);
    view.up = AxisVector(v.upAxis, v.upSign);
    view.halfWidth = static_cast<float>(v.halfWidth);
    view.halfHeight = static_cast<float>(v.halfHeight);
    view.sectionEnabled = v.sectionEnabled;
    view.sectionAxis = v.sectionAxis;
    view.sectionCoord = static_cast<float>(v.sectionCoord);
    view.sectionSign = static_cast<float>(v.sectionSign);

    std::vector<viewer::CtpRenderDimension> dims;
    for (const auto& d : plan.dims) {
        if (!d.visible) {
            continue;
        }
        viewer::CtpRenderDimension rd;
        rd.measureA = ToQt(d.extA);
        rd.measureB = ToQt(d.extB);
        rd.lineA = ToQt(d.lineA);
        rd.lineB = ToQt(d.lineB);
        rd.label = QString::fromStdString(d.label);
        dims.push_back(rd);
    }
    const double aspect = v.halfWidth / std::max(1e-6, v.halfHeight);
    const int height = std::max(64, static_cast<int>(std::lround(kCtpImageWidthPx / aspect)));
    return panel->RenderCtpImage(handle, view, dims, QSize(kCtpImageWidthPx, height));
}

QString SaveCtpImage(const QImage& image) {
    if (image.isNull()) {
        return QString();
    }
    QDir dir(QDir::current().filePath("rule_images"));
    if (!dir.exists() && !dir.mkpath(".")) {
        return QString();
    }
    const QString path =
        dir.filePath("ctp_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".png");
    return image.save(path, "PNG") ? path : QString();
}

} // namespace ui
