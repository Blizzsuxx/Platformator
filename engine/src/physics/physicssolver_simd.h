#pragma once

#include <bit>
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

// Hot solve/relax block (832 bytes): stores projected lever arms (rn = cross(r, n), rt = cross(r, t) = dot(r, n))
// so warmstart, bias solve, relax, and restitution eliminate 2D relative-velocity and cross-product recomputation.
struct alignas(32) WideConstraint8
{
    alignas(32) float normalX[8] = {};
    alignas(32) float normalY[8] = {};

    alignas(32) float rnA0[8] = {};
    alignas(32) float rnB0[8] = {};
    alignas(32) float rtA0[8] = {};
    alignas(32) float rtB0[8] = {};
    alignas(32) float massNormal0[8] = {};
    alignas(32) float massTangent0[8] = {};
    alignas(32) float baseSep0[8] = {};
    alignas(32) float accumNormal0[8] = {};
    alignas(32) float accumTangent0[8] = {};

    alignas(32) float rnA1[8] = {};
    alignas(32) float rnB1[8] = {};
    alignas(32) float rtA1[8] = {};
    alignas(32) float rtB1[8] = {};
    alignas(32) float massNormal1[8] = {};
    alignas(32) float massTangent1[8] = {};
    alignas(32) float baseSep1[8] = {};
    alignas(32) float accumNormal1[8] = {};
    alignas(32) float accumTangent1[8] = {};

    alignas(32) float friction[8] = {};
    alignas(32) float invIA[8] = {};
    alignas(32) float invIB[8] = {};

