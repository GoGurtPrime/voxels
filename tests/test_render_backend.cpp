#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "voxels/render/camera.hpp"

TEST_CASE("Camera.ProjectionAndViewMatricesMatchKnownValues", "[render][camera]") {
    voxels::Camera camera;
    camera.position = {0.0f, 1.0f, 4.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    camera.fovY = 60.0f * (3.14159265f / 180.0f);
    camera.aspect = 1.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    const auto projection = camera.Projection();
    const auto view = camera.View();
    const auto vp = camera.ViewProjection();

    REQUIRE(projection[0][0] > 0.0f);
    REQUIRE(std::abs(view[3][0]) < 1.0e-5f);
    REQUIRE(vp[0][0] > 0.0f);
}

TEST_CASE("Frustum.CullsAabbsOutsideAndKeepsInside", "[render][camera]") {
    voxels::Camera camera;
    camera.position = {0.0f, 0.0f, 3.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    camera.aspect = 1.0f;

    voxels::Frustum frustum;
    frustum.Update(camera.ViewProjection());

    voxels::BoundingBox inside{{0, 0, 0}, {1, 1, 1}};
    voxels::BoundingBox outside{{10, 10, 10}, {12, 12, 12}};
    voxels::BoundingBox straddling{{-1, -1, 0}, {1, 1, 1}};

    REQUIRE(frustum.Intersects(inside));
    REQUIRE_FALSE(frustum.Intersects(outside));
    REQUIRE(frustum.Intersects(straddling));
}
