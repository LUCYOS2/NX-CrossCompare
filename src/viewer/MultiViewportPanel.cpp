#include "viewer/MultiViewportPanel.h"
#include "viewer/ModelViewport.h"

#include "geometry/AlignmentCheck.h"

#include <QDebug>
#include <QGridLayout>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>

#include <cmath>

namespace viewer {

MultiViewportPanel::MultiViewportPanel(geometry::IGeometryAdapter* adapter,
                                        const std::vector<std::string>& modelFiles,
                                        QWidget* parent)
    : QWidget(parent) {
    auto* grid = new QGridLayout(this);

    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(modelFiles.size())))));

    for (size_t i = 0; i < modelFiles.size(); ++i) {
        const std::string& filePath = modelFiles[i];
        const auto handle = adapter->LoadModel(filePath);

        // 정렬 QC: 어셈블리 중심이 절대좌표 원점 근처인지 확인 (개발계획_v2.md §5)
        const auto box = adapter->GetBoundingBox(handle);
        const auto alignment = geometry::CheckCenterAlignment(box, /*toleranceMm=*/1.0);
        if (!alignment.aligned) {
            qWarning() << QString::fromStdString(filePath)
                       << "정렬 QC 실패 - 중심 오프셋(mm):" << alignment.offsetMm;
        } else {
            qDebug() << QString::fromStdString(filePath) << "정렬 QC 통과";
        }

        auto* container = new QWidget(this);
        auto* vbox = new QVBoxLayout(container);
        vbox->setContentsMargins(2, 2, 2, 2);

        auto* label = new QLabel(QString::fromStdString(filePath), container);
        label->setAlignment(Qt::AlignCenter);

        auto* viewport = new ModelViewport(adapter, handle, &camera_, container);
        viewports_.push_back(viewport);

        vbox->addWidget(label);
        vbox->addWidget(viewport, 1);

        connect(viewport, &ModelViewport::cameraChanged, this, [this]() {
            for (auto* vp : viewports_) {
                vp->update();
            }
        });

        const int row = static_cast<int>(i) / columns;
        const int col = static_cast<int>(i) % columns;
        grid->addWidget(container, row, col);
    }
}

} // namespace viewer