    uint32_t bodyIndexA[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t bodyIndexB[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t twoPointMask = 0;
    uint8_t writeMaskA = 0;
    uint8_t writeMaskB = 0;
    uint8_t restitutionMask = 0;

    void resetMasks() noexcept
    {
        twoPointMask = 0;
        writeMaskA = 0;
        writeMaskB = 0;
        restitutionMask = 0;
    }
};

// Cold companion block: only touched on prepare, optional restitution pass, and final impulse writeback.
struct alignas(32) WideConstraint8Cold
{
    alignas(32) float relVel0[8] = {};
    alignas(32) float relVel1[8] = {};
    alignas(32) float restitution[8] = {};
    ClipPointsWithData *contactPoints[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    uint8_t pointCount[8] = {0, 0, 0, 0, 0, 0, 0, 0};
};

inline void clearWideLane(WideConstraint8 &out, WideConstraint8Cold &cold, size_t lane) noexcept
{
    out.bodyIndexA[lane] = 0;
    out.bodyIndexB[lane] = 0;
    out.normalX[lane] = 0.0f;
    out.normalY[lane] = 0.0f;
    out.rnA0[lane] = 0.0f;
    out.rnB0[lane] = 0.0f;
    out.rtA0[lane] = 0.0f;
    out.rtB0[lane] = 0.0f;
    out.massNormal0[lane] = 0.0f;
    out.massTangent0[lane] = 0.0f;
    out.baseSep0[lane] = 0.0f;
    out.accumNormal0[lane] = 0.0f;
    out.accumTangent0[lane] = 0.0f;
    out.rnA1[lane] = 0.0f;
    out.rnB1[lane] = 0.0f;
    out.rtA1[lane] = 0.0f;
    out.rtB1[lane] = 0.0f;
    out.massNormal1[lane] = 0.0f;
    out.massTangent1[lane] = 0.0f;
    out.baseSep1[lane] = 0.0f;
    out.accumNormal1[lane] = 0.0f;
    out.accumTangent1[lane] = 0.0f;
    out.friction[lane] = 0.0f;
    out.invIA[lane] = 0.0f;
    out.invIB[lane] = 0.0f;

    cold.contactPoints[lane] = nullptr;
    cold.pointCount[lane] = 0;
    cold.relVel0[lane] = 0.0f;
    cold.relVel1[lane] = 0.0f;
    cold.restitution[lane] = 0.0f;
}

template <typename ConstraintT>
inline void packWideLane(WideConstraint8 &out, WideConstraint8Cold &cold, size_t lane, const ConstraintT *c,
                         float invMA = 0.0f, float invIA = 0.0f, float invMB = 0.0f, float invIB = 0.0f,
                         float restitutionThreshold = 0.0f) noexcept
{
    if (c == nullptr || c->pointCount == 0)
    {
        clearWideLane(out, cold, lane);
        return;
    }

    out.bodyIndexA[lane] = c->bodyIndexA;
    out.bodyIndexB[lane] = c->bodyIndexB;
    cold.contactPoints[lane] = c->contactPoints;
    cold.pointCount[lane] = c->pointCount;

    const float nX = c->normal.x();
    const float nY = c->normal.y();
    out.normalX[lane] = nX;
    out.normalY[lane] = nY;

    const float rAx0 = c->rA[0].x();
    const float rAy0 = c->rA[0].y();
    const float rBx0 = c->rB[0].x();
    const float rBy0 = c->rB[0].y();
    out.rnA0[lane] = rAx0 * nY - rAy0 * nX;
    out.rnB0[lane] = rBx0 * nY - rBy0 * nX;
    out.rtA0[lane] = rAx0 * nX + rAy0 * nY;
    out.rtB0[lane] = rBx0 * nX + rBy0 * nY;
    out.massNormal0[lane] = c->massNormal[0];
    out.massTangent0[lane] = c->massTangent[0];
    out.baseSep0[lane] = c->baseSeparation[0];
    out.accumNormal0[lane] = c->accumulatedNormalImpulse[0];
    out.accumTangent0[lane] = c->accumulatedTangentImpulse[0];
    cold.relVel0[lane] = c->relativeVelocity[0];

    if (c->pointCount == 2)
    {
        const float rAx1 = c->rA[1].x();
        const float rAy1 = c->rA[1].y();
        const float rBx1 = c->rB[1].x();
        const float rBy1 = c->rB[1].y();
        out.rnA1[lane] = rAx1 * nY - rAy1 * nX;
        out.rnB1[lane] = rBx1 * nY - rBy1 * nX;
        out.rtA1[lane] = rAx1 * nX + rAy1 * nY;
        out.rtB1[lane] = rBx1 * nX + rBy1 * nY;
        out.massNormal1[lane] = c->massNormal[1];
        out.massTangent1[lane] = c->massTangent[1];
        out.baseSep1[lane] = c->baseSeparation[1];
        out.accumNormal1[lane] = c->accumulatedNormalImpulse[1];
        out.accumTangent1[lane] = c->accumulatedTangentImpulse[1];
        cold.relVel1[lane] = c->relativeVelocity[1];
        out.twoPointMask |= static_cast<uint8_t>(1u << lane);
    }
    else
    {
        out.rnA1[lane] = 0.0f;
        out.rnB1[lane] = 0.0f;
        out.rtA1[lane] = 0.0f;
        out.rtB1[lane] = 0.0f;
        out.massNormal1[lane] = 0.0f;
        out.massTangent1[lane] = 0.0f;
        out.baseSep1[lane] = 0.0f;
        out.accumNormal1[lane] = 0.0f;
        out.accumTangent1[lane] = 0.0f;
        cold.relVel1[lane] = 0.0f;
    }

    out.friction[lane] = c->solverFriction;
    cold.restitution[lane] = c->solverRestitution;
    if (c->solverRestitution > 0.0f &&
        (c->relativeVelocity[0] < -restitutionThreshold ||
         (c->pointCount == 2 && c->relativeVelocity[1] < -restitutionThreshold)))
    {
        out.restitutionMask |= static_cast<uint8_t>(1u << lane);
    }
    out.invIA[lane] = invIA;
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

inline void storeWideImpulses(const WideConstraint8 &wide, const WideConstraint8Cold &cold) noexcept
{
    for (size_t lane = 0; lane < 8; ++lane)
    {
        ClipPointsWithData *cp = cold.contactPoints[lane];
        const uint8_t count = cold.pointCount[lane];
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
__attribute__((target("avx2,fma"))) inline void finalizeWideEffectiveMasses8AVX2(WideConstraint8 &c,
                                                                                 const BodyStateT *bodies) noexcept
{
    const __m256 invMA = _mm256_setr_ps(
        bodies[c.bodyIndexA[0]].inverseMass, bodies[c.bodyIndexA[1]].inverseMass, bodies[c.bodyIndexA[2]].inverseMass,
        bodies[c.bodyIndexA[3]].inverseMass, bodies[c.bodyIndexA[4]].inverseMass, bodies[c.bodyIndexA[5]].inverseMass,
        bodies[c.bodyIndexA[6]].inverseMass, bodies[c.bodyIndexA[7]].inverseMass);
    const __m256 invMB = _mm256_setr_ps(
        bodies[c.bodyIndexB[0]].inverseMass, bodies[c.bodyIndexB[1]].inverseMass, bodies[c.bodyIndexB[2]].inverseMass,
        bodies[c.bodyIndexB[3]].inverseMass, bodies[c.bodyIndexB[4]].inverseMass, bodies[c.bodyIndexB[5]].inverseMass,
        bodies[c.bodyIndexB[6]].inverseMass, bodies[c.bodyIndexB[7]].inverseMass);
    const __m256 invMSum = _mm256_add_ps(invMA, invMB);
    const __m256 invIA = _mm256_load_ps(c.invIA);
    const __m256 invIB = _mm256_load_ps(c.invIB);
    const __m256 zero = _mm256_setzero_ps();
    const __m256 one = _mm256_set1_ps(1.0f);

    {
        const __m256 rnA = _mm256_load_ps(c.rnA0);
        const __m256 rnB = _mm256_load_ps(c.rnB0);
        const __m256 kN = _mm256_fmadd_ps(invIB, _mm256_mul_ps(rnB, rnB),
                                          _mm256_fmadd_ps(invIA, _mm256_mul_ps(rnA, rnA), invMSum));
        const __m256 mN = _mm256_and_ps(_mm256_cmp_ps(kN, zero, _CMP_GT_OQ), _mm256_div_ps(one, kN));
        _mm256_store_ps(c.massNormal0, mN);

        const __m256 rtA = _mm256_load_ps(c.rtA0);
        const __m256 rtB = _mm256_load_ps(c.rtB0);
        const __m256 kT = _mm256_fmadd_ps(invIB, _mm256_mul_ps(rtB, rtB),
                                          _mm256_fmadd_ps(invIA, _mm256_mul_ps(rtA, rtA), invMSum));
        const __m256 mT = _mm256_and_ps(_mm256_cmp_ps(kT, zero, _CMP_GT_OQ), _mm256_div_ps(one, kT));
        _mm256_store_ps(c.massTangent0, mT);
    }

    if (c.twoPointMask != 0)
    {
        const __m256i bits = _mm256_setr_epi32(1, 2, 4, 8, 16, 32, 64, 128);
        const __m256 active2 = _mm256_castsi256_ps(
            _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_set1_epi32(c.twoPointMask), bits), bits));

        const __m256 rnA = _mm256_load_ps(c.rnA1);
        const __m256 rnB = _mm256_load_ps(c.rnB1);
        const __m256 kN = _mm256_fmadd_ps(invIB, _mm256_mul_ps(rnB, rnB),
                                          _mm256_fmadd_ps(invIA, _mm256_mul_ps(rnA, rnA), invMSum));
        const __m256 mN =
            _mm256_and_ps(active2, _mm256_and_ps(_mm256_cmp_ps(kN, zero, _CMP_GT_OQ), _mm256_div_ps(one, kN)));
        _mm256_store_ps(c.massNormal1, mN);

        const __m256 rtA = _mm256_load_ps(c.rtA1);
        const __m256 rtB = _mm256_load_ps(c.rtB1);
        const __m256 kT = _mm256_fmadd_ps(invIB, _mm256_mul_ps(rtB, rtB),
                                          _mm256_fmadd_ps(invIA, _mm256_mul_ps(rtA, rtA), invMSum));
        const __m256 mT =
            _mm256_and_ps(active2, _mm256_and_ps(_mm256_cmp_ps(kT, zero, _CMP_GT_OQ), _mm256_div_ps(one, kT)));
        _mm256_store_ps(c.massTangent1, mT);
    }
}

template <typename BodyStateT, float BodyStateT::*FirstField>
__attribute__((target("avx2,fma"))) inline void gatherBodyVec4x8AVX2(const uint32_t idx[8], const BodyStateT *bodies,
                                                                     __m256 &c0, __m256 &c1, __m256 &c2,
                                                                     __m256 &c3) noexcept
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
    c3 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(3, 2, 3, 2));
}

template <typename BodyStateT>
__attribute__((target("avx2,fma"))) inline void gatherBodyVels8AVX2(const uint32_t idx[8], const BodyStateT *bodies,
                                                                    __m256 &vx, __m256 &vy, __m256 &w,
                                                                    __m256 &invM) noexcept
{
    gatherBodyVec4x8AVX2<BodyStateT, &BodyStateT::vx>(idx, bodies, vx, vy, w, invM);
}

template <typename BodyStateT>
__attribute__((target("avx2,fma"))) inline void gatherBodyDeltas8AVX2(const uint32_t idx[8], const BodyStateT *bodies,
                                                                      __m256 &dpX, __m256 &dpY,
                                                                      __m256 &dAngle, __m256 &invI) noexcept
{
    gatherBodyVec4x8AVX2<BodyStateT, &BodyStateT::dpX>(idx, bodies, dpX, dpY, dAngle, invI);
}

template <typename BodyStateT>
__attribute__((target("avx2,fma"))) inline void scatterBodyVels8AVX2(const uint32_t idx[8], BodyStateT *bodies,
                                                                     __m256 vx, __m256 vy, __m256 w, __m256 invM,
                                                                     uint8_t writeMask) noexcept
{
    if (writeMask == 0)
    {
        return;
    }

    const __m256 u0 = _mm256_unpacklo_ps(vx, vy);
    const __m256 u1 = _mm256_unpackhi_ps(vx, vy);
    const __m256 u2 = _mm256_unpacklo_ps(w, invM);
    const __m256 u3 = _mm256_unpackhi_ps(w, invM);

    const __m256 row04 = _mm256_shuffle_ps(u0, u2, _MM_SHUFFLE(1, 0, 1, 0));
    const __m256 row15 = _mm256_shuffle_ps(u0, u2, _MM_SHUFFLE(3, 2, 3, 2));
    const __m256 row26 = _mm256_shuffle_ps(u1, u3, _MM_SHUFFLE(1, 0, 1, 0));
    const __m256 row37 = _mm256_shuffle_ps(u1, u3, _MM_SHUFFLE(3, 2, 3, 2));

    if (writeMask == 0xFFu)
    {
        _mm_store_ps(&bodies[idx[0]].vx, _mm256_castps256_ps128(row04));
        _mm_store_ps(&bodies[idx[1]].vx, _mm256_castps256_ps128(row15));
        _mm_store_ps(&bodies[idx[2]].vx, _mm256_castps256_ps128(row26));
        _mm_store_ps(&bodies[idx[3]].vx, _mm256_castps256_ps128(row37));
        _mm_store_ps(&bodies[idx[4]].vx, _mm256_extractf128_ps(row04, 1));
        _mm_store_ps(&bodies[idx[5]].vx, _mm256_extractf128_ps(row15, 1));
        _mm_store_ps(&bodies[idx[6]].vx, _mm256_extractf128_ps(row26, 1));
        _mm_store_ps(&bodies[idx[7]].vx, _mm256_extractf128_ps(row37, 1));
        return;
    }

    if (writeMask & (1u << 0)) _mm_store_ps(&bodies[idx[0]].vx, _mm256_castps256_ps128(row04));
    if (writeMask & (1u << 1)) _mm_store_ps(&bodies[idx[1]].vx, _mm256_castps256_ps128(row15));
    if (writeMask & (1u << 2)) _mm_store_ps(&bodies[idx[2]].vx, _mm256_castps256_ps128(row26));
    if (writeMask & (1u << 3)) _mm_store_ps(&bodies[idx[3]].vx, _mm256_castps256_ps128(row37));
    if (writeMask & (1u << 4)) _mm_store_ps(&bodies[idx[4]].vx, _mm256_extractf128_ps(row04, 1));
    if (writeMask & (1u << 5)) _mm_store_ps(&bodies[idx[5]].vx, _mm256_extractf128_ps(row15, 1));
    if (writeMask & (1u << 6)) _mm_store_ps(&bodies[idx[6]].vx, _mm256_extractf128_ps(row26, 1));
    if (writeMask & (1u << 7)) _mm_store_ps(&bodies[idx[7]].vx, _mm256_extractf128_ps(row37, 1));
}

template <typename BodyStateT>
__attribute__((target("avx2,fma"))) inline void applyWarmStartWide8AVX2(const WideConstraint8 &c,
                                                                        BodyStateT *bodies) noexcept
{
    __m256 vAx, vAy, wA, invMA, vBx, vBy, wB, invMB;
    gatherBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, invMA);
    gatherBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, invMB);
    const __m256 invIA = _mm256_load_ps(c.invIA);
    const __m256 invIB = _mm256_load_ps(c.invIB);

