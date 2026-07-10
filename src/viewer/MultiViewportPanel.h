#pragma once

#include <QWidget>

#include <string>
#include <vector>

#include "geometry/IGeometryAdapter.h"
#include "viewer/Camera.h"

namespace viewer {

class ModelViewport;

// 여러 인치의 ModelViewport를 그리드로 배치하고, 하나의 Camera를 공유시켜
// 동기 회전/확대/이동을 구현한다. 모델 로드 시 정렬 QC(§5)도 함께 수행한다.
class MultiViewportPanel : public QWidget {
    Q_OBJECT

public:
    MultiViewportPanel(geometry::IGeometryAdapter* adapter,
                        const std::vector<std::string>& modelFiles,
                        QWidget* parent = nullptr);

private:
    Camera camera_;
    std::vector<ModelViewport*> viewports_;
};

} // namespace viewer
