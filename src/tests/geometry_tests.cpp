#include "geometry/AlignmentCheck.h"
#include "geometry/MockGeometryAdapter.h"

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

void TestCenterAlignment_PassesWhenCentered() {
    geometry::BoundingBox box{{-100, -50, -20}, {100, 50, 20}};
    const auto result = geometry::CheckCenterAlignment(box, 1.0);
    Check(result.aligned, "centered box should pass alignment check");
}

void TestCenterAlignment_FailsWhenOffset() {
    geometry::BoundingBox box{{-100, -50, -20}, {150, 50, 20}}; // center.x = 25
    const auto result = geometry::CheckCenterAlignment(box, 1.0);
    Check(!result.aligned, "offset box should fail alignment check");
}

void TestMockAdapter_ScalesWidthHeightKeepsDepthFixed() {
    geometry::MockGeometryAdapter adapter;
    const auto handle43 = adapter.LoadModel("43inch.jt");
    const auto handle85 = adapter.LoadModel("85inch.jt");

    const auto box43 = adapter.GetBoundingBox(handle43);
    const auto box85 = adapter.GetBoundingBox(handle85);

    const double width43 = box43.max.x - box43.min.x;
    const double width85 = box85.max.x - box85.min.x;
    const double depth43 = box43.max.z - box43.min.z;
    const double depth85 = box85.max.z - box85.min.z;

    Check(width85 > width43, "85inch model should be wider than 43inch");
    Check(std::abs(depth85 - depth43) < 1e-9, "depth should stay fixed regardless of inch");

    const auto alignment43 = geometry::CheckCenterAlignment(box43, 1.0);
    Check(alignment43.aligned, "mock model should be centered at origin");
}

} // namespace

int main() {
    TestCenterAlignment_PassesWhenCentered();
    TestCenterAlignment_FailsWhenOffset();
    TestMockAdapter_ScalesWidthHeightKeepsDepthFixed();

    if (g_failures == 0) {
        std::printf("All geometry tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
