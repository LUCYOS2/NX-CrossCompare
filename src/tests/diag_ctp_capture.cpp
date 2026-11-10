// § CTP 캡처 이미지 점검(2026-11-09) - 실제 STEP에서 형상 프리셋 -> 자동 치수 후보 -> 자동 구도 -> 화면 밖 그리기까지
// 앱과 같은 코드로 거쳐 PNG를 남긴다. 사람이 이미지를 보고 구도/치수선/라벨을 확인하는 용도라 CTest에는 안 넣는다
// (창이 뜨고 STEP 경로가 필요하다).
//
//   diag_ctp_capture <a.stp> <출력 폴더>
//
// 면이 가장 많은 패치를 프리셋처럼 잡고, 같은 형상들 중 첫 번째에서 치수 후보를 1~4개 골라 페이지 크기별로 그린다.

#include <QApplication>
#include <QDir>
#include <QSurfaceFormat>
#include <QTimer>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "geometry/StepGeometryAdapter.h"
#include "rule/CtpCapture.h"
#include "rule/PresetDimensions.h"
#include "ui/CtpImage.h"
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
    if (args.size() < 3) {
        std::fprintf(stderr, "usage: diag_ctp_capture <a.stp> <out_dir>\n");
        return 1;
    }
    const QString outDir = args[2];
    QDir().mkpath(outDir);

    geometry::StepGeometryAdapter adapter;
    geometry::ModelHandle handle = geometry::kInvalidModelHandle;
    try {
        handle = adapter.LoadModel(args[1].toStdString());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "LoadModel failed: %s\n", e.what());
        return 1;
    }

    // 프리셋 대신: 면이 가장 많은 패치(후크/스냅 같은 복합 형상일 가능성이 큼).
    geometry::FeaturePatchDescriptor descriptor;
    int bestFaces = -1;
    for (const auto& face : adapter.FindFaceCandidates(handle, "", "")) {
        const auto capture = adapter.CaptureFeaturePatch(handle, face.center);
        if (capture.valid && capture.descriptor.faceCount > bestFaces) {
            bestFaces = capture.descriptor.faceCount;
            descriptor = capture.descriptor;
        }
    }
    const auto modelBox = adapter.GetBoundingBox(handle);
    const auto modelPlanes = adapter.FindPlaneCandidates(handle, "Datum_Plane", std::string());
    const auto shapes = adapter.FindPatchCandidates(handle, descriptor, geometry::kPatchSimilarityThreshold);
    std::printf("patch faces=%d, shapes=%zu\n", bestFaces, shapes.size());

    const geometry::PatchCandidate* shape = nullptr;
    std::vector<rule::PresetDimensionCandidate> candidates;
    for (const auto& s : shapes) {
        auto c = rule::ComputePresetDimensionCandidates(s, modelBox, modelPlanes);
        if (c.size() > candidates.size()) {
            candidates = std::move(c);
            shape = &s;
        }
    }
    if (!shape || candidates.empty()) {
        std::fprintf(stderr, "no dimension candidates on any shape\n");
        return 1;
    }
    for (const auto& c : candidates) {
        std::printf("candidate: %s  %.3f mm\n", rule::DescribePresetDimension(c).c_str(), c.valueMm);
    }

    viewer::MultiViewportPanel panel(&adapter, {{"model", handle}});
    panel.resize(900, 700);
    panel.show();

    int exitCode = 0;
    QTimer::singleShot(800, [&]() {
        // 형상 크기 후보를 먼저, 그다음 위치 후보 - 최대 4개.
        std::vector<rule::CtpDimensionInput> all;
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto& c : candidates) {
                const bool size = c.kind == rule::PresetDimensionKind::ShapeSize;
                if ((pass == 0) != size || all.size() >= 4) {
                    continue;
                }
                all.push_back(rule::MakeCtpDimension(c, std::string(1, static_cast<char>('A' + all.size()))));
            }
        }
        for (size_t n = 1; n <= all.size(); ++n) {
            const std::vector<rule::CtpDimensionInput> dims(all.begin(), all.begin() + static_cast<long>(n));
            const double aspect = rule::CtpImageAspect(static_cast<int>(n), 5);
            const auto plan = rule::PlanCtpCapture(dims, shape->box, shape->planes, aspect);
            const QImage image = ui::RenderCtpPlan(&panel, handle, plan);
            const QString path = QDir(outDir).filePath(QString("ctp_%1dims.png").arg(n));
            if (image.isNull() || !image.save(path)) {
                std::fprintf(stderr, "render/save failed for %zu dims\n", n);
                exitCode = 1;
                continue;
            }
            std::printf("%zu dims: view axis %d, up %d, %dx%d -> %s %s\n", n, plan.view.viewAxis, plan.view.upAxis,
                        image.width(), image.height(), path.toLocal8Bit().constData(), plan.warning.c_str());
            const auto& b = shape->box;
            const auto& v = plan.view;
            std::printf("   shape box (%.2f %.2f %.2f)-(%.2f %.2f %.2f)  center (%.2f %.2f %.2f) half %.2f x %.2f "
                        "right %d%+.0f up %d%+.0f view %d%+.0f\n",
                        b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z, v.center.x, v.center.y, v.center.z,
                        v.halfWidth, v.halfHeight, v.rightAxis, v.rightSign, v.upAxis, v.upSign, v.viewAxis, v.viewSign);
            for (const auto& d : plan.dims) {
                std::printf("   %s axis %d  m (%.2f %.2f %.2f)-(%.2f %.2f %.2f)  line (%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n",
                            d.label.c_str(), d.axis, d.measureA.x, d.measureA.y, d.measureA.z, d.measureB.x,
                            d.measureB.y, d.measureB.z, d.lineA.x, d.lineA.y, d.lineA.z, d.lineB.x, d.lineB.y, d.lineB.z);
            }
        }
        app.quit();
    });
    app.exec();
    return exitCode;
}
