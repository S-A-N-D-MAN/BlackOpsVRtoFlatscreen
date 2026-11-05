// SPDX-License-Identifier: GPL-3.0-only
// Actual setup/removal tests. The game is never launched and retail is read-only.
using BlackOpsVR.Launcher;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

static class Program
{
    static string Root = "", Steam = "", output = "", proxy = "", multiplayer = "";
    static string run = "", package = "";
    static readonly List<object> results = [];
    static int checks, failures;
    static void Check(bool okay,string name)
    {
        ++checks; if(!okay) ++failures;
        Console.WriteLine((okay?"PASS ":"FAIL ")+name);
        results.Add(new {name,passed=okay});
    }
    static async Task Refuses(Func<Task> action,string name)
    {
        try { await action(); Check(false,name); }
        catch(Exception e) when(e is IOException or InvalidOperationException or JsonException)
        { Check(true,name); Console.WriteLine("  "+e.Message); }
    }
    static LauncherSettings Settings()=>new(){GamePath=Steam};
    static LauncherService Service(string name)=>new(package,Path.Combine(run,name));
    static string Hash(string p)=>LauncherService.Sha(p);
    static void Allows(Action action,string name)
    {
        try { action(); Check(true,name); }
        catch(Exception e) { Check(false,name); Console.WriteLine("  "+e.Message); }
    }
    static void PopulateGame(string folder,string executable)
    {
        Directory.CreateDirectory(folder);
        File.Copy(executable,Path.Combine(folder,"BlackOps.exe"));
        foreach(var file in new[]{"binkw32.dll","steam_api.dll","localization.txt"})
            File.Copy(Path.Combine(Steam,file),Path.Combine(folder,file));
        Directory.CreateDirectory(Path.Combine(folder,"zone"));Directory.CreateDirectory(Path.Combine(folder,"main"));
        File.WriteAllText(Path.Combine(folder,"main","iw_00.iwd"),"private validation fixture; never launched");
    }
    static string GameFixture(string name,string executable)
    {
        var folder=Path.Combine(run,name);PopulateGame(folder,executable);
        return folder;
    }
    static string SteamFixture(string name,string executable)
    {
        var root=Path.Combine(run,name);var game=Path.Combine(root,"steamapps","common","Call of Duty Black Ops");
        PopulateGame(game,executable);var manifest=Path.Combine(root,"steamapps","appmanifest_42700.acf");
        File.WriteAllText(manifest,"\"AppState\"\n{\n\t\"appid\" \"42700\"\n\t\"installdir\" \"Call of Duty Black Ops\"\n\t\"buildid\" \"87954\"\n\t\"InstalledDepots\"\n\t{\n\t\t\"42701\"\n\t\t{\n\t\t\t\"manifest\" \"5564723134361260769\"\n\t\t}\n\t}\n}\n");
        return game;
    }
    static void MutateChecksum(string path)
    {
        using var file=File.Open(path,FileMode.Open,FileAccess.ReadWrite,FileShare.None);
        using var reader=new BinaryReader(file,Encoding.UTF8,leaveOpen:true);
        file.Position=0x3c;var pe=reader.ReadInt32();file.Position=pe+24+64;
        var checksum=reader.ReadUInt32();file.Position=pe+24+64;
        using var writer=new BinaryWriter(file,Encoding.UTF8,leaveOpen:true);writer.Write(checksum^0x01010101u);
    }
    static void MutateSignedContent(string path)
    {
        using var file=File.Open(path,FileMode.Open,FileAccess.ReadWrite,FileShare.None);
        file.Position=0x2000;var value=file.ReadByte();file.Position=0x2000;file.WriteByte((byte)(value^1));
    }
    static void Json(string p,object value)=>File.WriteAllText(p,JsonSerializer.Serialize(value,new JsonSerializerOptions{WriteIndented=true})+"\n");
    static Dictionary<string,string> SnapshotRetail()
    {
        var files=Directory.EnumerateFiles(Steam).ToList();
        void Visit(string dir)
        {
            if(!Directory.Exists(dir)) return;
            foreach(var p in Directory.EnumerateFileSystemEntries(dir))
            {
                var a=File.GetAttributes(p);
                if((a&FileAttributes.ReparsePoint)!=0) continue;
                if((a&FileAttributes.Directory)!=0) Visit(p); else files.Add(p);
            }
        }
        Visit(Path.Combine(Steam,"players"));
        return files.Order().ToDictionary(p=>Path.GetRelativePath(Steam,p),Hash);
    }
    static void Link(string link,string target)
    {
        // Call the actual Core helper, with all destinations beneath this owned run.
        if(!Path.GetFullPath(link).StartsWith(run+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Test link escapes run");
        var t=typeof(LauncherService).Assembly.GetType("BlackOpsVR.Launcher.Junction",true)!;
        try { t.GetMethod("Ensure",BindingFlags.Public|BindingFlags.Static)!.Invoke(null,[link,target]); }
        catch(TargetInvocationException e) { throw e.InnerException!; }
    }
    static void UnlinkOwnedZone(string zone)
    {
        var actual=new DirectoryInfo(zone).ResolveLinkTarget(true);
        if(!Path.GetFullPath(zone).StartsWith(run+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase) ||
           actual?.FullName.TrimEnd('\\')!=Path.Combine(Steam,"zone")) throw new InvalidOperationException("Refuse unverified test link unlink");
        Directory.Delete(zone,false); // removes only this verified junction, never its target.
    }
    static async Task Main(string[] args)
    {
        if(args.Length==0 || args.Contains("--help"))
        {
            Console.WriteLine("Usage: launcher-tests --workspace SOURCE_ROOT --game STEAM_GAME_FOLDER --output TEST_FOLDER_WITHIN_SOURCE_ROOT --proxy BUILT_MOD_DLL --multiplayer BLACKOPS_MP_EXE");
            Console.WriteLine("Runs real private install/remove tests without launching the game. Requires the user's supported Steam SP files. Keeps every test run; never recursively deletes files. No retail executable/assets/profile are distributed with this harness.");
            return;
        }
        var options=new Dictionary<string,string>();
        for(int i=0;i<args.Length;i+=2)
        {
            if(i+1>=args.Length || args[i] is not ("--workspace" or "--game" or "--output" or "--proxy" or "--multiplayer") || !options.TryAdd(args[i],args[i+1]))
                throw new ArgumentException("Use each documented path option exactly once; see --help.");
        }
        if(options.Count!=5) throw new ArgumentException("All five path options are required; see --help.");
        Root=Path.GetFullPath(options["--workspace"]).TrimEnd('\\','/');
        Steam=Path.GetFullPath(options["--game"]).TrimEnd('\\','/');
        output=Path.GetFullPath(options["--output"]).TrimEnd('\\','/');
        proxy=Path.GetFullPath(options["--proxy"]);
        multiplayer=Path.GetFullPath(options["--multiplayer"]);
        if(!Directory.Exists(Root) || !output.StartsWith(Root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("The explicit test output must be a descendant of the existing source workspace.");
        for(var cursor=output;cursor is not null;cursor=Path.GetDirectoryName(cursor))
            if((Directory.Exists(cursor)||File.Exists(cursor)) && (File.GetAttributes(cursor)&FileAttributes.ReparsePoint)!=0)
                throw new ArgumentException("Test output ancestors must not be redirected.");
        if(output.StartsWith(Steam+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase) || output.Equals(Steam,StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("Test output must not be inside the user's Steam installation.");
        LauncherService.ValidateSteamSource(Steam);
        Directory.CreateDirectory(output);
        run=Path.GetFullPath(Path.Combine(output,DateTime.UtcNow.ToString("yyyyMMddTHHmmssfffffffZ")));
        Directory.CreateDirectory(run);
        package=Path.Combine(run,"package"); var payload=Path.Combine(package,"payload"); Directory.CreateDirectory(payload);
        File.Copy(proxy,Path.Combine(payload,"d3d9.dll"));
        foreach(var name in new[]{"runtime-env.json","config-default.cfg"}) File.Copy(Path.Combine(Root,"release/payload",name),Path.Combine(payload,name));
        Json(Path.Combine(payload,"manifest.json"),Directory.EnumerateFiles(payload).ToDictionary(p=>Path.GetFileName(p),Hash));
        var testedArtifacts=new{version=BuildIdentity.Version,nativeDllSha256=Hash(Path.Combine(payload,"d3d9.dll")),
            launcherCoreSha256=Hash(typeof(LauncherService).Assembly.Location),
            runtimeEnvSha256=Hash(Path.Combine(payload,"runtime-env.json")),
            configDefaultSha256=Hash(Path.Combine(payload,"config-default.cfg"))};
        var before=SnapshotRetail(); Json(Path.Combine(run,"retail-before.json"),before);
        try
        {
            LauncherService.ValidateSteamSource(Steam);
            var baseline=JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(Path.Combine(payload,"runtime-env.json")))!;
            var svc=Service("main");
            foreach(var turn in new[]{"snap","smooth"})
            foreach(var movement in new[]{"hmd","controller"})
            foreach(var reload in new[]{"manual","automatic"})
            foreach(var grenade in new[]{"motion","buttons"})
            {
                var s=Settings();s.TurnMode=turn;s.ReloadMode=reload;s.GrenadeMode=grenade;
                s.MovementOrientation=movement;
                var env=svc.LaunchEnvironment(s,"X:\\example-runtime.json","X:\\example.log");
                var label=$"options {turn}/{movement}/{reload}/{grenade}";
                Check(env["BOVR_SMOOTH_TURN"]==(turn=="smooth"?"1":"0") && env["BOVR_MANUAL_RELOAD"]==(reload=="manual"?"1":"0") && env["BOVR_BUTTON_GRENADES"]==(grenade=="buttons"?"1":"0"),label+" exact mode values");
                Check(baseline.Where(k=>k.Key!="BOVR_MANUAL_RELOAD"&&k.Key!="BOVR_MOVEMENT_ORIENTATION").All(k=>env.GetValueOrDefault(k.Key)==k.Value)
                    && env.GetValueOrDefault("BOVR_HITSCAN_EFFECTS_MODE")=="0"
                    && env.GetValueOrDefault("BOVR_ORDINARY_EVENT_FX")=="0"
                    && env.GetValueOrDefault("BOVR_CLIENT_COMMAND_ANGLES")=="correct"
                    && env.GetValueOrDefault("BOVR_RENDER_NEUTRAL_RECOVERY")=="1"
                    && env.GetValueOrDefault("BOVR_ROOM_SCALE")=="1"
                    && env.GetValueOrDefault("BOVR_COMMAND_RIG_PROBE")=="0"
                    && env.GetValueOrDefault("BOVR_CONSUMED_COMMAND_PROBE")=="0",label+" release defaults and other runtime values unchanged");
                Check(env["BOVR_MOVEMENT_ORIENTATION"]==movement,label+" movement orientation value");
                Check(env["XR_RUNTIME_JSON"]=="X:\\example-runtime.json" && env["BOVR_TRACE_FILE"]=="X:\\example.log" && !env.ContainsKey("LOCALAPPDATA"),label+" dynamic child paths only");
            }
            var invalid=Settings();invalid.TurnMode="invalid";
            await Refuses(()=>Task.FromResult(svc.LaunchEnvironment(invalid,"runtime","log")),"invalid option rejected");
            var invalidMovement=Settings();invalidMovement.MovementOrientation="invalid";
            await Refuses(()=>Task.FromResult(svc.LaunchEnvironment(invalidMovement,"runtime","log")),"invalid movement orientation rejected");
            var invalidResolution=Settings();invalidResolution.RenderResolution="8192x8192";
            await Refuses(()=>Task.FromResult(svc.LaunchEnvironment(invalidResolution,"runtime","log")),"unsupported render resolution refused before launch");
            Check(JsonSerializer.Deserialize<LauncherSettings>("{}")!.RenderResolution=="3840x2160","older settings retain standard source resolution");
            foreach(var resolution in new[]{"2560x1440","3840x2160","5120x2880"})
            {
                var selected=Settings();selected.RenderResolution=resolution;
                var resolutionService=Service("resolution-preservation");
                await resolutionService.InstallAsync(selected);
                var resolutionGame=resolutionService.RuntimeDirectory(selected);
                var resolutionConfig=Path.Combine(resolutionGame,"players","config.cfg");
                LauncherService.VerifyRenderProfile(resolutionConfig,resolution);
                Check(resolutionService.LoadSettings().RenderResolution==resolution,"selected resolution survives settings save/load "+resolution);
                Check(resolutionService.LaunchEnvironment(selected,"X:\\runtime.json","X:\\log.txt")["BOVR_NATIVE_RENDER_SIZE"]==resolution,
                    "profile and native resolution allowance agree "+resolution);
                var retainedSave=Path.Combine(resolutionGame,"players","save","resolution-test.svg");
                Directory.CreateDirectory(Path.GetDirectoryName(retainedSave)!);
                if(!File.Exists(retainedSave))File.WriteAllText(retainedSave,"resolution changes preserve this save");
                var retainedHash=Hash(retainedSave);
                File.AppendAllText(resolutionConfig,"\r\nbind F \"+activate\"\r\nseta sv_lastSaveGame \"save\\resolution-test.svg\"\r\n");
                await resolutionService.InstallAsync(selected);
                Check(Hash(retainedSave)==retainedHash&&File.ReadAllText(resolutionConfig).Contains("save\\resolution-test.svg"),
                    "resolution update preserves checkpoint and save bytes "+resolution);
                if(resolution!="3840x2160")
                    await Refuses(()=>{LauncherService.VerifyRenderProfile(resolutionConfig,"3840x2160");return Task.CompletedTask;},
                        "mismatched profile and requested resolution refused "+resolution);
            }
            var native=LauncherService.NativeLaunchSettings(Path.Combine(run,"native-settings-game"));
            Check(native["com_maxfps"]=="0","native launch leaves frame rate uncapped for OpenXR cadence");
            Check(native["r_vsync"]=="0","native launch disables legacy desktop vblank wait");
            Check(native["r_multiGpu"]=="0","native launch disables legacy multi-GPU synchronization");
            Check(native["r_fullscreen"]=="0"&&native["onlinegame"]=="0","native launch retains windowed offline safety settings");
            var input="// retained\r\nbind F \"+activate\"\r\nseta sv_lastSaveGame \"save\\my-campaign.svg\"\r\nseta r_fullscreen \"1\"\r\nseta R_FULLSCREEN \"1\"\r\nseta r_fullscreen \"0\"\r\n";
            var merged=LauncherService.ApplyConfig(input,"seta r_fullscreen \"0\"\nseta r_aaSamples \"1\"\n");
            Check(Regex.Matches(merged,@"(?im)^seta r_fullscreen\b").Count==1,"config collapses mixed-case duplicate native settings");
            Check(merged.Contains("bind F \"+activate\"") && merged.Contains("save\\my-campaign.svg") && merged.Contains("// retained"),"config preserves bindings/save pointer/comments");
            Check(LauncherService.ApplyConfig(merged,"seta r_fullscreen \"0\"\nseta r_aaSamples \"1\"\n")==merged,"config overlay idempotent");

            Check(native["mis_01"]=="20","native launch unlocks campaign from a new profile");
            Check(LauncherService.CampaignMissionUnlock("seta mis_01 \"4\"\n")==20,"campaign unlock raises partial progress");
            var earned="seta MIS_01 \"27\"\nseta mis_01 \"3\"\n";
            var unlocked=LauncherService.ApplyConfig(earned,"seta mis_01 \"20\"\n");
            Check(Regex.Matches(unlocked,@"(?im)^seta mis_01\b").Count==1 && unlocked.Contains("seta mis_01 \"27\""),"campaign unlock retains maximum earned progress across duplicate settings");
            Check(LauncherService.CampaignMissionUnlock("seta mis_01 \"999999999999999\"\n")==20,"campaign unlock tolerates malformed out-of-range progress");
            var missionGame=Path.Combine(run,"native-mission-game");Directory.CreateDirectory(Path.Combine(missionGame,"players"));
            File.WriteAllText(Path.Combine(missionGame,"players","config.cfg"),earned,Encoding.Unicode);
            Check(LauncherService.NativeLaunchSettings(missionGame)["mis_01"]=="27","native launch reads earned progress from Unicode private profile");

            var badGame=Path.Combine(run,"invalid-game");Directory.CreateDirectory(badGame);File.WriteAllText(Path.Combine(badGame,"BlackOps.exe"),"invalid test executable; never run");
            var bad=Settings();bad.GamePath=badGame; var badSvc=Service("invalid-exe");
            await Refuses(()=>badSvc.InstallAsync(bad),"unsupported EXE rejected before setup");
            Check(!Directory.Exists(badSvc.RuntimeDirectory(bad)),"invalid EXE leaves no runtime");

            var currentIdentity=LauncherService.InspectGame(Steam);
            Check(currentIdentity.Accepted&&currentIdentity.Sha256==Hash(Path.Combine(Steam,"BlackOps.exe")),"actual Steam SP executable family accepted");
            var signedVariant=SteamFixture("signed-account-library",Path.Combine(Steam,"BlackOps.exe"));
            var signedVariantExe=Path.Combine(signedVariant,"BlackOps.exe");var referenceHash=Hash(signedVariantExe);MutateChecksum(signedVariantExe);
            var signedVariantIdentity=LauncherService.InspectGame(signedVariant);
            Check(Hash(signedVariantExe)!=referenceHash,"signature-preserving checksum variant differs from reference whole-file hash");
            Check(signedVariantIdentity.Accepted&&signedVariantIdentity.Sha256==Hash(signedVariantExe)&&signedVariantIdentity.Signer.StartsWith("Valve",StringComparison.Ordinal),"different whole-file hash with valid Valve signature accepted");
            Allows(()=>LauncherService.ValidateSteamSource(signedVariant),"signature-preserving checksum variant accepted as supported depot");
            var variantSettings=Settings();variantSettings.GamePath=signedVariant;
            var report=Service("compatibility-report").CompatibilityReport(variantSettings);
            Check(report.StartsWith("Black Ops VR v"+BuildIdentity.Version+" compatibility report",StringComparison.Ordinal)&&report.Contains(signedVariantIdentity.Sha256)&&report.Contains("Accepted: True")&&!report.Contains("players",StringComparison.OrdinalIgnoreCase)&&!report.Contains("save",StringComparison.OrdinalIgnoreCase)&&!report.Contains(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),StringComparison.OrdinalIgnoreCase),"compatibility report includes private build identity while redacting profile, save, and user-home path data");
            var variantService=Service("signed-variant-install");await variantService.InstallAsync(variantSettings);var variantRuntime=variantService.RuntimeDirectory(variantSettings);
            Check(Hash(Path.Combine(variantRuntime,"BlackOps.exe"))==signedVariantIdentity.Sha256,"installer copies different signed account executable exactly");
            using(var installed=JsonDocument.Parse(File.ReadAllText(Path.Combine(variantRuntime,"installed.json"))))
                Check(installed.RootElement.GetProperty("version").GetString()==BuildIdentity.Version&&installed.RootElement.GetProperty("sourceExeSha256").GetString()==signedVariantIdentity.Sha256&&installed.RootElement.GetProperty("steamBuildId").GetString()==LauncherService.SupportedBuildId&&installed.RootElement.GetProperty("steamSpDepotManifest").GetString()==LauncherService.SupportedSpDepotManifest,"install marker pins private build, selected CEG executable and Steam depot identity");
            var wrongBuild=SteamFixture("wrong-build-library",Path.Combine(Steam,"BlackOps.exe"));var wrongBuildManifest=Path.GetFullPath(Path.Combine(wrongBuild,"..","..","appmanifest_42700.acf"));
            File.WriteAllText(wrongBuildManifest,File.ReadAllText(wrongBuildManifest).Replace("\"87954\"","\"1\""));
            await Refuses(()=>{LauncherService.ValidateSteamSource(wrongBuild);return Task.CompletedTask;},"wrong Steam build ID rejected");
            var wrongDepot=SteamFixture("wrong-depot-library",Path.Combine(Steam,"BlackOps.exe"));var wrongDepotManifest=Path.GetFullPath(Path.Combine(wrongDepot,"..","..","appmanifest_42700.acf"));
            File.WriteAllText(wrongDepotManifest,File.ReadAllText(wrongDepotManifest).Replace("\"5564723134361260769\"","\"1\""));
            await Refuses(()=>{LauncherService.ValidateSteamSource(wrongDepot);return Task.CompletedTask;},"wrong Steam SP depot manifest rejected");
            var unregistered=GameFixture("unregistered-valid-game",Path.Combine(Steam,"BlackOps.exe"));
            await Refuses(()=>{LauncherService.ValidateSteamSource(unregistered);return Task.CompletedTask;},"valid executable outside registered Steam library rejected as install source");

            var tamperedSigned=GameFixture("tampered-signed",Path.Combine(Steam,"BlackOps.exe"));MutateSignedContent(Path.Combine(tamperedSigned,"BlackOps.exe"));
            var tamperedIdentity=LauncherService.InspectGame(tamperedSigned);
            Check(!tamperedIdentity.Accepted&&tamperedIdentity.Reason.Contains("signature",StringComparison.OrdinalIgnoreCase),"content-tampered signed executable rejected");
            await Refuses(()=>{LauncherService.ValidateGame(tamperedSigned);return Task.CompletedTask;},"content-tampered executable rejected before setup");
            if(File.Exists(multiplayer))
            {
                var renamedMp=GameFixture("renamed-multiplayer",multiplayer);
                Check(!LauncherService.InspectGame(renamedMp).Accepted,"renamed multiplayer executable rejected by SP family");
                await Refuses(()=>{LauncherService.ValidateGame(renamedMp);return Task.CompletedTask;},"renamed multiplayer executable rejected before setup");
            }
            else
            {
                Console.WriteLine("SKIP multiplayer identity checks: BlackOpsMP.exe is not installed; no substitute executable used.");
                results.Add(new {name="multiplayer executable identity",skipped=true,reason="BlackOpsMP.exe not installed"});
            }
            var envFile=Path.Combine(payload,"runtime-env.json");var envOriginal=File.ReadAllBytes(envFile);File.AppendAllText(envFile," ");
            await Refuses(()=>Service("tampered-payload").InstallAsync(Settings()),"tampered payload rejected before setup");
            File.WriteAllBytes(envFile,envOriginal);

            await svc.InstallAsync(Settings());var game=svc.RuntimeDirectory(Settings());
            Check(Hash(Path.Combine(game,"BlackOps.exe"))==Hash(Path.Combine(Steam,"BlackOps.exe")),"actual install copies selected legitimate EXE exactly");
            Check(Hash(Path.Combine(game,"d3d9.dll"))==Hash(Path.Combine(payload,"d3d9.dll")),"actual install exact mod DLL");
            var installedConfig=File.ReadAllText(Path.Combine(game,"players","config.cfg"));
            Check(LauncherService.CampaignMissionUnlock(installedConfig)>=20 && Regex.IsMatch(installedConfig,@"(?im)^seta mis_01 ""\d+""\r?$"),"actual install unlocks campaign in private profile");
            Check(Regex.Matches(installedConfig,@"(?im)^seta com_maxfps ""0""\r?$").Count==1 &&
                  Regex.Matches(installedConfig,@"(?im)^seta r_vsync ""0""\r?$").Count==1 &&
                  Regex.Matches(installedConfig,@"(?im)^seta r_multiGpu ""0""\r?$").Count==1,
                  "actual install applies one exact OpenXR pacing profile");
            Check((File.GetAttributes(Path.Combine(game,"main"))&FileAttributes.ReparsePoint)==0,"main is private writable directory");
            Check(new DirectoryInfo(Path.Combine(game,"zone")).ResolveLinkTarget(true)?.FullName.TrimEnd('\\')==Path.Combine(Steam,"zone"),"actual zone junction points to selected Steam assets");
            Check(File.ReadAllText(Path.Combine(game,"steam_appid.txt")).Trim()=="42700","private Steam app ID correct");
            var caseSettings=Settings();caseSettings.GamePath=Steam.ToUpperInvariant();
            await svc.InstallAsync(caseSettings);
            Check(svc.RuntimeDirectory(caseSettings)==game && File.Exists(Path.Combine(game,"d3d9.dll")),"case-only game path update reuses the owned runtime");
            await svc.RestoreAsync(caseSettings);
            Check(!File.Exists(Path.Combine(game,"d3d9.dll")) && Directory.Exists(Path.Combine(game,"players")),"case-only game path removal keeps the owned profile");
            await svc.InstallAsync(Settings());
            var config=Path.Combine(game,"players/config.cfg");
            LauncherService.VerifyRenderProfile(config);Check(true,"installed current renderer profile accepted");
            var renderFixture=Path.Combine(run,"render-profile-fixture.cfg");
            var renderText="seta R_CUSTOMMODE \"3840x2160\"\r\nseta r_fullscreen \"0\"\r\nseta r_aaSamples \"1\"\r\n";
            File.WriteAllText(renderFixture,renderText,Encoding.BigEndianUnicode);
            LauncherService.VerifyRenderProfile(renderFixture);Check(true,"UTF16 BE and mixed-case renderer profile accepted");
            File.WriteAllText(renderFixture,renderText.Replace("3840x2160","1920x1080"),Encoding.Unicode);
            await Refuses(()=>{LauncherService.VerifyRenderProfile(renderFixture);return Task.CompletedTask;},"native optimal-settings resolution reset refused");
            File.WriteAllText(renderFixture,renderText+"seta R_FULLSCREEN \"0\"\r\n",new UTF8Encoding(true));
            await Refuses(()=>{LauncherService.VerifyRenderProfile(renderFixture);return Task.CompletedTask;},"duplicate native renderer setting refused even with same value");
            File.WriteAllText(renderFixture,renderText.Replace("r_aaSamples \"1\"","r_aaSamples \"4\""),Encoding.Latin1);
            await Refuses(()=>{LauncherService.VerifyRenderProfile(renderFixture);return Task.CompletedTask;},"multisample native renderer profile refused");
            File.Copy(config,Path.Combine(run,"imported-config-before-test.cfg"));
            var text="// synthetic private UTF16 preservation fixture\r\nbind F \"+activate\"\r\nseta name \"Fran\u00e7ois\"\r\nseta sv_lastSaveGame \"save\\my-campaign.svg\"\r\nseta r_fullscreen \"1\"\r\n";
            File.WriteAllText(config,text,Encoding.Unicode);
            var save=Path.Combine(game,"players/save/launcher-preservation-test.svg");Directory.CreateDirectory(Path.GetDirectoryName(save)!);File.WriteAllBytes(save,[0,255,8,7,3,9]);var saveHash=Hash(save);
            await svc.InstallAsync(Settings());
            var bytes=File.ReadAllBytes(config);var decoded=File.ReadAllText(config);
            Check(bytes.Length>2 && bytes[0]==255 && bytes[1]==254,"actual update preserves UTF16 config encoding");
            Check(decoded.Contains("Fran\u00e7ois") && decoded.Contains("bind F \"+activate\"") && decoded.Contains("save\\my-campaign.svg"),"actual update preserves non-ASCII/bind/save pointer");
            Check(Hash(save)==saveHash,"actual update preserves independent VR save bytes");
            var configHash=Hash(config); await svc.RestoreAsync(Settings());
            Check(!File.Exists(Path.Combine(game,"d3d9.dll")) && !File.Exists(Path.Combine(game,"installed.json")),"actual remove deletes only active mod and installed marker");
            Check(Hash(config)==configHash && Hash(save)==saveHash && File.Exists(Path.Combine(game,"BlackOps.exe")) && Directory.Exists(Path.Combine(game,"zone")),"actual removal retains profile/game/assets");
            await svc.InstallAsync(Settings());Check(Hash(save)==saveHash,"actual reinstall retains VR progress instead of recopying Steam profile");

            var unowned=Service("unowned");var unownedGame=unowned.RuntimeDirectory(Settings());Directory.CreateDirectory(unownedGame);var keep=Path.Combine(unownedGame,"unrecognized.txt");File.WriteAllText(keep,"retain");
            await Refuses(()=>unowned.InstallAsync(Settings()),"unowned populated runtime refused");Check(File.ReadAllText(keep)=="retain" && !File.Exists(Path.Combine(unownedGame,"vr-owner.json")),"unowned contents and missing owner preserved");
            var partial=Service("partial-import");var pg=partial.RuntimeDirectory(Settings());var pp=Path.Combine(pg,"players");Directory.CreateDirectory(pp);
            Json(Path.Combine(pg,"vr-owner.json"),new{game=Steam});var partialFile=Path.Combine(pp,"partial-save.svg");File.WriteAllText(partialFile,"retained interrupted-copy fixture");
            await Refuses(()=>partial.InstallAsync(Settings()),"interrupted unmarked profile refuses false completion");
            Check(!File.Exists(Path.Combine(pg,"profile-imported.json")) && File.ReadAllText(partialFile)=="retained interrupted-copy fixture","partial profile retained without claiming completed import");
            var owned=Service("wrong-owner");await owned.InstallAsync(Settings());var og=owned.RuntimeDirectory(Settings());Json(Path.Combine(og,"vr-owner.json"),new{game="C:\\another-game"});
            await Refuses(()=>owned.InstallAsync(Settings()),"mismatched ownership marker refuses install");
            await Refuses(()=>owned.RestoreAsync(Settings()),"mismatched ownership marker refuses removal");
            Check(File.Exists(Path.Combine(og,"d3d9.dll")),"wrong-owner removal leaves DLL untouched");

            var foreign=Service("foreign-dll");await foreign.InstallAsync(Settings());var fg=foreign.RuntimeDirectory(Settings());var fd=Path.Combine(fg,"d3d9.dll");File.AppendAllText(fd,"foreign replacement test");var foreignHash=Hash(fd);
            await Refuses(()=>foreign.RestoreAsync(Settings()),"changed DLL refuses removal");
            await Refuses(()=>foreign.InstallAsync(Settings()),"changed DLL refuses install overwrite");Check(Hash(fd)==foreignHash,"changed DLL remains intact after refused install");

            var zone=Path.Combine(game,"zone");UnlinkOwnedZone(zone);var wrong=Path.Combine(run,"unrelated-assets");Directory.CreateDirectory(wrong);File.WriteAllText(Path.Combine(wrong,"keep.txt"),"retain");Link(zone,wrong);
            await Refuses(()=>svc.InstallAsync(Settings()),"wrong asset junction refused");
            Check(new DirectoryInfo(zone).ResolveLinkTarget(true)?.FullName==wrong && File.ReadAllText(Path.Combine(wrong,"keep.txt"))=="retain","wrong junction and target left intact");
        }
        catch(Exception e) { ++failures;Console.WriteLine("HARNESS ERROR "+e);results.Add(new{error=e.ToString(),passed=false}); }
        finally
        {
            var after=SnapshotRetail();Json(Path.Combine(run,"retail-after.json"),after);
            Check(before.Count==after.Count && before.All(k=>after.GetValueOrDefault(k.Key)==k.Value),"retail root files and entire original players inventory unchanged");
            Json(Path.Combine(run,"results.json"),new{checks,failures,gameLaunched=false,retailWrites=0,testedArtifacts,run,results});
            Console.WriteLine($"RESULT {checks} checks, {failures} failures; evidence {run}");
            Environment.ExitCode=failures==0?0:1;
        }
    }
}
