#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "platformator/collisiontypes.h"

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
        return __builtin_cpu_supports("avx2") != 0 && __builtin_cpu_supports("fma") != 0;
    }();
    return supported;
#else
    return false;
#endif
}

struct alignas(32) WideConstraint8
{
    // Hot solve/relax prefix (~736 bytes).
    alignas(32) float normalX[8] = {};
    alignas(32) float normalY[8] = {};

    alignas(32) float rAx0[8] = {};
    alignas(32) float rAy0[8] = {};
    alignas(32) float rBx0[8] = {};
    alignas(32) float rBy0[8] = {};
    alignas(32) float massNormal0[8] = {};
    alignas(32) float massTangent0[8] = {};
    alignas(32) float baseSep0[8] = {};
    alignas(32) float accumNormal0[8] = {};
    alignas(32) float accumTangent0[8] = {};

    alignas(32) float rAx1[8] = {};
    alignas(32) float rAy1[8] = {};
    alignas(32) float rBx1[8] = {};
    alignas(32) float rBy1[8] = {};
    alignas(32) float massNormal1[8] = {};
    alignas(32) float massTangent1[8] = {};
    alignas(32) float baseSep1[8] = {};
    alignas(32) float accumNormal1[8] = {};
    alignas(32) float accumTangent1[8] = {};

    alignas(32) float friction[8] = {};
    alignas(32) float invMA[8] = {};
    alignas(32) float invIA[8] = {};
    alignas(32) float invMB[8] = {};
    alignas(32) float invIB[8] = {};

