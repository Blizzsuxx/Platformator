using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using Unity.Jobs.LowLevel.Unsafe;
using UnityEngine;

public sealed class BenchmarkRunner : MonoBehaviour
{
    // 10 Platformator units = 1.0 meter (matches Box2D meter-scale tuning).
    private const float UnitScale = 0.1f;
    private const float GravityY = 98.1f * UnitScale; // 9.81 m/s^2

    private static readonly float[] BroadStartX = {
        -1400f, 1470f, -1540f, 1610f, -1680f, 1750f, -1820f, 1890f,
        -1960f, 2030f, -2100f, 2170f, -2240f, 2310f, -2380f, 2450f,
    };
    private static readonly float[] BroadVelX = {
        420f, -500f, 580f, -660f, 740f, -820f, 900f, -980f,
        1060f, -1140f, 1220f, -1300f, 1380f, -1460f, 1540f, -1620f,
    };
    private static readonly float[] RbcVx = { -48f, 36f, -28f, 44f, -40f, 24f, -32f, 52f };
    private static readonly float[] RbcVy = { 0f, -18f, -12f, 8f, -6f, 14f };
    private static readonly float[] RbcOmega = { -1.4f, 0.9f, -0.7f, 1.1f, -1.2f, 0.8f, -0.6f, 1.3f };

    private string _scenario = "rigid_body_container";
    private int _count = 500;
    private int _warmupFrames = 120;
    private int _measureFrames = 600;
    private int _physicsFps = 120;
    private int _velocityIterations = 10;
    private int _positionIterations = 3;
    private bool _enableSleep = true;
    private bool _multithreaded = false;
    private int _workers = 1;

    private readonly List<Rigidbody2D> _trackedDynamic = new List<Rigidbody2D>();
    private int _dynamicCount;
    private int _staticCount;
    private bool _started;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    private static void Bootstrap()
    {
        if (FindAnyObjectByType<BenchmarkRunner>() != null)
            return;
        var go = new GameObject("PlatformatorUnityBenchmarkRunner");
        go.AddComponent<BenchmarkRunner>();
    }

    private void Awake()
    {
        ParseArgs();
        Application.targetFrameRate = -1;
        QualitySettings.vSyncCount = 0;

        Physics2D.simulationMode = SimulationMode2D.Script;
        Physics2D.autoSyncTransforms = false;
        Physics2D.reuseCollisionCallbacks = true;
        Physics2D.velocityIterations = _velocityIterations;
        Physics2D.positionIterations = _positionIterations;
        Physics2D.gravity = new Vector2(0f, _scenario == "broad_phase" ? 0f : GravityY);

        var jobs = Physics2D.jobOptions;
        jobs.useMultithreading = _multithreaded;
        jobs.useConsistencySorting = true;
        Physics2D.jobOptions = jobs;

        if (_multithreaded && _workers > 0)
        {
            // Unity's JobWorkerCount counts background worker threads (main thread also participates).
            int maxBg = JobsUtility.JobWorkerMaximumCount;
            JobsUtility.JobWorkerCount = Math.Clamp(_workers - 1, 1, Math.Max(1, maxBg));
        }
    }

    private void Update()
    {
        if (_started)
            return;
        _started = true;
        RunBenchmark();
    }

