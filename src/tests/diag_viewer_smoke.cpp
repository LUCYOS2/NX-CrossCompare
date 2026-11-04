// § 뷰어 점검(2026-11-04) - 실제 STEP을 불러와 뷰어(MultiViewportPanel/ModelViewport)를 띄우고, 앱에서
// 하는 조작(단면, 하이라이트, 뷰 프리셋, F1~F4 모드, 패널 재생성)을 자동으로 한 번씩 거친 뒤 닫는다.
// Debug 빌드면 CRT 힙 검사가 켜져 있어서 메모리를 잘못 쓰면 여기서 바로 잡힌다(회사PC "HEAP CORRUPTION
// DETECTED" 리포트 때 뷰어 코드 자체 문제인지 가려내려고 만듦). CTest에는 안 넣는다 - 창이 뜨고 실제
// STEP 경로가 필요하다.
//
//   diag_viewer_smoke <a.stp> [b.stp ...]
//
// 끝까지 가면 "viewer smoke OK"를 찍고 0으로 끝난다.

#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "geometry/StepGeometryAdapter.h"
#include "viewer/MultiViewportPanel.h"

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        std::fprintf(stderr, "usage: diag_viewer_smoke <a.stp> [b.stp ...]\n");
        return 1;
    }
    geometry::StepGeometryAdapter adapter;
    std::vector<std::pair<std::string, geometry::ModelHandle>> models;
    for (int i = 1; i < args.size(); ++i) {
        try {
            models.emplace_back("model " + std::to_string(i), adapter.LoadModel(args[i].toStdString()));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "LoadModel failed: %s\n", e.what());
            return 1;
        }
    }
    std::printf("loaded %zu model(s)\n", models.size());

    // § 피킹 속도(2026-11-04, "형상 프리셋 렉" 리포트) - 첫 모델 bbox를 화면처럼 20x20 격자로 훑는 광선으로
    // PickFace/PickFaceIndex를 불러 한 번당 평균 시간을 잰다(첫 호출의 캐시 생성 시간은 따로 찍음).
    {
        const auto handle = models.front().second;
        const auto box = adapter.GetBoundingBox(handle);
        const geometry::Vec3 dir{0.0, 0.0, -1.0};
        const double z = box.max.z + 100.0;
        auto rayAt = [&](int i, int j) {
            return geometry::Vec3{box.min.x + (box.max.x - box.min.x) * (i + 0.5) / 20.0,
                                  box.min.y + (box.max.y - box.min.y) * (j + 0.5) / 20.0, z};
        };
        auto start = std::chrono::steady_clock::now();
        adapter.PickFace(handle, rayAt(10, 10), dir);
        const double firstMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        int hits = 0;
        start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) {
            for (int j = 0; j < 20; ++j) {
                hits += adapter.PickFace(handle, rayAt(i, j), dir).kind != geometry::PickedFaceKind::None ? 1 : 0;
            }
        }
        const double pickMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 400.0;
        int faceSum = 0;
        start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) {
            for (int j = 0; j < 20; ++j) {
                faceSum += adapter.PickFaceIndex(handle, rayAt(i, j), dir);
            }
        }
        const double indexMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 400.0;
        std::printf("PickFace: first %.1f ms, then %.2f ms/call (hits %d/400) | PickFaceIndex %.2f ms/call (sum %d)\n",
                    firstMs, pickMs, hits, indexMs, faceSum);
    }

    auto panel = std::make_unique<viewer::MultiViewportPanel>(&adapter, models);
    panel->resize(1200, 800);
    panel->show();

    // 이벤트 루프가 돌면서 실제로 그려지게 단계마다 잠깐씩 텀을 둔다.
    int step = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        switch (step++) {
            case 0: panel->resetToNegIsoView(); break;
            case 1: panel->resetToTrimetricView(); break;
            case 2: panel->snapToNearestView(); break;
            case 3: panel->setMouseDragMode(viewer::MouseDragMode::Pan); break;
            case 4: panel->setSectionEnabled(true); break;
            case 5: {
                const auto box = adapter.GetBoundingBox(models.front().second);
                panel->SetCapturedPatchHighlight({box.min, box.max, geometry::Vec3{box.min.x, box.max.y, box.min.z}});
                break;
            }
            case 6: panel->setPickModeActive(true); panel->setRegionSelectModeActive(true); break;
            case 7: panel->setSectionEnabled(false); panel->SetCapturedPatchHighlight({}); break;
            case 8: {
                // STEP을 다시 불러올 때처럼 패널을 새로 만든다(정적 공유 상태/단축키 재등록 경로).
                panel.reset();
                panel = std::make_unique<viewer::MultiViewportPanel>(&adapter, models);
                panel->resize(1200, 800);
                panel->show();
                break;
            }
            case 9: panel->setMouseDragMode(viewer::MouseDragMode::Rotate); panel->resetToIsoView(); break;
            default:
                timer.stop();
                panel.reset();
                std::printf("viewer smoke OK\n");
                app.quit();
                break;
        }
    });
    timer.start(400);
    return app.exec();
}