    uint32_t bodyIndexA[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t bodyIndexB[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t twoPointMask = 0;
    uint8_t writeMaskA = 0;
    uint8_t writeMaskB = 0;
    uint8_t restitutionMask = 0;

    // Cold tail: only touched on restitution (if restitutionMask != 0) or final impulse store.
    alignas(32) float relVel0[8] = {};
    alignas(32) float relVel1[8] = {};
    alignas(32) float restitution[8] = {};
    ClipPointsWithData *contactPoints[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    uint8_t pointCount[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    void resetMasks() noexcept
    {
        twoPointMask = 0;
        writeMaskA = 0;
        writeMaskB = 0;
        restitutionMask = 0;
    }
};

template <typename ConstraintT>
inline void packWideLane(WideConstraint8 &out, size_t lane, const ConstraintT *c, float invMA = 0.0f,
                         float invIA = 0.0f, float invMB = 0.0f, float invIB = 0.0f) noexcept
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
        out.massNormal0[lane] = 0.0f;
        out.massTangent0[lane] = 0.0f;
        out.baseSep0[lane] = 0.0f;
        out.accumNormal0[lane] = 0.0f;
        out.accumTangent0[lane] = 0.0f;
        out.rAx1[lane] = 0.0f;
        out.rAy1[lane] = 0.0f;
        out.rBx1[lane] = 0.0f;
        out.rBy1[lane] = 0.0f;
        out.massNormal1[lane] = 0.0f;
        out.massTangent1[lane] = 0.0f;
        out.baseSep1[lane] = 0.0f;
        out.accumNormal1[lane] = 0.0f;
        out.accumTangent1[lane] = 0.0f;
        out.friction[lane] = 0.0f;
        out.invMA[lane] = 0.0f;
        out.invIA[lane] = 0.0f;
        out.invMB[lane] = 0.0f;
        out.invIB[lane] = 0.0f;
        out.relVel0[lane] = 0.0f;
        out.relVel1[lane] = 0.0f;
        out.restitution[lane] = 0.0f;
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
    out.baseSep0[lane] = c->baseSeparation[0];
    out.accumNormal0[lane] = c->accumulatedNormalImpulse[0];
    out.accumTangent0[lane] = c->accumulatedTangentImpulse[0];
    out.relVel0[lane] = c->relativeVelocity[0];

    if (c->pointCount == 2)
    {
        out.rAx1[lane] = c->rA[1].x();
        out.rAy1[lane] = c->rA[1].y();
        out.rBx1[lane] = c->rB[1].x();
        out.rBy1[lane] = c->rB[1].y();
        out.massNormal1[lane] = c->massNormal[1];
        out.massTangent1[lane] = c->massTangent[1];
        out.baseSep1[lane] = c->baseSeparation[1];
        out.accumNormal1[lane] = c->accumulatedNormalImpulse[1];
        out.accumTangent1[lane] = c->accumulatedTangentImpulse[1];
        out.relVel1[lane] = c->relativeVelocity[1];
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
        out.baseSep1[lane] = 0.0f;
        out.accumNormal1[lane] = 0.0f;
        out.accumTangent1[lane] = 0.0f;
        out.relVel1[lane] = 0.0f;
    }

    out.friction[lane] = c->solverFriction;
    out.restitution[lane] = c->solverRestitution;
    if (c->solverRestitution > 0.0f)
    {
        out.restitutionMask |= static_cast<uint8_t>(1u << lane);
    }
    out.invMA[lane] = invMA;
    out.invIA[lane] = invIA;
    out.invMB[lane] = invMB;
    out.invIB[lane] = invIB;
    if (invMA != 0.0f || invIA != 0.0f)
    {
        out.writeMaskA |= static_cast<uint8_t>(1u << lane);
    }
    if (invMB != 0.0f || invIB != 0.0f)
    {
        out.writeMaskB |= static_cast<uint8_t>(1u << lane);
    }
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

template <typename BodyStateT, float BodyStateT::*FirstField>
__attribute__((target("avx2,fma"))) inline void gatherBodyVec3x8AVX2(const uint32_t idx[8], const BodyStateT *bodies,
                                                                     __m256 &c0, __m256 &c1, __m256 &c2) noexcept
{
    const __m256 r0 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&(bodies[idx[0]].*FirstField))),
                                           _mm_load_ps(&(bodies[idx[4]].*FirstField)), 1);
    const __m256 r1 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&(bodies[idx[1]].*FirstField))),
                                           _mm_load_ps(&(bodies[idx[5]].*FirstField)), 1);
    const __m256 r2 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&(bodies[idx[2]].*FirstField))),
                                           _mm_load_ps(&(bodies[idx[6]].*FirstField)), 1);
    const __m256 r3 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&(bodies[idx[3]].*FirstField))),
                                           _mm_load_ps(&(bodies[idx[7]].*FirstField)), 1);

    const __m256 t0 = _mm256_unpacklo_ps(r0, r1);
    const __m256 t1 = _mm256_unpackhi_ps(r0, r1);
    const __m256 t2 = _mm256_unpacklo_ps(r2, r3);
    const __m256 t3 = _mm256_unpackhi_ps(r2, r3);

    c0 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(1, 0, 1, 0));
    c1 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 2, 3, 2));
    c2 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(1, 0, 1, 0));
}

template <typename BodyStateT>
__attribute__((target("avx2,fma"))) inline void gatherBodyVels8AVX2(const uint32_t idx[8], const BodyStateT *bodies,
                                                                    __m256 &vx, __m256 &vy, __m256 &w) noexcept
{
    gatherBodyVec3x8AVX2<BodyStateT, &BodyStateT::vx>(idx, bodies, vx, vy, w);
}

template <typename BodyStateT>
__attribute__((target("avx2,fma"))) inline void gatherBodyDeltas8AVX2(const uint32_t idx[8], const BodyStateT *bodies,
                                                                      __m256 &dpX, __m256 &dpY,
                                                                      __m256 &dAngle) noexcept
{
    gatherBodyVec3x8AVX2<BodyStateT, &BodyStateT::dpX>(idx, bodies, dpX, dpY, dAngle);
}

template <typename BodyStateT>
__attribute__((target("avx2,fma"))) inline void scatterBodyVels8AVX2(const uint32_t idx[8], BodyStateT *bodies,
                                                                     __m256 vx, __m256 vy, __m256 w,
                                                                     uint8_t writeMask) noexcept
{
    uint32_t mask = writeMask;
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

    if (mask == 0xFFu)
    {
        for (unsigned lane = 0; lane < 8; ++lane)
        {
            BodyStateT &b = bodies[idx[lane]];
            b.vx = svx[lane];
            b.vy = svy[lane];
            b.w = sw[lane];
        }
        return;
    }

    while (mask != 0)
    {
        const unsigned lane = static_cast<unsigned>(__builtin_ctz(mask));
        mask &= mask - 1u;
        BodyStateT &b = bodies[idx[lane]];
        b.vx = svx[lane];
        b.vy = svy[lane];
        b.w = sw[lane];
    }
}

