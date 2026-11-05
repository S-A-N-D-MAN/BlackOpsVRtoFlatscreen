using System;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using Microsoft.Win32;

namespace BlackOpsVR.Launcher;

public partial class MainWindow : Window
{
    private readonly LauncherService _launcherService;
    private bool _isBusy;

    public MainWindow(LauncherService launcherService)
    {
        _launcherService = launcherService ?? throw new ArgumentNullException(nameof(launcherService));
        InitializeComponent();
        Title = BuildIdentity.DisplayName;
        Loaded += MainWindow_Loaded;
    }

    protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
    {
        if (_isBusy)
        {
            e.Cancel = true;
            SetStatus("Close the game normally before closing the launcher.");
            return;
        }

        base.OnClosing(e);
    }

    private void MainWindow_Loaded(object? sender, RoutedEventArgs e)
    {
        LoadSettings();
    }

    private void LoadSettings()
    {
        try
        {
            var settings = _launcherService.LoadSettings();
            ApplySettingsToUi(settings);

            if (string.IsNullOrWhiteSpace(GameFolderTextBox.Text))
            {
                try
                {
                    var detected = _launcherService.DetectGamePath();
                    if (!string.IsNullOrWhiteSpace(detected))
                    {
                        GameFolderTextBox.Text = detected;
                    }
                }
                catch (Exception)
                {
                }
            }

            RefreshGameStatus();
        }
        catch (Exception ex)
        {
            SetStatus("Could not load settings.");
            ShowFriendlyError("Load settings", ex);
        }
    }

    private void ApplySettingsToUi(LauncherSettings settings)
    {
        GameFolderTextBox.Text = settings.GamePath ?? string.Empty;
        TurnSnapRadioButton.IsChecked = !string.Equals(settings.TurnMode, "smooth", StringComparison.OrdinalIgnoreCase);
        TurnSmoothRadioButton.IsChecked = string.Equals(settings.TurnMode, "smooth", StringComparison.OrdinalIgnoreCase);
        MovementHmdRadioButton.IsChecked = !string.Equals(settings.MovementOrientation, "controller", StringComparison.OrdinalIgnoreCase);
        MovementControllerRadioButton.IsChecked = string.Equals(settings.MovementOrientation, "controller", StringComparison.OrdinalIgnoreCase);
        ReloadManualRadioButton.IsChecked = !string.Equals(settings.ReloadMode, "automatic", StringComparison.OrdinalIgnoreCase);
        ReloadAutomaticRadioButton.IsChecked = string.Equals(settings.ReloadMode, "automatic", StringComparison.OrdinalIgnoreCase);
        GrenadeMotionRadioButton.IsChecked = !string.Equals(settings.GrenadeMode, "buttons", StringComparison.OrdinalIgnoreCase);
        GrenadeButtonsRadioButton.IsChecked = string.Equals(settings.GrenadeMode, "buttons", StringComparison.OrdinalIgnoreCase);
        RuntimeJsonTextBox.Text = settings.RuntimePath ?? string.Empty;
        ResolutionPerformanceRadioButton.IsChecked = settings.RenderResolution == "2560x1440";
        ResolutionStandardRadioButton.IsChecked = settings.RenderResolution == "3840x2160";
        ResolutionHighRadioButton.IsChecked = settings.RenderResolution == "5120x2880";
    }

    private LauncherSettings CaptureSettingsFromUi()
    {
        return new LauncherSettings
        {
            GamePath = (GameFolderTextBox.Text ?? string.Empty).Trim(),
            TurnMode = TurnSmoothRadioButton.IsChecked == true ? "smooth" : "snap",
            MovementOrientation = MovementControllerRadioButton.IsChecked == true ? "controller" : "hmd",
            ReloadMode = ReloadAutomaticRadioButton.IsChecked == true ? "automatic" : "manual",
            GrenadeMode = GrenadeButtonsRadioButton.IsChecked == true ? "buttons" : "motion",
            RuntimePath = (RuntimeJsonTextBox.Text ?? string.Empty).Trim(),
            RenderResolution = ResolutionHighRadioButton.IsChecked == true ? "5120x2880" :
                ResolutionPerformanceRadioButton.IsChecked == true ? "2560x1440" : "3840x2160"
        };
    }

    private async void InstallUpdateButton_Click(object sender, RoutedEventArgs e)
    {
        await RunOperationAsync(
            "Install / Update",
            settings => _launcherService.InstallAsync(settings));
    }

    private async void LaunchVrButton_Click(object sender, RoutedEventArgs e)
    {
        await RunOperationAsync("Launch VR", settings => _launcherService.LaunchAsync(settings));
    }

    private async void RestoreButton_Click(object sender, RoutedEventArgs e)
    {
        await RunOperationAsync("Remove VR setup", settings => _launcherService.RestoreAsync(settings));
    }

    private void OpenControlsButton_Click(object sender, RoutedEventArgs e)
    {
        ExecuteServiceAction("Open controls", () => _launcherService.OpenControls());
    }

    private void OpenLogsButton_Click(object sender, RoutedEventArgs e)
    {
        ExecuteServiceAction("Open logs", () => _launcherService.OpenLogs());
    }

    private void CheckGameButton_Click(object sender, RoutedEventArgs e)
    {
        RefreshGameStatus();
    }