    private void ParseArgs()
    {
        string[] args = Environment.GetCommandLineArgs();
        for (int i = 1; i < args.Length; ++i)
        {
            switch (args[i])
            {
                case "--scenario":
                    if (i + 1 < args.Length) _scenario = args[++i];
                    break;
                case "--count":
                    if (i + 1 < args.Length) _count = int.Parse(args[++i], CultureInfo.InvariantCulture);
                    break;
                case "--warmup-frames":
                    if (i + 1 < args.Length) _warmupFrames = int.Parse(args[++i], CultureInfo.InvariantCulture);
                    break;
                case "--measure-frames":
                    if (i + 1 < args.Length) _measureFrames = int.Parse(args[++i], CultureInfo.InvariantCulture);
                    break;
                case "--physics-fps":
                    if (i + 1 < args.Length) _physicsFps = int.Parse(args[++i], CultureInfo.InvariantCulture);
                    break;
                case "--velocity-iterations":
                    if (i + 1 < args.Length) _velocityIterations = int.Parse(args[++i], CultureInfo.InvariantCulture);
                    break;
                case "--position-iterations":
                    if (i + 1 < args.Length) _positionIterations = int.Parse(args[++i], CultureInfo.InvariantCulture);
                    break;
                case "--can-sleep":
                    if (i + 1 < args.Length)
                    {
                        string v = args[++i];
                        _enableSleep = v == "true" || v == "1";
                    }
                    break;
                case "--workers":
                    if (i + 1 < args.Length)
                    {
                        _workers = Math.Max(1, int.Parse(args[++i], CultureInfo.InvariantCulture));
                        _multithreaded = _workers > 1;
                    }
                    break;
            }
        }
    }

    private void RunBenchmark()
    {
        switch (_scenario)
        {
            case "broad_phase":
                BuildBroadPhase();
                break;
            case "rigid_body_container":
                BuildRigidBodyContainer();
                break;
            case "ideal_world":
                BuildIdealWorld();
                break;
            default:
                Console.Error.WriteLine("Unknown scenario: " + _scenario);
                Application.Quit(1);
                return;
        }

        // Push initial GameObject transforms into Physics2D once before stepping.
        Physics2D.SyncTransforms();

        float dt = 1f / _physicsFps;
        double[] samplesMs = new double[_measureFrames];
        double awakeSum = 0.0;
        var sw = new Stopwatch();
        int totalSteps = _warmupFrames + _measureFrames;

        for (int step = 0; step < totalSteps; ++step)
        {
            sw.Restart();
            Physics2D.Simulate(dt);
            sw.Stop();

            if (step >= _warmupFrames)
            {
                samplesMs[step - _warmupFrames] = sw.Elapsed.TotalMilliseconds;
                int awake = 0;
                for (int b = 0; b < _trackedDynamic.Count; ++b)
                {
                    if (_trackedDynamic[b].IsAwake())
                        awake++;
                }
                awakeSum += awake;
            }
        }

        double sum = 0.0;
        for (int i = 0; i < samplesMs.Length; ++i)
            sum += samplesMs[i];
        double meanMs = sum / samplesMs.Length;

        Array.Sort(samplesMs);
        double medianMs = samplesMs[samplesMs.Length / 2];
        int p95Idx = Math.Clamp((int)Math.Ceiling(0.95 * samplesMs.Length) - 1, 0, samplesMs.Length - 1);
        double p95Ms = samplesMs[p95Idx];
        double avgAwake = awakeSum / _measureFrames;

        string json = string.Format(
            CultureInfo.InvariantCulture,
            "{{\"engine\":\"unity_{0}\",\"scenario\":\"{1}\",\"count\":{2},\"workers\":{3},\"multithreaded\":{4}," +
            "\"job_worker_count\":{5},\"warmup_frames\":{6},\"measure_frames\":{7},\"physics_fps\":{8}," +
            "\"unit_scale\":{9:0.00},\"dynamic_bodies\":{10},\"static_bodies\":{11}," +
            "\"mean_ms\":{12:0.0000},\"median_ms\":{13:0.0000},\"p95_ms\":{14:0.0000},\"avg_awake_bodies\":{15:0.0}}}",
            Application.unityVersion,
            _scenario,
            _count,
            _workers,
            Physics2D.jobOptions.useMultithreading ? "true" : "false",
            JobsUtility.JobWorkerCount,
            _warmupFrames,
            _measureFrames,
            _physicsFps,
            UnitScale,
            _dynamicCount,
            _staticCount,
            meanMs,
            medianMs,
            p95Ms,
            avgAwake
        );
        Console.WriteLine(json);
        Application.Quit(0);
    }

