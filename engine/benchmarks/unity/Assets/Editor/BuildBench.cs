using System;
using System.IO;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;

public static class BuildBench
{
    public static void BuildLinux64()
    {
        const string sceneDir = "Assets/Scenes";
        const string scenePath = sceneDir + "/Bench.unity";

        if (!Directory.Exists(sceneDir))
            Directory.CreateDirectory(sceneDir);

        var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var runnerObj = new GameObject("PlatformatorUnityBenchmarkRunner");
        runnerObj.AddComponent<BenchmarkRunner>();
        EditorSceneManager.SaveScene(scene, scenePath);

        string repoRoot = Path.GetFullPath(Path.Combine(Application.dataPath, "../../../.."));
        string outDir = Path.Combine(repoRoot, "build", "unity-bench");
        Directory.CreateDirectory(outDir);
        string outPath = Path.Combine(outDir, "UnityBench.x86_64");

        var options = new BuildPlayerOptions
        {
            scenes = new[] { scenePath },
            locationPathName = outPath,
            target = BuildTarget.StandaloneLinux64,
            options = BuildOptions.None,
        };

        BuildReport report = BuildPipeline.BuildPlayer(options);
        if (report.summary.result != BuildResult.Succeeded)
        {
            Debug.LogError("UnityBench Linux64 build failed: " + report.summary.result);
            EditorApplication.Exit(1);
            return;
        }

        Debug.Log("UnityBench Linux64 build succeeded: " + outPath);
        EditorApplication.Exit(0);
    }
}