__attribute__((target("avx2,fma"))) inline void applyImpulsePairAVX2(__m256 &vAx, __m256 &vAy, __m256 &wA,
                                                                     __m256 invMA, __m256 invIA, __m256 &vBx,
                                                                     __m256 &vBy, __m256 &wB, __m256 invMB,
                                                                     __m256 invIB, __m256 impX, __m256 impY,
                                                                     __m256 rAx, __m256 rAy, __m256 rBx,
                                                                     __m256 rBy) noexcept
{
    vAx = _mm256_fnmadd_ps(impX, invMA, vAx);
    vAy = _mm256_fnmadd_ps(impY, invMA, vAy);
    const __m256 crossA = _mm256_fmsub_ps(rAy, impX, _mm256_mul_ps(rAx, impY));
    wA = _mm256_fmadd_ps(invIA, crossA, wA);

    vBx = _mm256_fmadd_ps(impX, invMB, vBx);
    vBy = _mm256_fmadd_ps(impY, invMB, vBy);
    const __m256 crossB = _mm256_fmsub_ps(rBx, impY, _mm256_mul_ps(rBy, impX));
    wB = _mm256_fmadd_ps(invIB, crossB, wB);
}

__attribute__((target("avx2,fma"))) inline void computeRelVelAVX2(__m256 vAx, __m256 vAy, __m256 wA, __m256 vBx,
                                                                  __m256 vBy, __m256 wB, __m256 rAx, __m256 rAy,
                                                                  __m256 rBx, __m256 rBy, __m256 &relX,
                                                                  __m256 &relY) noexcept
{
    const __m256 pBx = _mm256_fnmadd_ps(wB, rBy, vBx);
    const __m256 pBy = _mm256_fmadd_ps(wB, rBx, vBy);
    const __m256 pAx = _mm256_fnmadd_ps(wA, rAy, vAx);
    const __m256 pAy = _mm256_fmadd_ps(wA, rAx, vAy);
    relX = _mm256_sub_ps(pBx, pAx);
    relY = _mm256_sub_ps(pBy, pAy);
}

template <typename BodyStateT>
__attribute__((target("avx2,fma"))) inline void applyWarmStartWide8AVX2(const WideConstraint8 &c,
                                                                        BodyStateT *bodies) noexcept
{
    __m256 vAx, vAy, wA, vBx, vBy, wB;
    gatherBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA);
    gatherBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB);
    const __m256 invMA = _mm256_load_ps(c.invMA);
    const __m256 invIA = _mm256_load_ps(c.invIA);
    const __m256 invMB = _mm256_load_ps(c.invMB);
    const __m256 invIB = _mm256_load_ps(c.invIB);

    const __m256 nX = _mm256_load_ps(c.normalX);
    const __m256 nY = _mm256_load_ps(c.normalY);
    const __m256 tX = _mm256_sub_ps(_mm256_setzero_ps(), nY);
    const __m256 tY = nX;

    {
        const __m256 aN0 = _mm256_load_ps(c.accumNormal0);
        const __m256 aT0 = _mm256_load_ps(c.accumTangent0);
        const __m256 impX = _mm256_fmadd_ps(aN0, nX, _mm256_mul_ps(aT0, tX));
        const __m256 impY = _mm256_fmadd_ps(aN0, nY, _mm256_mul_ps(aT0, tY));
        applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, impX, impY,
                             _mm256_load_ps(c.rAx0), _mm256_load_ps(c.rAy0), _mm256_load_ps(c.rBx0),
                             _mm256_load_ps(c.rBy0));
    }

    if (c.twoPointMask != 0)
    {
        const __m256 aN1 = _mm256_load_ps(c.accumNormal1);
        const __m256 aT1 = _mm256_load_ps(c.accumTangent1);
        const __m256 impX = _mm256_fmadd_ps(aN1, nX, _mm256_mul_ps(aT1, tX));
        const __m256 impY = _mm256_fmadd_ps(aN1, nY, _mm256_mul_ps(aT1, tY));
        applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, impX, impY,
                             _mm256_load_ps(c.rAx1), _mm256_load_ps(c.rAy1), _mm256_load_ps(c.rBx1),
                             _mm256_load_ps(c.rBy1));
    }

    scatterBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, c.writeMaskA);
    scatterBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, c.writeMaskB);
}