    private void AddStaticWall(float x, float y, float w, float h, PhysicsMaterial2D mat)
    {
        var go = new GameObject();
        go.transform.position = new Vector3(x * UnitScale, y * UnitScale, 0f);
        var rb = go.AddComponent<Rigidbody2D>();
        rb.bodyType = RigidbodyType2D.Static;
        var col = go.AddComponent<BoxCollider2D>();
        col.size = new Vector2(w * UnitScale, h * UnitScale);
        col.sharedMaterial = mat;
        _staticCount++;
    }

    private void AddDynamicBody(bool isBox, float x, float y, float sizeOrRadius, float vx, float vy, float omega, PhysicsMaterial2D mat)
    {
        var go = new GameObject();
        go.transform.position = new Vector3(x * UnitScale, y * UnitScale, 0f);
        var rb = go.AddComponent<Rigidbody2D>();
        rb.bodyType = RigidbodyType2D.Dynamic;
        rb.gravityScale = 1f;
        rb.linearDamping = 0f;
        rb.angularDamping = 0f;
        rb.collisionDetectionMode = CollisionDetectionMode2D.Discrete;
        rb.sleepMode = _enableSleep ? RigidbodySleepMode2D.StartAwake : RigidbodySleepMode2D.NeverSleep;
        rb.linearVelocity = new Vector2(vx * UnitScale, vy * UnitScale);
        rb.angularVelocity = omega * Mathf.Rad2Deg;

        if (isBox)
        {
            var col = go.AddComponent<BoxCollider2D>();
            col.size = new Vector2(sizeOrRadius * UnitScale, sizeOrRadius * UnitScale);
            col.sharedMaterial = mat;
        }
        else
        {
            var col = go.AddComponent<CircleCollider2D>();
            col.radius = sizeOrRadius * UnitScale;
            col.sharedMaterial = mat;
        }

        _dynamicCount++;
        _trackedDynamic.Add(rb);
    }

    private void BuildBroadPhase()
    {
        const float laneStepY = 80f;
        const float laneSize = 48f;
        float laneStartY = -0.5f * (_count - 1) * laneStepY;

        for (int i = 0; i < _count; ++i)
        {
            int p = i % BroadStartX.Length;
            var go = new GameObject();
            go.transform.position = new Vector3(
                BroadStartX[p] * UnitScale,
                (laneStartY + i * laneStepY) * UnitScale,
                0f
            );
            var rb = go.AddComponent<Rigidbody2D>();
            rb.bodyType = RigidbodyType2D.Kinematic;
            rb.linearVelocity = new Vector2(BroadVelX[p] * UnitScale, 0f);
            rb.sleepMode = RigidbodySleepMode2D.NeverSleep;
            var col = go.AddComponent<BoxCollider2D>();
            col.size = new Vector2(laneSize * UnitScale, laneSize * UnitScale);
            _dynamicCount++;
            _trackedDynamic.Add(rb);
        }
    }

