#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "physics/physicsmanager.h"

#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
#define PLATFORMATOR_HAS_RUNTIME_AVX2 1
#include <immintrin.h>
#else
#define PLATFORMATOR_HAS_RUNTIME_AVX2 0
#endif

namespace platformator_solver_simd
{
inline bool cpuSupportsAVX2() noexcept
{
#if PLATFORMATOR_HAS_RUNTIME_AVX2
    static const bool supported = []() noexcept {
        if (const char *disable = std::getenv("PLATFORMATOR_DISABLE_SIMD_SOLVER"); disable != nullptr && disable[0] == '1')
        {
            return false;
        }
        __builtin_cpu_init();
        return __builtin_cpu_supports("avx2") != 0;
    }();
    return supported;
#else
    return false;
#endif
}

struct alignas(32) WideConstraint8
{
    alignas(32) float normalX[8] = {};
    alignas(32) float normalY[8] = {};

    alignas(32) float rAx0[8] = {};
    alignas(32) float rAy0[8] = {};
    alignas(32) float rBx0[8] = {};
    alignas(32) float rBy0[8] = {};
    alignas(32) float rAx1[8] = {};
    alignas(32) float rAy1[8] = {};
    alignas(32) float rBx1[8] = {};
    alignas(32) float rBy1[8] = {};

    alignas(32) float massNormal0[8] = {};
    alignas(32) float massNormal1[8] = {};
    alignas(32) float massTangent0[8] = {};
    alignas(32) float massTangent1[8] = {};
    alignas(32) float bias0[8] = {};
    alignas(32) float bias1[8] = {};
    alignas(32) float accumNormal0[8] = {};
    alignas(32) float accumNormal1[8] = {};
    alignas(32) float accumTangent0[8] = {};
    alignas(32) float accumTangent1[8] = {};

