#include "dr2hook/free_camera_math.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

bool Near(float a, float b, float eps = 1.0e-4f) {
  return std::fabs(a - b) <= eps;
}

bool Near(dr2hook::FreeCamVec3 v, float x, float y, float z, float eps = 1.0e-4f) {
  return Near(v.x, x, eps) && Near(v.y, y, eps) && Near(v.z, z, eps);
}

void TestLiveChaseBasis() {
  // Amostra ao vivo, pid 2050923, carro parado. +0x210 cima, +0x220 direita,
  // +0x230 frente.
  const dr2hook::FreeCamVec3 rows[3] = {
      {0.04116f, 0.99904f, 0.01512f},
      {0.59742f, -0.01247f, -0.80183f},
      {0.80087f, -0.04204f, 0.59736f},
  };
  dr2hook::FreeCamPose pose{};
  assert(dr2hook::FreeCamPoseFromRows(
      rows, {-4578.24316f, 125.64421f, 39.19302f}, 0.f, pose));
  assert(pose.upSlot == 0);
  assert(pose.rightSlot == 1);
  assert(pose.forwardSlot == 2);
  assert(Near(pose.forward, 0.80087f, -0.04204f, 0.59736f, 2.0e-4f));
  assert(std::fabs(pose.right.y) < 1.0e-4f);
  assert(pose.up.y > 0.9f);
  assert(Near(dr2hook::FreeCamDot(pose.right, pose.forward), 0.f, 1.0e-4f));
  assert(Near(dr2hook::FreeCamDot(pose.up, pose.forward), 0.f, 1.0e-4f));
  assert(Near(dr2hook::FreeCamLength(pose.forward), 1.f, 1.0e-4f));

  dr2hook::FreeCamVec3 written[3]{};
  dr2hook::FreeCamWriteRows(pose, written);
  assert(Near(written[0], pose.up.x, pose.up.y, pose.up.z));
  assert(Near(written[1], pose.right.x, pose.right.y, pose.right.z));
  assert(Near(written[2], pose.forward.x, pose.forward.y, pose.forward.z));
}

void TestRejectsGarbage() {
  const dr2hook::FreeCamVec3 rows[3] = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}};
  dr2hook::FreeCamPose pose{};
  assert(!dr2hook::FreeCamPoseFromRows(rows, {1.f, 2.f, 3.f}, 0.f, pose));
}

void TestYawLooksRight() {
  const dr2hook::FreeCamVec3 rows[3] = {
      {0.f, 1.f, 0.f},
      {1.f, 0.f, 0.f},
      {0.f, 0.f, 1.f},
  };
  dr2hook::FreeCamPose pose{};
  assert(dr2hook::FreeCamPoseFromRows(rows, {0.f, 0.f, 0.f}, 0.f, pose));
  const float quarter = 1.5707963f;
  dr2hook::FreeCamLook(pose, quarter, 0.f);
  assert(Near(pose.forward, 1.f, 0.f, 0.f, 1.0e-4f));
  assert(Near(pose.right, 0.f, 0.f, -1.f, 1.0e-4f));
}

void TestPitchLooksDown() {
  const dr2hook::FreeCamVec3 rows[3] = {
      {0.f, 1.f, 0.f},
      {1.f, 0.f, 0.f},
      {0.f, 0.f, 1.f},
  };
  dr2hook::FreeCamPose pose{};
  assert(dr2hook::FreeCamPoseFromRows(rows, {0.f, 10.f, 0.f}, 1.f, pose));
  dr2hook::FreeCamLook(pose, 0.f, 0.1f);
  assert(pose.forward.y < -0.05f);
  assert(pose.forward.z > 0.9f);
  assert(pose.eyeW == 1.f);
}

void TestMoveFollowsAxes() {
  const dr2hook::FreeCamVec3 rows[3] = {
      {0.f, 1.f, 0.f},
      {1.f, 0.f, 0.f},
      {0.f, 0.f, 1.f},
  };
  dr2hook::FreeCamPose pose{};
  assert(dr2hook::FreeCamPoseFromRows(rows, {10.f, 20.f, 30.f}, 0.f, pose));
  dr2hook::FreeCamMove(pose, 0.f, 1.f, 0.f, 2.f);
  assert(Near(pose.eye, 10.f, 20.f, 32.f));
  dr2hook::FreeCamMove(pose, -1.f, 0.f, 1.f, 3.f);
  assert(Near(pose.eye, 7.f, 23.f, 32.f));
}

} // namespace

int main() {
  TestLiveChaseBasis();
  TestRejectsGarbage();
  TestYawLooksRight();
  TestPitchLooksDown();
  TestMoveFollowsAxes();
  std::cout << "test_free_camera: ok\n";
  return 0;
}