    const __m256 nX = _mm256_load_ps(c.normalX);
    const __m256 nY = _mm256_load_ps(c.normalY);

    __m256 sumN = _mm256_load_ps(c.accumNormal0);
    __m256 sumT = _mm256_load_ps(c.accumTangent0);
    __m256 angA = _mm256_fmadd_ps(sumN, _mm256_load_ps(c.rnA0), _mm256_mul_ps(sumT, _mm256_load_ps(c.rtA0)));
    __m256 angB = _mm256_fmadd_ps(sumN, _mm256_load_ps(c.rnB0), _mm256_mul_ps(sumT, _mm256_load_ps(c.rtB0)));

    if (c.twoPointMask != 0)
    {
        const __m256 aN1 = _mm256_load_ps(c.accumNormal1);
        const __m256 aT1 = _mm256_load_ps(c.accumTangent1);
        sumN = _mm256_add_ps(sumN, aN1);
        sumT = _mm256_add_ps(sumT, aT1);
        angA = _mm256_fmadd_ps(aN1, _mm256_load_ps(c.rnA1), _mm256_fmadd_ps(aT1, _mm256_load_ps(c.rtA1), angA));
        angB = _mm256_fmadd_ps(aN1, _mm256_load_ps(c.rnB1), _mm256_fmadd_ps(aT1, _mm256_load_ps(c.rtB1), angB));
    }