    alignas(32) float k00[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    alignas(32) float k01[8] = {};
    alignas(32) float k11[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    alignas(32) float negM00[8] = {};
    alignas(32) float negM01[8] = {};
    alignas(32) float negM11[8] = {};

    alignas(32) float friction[8] = {};
    alignas(32) float blockMaskF[8] = {};     // all-1s bitmask if blockSolve, else 0.0f
    alignas(32) float seqPt0MaskF[8] = {};    // all-1s bitmask if !blockSolve && pointCount >= 1
    alignas(32) float seqPt1MaskF[8] = {};    // all-1s bitmask if !blockSolve && pointCount == 2

    uint32_t bodyIndexA[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t bodyIndexB[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    ClipPointsWithData *contactPoints[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    uint8_t pointCount[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t blockMask = 0;
    uint8_t twoPointMask = 0;
};

inline void setLaneMaskFloat(float &dst, bool active) noexcept
{
    const uint32_t bits = active ? 0xFFFFFFFFu : 0u;
    std::memcpy(&dst, &bits, sizeof(float));
}

template <typename ConstraintT>
inline void packWideLane(WideConstraint8 &out, size_t lane, const ConstraintT *c) noexcept
{
    if (c == nullptr || c->pointCount == 0)
    {
        out.bodyIndexA[lane] = 0;
        out.bodyIndexB[lane] = 0;
        out.contactPoints[lane] = nullptr;
        out.pointCount[lane] = 0;
        out.normalX[lane] = 0.0f;
        out.normalY[lane] = 0.0f;
        out.rAx0[lane] = 0.0f;
        out.rAy0[lane] = 0.0f;
        out.rBx0[lane] = 0.0f;
        out.rBy0[lane] = 0.0f;
        out.rAx1[lane] = 0.0f;
        out.rAy1[lane] = 0.0f;
        out.rBx1[lane] = 0.0f;
        out.rBy1[lane] = 0.0f;
        out.massNormal0[lane] = 0.0f;
        out.massNormal1[lane] = 0.0f;
        out.massTangent0[lane] = 0.0f;
        out.massTangent1[lane] = 0.0f;
        out.bias0[lane] = 0.0f;
        out.bias1[lane] = 0.0f;
        out.accumNormal0[lane] = 0.0f;
        out.accumNormal1[lane] = 0.0f;
        out.accumTangent0[lane] = 0.0f;
        out.accumTangent1[lane] = 0.0f;
        out.k00[lane] = 1.0f;
        out.k01[lane] = 0.0f;
        out.k11[lane] = 1.0f;
        out.negM00[lane] = 0.0f;
        out.negM01[lane] = 0.0f;
        out.negM11[lane] = 0.0f;
        out.friction[lane] = 0.0f;
        setLaneMaskFloat(out.blockMaskF[lane], false);
        setLaneMaskFloat(out.seqPt0MaskF[lane], false);
        setLaneMaskFloat(out.seqPt1MaskF[lane], false);
        return;
    }

    out.bodyIndexA[lane] = c->bodyIndexA;
    out.bodyIndexB[lane] = c->bodyIndexB;
    out.contactPoints[lane] = c->contactPoints;
    out.pointCount[lane] = c->pointCount;

    out.normalX[lane] = c->normal.x();
    out.normalY[lane] = c->normal.y();

    out.rAx0[lane] = c->rA[0].x();
    out.rAy0[lane] = c->rA[0].y();
    out.rBx0[lane] = c->rB[0].x();
    out.rBy0[lane] = c->rB[0].y();
    out.massNormal0[lane] = c->massNormal[0];
    out.massTangent0[lane] = c->massTangent[0];
    out.bias0[lane] = c->bias[0];
    out.accumNormal0[lane] = c->accumulatedNormalImpulse[0];
    out.accumTangent0[lane] = c->accumulatedTangentImpulse[0];

    if (c->pointCount == 2)
    {
        out.rAx1[lane] = c->rA[1].x();
        out.rAy1[lane] = c->rA[1].y();
        out.rBx1[lane] = c->rB[1].x();
        out.rBy1[lane] = c->rB[1].y();
        out.massNormal1[lane] = c->massNormal[1];
        out.massTangent1[lane] = c->massTangent[1];
        out.bias1[lane] = c->bias[1];
        out.accumNormal1[lane] = c->accumulatedNormalImpulse[1];
        out.accumTangent1[lane] = c->accumulatedTangentImpulse[1];
        out.twoPointMask |= static_cast<uint8_t>(1u << lane);
    }
    else
    {
        out.rAx1[lane] = 0.0f;
        out.rAy1[lane] = 0.0f;
        out.rBx1[lane] = 0.0f;
        out.rBy1[lane] = 0.0f;
        out.massNormal1[lane] = 0.0f;
        out.massTangent1[lane] = 0.0f;
        out.bias1[lane] = 0.0f;
        out.accumNormal1[lane] = 0.0f;
        out.accumTangent1[lane] = 0.0f;
    }

    if (c->blockSolve)
    {
        out.k00[lane] = c->normalK(0, 0);
        out.k01[lane] = c->normalK(0, 1);
        out.k11[lane] = c->normalK(1, 1);
        out.negM00[lane] = -c->normalMass(0, 0);
        out.negM01[lane] = -c->normalMass(0, 1);
        out.negM11[lane] = -c->normalMass(1, 1);
        out.blockMask |= static_cast<uint8_t>(1u << lane);
    }
    else
    {
        out.k00[lane] = 1.0f;
        out.k01[lane] = 0.0f;
        out.k11[lane] = 1.0f;
        out.negM00[lane] = 0.0f;
        out.negM01[lane] = 0.0f;
        out.negM11[lane] = 0.0f;
    }

    out.friction[lane] = c->solverFriction;
    setLaneMaskFloat(out.blockMaskF[lane], c->blockSolve);
    setLaneMaskFloat(out.seqPt0MaskF[lane], !c->blockSolve);
    setLaneMaskFloat(out.seqPt1MaskF[lane], !c->blockSolve && c->pointCount == 2);
}

inline void storeWideImpulses(const WideConstraint8 &wide) noexcept
{
    for (size_t lane = 0; lane < 8; ++lane)
    {
        ClipPointsWithData *cp = wide.contactPoints[lane];
        const uint8_t count = wide.pointCount[lane];
        if (cp == nullptr || count == 0)
        {
            continue;
        }
        cp->points[0].accumulatedNormalImpulse = wide.accumNormal0[lane];
        cp->points[0].accumulatedTangentImpulse = wide.accumTangent0[lane];
        if (count == 2)
        {
            cp->points[1].accumulatedNormalImpulse = wide.accumNormal1[lane];
            cp->points[1].accumulatedTangentImpulse = wide.accumTangent1[lane];
        }
    }
}

#if PLATFORMATOR_HAS_RUNTIME_AVX2

template <typename BodyStateT>
__attribute__((target("avx2"))) inline void gatherBodies8AVX2(const uint32_t idx[8], const BodyStateT *bodies,
                                                              __m256 &vx, __m256 &vy, __m256 &w, __m256 &invM,
                                                              __m256 &invI) noexcept
{
    const BodyStateT &b0 = bodies[idx[0]];
    const BodyStateT &b1 = bodies[idx[1]];
    const BodyStateT &b2 = bodies[idx[2]];
    const BodyStateT &b3 = bodies[idx[3]];
    const BodyStateT &b4 = bodies[idx[4]];
    const BodyStateT &b5 = bodies[idx[5]];
    const BodyStateT &b6 = bodies[idx[6]];
    const BodyStateT &b7 = bodies[idx[7]];

    vx = _mm256_setr_ps(b0.velocity.x(), b1.velocity.x(), b2.velocity.x(), b3.velocity.x(), b4.velocity.x(),
                        b5.velocity.x(), b6.velocity.x(), b7.velocity.x());
    vy = _mm256_setr_ps(b0.velocity.y(), b1.velocity.y(), b2.velocity.y(), b3.velocity.y(), b4.velocity.y(),
                        b5.velocity.y(), b6.velocity.y(), b7.velocity.y());
    w = _mm256_setr_ps(b0.angularVelocity, b1.angularVelocity, b2.angularVelocity, b3.angularVelocity,
                       b4.angularVelocity, b5.angularVelocity, b6.angularVelocity, b7.angularVelocity);
    invM = _mm256_setr_ps(b0.inverseMass, b1.inverseMass, b2.inverseMass, b3.inverseMass, b4.inverseMass,
                          b5.inverseMass, b6.inverseMass, b7.inverseMass);
    invI = _mm256_setr_ps(b0.inverseMomentOfInertia, b1.inverseMomentOfInertia, b2.inverseMomentOfInertia,
                          b3.inverseMomentOfInertia, b4.inverseMomentOfInertia, b5.inverseMomentOfInertia,
                          b6.inverseMomentOfInertia, b7.inverseMomentOfInertia);
}

template <typename BodyStateT>
__attribute__((target("avx2"))) inline void scatterBodies8AVX2(const uint32_t idx[8], BodyStateT *bodies, __m256 vx,
                                                               __m256 vy, __m256 w, __m256 invM, __m256 invI) noexcept
{
    const __m256 zero = _mm256_setzero_ps();
    const __m256 writable =
        _mm256_or_ps(_mm256_cmp_ps(invM, zero, _CMP_NEQ_OQ), _mm256_cmp_ps(invI, zero, _CMP_NEQ_OQ));
    uint32_t mask = static_cast<uint32_t>(_mm256_movemask_ps(writable));
    if (mask == 0)
    {
        return;
    }

    alignas(32) float svx[8];
    alignas(32) float svy[8];
    alignas(32) float sw[8];
    _mm256_store_ps(svx, vx);
    _mm256_store_ps(svy, vy);
    _mm256_store_ps(sw, w);

    while (mask != 0)
    {
        const unsigned lane = static_cast<unsigned>(__builtin_ctz(mask));
        mask &= mask - 1u;
        BodyStateT &b = bodies[idx[lane]];
        b.velocity.x() = svx[lane];
        b.velocity.y() = svy[lane];
        b.angularVelocity = sw[lane];
    }
}

__attribute__((target("avx2"))) inline void applyImpulsePairAVX2(__m256 &vAx, __m256 &vAy, __m256 &wA, __m256 invMA,
                                                                 __m256 invIA, __m256 &vBx, __m256 &vBy, __m256 &wB,
                                                                 __m256 invMB, __m256 invIB, __m256 impX, __m256 impY,
                                                                 __m256 rAx, __m256 rAy, __m256 rBx,
                                                                 __m256 rBy) noexcept
{
    const __m256 negImpX = _mm256_sub_ps(_mm256_setzero_ps(), impX);
    const __m256 negImpY = _mm256_sub_ps(_mm256_setzero_ps(), impY);

    vAx = _mm256_add_ps(vAx, _mm256_mul_ps(negImpX, invMA));
    vAy = _mm256_add_ps(vAy, _mm256_mul_ps(negImpY, invMA));
    const __m256 crossA = _mm256_sub_ps(_mm256_mul_ps(rAx, negImpY), _mm256_mul_ps(rAy, negImpX));
    wA = _mm256_add_ps(wA, _mm256_mul_ps(invIA, crossA));

    vBx = _mm256_add_ps(vBx, _mm256_mul_ps(impX, invMB));
    vBy = _mm256_add_ps(vBy, _mm256_mul_ps(impY, invMB));
    const __m256 crossB = _mm256_sub_ps(_mm256_mul_ps(rBx, impY), _mm256_mul_ps(rBy, impX));
    wB = _mm256_add_ps(wB, _mm256_mul_ps(invIB, crossB));
}

__attribute__((target("avx2"))) inline void computeRelVelAVX2(__m256 vAx, __m256 vAy, __m256 wA, __m256 vBx, __m256 vBy,
                                                              __m256 wB, __m256 rAx, __m256 rAy, __m256 rBx, __m256 rBy,
                                                              __m256 &relX, __m256 &relY) noexcept
{
    const __m256 zero = _mm256_setzero_ps();
    const __m256 negWA = _mm256_sub_ps(zero, wA);
    const __m256 negWB = _mm256_sub_ps(zero, wB);
    const __m256 pBx = _mm256_add_ps(vBx, _mm256_mul_ps(negWB, rBy));
    const __m256 pBy = _mm256_add_ps(vBy, _mm256_mul_ps(wB, rBx));
    const __m256 pAx = _mm256_add_ps(vAx, _mm256_mul_ps(negWA, rAy));
    const __m256 pAy = _mm256_add_ps(vAy, _mm256_mul_ps(wA, rAx));
    relX = _mm256_sub_ps(pBx, pAx);
    relY = _mm256_sub_ps(pBy, pAy);
}

template <typename BodyStateT>
__attribute__((target("avx2"))) inline void applyWarmStartWide8AVX2(const WideConstraint8 &c,
                                                                    BodyStateT *bodies) noexcept
{
    __m256 vAx, vAy, wA, invMA, invIA;
    __m256 vBx, vBy, wB, invMB, invIB;
    gatherBodies8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, invMA, invIA);
    gatherBodies8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, invMB, invIB);

    const __m256 nX = _mm256_load_ps(c.normalX);
    const __m256 nY = _mm256_load_ps(c.normalY);
    const __m256 tX = _mm256_sub_ps(_mm256_setzero_ps(), nY);
    const __m256 tY = nX;

    {
        const __m256 aN0 = _mm256_load_ps(c.accumNormal0);
        const __m256 aT0 = _mm256_load_ps(c.accumTangent0);
        const __m256 impX = _mm256_add_ps(_mm256_mul_ps(aN0, nX), _mm256_mul_ps(aT0, tX));
        const __m256 impY = _mm256_add_ps(_mm256_mul_ps(aN0, nY), _mm256_mul_ps(aT0, tY));
        applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, impX, impY,
                             _mm256_load_ps(c.rAx0), _mm256_load_ps(c.rAy0), _mm256_load_ps(c.rBx0),
                             _mm256_load_ps(c.rBy0));
    }

    if (c.twoPointMask != 0)
    {
        const __m256 aN1 = _mm256_load_ps(c.accumNormal1);
        const __m256 aT1 = _mm256_load_ps(c.accumTangent1);
        const __m256 impX = _mm256_add_ps(_mm256_mul_ps(aN1, nX), _mm256_mul_ps(aT1, tX));
        const __m256 impY = _mm256_add_ps(_mm256_mul_ps(aN1, nY), _mm256_mul_ps(aT1, tY));
        applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, impX, impY,
                             _mm256_load_ps(c.rAx1), _mm256_load_ps(c.rAy1), _mm256_load_ps(c.rBx1),
                             _mm256_load_ps(c.rBy1));
    }

    scatterBodies8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, invMA, invIA);
    scatterBodies8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, invMB, invIB);
}

template <typename BodyStateT>
__attribute__((target("avx2"))) inline void resolveWide8AVX2(WideConstraint8 &c, BodyStateT *bodies) noexcept
{
    __m256 vAx, vAy, wA, invMA, invIA;
    __m256 vBx, vBy, wB, invMB, invIB;
    gatherBodies8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, invMA, invIA);
    gatherBodies8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, invMB, invIB);

    const __m256 zero = _mm256_setzero_ps();
    const __m256 nX = _mm256_load_ps(c.normalX);
    const __m256 nY = _mm256_load_ps(c.normalY);
    const __m256 tX = _mm256_sub_ps(_mm256_setzero_ps(), nY);
    const __m256 tY = nX;
    const __m256 rAx0 = _mm256_load_ps(c.rAx0);
    const __m256 rAy0 = _mm256_load_ps(c.rAy0);
    const __m256 rBx0 = _mm256_load_ps(c.rBx0);
    const __m256 rBy0 = _mm256_load_ps(c.rBy0);
    const __m256 fric = _mm256_load_ps(c.friction);

    __m256 aN0 = _mm256_load_ps(c.accumNormal0);
    __m256 aN1 = _mm256_load_ps(c.accumNormal1);
    __m256 aT0 = _mm256_load_ps(c.accumTangent0);
    __m256 aT1 = _mm256_load_ps(c.accumTangent1);

    // 1. Block-solve 2x2 normal LCP for any lanes with blockSolve == true.
    if (c.blockMask != 0)
    {
        const __m256 rAx1 = _mm256_load_ps(c.rAx1);
        const __m256 rAy1 = _mm256_load_ps(c.rAy1);
        const __m256 rBx1 = _mm256_load_ps(c.rBx1);
        const __m256 rBy1 = _mm256_load_ps(c.rBy1);

        __m256 relX0, relY0, relX1, relY1;
        computeRelVelAVX2(vAx, vAy, wA, vBx, vBy, wB, rAx0, rAy0, rBx0, rBy0, relX0, relY0);
        computeRelVelAVX2(vAx, vAy, wA, vBx, vBy, wB, rAx1, rAy1, rBx1, rBy1, relX1, relY1);

        const __m256 vn0 = _mm256_add_ps(_mm256_mul_ps(relX0, nX), _mm256_mul_ps(relY0, nY));
        const __m256 vn1 = _mm256_add_ps(_mm256_mul_ps(relX1, nX), _mm256_mul_ps(relY1, nY));

        const __m256 k00 = _mm256_load_ps(c.k00);
        const __m256 k01 = _mm256_load_ps(c.k01);
        const __m256 k11 = _mm256_load_ps(c.k11);

        __m256 rhs0 = _mm256_sub_ps(vn0, _mm256_load_ps(c.bias0));
        __m256 rhs1 = _mm256_sub_ps(vn1, _mm256_load_ps(c.bias1));
        rhs0 = _mm256_sub_ps(rhs0, _mm256_add_ps(_mm256_mul_ps(k00, aN0), _mm256_mul_ps(k01, aN1)));
        rhs1 = _mm256_sub_ps(rhs1, _mm256_add_ps(_mm256_mul_ps(k01, aN0), _mm256_mul_ps(k11, aN1)));

        // Branch 0: unconstrained solution next = -normalMass * rhs
        const __m256 negM00 = _mm256_load_ps(c.negM00);
        const __m256 negM01 = _mm256_load_ps(c.negM01);
        const __m256 negM11 = _mm256_load_ps(c.negM11);
        __m256 next0 = _mm256_add_ps(_mm256_mul_ps(negM00, rhs0), _mm256_mul_ps(negM01, rhs1));
        __m256 next1 = _mm256_add_ps(_mm256_mul_ps(negM01, rhs0), _mm256_mul_ps(negM11, rhs1));
        __m256 acc =
            _mm256_and_ps(_mm256_cmp_ps(next0, zero, _CMP_GE_OQ), _mm256_cmp_ps(next1, zero, _CMP_GE_OQ));

        // Branch 1: active point 0 only (next1 = 0)
        const __m256 cand1_0 = _mm256_div_ps(_mm256_sub_ps(zero, rhs0), k00);
        const __m256 ok1 = _mm256_and_ps(
            _mm256_cmp_ps(cand1_0, zero, _CMP_GE_OQ),
            _mm256_cmp_ps(_mm256_add_ps(_mm256_mul_ps(k01, cand1_0), rhs1), zero, _CMP_GE_OQ));
        const __m256 take1 = _mm256_andnot_ps(acc, ok1);
        next0 = _mm256_blendv_ps(next0, cand1_0, take1);
        next1 = _mm256_blendv_ps(next1, zero, take1);
        acc = _mm256_or_ps(acc, take1);

        // Branch 2: active point 1 only (next0 = 0)
        const __m256 cand2_1 = _mm256_div_ps(_mm256_sub_ps(zero, rhs1), k11);
        const __m256 ok2 = _mm256_and_ps(
            _mm256_cmp_ps(cand2_1, zero, _CMP_GE_OQ),
            _mm256_cmp_ps(_mm256_add_ps(_mm256_mul_ps(k01, cand2_1), rhs0), zero, _CMP_GE_OQ));
        const __m256 take2 = _mm256_andnot_ps(acc, ok2);
        next0 = _mm256_blendv_ps(next0, zero, take2);
        next1 = _mm256_blendv_ps(next1, cand2_1, take2);
        acc = _mm256_or_ps(acc, take2);

        // Branch 3: both points zero
        const __m256 ok3 =
            _mm256_and_ps(_mm256_cmp_ps(rhs0, zero, _CMP_GE_OQ), _mm256_cmp_ps(rhs1, zero, _CMP_GE_OQ));
        const __m256 take3 = _mm256_andnot_ps(acc, ok3);
        next0 = _mm256_blendv_ps(next0, zero, take3);
        next1 = _mm256_blendv_ps(next1, zero, take3);
        acc = _mm256_or_ps(acc, take3);

        const __m256 applyMask =
            (c.blockMask == 0xFF) ? acc : _mm256_and_ps(acc, _mm256_load_ps(c.blockMaskF));

        const __m256 d0 = _mm256_and_ps(applyMask, _mm256_sub_ps(next0, aN0));
        const __m256 d1 = _mm256_and_ps(applyMask, _mm256_sub_ps(next1, aN1));
        aN0 = _mm256_blendv_ps(aN0, next0, applyMask);
        aN1 = _mm256_blendv_ps(aN1, next1, applyMask);

        applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, _mm256_mul_ps(d0, nX),
                             _mm256_mul_ps(d0, nY), rAx0, rAy0, rBx0, rBy0);
        applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, _mm256_mul_ps(d1, nX),
                             _mm256_mul_ps(d1, nY), rAx1, rAy1, rBx1, rBy1);
    }

    // 2. Point 0 sequential normal (only for !blockSolve lanes) + Point 0 tangent (all 8 lanes).
    if (c.blockMask != 0xFF)
    {
        __m256 relX0, relY0;
        computeRelVelAVX2(vAx, vAy, wA, vBx, vBy, wB, rAx0, rAy0, rBx0, rBy0, relX0, relY0);
        const __m256 vn0 = _mm256_add_ps(_mm256_mul_ps(relX0, nX), _mm256_mul_ps(relY0, nY));
        const __m256 deltaN = _mm256_mul_ps(_mm256_load_ps(c.massNormal0),
                                            _mm256_add_ps(_mm256_sub_ps(zero, vn0), _mm256_load_ps(c.bias0)));
        const __m256 newN = _mm256_max_ps(_mm256_add_ps(aN0, deltaN), zero);
        const __m256 seqMask = _mm256_load_ps(c.seqPt0MaskF);
        const __m256 appliedN = _mm256_and_ps(seqMask, _mm256_sub_ps(newN, aN0));
        aN0 = _mm256_blendv_ps(aN0, newN, seqMask);

        applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, _mm256_mul_ps(appliedN, nX),
                             _mm256_mul_ps(appliedN, nY), rAx0, rAy0, rBx0, rBy0);
    }

    {
        __m256 relX0, relY0;
        computeRelVelAVX2(vAx, vAy, wA, vBx, vBy, wB, rAx0, rAy0, rBx0, rBy0, relX0, relY0);
        const __m256 vt0 = _mm256_add_ps(_mm256_mul_ps(relX0, tX), _mm256_mul_ps(relY0, tY));
        const __m256 rawT = _mm256_mul_ps(_mm256_load_ps(c.massTangent0), _mm256_sub_ps(zero, vt0));
        const __m256 maxT = _mm256_mul_ps(fric, aN0);
        const __m256 negMaxT = _mm256_sub_ps(zero, maxT);
        const __m256 newT = _mm256_max_ps(negMaxT, _mm256_min_ps(_mm256_add_ps(aT0, rawT), maxT));
        const __m256 appliedT = _mm256_sub_ps(newT, aT0);
        aT0 = newT;

        applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, _mm256_mul_ps(appliedT, tX),
                             _mm256_mul_ps(appliedT, tY), rAx0, rAy0, rBx0, rBy0);
    }

    // 3. Point 1 (only if any lane has 2 points).
    if (c.twoPointMask != 0)
    {
        const __m256 rAx1 = _mm256_load_ps(c.rAx1);
        const __m256 rAy1 = _mm256_load_ps(c.rAy1);
        const __m256 rBx1 = _mm256_load_ps(c.rBx1);
        const __m256 rBy1 = _mm256_load_ps(c.rBy1);

        if ((c.twoPointMask & ~c.blockMask) != 0)
        {
            __m256 relX1, relY1;
            computeRelVelAVX2(vAx, vAy, wA, vBx, vBy, wB, rAx1, rAy1, rBx1, rBy1, relX1, relY1);
            const __m256 vn1 = _mm256_add_ps(_mm256_mul_ps(relX1, nX), _mm256_mul_ps(relY1, nY));
            const __m256 deltaN = _mm256_mul_ps(_mm256_load_ps(c.massNormal1),
                                                _mm256_add_ps(_mm256_sub_ps(zero, vn1), _mm256_load_ps(c.bias1)));
            const __m256 newN = _mm256_max_ps(_mm256_add_ps(aN1, deltaN), zero);
            const __m256 seqMask = _mm256_load_ps(c.seqPt1MaskF);
            const __m256 appliedN = _mm256_and_ps(seqMask, _mm256_sub_ps(newN, aN1));
            aN1 = _mm256_blendv_ps(aN1, newN, seqMask);

            applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, _mm256_mul_ps(appliedN, nX),
                                 _mm256_mul_ps(appliedN, nY), rAx1, rAy1, rBx1, rBy1);
        }

        // Point 1 tangent: 1-point lanes have massTangent1 == 0 and aT1 == 0, so appliedT == 0 automatically.
        __m256 relX1, relY1;
        computeRelVelAVX2(vAx, vAy, wA, vBx, vBy, wB, rAx1, rAy1, rBx1, rBy1, relX1, relY1);
        const __m256 vt1 = _mm256_add_ps(_mm256_mul_ps(relX1, tX), _mm256_mul_ps(relY1, tY));
        const __m256 rawT = _mm256_mul_ps(_mm256_load_ps(c.massTangent1), _mm256_sub_ps(zero, vt1));
        const __m256 maxT = _mm256_mul_ps(fric, aN1);
        const __m256 negMaxT = _mm256_sub_ps(zero, maxT);
        const __m256 newT = _mm256_max_ps(negMaxT, _mm256_min_ps(_mm256_add_ps(aT1, rawT), maxT));
        const __m256 appliedT = _mm256_sub_ps(newT, aT1);
        aT1 = newT;

        applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, _mm256_mul_ps(appliedT, tX),
                             _mm256_mul_ps(appliedT, tY), rAx1, rAy1, rBx1, rBy1);
    }

    _mm256_store_ps(c.accumNormal0, aN0);
    _mm256_store_ps(c.accumNormal1, aN1);
    _mm256_store_ps(c.accumTangent0, aT0);
    _mm256_store_ps(c.accumTangent1, aT1);

    scatterBodies8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, invMA, invIA);
    scatterBodies8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, invMB, invIB);
}

#endif // PLATFORMATOR_HAS_RUNTIME_AVX2
} // namespace platformator_solver_simd
