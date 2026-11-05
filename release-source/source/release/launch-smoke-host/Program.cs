// SPDX-License-Identifier: GPL-3.0-only
// Explicitly invoked integration host. Never registered as an unattended test.
using BlackOpsVR.Launcher;
using System.Text.Json;

if(args.Length==0 || args.Contains("--help"))
{
    Console.WriteLine("Usage: launch-smoke-host --package PACKAGE --data PRIVATE_DATA --game STEAM_GAME --runtime X86_RUNTIME_JSON --turn snap|smooth --reload manual|automatic --grenades motion|buttons [--install-only]");
    Console.WriteLine("Installs into explicit private data using the actual Core, prints its runtime directory, then launches and waits unless --install-only is supplied. Existing VR saves are retained. No game files are bundled. Start only when the game is closed and the intended runtime is ready.");
    return 0;
}
try
{
    var options=new Dictionary<string,string>();bool installOnly=false;
    for(int i=0;i<args.Length;++i)
    {
        if(args[i]=="--install-only")
        {
            if(installOnly) throw new ArgumentException("--install-only must appear once.");
            installOnly=true;continue;
        }
        if(args[i] is not ("--package" or "--data" or "--game" or "--runtime" or "--turn" or "--reload" or "--grenades") || i+1>=args.Length || !options.TryAdd(args[i],args[++i]))
            throw new ArgumentException("Use the documented options exactly once; see --help.");
    }
    if(options.Count!=7) throw new ArgumentException("All seven value options are required; see --help.");
    var package=Path.GetFullPath(options["--package"]);
    var data=Path.GetFullPath(options["--data"]).TrimEnd('\\','/');
    var game=Path.GetFullPath(options["--game"]).TrimEnd('\\','/');
    if(data.Equals(game,StringComparison.OrdinalIgnoreCase) || data.StartsWith(game+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))
        throw new ArgumentException("Private data must be outside the selected Steam game folder.");
    var settings=new LauncherSettings{GamePath=game,RuntimePath=Path.GetFullPath(options["--runtime"]),TurnMode=options["--turn"],ReloadMode=options["--reload"],GrenadeMode=options["--grenades"]};
    settings.Validate();
    var service=new LauncherService(package,data);
    Console.WriteLine(await service.InstallAsync(settings));
    Console.WriteLine(JsonSerializer.Serialize(new{runtimeDirectory=service.RuntimeDirectory(settings),data,installOnly,options=settings},new JsonSerializerOptions{WriteIndented=true}));
    if(!installOnly) Console.WriteLine(await service.LaunchAsync(settings));
    // Native exit status is in Core's logs/<session>/exit.json. This status reports
    // successful host execution; it does not reclassify the native game exit.
    return 0;
}
catch(Exception error)
{
    Console.Error.WriteLine(error);
    return 1;
}