    const __m256 impX = _mm256_fnmadd_ps(sumT, nY, _mm256_mul_ps(sumN, nX));
    const __m256 impY = _mm256_fmadd_ps(sumT, nX, _mm256_mul_ps(sumN, nY));

    vAx = _mm256_fnmadd_ps(impX, invMA, vAx);
    vAy = _mm256_fnmadd_ps(impY, invMA, vAy);
    wA = _mm256_fnmadd_ps(invIA, angA, wA);

    vBx = _mm256_fmadd_ps(impX, invMB, vBx);
    vBy = _mm256_fmadd_ps(impY, invMB, vBy);
    wB = _mm256_fmadd_ps(invIB, angB, wB);

    scatterBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, invMA, c.writeMaskA);
    scatterBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, invMB, c.writeMaskB);
}

template <bool UseBias>
__attribute__((target("avx2,fma"))) inline void solvePointSoftAVX2(
    __m256 &dvN, __m256 &dvT, __m256 &wA, __m256 &wB, __m256 &totalN, __m256 &totalT, __m256 invMSum, __m256 invIA,
    __m256 invIB, __m256 dpN, __m256 dAngA, __m256 dAngB, __m256 fric, __m256 vInvH, __m256 vBiasRate,
    __m256 vMassScale, __m256 vImpulseScale, __m256 vNegMaxPush, __m256 vSlop, const float *rnAPtr, const float *rnBPtr,
    const float *rtAPtr, const float *rtBPtr, const float *massNPtr, const float *massTPtr, const float *baseSepPtr,
    float *accumNPtr, float *accumTPtr) noexcept
{
    const __m256 zero = _mm256_setzero_ps();
    const __m256 rnA = _mm256_load_ps(rnAPtr);
    const __m256 rnB = _mm256_load_ps(rnBPtr);
    __m256 aN = _mm256_load_ps(accumNPtr);

    const __m256 vn = _mm256_fmadd_ps(wB, rnB, _mm256_fnmadd_ps(wA, rnA, dvN));

    __m256 deltaN;
    if constexpr (UseBias)
    {
        const __m256 sep = _mm256_fmadd_ps(dAngB, rnB, _mm256_fnmadd_ps(dAngA, rnA, _mm256_add_ps(_mm256_load_ps(baseSepPtr), dpN)));
        const __m256 posBias = _mm256_mul_ps(sep, vInvH);
        const __m256 penDepth = _mm256_min_ps(zero, _mm256_add_ps(sep, vSlop));
        const __m256 negBias = _mm256_max_ps(_mm256_mul_ps(vBiasRate, penDepth), vNegMaxPush);
        const __m256 bias = _mm256_blendv_ps(negBias, posBias, _mm256_cmp_ps(sep, zero, _CMP_GT_OQ));

        const __m256 rhs = _mm256_fmadd_ps(vMassScale, _mm256_add_ps(vn, bias), _mm256_mul_ps(vImpulseScale, aN));
        deltaN = _mm256_mul_ps(_mm256_load_ps(massNPtr), _mm256_sub_ps(zero, rhs));
    }
    else
    {
        (void)dpN;
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
    totalN = _mm256_add_ps(totalN, appliedN);
    dvN = _mm256_fmadd_ps(invMSum, appliedN, dvN);
    wA = _mm256_fnmadd_ps(_mm256_mul_ps(invIA, rnA), appliedN, wA);
    wB = _mm256_fmadd_ps(_mm256_mul_ps(invIB, rnB), appliedN, wB);

    const __m256 rtA = _mm256_load_ps(rtAPtr);
    const __m256 rtB = _mm256_load_ps(rtBPtr);
    __m256 aT = _mm256_load_ps(accumTPtr);
    const __m256 vt = _mm256_fmadd_ps(wB, rtB, _mm256_fnmadd_ps(wA, rtA, dvT));
    const __m256 rawT = _mm256_mul_ps(_mm256_load_ps(massTPtr), _mm256_sub_ps(zero, vt));
    const __m256 maxT = _mm256_mul_ps(fric, aN);
    const __m256 negMaxT = _mm256_sub_ps(zero, maxT);
    const __m256 newT = _mm256_max_ps(negMaxT, _mm256_min_ps(_mm256_add_ps(aT, rawT), maxT));
    const __m256 appliedT = _mm256_sub_ps(newT, aT);
    aT = newT;
    totalT = _mm256_add_ps(totalT, appliedT);
    dvT = _mm256_fmadd_ps(invMSum, appliedT, dvT);
    wA = _mm256_fnmadd_ps(_mm256_mul_ps(invIA, rtA), appliedT, wA);
    wB = _mm256_fmadd_ps(_mm256_mul_ps(invIB, rtB), appliedT, wB);

    _mm256_store_ps(accumNPtr, aN);
    _mm256_store_ps(accumTPtr, aT);
}

template <bool UseBias, typename BodyStateT, typename ParamsT>
__attribute__((target("avx2,fma"))) inline void solveWide8SoftAVX2(WideConstraint8 &c, BodyStateT *bodies,
                                                                   const ParamsT &params) noexcept
{
    __m256 vAx, vAy, wA, invMA, vBx, vBy, wB, invMB;
    gatherBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, invMA);
    gatherBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, invMB);

    const __m256 zero = _mm256_setzero_ps();
    const __m256 nX = _mm256_load_ps(c.normalX);
    const __m256 nY = _mm256_load_ps(c.normalY);

    __m256 dpN = zero;
    __m256 dAngA = zero;
    __m256 dAngB = zero;
    __m256 invIA, invIB;
    if constexpr (UseBias)
    {
        __m256 dpAx, dpAy, dpBx, dpBy;
        gatherBodyDeltas8AVX2(c.bodyIndexA, bodies, dpAx, dpAy, dAngA, invIA);
        gatherBodyDeltas8AVX2(c.bodyIndexB, bodies, dpBx, dpBy, dAngB, invIB);
        const __m256 dpX = _mm256_sub_ps(dpBx, dpAx);
        const __m256 dpY = _mm256_sub_ps(dpBy, dpAy);
        dpN = _mm256_fmadd_ps(dpX, nX, _mm256_mul_ps(dpY, nY));
    }
    else
    {
        invIA = _mm256_load_ps(c.invIA);
        invIB = _mm256_load_ps(c.invIB);
    }

    const __m256 dvX = _mm256_sub_ps(vBx, vAx);
    const __m256 dvY = _mm256_sub_ps(vBy, vAy);
    __m256 dvN = _mm256_fmadd_ps(dvX, nX, _mm256_mul_ps(dvY, nY));
    __m256 dvT = _mm256_fmsub_ps(dvY, nX, _mm256_mul_ps(dvX, nY));
    const __m256 invMSum = _mm256_add_ps(invMA, invMB);
    const __m256 fric = _mm256_load_ps(c.friction);

    const __m256 vInvH = UseBias ? _mm256_set1_ps(params.invH) : zero;
    const __m256 vBiasRate = UseBias ? _mm256_set1_ps(params.biasRate) : zero;
    const __m256 vMassScale = UseBias ? _mm256_set1_ps(params.massScale) : zero;
    const __m256 vImpulseScale = UseBias ? _mm256_set1_ps(params.impulseScale) : zero;
    const __m256 vNegMaxPush = UseBias ? _mm256_set1_ps(params.negMaxPushSpeed) : zero;
    const __m256 vSlop = UseBias ? _mm256_set1_ps(params.slop) : zero;

    __m256 totalN = zero;
    __m256 totalT = zero;
    solvePointSoftAVX2<UseBias>(dvN, dvT, wA, wB, totalN, totalT, invMSum, invIA, invIB, dpN, dAngA, dAngB, fric, vInvH,
                                vBiasRate, vMassScale, vImpulseScale, vNegMaxPush, vSlop, c.rnA0, c.rnB0, c.rtA0, c.rtB0,
                                c.massNormal0, c.massTangent0, c.baseSep0, c.accumNormal0, c.accumTangent0);

    if (c.twoPointMask != 0)
    {
        solvePointSoftAVX2<UseBias>(dvN, dvT, wA, wB, totalN, totalT, invMSum, invIA, invIB, dpN, dAngA, dAngB, fric,
                                    vInvH, vBiasRate, vMassScale, vImpulseScale, vNegMaxPush, vSlop, c.rnA1, c.rnB1,
                                    c.rtA1, c.rtB1, c.massNormal1, c.massTangent1, c.baseSep1, c.accumNormal1,
                                    c.accumTangent1);
    }

    const __m256 impX = _mm256_fnmadd_ps(totalT, nY, _mm256_mul_ps(totalN, nX));
    const __m256 impY = _mm256_fmadd_ps(totalT, nX, _mm256_mul_ps(totalN, nY));
    vAx = _mm256_fnmadd_ps(impX, invMA, vAx);
    vAy = _mm256_fnmadd_ps(impY, invMA, vAy);
    vBx = _mm256_fmadd_ps(impX, invMB, vBx);
    vBy = _mm256_fmadd_ps(impY, invMB, vBy);

    scatterBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, invMA, c.writeMaskA);
    scatterBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, invMB, c.writeMaskB);
}

