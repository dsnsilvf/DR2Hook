#pragma once

#include <cmath>
#include <cstdint>

namespace dr2hook {

// Pose da câmera de especial. Os três eixos são unitários, no mundo, Y para cima.
// right × up = forward. O olho é a posição em metros.
struct FreeCamVec3 {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
};

struct FreeCamPose {
  FreeCamVec3 right;
  FreeCamVec3 up;
  FreeCamVec3 forward;
  FreeCamVec3 eye;
  float eyeW = 0.f;
  // Índice da linha lida em câmera+0x210 / +0x220 / +0x230.
  int upSlot = 0;
  int rightSlot = 1;
  int forwardSlot = 2;
};

inline float FreeCamDot(FreeCamVec3 a, FreeCamVec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline FreeCamVec3 FreeCamCross(FreeCamVec3 a, FreeCamVec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float FreeCamLength(FreeCamVec3 v) { return std::sqrt(FreeCamDot(v, v)); }

inline FreeCamVec3 FreeCamScale(FreeCamVec3 v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

inline FreeCamVec3 FreeCamAdd(FreeCamVec3 a, FreeCamVec3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline bool FreeCamFinite(FreeCamVec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Devolve o vetor zero se o comprimento for desprezível.
inline FreeCamVec3 FreeCamNormalize(FreeCamVec3 v) {
  const float len = FreeCamLength(v);
  if (!(len > 1.0e-6f)) {
    return {};
  }
  return FreeCamScale(v, 1.f / len);
}

// rows[0..2] são as linhas em câmera+0x210, +0x220 e +0x230.
// A frente capturada é preservada. A direita e o cima são refeitos sem roll,
// com o cima do mundo em +Y. Falha se as linhas não forem uma base usável
// ou se a frente estiver quase vertical.
inline bool FreeCamPoseFromRows(const FreeCamVec3 rows[3], FreeCamVec3 eye,
                                float eyeW, FreeCamPose &out) {
  if (!FreeCamFinite(eye)) {
    return false;
  }
  int upSlot = 0;
  float bestY = -1.f;
  for (int i = 0; i < 3; ++i) {
    if (!FreeCamFinite(rows[i])) {
      return false;
    }
    const float len = FreeCamLength(rows[i]);
    if (len < 0.5f || len > 1.5f) {
      return false;
    }
    const float ay = std::fabs(rows[i].y);
    if (ay > bestY) {
      bestY = ay;
      upSlot = i;
    }
  }
  const int other[2] = {(upSlot + 1) % 3, (upSlot + 2) % 3};
  const FreeCamVec3 asRight = FreeCamCross(rows[other[0]], rows[upSlot]);
  const int rightSlot =
      FreeCamDot(asRight, rows[other[1]]) >=
              FreeCamDot(FreeCamCross(rows[other[1]], rows[upSlot]), rows[other[0]])
          ? other[0]
          : other[1];
  const int forwardSlot = rightSlot == other[0] ? other[1] : other[0];

  const FreeCamVec3 forward = FreeCamNormalize(rows[forwardSlot]);
  const FreeCamVec3 worldUp{0.f, 1.f, 0.f};
  const FreeCamVec3 right = FreeCamNormalize(FreeCamCross(worldUp, forward));
  if (FreeCamLength(right) < 0.5f) {
    return false;
  }
  const FreeCamVec3 up = FreeCamNormalize(FreeCamCross(forward, right));
  if (!FreeCamFinite(forward) || !FreeCamFinite(right) || !FreeCamFinite(up)) {
    return false;
  }

  out.forward = forward;
  out.right = right;
  out.up = up;
  out.eye = eye;
  out.eyeW = eyeW;
  out.upSlot = upSlot;
  out.rightSlot = rightSlot;
  out.forwardSlot = forwardSlot;
  return true;
}

// Rotação em torno de +Y. Ângulo positivo gira a frente em direção à direita.
inline FreeCamVec3 FreeCamRotY(FreeCamVec3 v, float radians) {
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

// yawRad positivo olha para a direita. pitchRad positivo olha para baixo.
// O pitch que deixaria a frente quase vertical é ignorado.
inline void FreeCamLook(FreeCamPose &pose, float yawRad, float pitchRad) {
  if (yawRad != 0.f) {
    const FreeCamVec3 turned = FreeCamNormalize(FreeCamRotY(pose.forward, yawRad));
    const FreeCamVec3 right =
        FreeCamNormalize(FreeCamCross(FreeCamVec3{0.f, 1.f, 0.f}, turned));
    if (FreeCamLength(right) > 0.5f) {
      pose.forward = turned;
      pose.right = right;
      pose.up = FreeCamNormalize(FreeCamCross(pose.forward, pose.right));
    }
  }
  if (pitchRad == 0.f) {
    return;
  }
  const float c = std::cos(pitchRad);
  const float s = std::sin(pitchRad);
  const FreeCamVec3 axis = pose.right;
  const FreeCamVec3 kxv = FreeCamCross(axis, pose.forward);
  const float kdv = FreeCamDot(axis, pose.forward);
  const FreeCamVec3 pitched = FreeCamNormalize(FreeCamAdd(
      FreeCamAdd(FreeCamScale(pose.forward, c), FreeCamScale(kxv, s)),
      FreeCamScale(axis, kdv * (1.f - c))));
  if (!FreeCamFinite(pitched) || std::fabs(pitched.y) >= 0.995f) {
    return;
  }
  pose.forward = pitched;
  pose.up = FreeCamNormalize(FreeCamCross(pose.forward, pose.right));
  pose.right = FreeCamNormalize(FreeCamCross(pose.up, pose.forward));
}

// Eixos em [-1, 1]. meters já inclui o dt e o fator de velocidade.
inline void FreeCamMove(FreeCamPose &pose, float rightAxis, float forwardAxis,
                        float upAxis, float meters) {
  pose.eye = FreeCamAdd(
      pose.eye, FreeCamScale(pose.right, rightAxis * meters));
  pose.eye = FreeCamAdd(
      pose.eye, FreeCamScale(pose.forward, forwardAxis * meters));
  pose.eye = FreeCamAdd(pose.eye, FreeCamScale(pose.up, upAxis * meters));
}

inline void FreeCamWriteRows(const FreeCamPose &pose, FreeCamVec3 rows[3]) {
  rows[pose.upSlot] = pose.up;
  rows[pose.rightSlot] = pose.right;
  rows[pose.forwardSlot] = pose.forward;
}

} // namespace dr2hook