template <bool UseBias>
__attribute__((target("avx2,fma"))) inline void solvePointSoftAVX2(
    __m256 &vAx, __m256 &vAy, __m256 &wA, __m256 invMA, __m256 invIA, __m256 &vBx, __m256 &vBy, __m256 &wB,
    __m256 invMB, __m256 invIB, __m256 dpX, __m256 dpY, __m256 dAngA, __m256 dAngB, __m256 nX, __m256 nY, __m256 tX,
    __m256 tY, __m256 fric, __m256 vInvH, __m256 vBiasRate, __m256 vMassScale, __m256 vImpulseScale,
    __m256 vNegMaxPush, __m256 vSlop, const float *rAxPtr, const float *rAyPtr, const float *rBxPtr,
    const float *rByPtr, const float *massNPtr, const float *massTPtr, const float *baseSepPtr, float *accumNPtr,
    float *accumTPtr) noexcept
{
    const __m256 zero = _mm256_setzero_ps();
    const __m256 rAx = _mm256_load_ps(rAxPtr);
    const __m256 rAy = _mm256_load_ps(rAyPtr);
    const __m256 rBx = _mm256_load_ps(rBxPtr);
    const __m256 rBy = _mm256_load_ps(rByPtr);
    __m256 aN = _mm256_load_ps(accumNPtr);
    __m256 aT = _mm256_load_ps(accumTPtr);

    __m256 relX, relY;
    computeRelVelAVX2(vAx, vAy, wA, vBx, vBy, wB, rAx, rAy, rBx, rBy, relX, relY);
    const __m256 vn = _mm256_fmadd_ps(relX, nX, _mm256_mul_ps(relY, nY));

    __m256 deltaN;
    if constexpr (UseBias)
    {
        const __m256 dsX = _mm256_fmadd_ps(dAngA, rAy, _mm256_fnmadd_ps(dAngB, rBy, dpX));
        const __m256 dsY = _mm256_fnmadd_ps(dAngA, rAx, _mm256_fmadd_ps(dAngB, rBx, dpY));
        const __m256 sep = _mm256_fmadd_ps(dsX, nX, _mm256_fmadd_ps(dsY, nY, _mm256_load_ps(baseSepPtr)));

        const __m256 posBias = _mm256_mul_ps(sep, vInvH);
        const __m256 penDepth = _mm256_min_ps(zero, _mm256_add_ps(sep, vSlop));
        const __m256 negBias = _mm256_max_ps(_mm256_mul_ps(vBiasRate, penDepth), vNegMaxPush);
        const __m256 bias = _mm256_blendv_ps(negBias, posBias, _mm256_cmp_ps(sep, zero, _CMP_GT_OQ));

        const __m256 rhs = _mm256_fmadd_ps(vMassScale, _mm256_add_ps(vn, bias), _mm256_mul_ps(vImpulseScale, aN));
        deltaN = _mm256_mul_ps(_mm256_load_ps(massNPtr), _mm256_sub_ps(zero, rhs));
    }
    else
    {
        (void)dpX;
        (void)dpY;
        (void)dAngA;
        (void)dAngB;
        (void)vInvH;
        (void)vBiasRate;
        (void)vMassScale;
        (void)vImpulseScale;
        (void)vNegMaxPush;
        (void)vSlop;
        (void)baseSepPtr;
        deltaN = _mm256_mul_ps(_mm256_load_ps(massNPtr), _mm256_sub_ps(zero, vn));
    }

    const __m256 newN = _mm256_max_ps(_mm256_add_ps(aN, deltaN), zero);
    const __m256 appliedN = _mm256_sub_ps(newN, aN);
    aN = newN;

    applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, _mm256_mul_ps(appliedN, nX),
                         _mm256_mul_ps(appliedN, nY), rAx, rAy, rBx, rBy);

    computeRelVelAVX2(vAx, vAy, wA, vBx, vBy, wB, rAx, rAy, rBx, rBy, relX, relY);
    const __m256 vt = _mm256_fmadd_ps(relX, tX, _mm256_mul_ps(relY, tY));
    const __m256 rawT = _mm256_mul_ps(_mm256_load_ps(massTPtr), _mm256_sub_ps(zero, vt));
    const __m256 maxT = _mm256_mul_ps(fric, aN);
    const __m256 negMaxT = _mm256_sub_ps(zero, maxT);
    const __m256 newT = _mm256_max_ps(negMaxT, _mm256_min_ps(_mm256_add_ps(aT, rawT), maxT));
    const __m256 appliedT = _mm256_sub_ps(newT, aT);
    aT = newT;

    applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, _mm256_mul_ps(appliedT, tX),
                         _mm256_mul_ps(appliedT, tY), rAx, rAy, rBx, rBy);

    _mm256_store_ps(accumNPtr, aN);
    _mm256_store_ps(accumTPtr, aT);
}

