// Minimal replacement for the DirectX 8.1 SDK's d3dx8math.h.
//
// D3DX8 is no longer distributed, and D3DX9's headers cannot be included next to
// d3d8.h (d3d9types.h redefines the same types). The client only uses a small part
// of D3DX math, so it is reimplemented here with the same names and layouts.
//
// Without SE1_D3D (macOS) it does not need d3d8.h: the math is plain CPU code that the
// engine also uses outside the renderer (terrain height, shadow matrices).
#pragma once

#ifdef SE1_D3D
#include <d3d8.h>
#else
// same layouts as in d3d8types.h; FLOAT and UINT come from the engine's types
struct D3DVECTOR {
  FLOAT x, y, z;
};
struct D3DMATRIX {
  union {
    struct {
      FLOAT _11, _12, _13, _14;
      FLOAT _21, _22, _23, _24;
      FLOAT _31, _32, _33, _34;
      FLOAT _41, _42, _43, _44;
    };
    FLOAT m[4][4];
  };
};
#endif // SE1_D3D
#include <math.h>

#define D3DX_PI ((FLOAT)3.141592654f)

struct D3DXVECTOR2 {
  FLOAT x, y;
  D3DXVECTOR2() {}
  D3DXVECTOR2(FLOAT fx, FLOAT fy) : x(fx), y(fy) {}
  operator FLOAT*() { return &x; }
  operator const FLOAT*() const { return &x; }
};