__attribute__((target("avx2,fma"))) inline bool bouncePointSoftAVX2(
    __m256 &dvN, __m256 &wA, __m256 &wB, __m256 &totalN, __m256 invMSum, __m256 invIA, __m256 invIB, __m256 rest,
    __m256 negThreshold, const float *rnAPtr, const float *rnBPtr, const float *massNPtr,
    const float *relVelPtr) noexcept
{
    const __m256 zero = _mm256_setzero_ps();
    const __m256 relVel = _mm256_load_ps(relVelPtr);
    const __m256 active =
        _mm256_and_ps(_mm256_cmp_ps(rest, zero, _CMP_GT_OQ), _mm256_cmp_ps(relVel, negThreshold, _CMP_LT_OQ));
    if (_mm256_movemask_ps(active) == 0)
    {
        return false;
    }
    const __m256 rnA = _mm256_load_ps(rnAPtr);
    const __m256 rnB = _mm256_load_ps(rnBPtr);
    const __m256 vn = _mm256_fmadd_ps(wB, rnB, _mm256_fnmadd_ps(wA, rnA, dvN));

    const __m256 targetVn = _mm256_sub_ps(zero, _mm256_mul_ps(rest, relVel));
    const __m256 rawImpulse = _mm256_mul_ps(_mm256_load_ps(massNPtr), _mm256_sub_ps(targetVn, vn));
    const __m256 appliedN = _mm256_and_ps(active, _mm256_max_ps(rawImpulse, zero));

    totalN = _mm256_add_ps(totalN, appliedN);
    dvN = _mm256_fmadd_ps(invMSum, appliedN, dvN);
    wA = _mm256_fnmadd_ps(_mm256_mul_ps(invIA, rnA), appliedN, wA);
    wB = _mm256_fmadd_ps(_mm256_mul_ps(invIB, rnB), appliedN, wB);
    return true;
}

