#include "rule/SyntheticFixture.h"

namespace rule {

std::vector<PointSample> GenerateBossScrewFixture(
    const std::vector<int>& inches, const SyntheticFixtureConfig& config) {
    std::vector<PointSample> points;
    points.reserve(inches.size() * 2);

    for (int inch : inches) {
        const double scale = static_cast<double>(inch) / config.baseInch;
        const double ax = config.baseX * scale;
        const double ay = config.baseY * scale;

        points.push_back(PointSample{inch, "A", ax, ay, config.baseZ});
        points.push_back(PointSample{
            inch, "B", ax + config.fixedOffsetX, ay + config.fixedOffsetY, config.baseZ});
    }

    return points;
}

} // namespace rule
