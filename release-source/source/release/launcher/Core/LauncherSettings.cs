// SPDX-License-Identifier: GPL-3.0-only
namespace BlackOpsVR.Launcher;
public sealed class LauncherSettings
{
    public string GamePath { get; set; } = "";
    public string TurnMode { get; set; } = "snap";
    public string MovementOrientation { get; set; } = "hmd";
    public string ReloadMode { get; set; } = "manual";
    public string GrenadeMode { get; set; } = "motion";
    public string RuntimePath { get; set; } = "";
    public string RenderResolution { get; set; } = "3840x2160";
    public void Validate()
    {
        if (TurnMode is not ("snap" or "smooth") || MovementOrientation is not ("hmd" or "controller") || ReloadMode is not ("manual" or "automatic") || GrenadeMode is not ("motion" or "buttons"))
            throw new InvalidOperationException("Choose one of the listed control options.");
        if (RenderResolution is not ("2560x1440" or "3840x2160" or "5120x2880"))
            throw new InvalidOperationException("Choose one of the listed rendering resolutions.");
    }
}