template <typename BodyStateT, typename ParamsT>
__attribute__((target("avx2,fma"))) inline void applyRestitutionWide8AVX2(const WideConstraint8 &c,
                                                                          const WideConstraint8Cold &cold,
                                                                          BodyStateT *bodies,
                                                                          const ParamsT &params) noexcept
{
    if (c.restitutionMask == 0)
    {
        return;
    }

    __m256 vAx, vAy, wA, invMA, vBx, vBy, wB, invMB;
    gatherBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, invMA);
    gatherBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, invMB);
    const __m256 invIA = _mm256_load_ps(c.invIA);
    const __m256 invIB = _mm256_load_ps(c.invIB);

    const __m256 nX = _mm256_load_ps(c.normalX);
    const __m256 nY = _mm256_load_ps(c.normalY);
    const __m256 rest = _mm256_load_ps(cold.restitution);
    const __m256 negThreshold = _mm256_set1_ps(-params.restitutionThreshold);

    const __m256 dvX = _mm256_sub_ps(vBx, vAx);
    const __m256 dvY = _mm256_sub_ps(vBy, vAy);
    __m256 dvN = _mm256_fmadd_ps(dvX, nX, _mm256_mul_ps(dvY, nY));
    const __m256 invMSum = _mm256_add_ps(invMA, invMB);
    __m256 totalN = _mm256_setzero_ps();

    bool bounced = bouncePointSoftAVX2(dvN, wA, wB, totalN, invMSum, invIA, invIB, rest, negThreshold, c.rnA0, c.rnB0,
                                       c.massNormal0, cold.relVel0);
    if (c.twoPointMask != 0)
    {
        bounced |= bouncePointSoftAVX2(dvN, wA, wB, totalN, invMSum, invIA, invIB, rest, negThreshold, c.rnA1, c.rnB1,
                                       c.massNormal1, cold.relVel1);
    }

    if (bounced)
    {
        const __m256 impX = _mm256_mul_ps(totalN, nX);
        const __m256 impY = _mm256_mul_ps(totalN, nY);
        vAx = _mm256_fnmadd_ps(impX, invMA, vAx);
        vAy = _mm256_fnmadd_ps(impY, invMA, vAy);
        vBx = _mm256_fmadd_ps(impX, invMB, vBx);
        vBy = _mm256_fmadd_ps(impY, invMB, vBy);
        scatterBodyVels8AVX2(c.bodyIndexA, bodies, vAx, vAy, wA, invMA, c.writeMaskA);
        scatterBodyVels8AVX2(c.bodyIndexB, bodies, vBx, vBy, wB, invMB, c.writeMaskB);
    }
}

#endif // PLATFORMATOR_HAS_RUNTIME_AVX2
} // namespace platformator_solver_simd