    private void BuildRigidBodyContainer()
    {
        const float boxSize = 28f;
        const float circleRadius = 14f;
        const float wallThickness = 64f;
        const float spawnMargin = 10f;
        const float cellPadding = 10f;
        const float minW = 1600f;
        const float minH = 900f;

        var mat = new PhysicsMaterial2D("RbcMat") { friction = 1.0f, bounciness = 0.0f };

        int boxCount = _count / 2;
        int circleCount = _count - boxCount;
        float cellSize = boxSize + cellPadding;
        float usableW = Mathf.Max(minW - 2f * spawnMargin, cellSize);
        int minCols = Mathf.Max(1, (int)(usableW / cellSize));
        double aspectRatio = (double)minW / (double)minH;
        int scaledCols = Mathf.Max(1, (int)System.Math.Ceiling(System.Math.Sqrt((double)_count * aspectRatio)));
        int cols = Mathf.Max(minCols, scaledCols);
        int rows = Mathf.Max(1, (_count + cols - 1) / cols);
        float interiorW = Mathf.Max(minW, cols * cellSize + 2f * spawnMargin);
        float interiorH = Mathf.Max(minH, rows * cellSize + 2f * spawnMargin);
        float left = -0.5f * interiorW;
        float top = -0.5f * interiorH;
        float halfW = 0.5f * interiorW;
        float halfH = 0.5f * interiorH;

        float horizW = interiorW + 2f * wallThickness;
        float vertH = interiorH + 2f * wallThickness;

        AddStaticWall(0f, halfH + 0.5f * wallThickness + 20f, horizW, wallThickness, mat);
        AddStaticWall(0f, -halfH - 0.5f * wallThickness, horizW, wallThickness, mat);
        AddStaticWall(-halfW - 0.5f * wallThickness, 0f, wallThickness, vertH, mat);
        AddStaticWall(halfW + 0.5f * wallThickness, 0f, wallThickness, vertH, mat);

        int sb = 0, sc = 0, idx = 0;
        while (sb < boxCount || sc < circleCount)
        {
            bool isBox = sb < boxCount && (sc >= circleCount || idx % 2 == 0);
            int col = idx % cols;
            int row = idx / cols;
            float x = left + spawnMargin + (col + 0.5f) * cellSize + ((idx % 3) - 1) * 2.0f;
            float y = top + spawnMargin + (row + 0.5f) * cellSize + ((idx % 4) - 1) * 1.5f;
            float vx = RbcVx[idx % RbcVx.Length];
            float vy = RbcVy[idx % RbcVy.Length];
            float omega = RbcOmega[idx % RbcOmega.Length];

            AddDynamicBody(isBox, x, y, isBox ? boxSize : circleRadius, vx, vy, omega, mat);
            if (isBox) sb++; else sc++;
            idx++;
        }
    }

    private void BuildIdealWorld()
    {
        const int bodiesPerChamber = 20;
        const float chamberInterior = 300f;
        const float wallThickness = 24f;
        const float chamberPitch = 600f;
        const float boxSize = 24f;
        const float circleRadius = 12f;
        const float bodyStep = 46f;

        var mat = new PhysicsMaterial2D("IdealMat") { friction = 0.8f, bounciness = 0.15f };

        int total = Math.Max(1, _count);
        int chamberCount = (total + bodiesPerChamber - 1) / bodiesPerChamber;
        int chamberCols = Math.Max(1, (int)Math.Ceiling(Math.Sqrt(chamberCount)));
        int chamberRows = Math.Max(1, (chamberCount + chamberCols - 1) / chamberCols);
        float originX = -0.5f * (chamberCols - 1) * chamberPitch;
        float originY = -0.5f * (chamberRows - 1) * chamberPitch;
        float halfInterior = 0.5f * chamberInterior;
        float fullSpan = chamberInterior + 2f * wallThickness;

        int spawned = 0;
        for (int c = 0; c < chamberCount; ++c)
        {
            float cx = originX + (c % chamberCols) * chamberPitch;
            float cy = originY + (c / chamberCols) * chamberPitch;

            AddStaticWall(cx, cy + halfInterior + 0.5f * wallThickness, fullSpan, wallThickness, mat);
            AddStaticWall(cx, cy - halfInterior - 0.5f * wallThickness, fullSpan, wallThickness, mat);
            AddStaticWall(cx - halfInterior - 0.5f * wallThickness, cy, wallThickness, fullSpan, mat);
            AddStaticWall(cx + halfInterior + 0.5f * wallThickness, cy, wallThickness, fullSpan, mat);

            for (int local = 0; local < bodiesPerChamber && spawned < total; ++local, ++spawned)
            {
                int bCol = local % 5;
                int bRow = local / 5;
                float jx = ((spawned % 3) - 1) * 1.5f;
                float jy = ((spawned % 4) - 1) * 1.0f;
                float bx = cx + (bCol - 2.0f) * bodyStep + jx;
                float by = cy + (bRow - 1.5f) * bodyStep + jy;
                float vx = RbcVx[spawned % RbcVx.Length];
                float vy = RbcVy[spawned % RbcVy.Length];
                float omega = RbcOmega[spawned % RbcOmega.Length];
                bool isBox = (spawned % 2 == 0);

                AddDynamicBody(isBox, bx, by, isBox ? boxSize : circleRadius, vx, vy, omega, mat);
            }
        }
    }
}
