#include "rule/Rule.h"

#include <stdexcept>

namespace rule {

std::string ToString(MeasurementType type) {
    switch (type) {
        case MeasurementType::PointToPoint: return "point_to_point";
        case MeasurementType::PointToPlane: return "point_to_plane";
        case MeasurementType::AxisProjection: return "axis_projection";
        case MeasurementType::FaceToFaceGap: return "face_to_face_gap";
        case MeasurementType::OverallSize: return "overall_size";
        case MeasurementType::InstanceCount: return "instance_count";
        case MeasurementType::MinPitch: return "min_pitch";
    }
    throw std::invalid_argument("unknown MeasurementType");
}

MeasurementType MeasurementTypeFromString(const std::string& s) {
    if (s == "point_to_point") return MeasurementType::PointToPoint;
    if (s == "point_to_plane") return MeasurementType::PointToPlane;
    if (s == "axis_projection") return MeasurementType::AxisProjection;
    if (s == "face_to_face_gap") return MeasurementType::FaceToFaceGap;
    if (s == "overall_size") return MeasurementType::OverallSize;
    if (s == "instance_count") return MeasurementType::InstanceCount;
    if (s == "min_pitch") return MeasurementType::MinPitch;
    throw std::invalid_argument("unknown measurement_type: " + s);
}

} // namespace rule
