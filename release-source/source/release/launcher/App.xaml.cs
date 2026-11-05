using System.Windows;
namespace BlackOpsVR.Launcher;
public partial class App : Application
{
    System.Threading.Mutex? instance;
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        instance=new System.Threading.Mutex(true,@"Local\BlackOpsVR.Patreon.Launcher",out var first);
        if(!first){MessageBox.Show("Black Ops VR is already open. Use the existing launcher window.","Black Ops VR");Shutdown();return;}
        new MainWindow(new LauncherService()).Show();
    }
    protected override void OnExit(ExitEventArgs e){instance?.Dispose();base.OnExit(e);}
}