template <bool UseBias, typename BodyStateT, typename ParamsT>
__attribute__((target("avx2,fma"))) inline void solveWide8SoftAVX2(WideConstraint8 &c, BodyStateT *bodies,
                                                                   const ParamsT &params) noexcept
{
    __m256 vAx, vAy, wA, vBx, vBy, wB;
    gatherBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA);
    gatherBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB);

    __m256 dpX = _mm256_setzero_ps();
    __m256 dpY = _mm256_setzero_ps();
    __m256 dAngA = _mm256_setzero_ps();
    __m256 dAngB = _mm256_setzero_ps();
    if constexpr (UseBias)
    {
        __m256 dpAx, dpAy, dpBx, dpBy;
        gatherBodyDeltas8AVX2(c.bodyIndexA, bodies, dpAx, dpAy, dAngA);
        gatherBodyDeltas8AVX2(c.bodyIndexB, bodies, dpBx, dpBy, dAngB);
        dpX = _mm256_sub_ps(dpBx, dpAx);
        dpY = _mm256_sub_ps(dpBy, dpAy);
    }

    const __m256 invMA = _mm256_load_ps(c.invMA);
    const __m256 invIA = _mm256_load_ps(c.invIA);
    const __m256 invMB = _mm256_load_ps(c.invMB);
    const __m256 invIB = _mm256_load_ps(c.invIB);

    const __m256 zero = _mm256_setzero_ps();
    const __m256 nX = _mm256_load_ps(c.normalX);
    const __m256 nY = _mm256_load_ps(c.normalY);
    const __m256 tX = _mm256_sub_ps(zero, nY);
    const __m256 tY = nX;
    const __m256 fric = _mm256_load_ps(c.friction);

    const __m256 vInvH = UseBias ? _mm256_set1_ps(params.invH) : zero;
    const __m256 vBiasRate = UseBias ? _mm256_set1_ps(params.biasRate) : zero;
    const __m256 vMassScale = UseBias ? _mm256_set1_ps(params.massScale) : zero;
    const __m256 vImpulseScale = UseBias ? _mm256_set1_ps(params.impulseScale) : zero;
    const __m256 vNegMaxPush = UseBias ? _mm256_set1_ps(params.negMaxPushSpeed) : zero;
    const __m256 vSlop = UseBias ? _mm256_set1_ps(params.slop) : zero;

    solvePointSoftAVX2<UseBias>(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, dpX, dpY, dAngA, dAngB, nX, nY,
                                tX, tY, fric, vInvH, vBiasRate, vMassScale, vImpulseScale, vNegMaxPush, vSlop, c.rAx0,
                                c.rAy0, c.rBx0, c.rBy0, c.massNormal0, c.massTangent0, c.baseSep0, c.accumNormal0,
                                c.accumTangent0);

    if (c.twoPointMask != 0)
    {
        solvePointSoftAVX2<UseBias>(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, dpX, dpY, dAngA, dAngB, nX,
                                    nY, tX, tY, fric, vInvH, vBiasRate, vMassScale, vImpulseScale, vNegMaxPush, vSlop,
                                    c.rAx1, c.rAy1, c.rBx1, c.rBy1, c.massNormal1, c.massTangent1, c.baseSep1,
                                    c.accumNormal1, c.accumTangent1);
    }

    scatterBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, c.writeMaskA);
    scatterBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, c.writeMaskB);
}