    private void CopyReportButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var settings=CaptureSettingsFromUi();SaveSettings(settings);
            Clipboard.SetText(_launcherService.CompatibilityReport(settings));
            SetStatus("Compatibility report copied. You can paste it into your bug report; it contains no saves or profile data.");
        }
        catch(Exception ex)
        {
            ShowFriendlyError("Copy compatibility report",ex);
        }
    }

    private void RefreshGameStatus()
    {
        try
        {
            var game=(GameFolderTextBox.Text??string.Empty).Trim();
            if(string.IsNullOrWhiteSpace(game)) { SetStatus("Select the Steam folder containing BlackOps.exe, then choose Check game.");return; }
            var identity=LauncherService.ValidateSteamSource(game);
            SetStatus("Supported Valve-signed Steam single-player build found.\n"+game+"\nExecutable: "+identity.Executable.Sha256);
        }
        catch(Exception ex)
        {
            SetStatus(ex.Message);
        }
    }

    private void BrowseGameFolderButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var dialog = new OpenFolderDialog();
            if (!string.IsNullOrWhiteSpace(GameFolderTextBox.Text))
            {
                dialog.InitialDirectory = GameFolderTextBox.Text;
            }

            if (dialog.ShowDialog() == true)
            {
                GameFolderTextBox.Text = dialog.FolderName;
                SaveSettings();
                RefreshGameStatus();
            }
        }
        catch (Exception ex)
        {
            ShowFriendlyError("Select game folder", ex);
        }
    }

    private void BrowseRuntimeJsonButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var dialog = new OpenFileDialog
            {
                Title = "Select OpenXR runtime JSON",
                Filter = "JSON files (*.json)|*.json|All files (*.*)|*.*"
            };

            if (dialog.ShowDialog() == true)
            {
                RuntimeJsonTextBox.Text = dialog.FileName;
                SaveSettings();
                SetStatus("OpenXR runtime path updated.");
            }
        }
        catch (Exception ex)
        {
            ShowFriendlyError("Select OpenXR runtime JSON", ex);
        }
    }

    private async Task RunOperationAsync(string actionLabel, Func<LauncherSettings, Task<string>> operation)
    {
        if (_isBusy)
        {
            return;
        }

        try
        {
            var settings = CaptureSettingsFromUi();
            SaveSettings(settings);

            var busyLabel = actionLabel == "Install / Update"
                ? "Preparing VR files. First setup may take a few minutes."
                : actionLabel;
            ClearStatus();
            SetBusy(true, busyLabel);

            var result = await operation(settings);
            if (!string.IsNullOrWhiteSpace(result))
            {
                SetStatus(result);
            }
            else
            {
                SetStatus($"{actionLabel} complete.");
            }
        }
        catch (Exception ex)
        {
            ShowFriendlyError(actionLabel, ex);
        }
        finally
        {
            SetBusy(false, string.Empty);
        }
    }

    private void ExecuteServiceAction(string actionLabel, Action action)
    {
        if (_isBusy)
        {
            return;
        }

        ClearStatus();
        SetBusy(true, actionLabel);

        try
        {
            var settings = CaptureSettingsFromUi();
            SaveSettings(settings);
            action();
            SetStatus($"{actionLabel} opened.");
        }
        catch (Exception ex)
        {
            ShowFriendlyError(actionLabel, ex);
        }
        finally
        {
            SetBusy(false, string.Empty);
        }
    }

    private void SaveSettings(LauncherSettings? settings = null)
    {
        _launcherService.SaveSettings(settings ?? CaptureSettingsFromUi());
    }

    private void SetStatus(string message)
    {
        StatusTextBlock.Text = message;
    }

    private void ClearStatus()
    {
        StatusTextBlock.Text = string.Empty;
    }

    private void ShowFriendlyError(string operation, Exception ex)
    {
        var message = $"{operation} failed: {ex.Message}";
        MessageBox.Show(this, message, "Black Ops VR", MessageBoxButton.OK, MessageBoxImage.Error);
        SetStatus(message);
    }

    private void SetBusy(bool isBusy, string actionLabel)
    {
        _isBusy = isBusy;
        Cursor = isBusy ? Cursors.Wait : Cursors.Arrow;

        InstallUpdateButton.IsEnabled = !isBusy;
        LaunchVrButton.IsEnabled = !isBusy;
        RestoreButton.IsEnabled = !isBusy;
        CheckGameButton.IsEnabled = !isBusy;
        CopyReportButton.IsEnabled = !isBusy;
        OpenControlsButton.IsEnabled = !isBusy;
        OpenLogsButton.IsEnabled = !isBusy;
        BrowseGameFolderButton.IsEnabled = !isBusy;
        BrowseRuntimeJsonButton.IsEnabled = !isBusy;
        TurnSnapRadioButton.IsEnabled = !isBusy;
        TurnSmoothRadioButton.IsEnabled = !isBusy;
        MovementHmdRadioButton.IsEnabled = !isBusy;
        MovementControllerRadioButton.IsEnabled = !isBusy;
        ReloadManualRadioButton.IsEnabled = !isBusy;
        ReloadAutomaticRadioButton.IsEnabled = !isBusy;
        GrenadeMotionRadioButton.IsEnabled = !isBusy;
        GrenadeButtonsRadioButton.IsEnabled = !isBusy;
        AdvancedExpander.IsEnabled = !isBusy;
        GameFolderTextBox.IsEnabled = !isBusy;
        RuntimeJsonTextBox.IsEnabled = !isBusy;

        BusyProgress.Visibility = isBusy ? Visibility.Visible : Visibility.Collapsed;

        if (!string.IsNullOrWhiteSpace(actionLabel))
        {
            SetStatus(isBusy ? $"Running: {actionLabel}..." : StatusTextBlock.Text);
        }
    }
}