struct D3DXVECTOR3 : public D3DVECTOR {
  D3DXVECTOR3() {}
  D3DXVECTOR3(FLOAT fx, FLOAT fy, FLOAT fz) { x = fx; y = fy; z = fz; }
  D3DXVECTOR3(const D3DVECTOR& v) { x = v.x; y = v.y; z = v.z; }
  operator FLOAT*() { return &x; }
  operator const FLOAT*() const { return &x; }
  D3DXVECTOR3 operator+(const D3DXVECTOR3& v) const { return D3DXVECTOR3(x + v.x, y + v.y, z + v.z); }
  D3DXVECTOR3 operator-(const D3DXVECTOR3& v) const { return D3DXVECTOR3(x - v.x, y - v.y, z - v.z); }
  D3DXVECTOR3 operator*(FLOAT f) const { return D3DXVECTOR3(x * f, y * f, z * f); }
  D3DXVECTOR3 operator-() const { return D3DXVECTOR3(-x, -y, -z); }
  D3DXVECTOR3& operator+=(const D3DXVECTOR3& v) { x += v.x; y += v.y; z += v.z; return *this; }
  D3DXVECTOR3& operator-=(const D3DXVECTOR3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
  D3DXVECTOR3& operator*=(FLOAT f) { x *= f; y *= f; z *= f; return *this; }
};

struct D3DXVECTOR4 {
  FLOAT x, y, z, w;
  D3DXVECTOR4() {}
  D3DXVECTOR4(FLOAT fx, FLOAT fy, FLOAT fz, FLOAT fw) : x(fx), y(fy), z(fz), w(fw) {}
  operator FLOAT*() { return &x; }
  operator const FLOAT*() const { return &x; }
};

struct D3DXPLANE {
  FLOAT a, b, c, d;
  D3DXPLANE() {}
  D3DXPLANE(FLOAT fa, FLOAT fb, FLOAT fc, FLOAT fd) : a(fa), b(fb), c(fc), d(fd) {}
  operator FLOAT*() { return &a; }
  operator const FLOAT*() const { return &a; }
};

struct D3DXMATRIX : public D3DMATRIX {
  D3DXMATRIX() {}
  D3DXMATRIX(const D3DMATRIX& m) : D3DMATRIX(m) {}
  D3DXMATRIX(FLOAT f11, FLOAT f12, FLOAT f13, FLOAT f14,
             FLOAT f21, FLOAT f22, FLOAT f23, FLOAT f24,
             FLOAT f31, FLOAT f32, FLOAT f33, FLOAT f34,
             FLOAT f41, FLOAT f42, FLOAT f43, FLOAT f44) {
    _11 = f11; _12 = f12; _13 = f13; _14 = f14;
    _21 = f21; _22 = f22; _23 = f23; _24 = f24;
    _31 = f31; _32 = f32; _33 = f33; _34 = f34;
    _41 = f41; _42 = f42; _43 = f43; _44 = f44;
  }
  FLOAT& operator()(UINT row, UINT col) { return m[row][col]; }
  FLOAT operator()(UINT row, UINT col) const { return m[row][col]; }
  operator FLOAT*() { return &_11; }
  operator const FLOAT*() const { return &_11; }
  D3DXMATRIX operator*(const D3DXMATRIX& o) const;
  D3DXMATRIX& operator*=(const D3DXMATRIX& o) { return *this = *this * o; }
};

inline D3DXMATRIX* D3DXMatrixIdentity(D3DXMATRIX* pOut) {
  *pOut = D3DXMATRIX(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
  return pOut;
}

inline D3DXMATRIX* D3DXMatrixMultiply(D3DXMATRIX* pOut, const D3DXMATRIX* pM1, const D3DXMATRIX* pM2) {
  D3DXMATRIX r;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      r.m[i][j] = pM1->m[i][0] * pM2->m[0][j] + pM1->m[i][1] * pM2->m[1][j] +
                  pM1->m[i][2] * pM2->m[2][j] + pM1->m[i][3] * pM2->m[3][j];
  *pOut = r;
  return pOut;
}

inline D3DXMATRIX D3DXMATRIX::operator*(const D3DXMATRIX& o) const {
  D3DXMATRIX r;
  D3DXMatrixMultiply(&r, this, &o);
  return r;
}

inline D3DXMATRIX* D3DXMatrixTranspose(D3DXMATRIX* pOut, const D3DXMATRIX* pM) {
  D3DXMATRIX r;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) r.m[i][j] = pM->m[j][i];
  *pOut = r;
  return pOut;
}

inline D3DXMATRIX* D3DXMatrixTranslation(D3DXMATRIX* pOut, FLOAT x, FLOAT y, FLOAT z) {
  D3DXMatrixIdentity(pOut);
  pOut->_41 = x; pOut->_42 = y; pOut->_43 = z;
  return pOut;
}

inline D3DXMATRIX* D3DXMatrixScaling(D3DXMATRIX* pOut, FLOAT sx, FLOAT sy, FLOAT sz) {
  *pOut = D3DXMATRIX(sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1);
  return pOut;
}

// General 4x4 inverse (cofactor expansion). Returns NULL for a singular matrix.
inline D3DXMATRIX* D3DXMatrixInverse(D3DXMATRIX* pOut, FLOAT* pDeterminant, const D3DXMATRIX* pM) {
  const FLOAT* m = &pM->_11;
  FLOAT inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  FLOAT det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (pDeterminant) *pDeterminant = det;
  if (det == 0.0f) return NULL;
  FLOAT* o = &pOut->_11;
  for (int i = 0; i < 16; i++) o[i] = inv[i] / det;
  return pOut;
}

inline D3DXVECTOR3* D3DXVec3Normalize(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV) {
  FLOAT len = sqrtf(pV->x * pV->x + pV->y * pV->y + pV->z * pV->z);
  if (len > 0.0f) *pOut = D3DXVECTOR3(pV->x / len, pV->y / len, pV->z / len);
  else *pOut = D3DXVECTOR3(0, 0, 0);
  return pOut;
}

inline FLOAT D3DXVec3Dot(const D3DXVECTOR3* a, const D3DXVECTOR3* b) { return a->x * b->x + a->y * b->y + a->z * b->z; }

inline D3DXVECTOR3* D3DXVec3Cross(D3DXVECTOR3* pOut, const D3DXVECTOR3* a, const D3DXVECTOR3* b) {
  *pOut = D3DXVECTOR3(a->y * b->z - a->z * b->y, a->z * b->x - a->x * b->z, a->x * b->y - a->y * b->x);
  return pOut;
}

inline D3DXVECTOR3* D3DXVec3TransformNormal(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV, const D3DXMATRIX* pM) {
  D3DXVECTOR3 v = *pV;
  *pOut = D3DXVECTOR3(v.x * pM->_11 + v.y * pM->_21 + v.z * pM->_31,
                      v.x * pM->_12 + v.y * pM->_22 + v.z * pM->_32,
                      v.x * pM->_13 + v.y * pM->_23 + v.z * pM->_33);
  return pOut;
}

// Right/left-handed projection and view helpers, same formulas as the D3DX documentation.
inline D3DXMATRIX* D3DXMatrixOrthoOffCenterRH(D3DXMATRIX* pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf) {
  *pOut = D3DXMATRIX(2 / (r - l), 0, 0, 0, 0, 2 / (t - b), 0, 0, 0, 0, 1 / (zn - zf), 0,
                     (l + r) / (l - r), (t + b) / (b - t), zn / (zn - zf), 1);
  return pOut;
}

inline D3DXMATRIX* D3DXMatrixOrthoRH(D3DXMATRIX* pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf) {
  *pOut = D3DXMATRIX(2 / w, 0, 0, 0, 0, 2 / h, 0, 0, 0, 0, 1 / (zn - zf), 0, 0, 0, zn / (zn - zf), 1);
  return pOut;
}

inline D3DXMATRIX* D3DXMatrixOrthoLH(D3DXMATRIX* pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf) {
  *pOut = D3DXMATRIX(2 / w, 0, 0, 0, 0, 2 / h, 0, 0, 0, 0, 1 / (zf - zn), 0, 0, 0, zn / (zn - zf), 1);
  return pOut;
}

inline D3DXMATRIX* D3DXMatrixLookAtLH(D3DXMATRIX* pOut, const D3DXVECTOR3* pEye, const D3DXVECTOR3* pAt, const D3DXVECTOR3* pUp) {
  D3DXVECTOR3 zaxis = *pAt - *pEye, xaxis, yaxis;
  D3DXVec3Normalize(&zaxis, &zaxis);
  D3DXVec3Cross(&xaxis, pUp, &zaxis);
  D3DXVec3Normalize(&xaxis, &xaxis);
  D3DXVec3Cross(&yaxis, &zaxis, &xaxis);
  *pOut = D3DXMATRIX(xaxis.x, yaxis.x, zaxis.x, 0, xaxis.y, yaxis.y, zaxis.y, 0, xaxis.z, yaxis.z, zaxis.z, 0,
                     -D3DXVec3Dot(&xaxis, pEye), -D3DXVec3Dot(&yaxis, pEye), -D3DXVec3Dot(&zaxis, pEye), 1);
  return pOut;
}

inline D3DXPLANE* D3DXPlaneFromPointNormal(D3DXPLANE* pOut, const D3DXVECTOR3* pPoint, const D3DXVECTOR3* pNormal) {
  *pOut = D3DXPLANE(pNormal->x, pNormal->y, pNormal->z, -D3DXVec3Dot(pPoint, pNormal));
  return pOut;
}

inline D3DXPLANE* D3DXPlaneFromPoints(D3DXPLANE* pOut, const D3DXVECTOR3* p1, const D3DXVECTOR3* p2, const D3DXVECTOR3* p3) {
  D3DXVECTOR3 e1 = *p2 - *p1, e2 = *p3 - *p1, n;
  D3DXVec3Cross(&n, &e1, &e2);
  D3DXVec3Normalize(&n, &n);
  return D3DXPlaneFromPointNormal(pOut, p1, &n);
}

// Transforms a plane by a matrix; as in D3DX, pM must be the inverse transpose of the point transform.
inline D3DXPLANE* D3DXPlaneTransform(D3DXPLANE* pOut, const D3DXPLANE* pP, const D3DXMATRIX* pM) {
  D3DXPLANE p = *pP;
  *pOut = D3DXPLANE(p.a * pM->_11 + p.b * pM->_21 + p.c * pM->_31 + p.d * pM->_41,
                    p.a * pM->_12 + p.b * pM->_22 + p.c * pM->_32 + p.d * pM->_42,
                    p.a * pM->_13 + p.b * pM->_23 + p.c * pM->_33 + p.d * pM->_43,
                    p.a * pM->_14 + p.b * pM->_24 + p.c * pM->_34 + p.d * pM->_44);
  return pOut;
}

typedef D3DXMATRIX* LPD3DXMATRIX;