__attribute__((target("avx2,fma"))) inline void bouncePointSoftAVX2(
    __m256 &vAx, __m256 &vAy, __m256 &wA, __m256 invMA, __m256 invIA, __m256 &vBx, __m256 &vBy, __m256 &wB,
    __m256 invMB, __m256 invIB, __m256 nX, __m256 nY, __m256 rest, __m256 negThreshold, const float *rAxPtr,
    const float *rAyPtr, const float *rBxPtr, const float *rByPtr, const float *massNPtr,
    const float *relVelPtr) noexcept
{
    const __m256 zero = _mm256_setzero_ps();
    const __m256 relVel = _mm256_load_ps(relVelPtr);
    const __m256 active =
        _mm256_and_ps(_mm256_cmp_ps(rest, zero, _CMP_GT_OQ), _mm256_cmp_ps(relVel, negThreshold, _CMP_LT_OQ));
    if (_mm256_movemask_ps(active) == 0)
    {
        return;
    }
    const __m256 rAx = _mm256_load_ps(rAxPtr);
    const __m256 rAy = _mm256_load_ps(rAyPtr);
    const __m256 rBx = _mm256_load_ps(rBxPtr);
    const __m256 rBy = _mm256_load_ps(rByPtr);

    __m256 relX, relY;
    computeRelVelAVX2(vAx, vAy, wA, vBx, vBy, wB, rAx, rAy, rBx, rBy, relX, relY);
    const __m256 vn = _mm256_fmadd_ps(relX, nX, _mm256_mul_ps(relY, nY));

    const __m256 targetVn = _mm256_sub_ps(zero, _mm256_mul_ps(rest, relVel));
    const __m256 rawImpulse = _mm256_mul_ps(_mm256_load_ps(massNPtr), _mm256_sub_ps(targetVn, vn));
    const __m256 appliedN = _mm256_and_ps(active, _mm256_max_ps(rawImpulse, zero));

    applyImpulsePairAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, _mm256_mul_ps(appliedN, nX),
                         _mm256_mul_ps(appliedN, nY), rAx, rAy, rBx, rBy);
}

template <typename BodyStateT, typename ParamsT>
__attribute__((target("avx2,fma"))) inline void applyRestitutionWide8AVX2(WideConstraint8 &c, BodyStateT *bodies,
                                                                          const ParamsT &params) noexcept
{
    if (c.restitutionMask == 0)
    {
        return;
    }

    __m256 vAx, vAy, wA, vBx, vBy, wB;
    gatherBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA);
    gatherBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB);
    const __m256 invMA = _mm256_load_ps(c.invMA);
    const __m256 invIA = _mm256_load_ps(c.invIA);
    const __m256 invMB = _mm256_load_ps(c.invMB);
    const __m256 invIB = _mm256_load_ps(c.invIB);

    const __m256 nX = _mm256_load_ps(c.normalX);
    const __m256 nY = _mm256_load_ps(c.normalY);
    const __m256 rest = _mm256_load_ps(c.restitution);
    const __m256 negThreshold = _mm256_set1_ps(-params.restitutionThreshold);

    bouncePointSoftAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, nX, nY, rest, negThreshold, c.rAx0,
                        c.rAy0, c.rBx0, c.rBy0, c.massNormal0, c.relVel0);
    if (c.twoPointMask != 0)
    {
        bouncePointSoftAVX2(vAx, vAy, wA, invMA, invIA, vBx, vBy, wB, invMB, invIB, nX, nY, rest, negThreshold, c.rAx1,
                            c.rAy1, c.rBx1, c.rBy1, c.massNormal1, c.relVel1);
    }

    scatterBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, c.writeMaskA);
    scatterBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, c.writeMaskB);
}

#endif // PLATFORMATOR_HAS_RUNTIME_AVX2
} // namespace platformator_solver_simd
