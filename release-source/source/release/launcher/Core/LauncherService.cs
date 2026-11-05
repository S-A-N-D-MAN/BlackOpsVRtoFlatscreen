// SPDX-License-Identifier: GPL-3.0-only
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Runtime.InteropServices;
using Microsoft.Win32;
namespace BlackOpsVR.Launcher;

public sealed record SteamSourceIdentity(GameIdentityResult Executable,string BuildId,string SpDepotManifest);

public sealed class LauncherService
{
    public const string SupportedGameHash = GameIdentity.ReferenceSha256;
    public const string SupportedBuildId = "87954";
    public const string SupportedSpDepotManifest = "5564723134361260769";
    readonly string package, data;
    readonly SemaphoreSlim operation = new(1,1);
    static readonly JsonSerializerOptions Json = new() { WriteIndented=true, PropertyNameCaseInsensitive=true };
    static readonly string[] GameFiles = ["BlackOps.exe", "binkw32.dll", "steam_api.dll", "localization.txt", "cod.bmp", "codlogo.bmp"];
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool CreateHardLink(string name, string existing, IntPtr security);
    public LauncherService(string? packageDirectory=null, string? dataDirectory=null)
    {
        package = Path.GetFullPath(packageDirectory ?? AppContext.BaseDirectory);
        data = Path.GetFullPath(dataDirectory ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BlackOpsVR"));
    }
    public LauncherSettings LoadSettings()
    {
        var path=Path.Combine(data,"settings.json");
        if (!File.Exists(path)) return new() { GamePath=DetectGamePath() };
        try { var s=JsonSerializer.Deserialize<LauncherSettings>(File.ReadAllText(path),Json) ?? new(); s.Validate(); return s; }
        catch (JsonException) { throw new IOException("The saved launcher settings could not be read. Your saves are unchanged. Rename settings.json in the BlackOpsVR app-data folder to reset the options."); }
    }
    public void SaveSettings(LauncherSettings settings)
    {
        settings.Validate(); Directory.CreateDirectory(data); SafeLocal(data);
        WriteJson(Path.Combine(data,"settings.json"),settings);
    }
    public string DetectGamePath()
    {
        var candidates=new List<string>();
        foreach(var hive in new[] {RegistryHive.LocalMachine,RegistryHive.CurrentUser})
        foreach(var view in new[] {RegistryView.Registry32,RegistryView.Registry64})
        {
            using var key=RegistryKey.OpenBaseKey(hive,view).OpenSubKey(@"SOFTWARE\Valve\Steam");
            if (key?.GetValue("InstallPath") is string path) candidates.Add(path);
            if (key?.GetValue("SteamPath") is string steamPath) candidates.Add(steamPath.Replace('/','\\'));
        }
        candidates.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"Steam"));
        foreach(var steam in candidates.Distinct(StringComparer.OrdinalIgnoreCase).ToArray())
        {
            var libraries=Path.Combine(steam,"steamapps","libraryfolders.vdf");
            if (!File.Exists(libraries)) continue;
            foreach(Match m in Regex.Matches(File.ReadAllText(libraries),"\"path\"\\s+\"([^\"]+)\"")) candidates.Add(m.Groups[1].Value.Replace(@"\\",@"\"));
        }
        var games=candidates.Select(GameFromSteamRoot).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        return games.FirstOrDefault(IsSupportedSteamSource)
            ?? games.FirstOrDefault(x=>File.Exists(Path.Combine(x,"BlackOps.exe"))) ?? "";
    }
    static string GameFromSteamRoot(string root)
    {
        var manifest=Path.Combine(root,"steamapps","appmanifest_42700.acf");
        if(File.Exists(manifest))
        {
            var text=File.ReadAllText(manifest);
            var match=Regex.Match(text,"\\\"installdir\\\"\\s+\\\"([^\\\"]+)\\\"",RegexOptions.IgnoreCase);
            if(match.Success)return Path.Combine(root,"steamapps","common",match.Groups[1].Value);
        }
        return Path.Combine(root,"steamapps","common","Call of Duty Black Ops");
    }
    static bool IsSupportedSteamSource(string game)
    {
        try { ValidateSteamSource(game);return true; } catch { return false; }
    }
    public string RuntimeDirectory(LauncherSettings s)
    {
        var game=GamePath(s);
        var key=Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(game.ToUpperInvariant())))[..16].ToLowerInvariant();
        return Path.Combine(data,"runtimes",key,"game");
    }
    static string GamePath(LauncherSettings s)
    {
        s.Validate();
        if (string.IsNullOrWhiteSpace(s.GamePath)) throw new IOException("Select the folder containing BlackOps.exe.");
        var path=Path.GetFullPath(s.GamePath).TrimEnd('\\','/');
        if (path.StartsWith(@"\\") || !Path.IsPathFullyQualified(path)) throw new IOException("Select a game installed on a local Windows drive.");
        return path;
    }
    public static string Sha(string path) { using var f=File.OpenRead(path); return Convert.ToHexString(SHA256.HashData(f)).ToLowerInvariant(); }
    public static void ValidateGame(string game)
    {
        var exe=Path.Combine(game,"BlackOps.exe");
        if (!File.Exists(exe)) throw new IOException("Select the Steam game folder containing BlackOps.exe (single player).");
        var identity=GameIdentity.Inspect(exe);
        if (!identity.Accepted) throw new IOException("Unsupported Black Ops single-player executable. Setup stopped before changing game or save files. Selected folder: "+game+". Detected SHA-256: "+(identity.Sha256.Length==64?identity.Sha256:"unavailable")+". Reason: "+identity.Reason+". Verify the game in Steam. Do not rename BlackOpsMP.exe or download a replacement executable. If Steam keeps this file, send this message with your Steam language/region so the build can be reviewed.");
        foreach(var name in new[] {"binkw32.dll","steam_api.dll","localization.txt"})
            if (!File.Exists(Path.Combine(game,name))) throw new IOException("The game installation is missing "+name+". Verify its files in Steam.");
        if (!Directory.Exists(Path.Combine(game,"zone")) || !Directory.Exists(Path.Combine(game,"main"))) throw new IOException("The main and zone game folders are required.");
        if (!Directory.EnumerateFiles(Path.Combine(game,"main"),"iw_*.iwd").Any()) throw new IOException("The game data is missing. Finish installing the game in Steam first.");
    }
    static string VdfValue(string text,string key)
    {
        var match=Regex.Match(text,"\\\""+Regex.Escape(key)+"\\\"\\s+\\\"([^\\\"]+)\\\"",RegexOptions.IgnoreCase);
        return match.Success?match.Groups[1].Value:"";
    }
    public static SteamSourceIdentity ValidateSteamSource(string game)
    {
        game=Path.GetFullPath(game).TrimEnd('\\','/');ValidateGame(game);
        var manifest=Path.GetFullPath(Path.Combine(game,"..","..","appmanifest_42700.acf"));
        if(!File.Exists(manifest))throw new IOException("The selected folder is not registered as the Steam installation for App ID 42700. Select Call of Duty Black Ops from a Steam library.");
        var text=File.ReadAllText(manifest);
        var appid=VdfValue(text,"appid");var build=VdfValue(text,"buildid");var install=VdfValue(text,"installdir");
        var depotBlock=Regex.Match(text,"\\\"42701\\\"\\s*\\{(?<body>.*?)\\}",RegexOptions.IgnoreCase|RegexOptions.Singleline);
        var depot=depotBlock.Success?VdfValue(depotBlock.Groups["body"].Value,"manifest"):"";
        if(appid!="42700"||build!=SupportedBuildId||depot!=SupportedSpDepotManifest||
            !Path.GetFileName(game).Equals(install,StringComparison.OrdinalIgnoreCase))
            throw new IOException("The selected Steam installation is not the supported standard/international (ROW) Black Ops single-player depot (App 42700, depot 42701, build "+SupportedBuildId+"). The separate German low-violence depot 42714 is not yet verified. Verify the game in Steam, select its main folder, then use Copy compatibility report if this continues.");
        return new(InspectGame(game),build,depot);
    }
    public static GameIdentityResult InspectGame(string game)=>GameIdentity.Inspect(Path.Combine(Path.GetFullPath(game),"BlackOps.exe"));
    static string ShareablePath(string path)
    {
        var full=Path.GetFullPath(path);
        var home=Environment.GetFolderPath(Environment.SpecialFolder.UserProfile).TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar);
        if (home.Length>0 && (full.Equals(home,StringComparison.OrdinalIgnoreCase)||
            full.StartsWith(home+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)))
            return "%USERPROFILE%"+full[home.Length..];
        return full;
    }
    public string CompatibilityReport(LauncherSettings settings)
    {
        var game=GamePath(settings);var identity=InspectGame(game);
        string steam;
        try { var source=ValidateSteamSource(game);steam="Steam build: "+source.BuildId+"; SP depot manifest: "+source.SpDepotManifest; }
        catch(Exception e) { steam="Steam registration: "+e.Message; }
        return "Black Ops VR v"+BuildIdentity.Version+" compatibility report\r\n"+
            "Selected folder: "+ShareablePath(game)+"\r\n"+
            "BlackOps.exe bytes: "+identity.Bytes+"\r\n"+
            "BlackOps.exe SHA-256: "+(identity.Sha256.Length==64?identity.Sha256:"unavailable")+"\r\n"+
            "Accepted: "+identity.Accepted+"\r\n"+
            "Reason: "+identity.Reason+"\r\n"+
            "Signer: "+(identity.Signer.Length>0?identity.Signer:"unavailable")+"\r\n"+
            steam+"\r\n";
    }
    Dictionary<string,string> VerifyPayload()
    {
        var dir=Path.Combine(package,"payload");
        var manifest=JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(Path.Combine(dir,"manifest.json")),Json) ?? throw new IOException("The release manifest is missing.");
        foreach(var required in new[] {"d3d9.dll","runtime-env.json","config-default.cfg"}) if (!manifest.ContainsKey(required)) throw new IOException("The release package is incomplete. Extract the complete ZIP again.");
        foreach(var row in manifest)
        {
            if (Path.GetFileName(row.Key)!=row.Key || row.Key.Contains(':') || !Regex.IsMatch(row.Value,"^[0-9a-f]{64}$")) throw new IOException("Invalid release manifest.");
            var file=Path.Combine(dir,row.Key);
            if (!File.Exists(file) || Sha(file)!=row.Value) throw new IOException("The release file "+row.Key+" is missing or changed. Extract the complete ZIP again.");
        }
        return manifest;
    }
    void SafeLocal(string path)
    {
        path=Path.GetFullPath(path);
        if (!path.Equals(data,StringComparison.OrdinalIgnoreCase) && !path.StartsWith(data+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)) throw new IOException("The VR setup path is outside its own data folder.");
        for(var current=path; current.Length>=data.Length; current=Path.GetDirectoryName(current) ?? "")
        {
            if ((File.Exists(current)||Directory.Exists(current)) && (File.GetAttributes(current)&FileAttributes.ReparsePoint)!=0) throw new IOException("The VR writable folder contains a redirected path. Choose a normal local app-data folder.");
            if (current.Equals(data,StringComparison.OrdinalIgnoreCase)) break;
        }
    }
    static void Closed()
    {
        foreach(var name in new[] {"BlackOps","BlackOpsMP"})
            foreach(var process in Process.GetProcessesByName(name)) { process.Dispose(); throw new IOException("Close Black Ops normally before installing, removing or starting VR."); }
    }
    static void Atomic(string path,byte[] bytes)
    {
        var temp=path+".bovr-"+Guid.NewGuid().ToString("N");
        File.WriteAllBytes(temp,bytes); File.Move(temp,path,true);
    }
    static void WriteJson<T>(string path,T value)=>Atomic(path,Encoding.UTF8.GetBytes(JsonSerializer.Serialize(value,Json)+"\n"));
    static void CopyProfile(string source,string destination)
    {
        Directory.CreateDirectory(destination);
        if (!Directory.Exists(source)) return;
        foreach(var path in Directory.EnumerateFileSystemEntries(source))
        {
            var attrs=File.GetAttributes(path);
            if ((attrs&FileAttributes.ReparsePoint)!=0) continue;
            var target=Path.Combine(destination,Path.GetFileName(path));
            if ((attrs&FileAttributes.Directory)!=0) CopyProfile(path,target);
            else File.Copy(path,target,false);
        }
    }
    public static string ApplyConfig(string original,string defaults)
    {
        // Replace only explicitly listed VR settings; keep the user's bindings and progress.
        foreach(var line in defaults.Split('\n'))
        {
            var match=Regex.Match(line.Trim(),"^seta ([A-Za-z0-9_]+) \"([^\"\\r\\n]*)\"$");
            if (!match.Success) continue;
            var name=match.Groups[1].Value;
            var replacement=match.Value;
            if(name.Equals("mis_01",StringComparison.OrdinalIgnoreCase))
            {
                // Keep earned progress above the release minimum, including duplicate legacy entries.
                var mission=CampaignMissionUnlock(original);
                replacement="seta mis_01 \""+mission+"\"";
            }
            original=Regex.Replace(original,@"(?m)^[ \t]*seta[ \t]+"+Regex.Escape(name)+"[ \\t]+[^\\r\\n]*(?:\\r?\\n|$)","",RegexOptions.IgnoreCase);
            original=original.TrimEnd()+"\r\n"+replacement+"\r\n";
        }
        return original;
    }
    public static int CampaignMissionUnlock(string config)
    {
        var unlocked=20;
        foreach(Match match in Regex.Matches(config,@"(?im)^[ \t]*seta[ \t]+mis_01[ \t]+""(\d+)""[ \t]*\r?$"))
            if(int.TryParse(match.Groups[1].Value,out var earned)) unlocked=Math.Max(unlocked,earned);
        return unlocked;
    }
    static (string Text,Encoding Encoding,int Prefix) DecodeConfig(byte[] original)
    {
        Encoding encoding=Encoding.Latin1; int prefix=0;
        if(original.AsSpan().StartsWith(new byte[]{0xFF,0xFE})){encoding=Encoding.Unicode;prefix=2;}
        else if(original.AsSpan().StartsWith(new byte[]{0xFE,0xFF})){encoding=Encoding.BigEndianUnicode;prefix=2;}
        else if(original.AsSpan().StartsWith(new byte[]{0xEF,0xBB,0xBF})){encoding=new UTF8Encoding(true);prefix=3;}
        else { try { new UTF8Encoding(false,true).GetString(original);encoding=new UTF8Encoding(false); } catch(DecoderFallbackException) {} }
        return (encoding.GetString(original,prefix,original.Length-prefix),encoding,prefix);
    }
    static byte[] OverlayConfig(byte[] original,string defaults)
    {
        var value=DecodeConfig(original);
        var bytes=value.Encoding.GetBytes(ApplyConfig(value.Text,defaults));
        return value.Prefix>0?value.Encoding.GetPreamble().Concat(bytes).ToArray():bytes;
    }
    public static void VerifyRenderProfile(string path,string resolution="3840x2160")
    {
        new LauncherSettings { RenderResolution=resolution }.Validate();
        var text=DecodeConfig(File.ReadAllBytes(path)).Text;
        foreach(var row in new Dictionary<string,string>{{"r_customMode",resolution},{"r_fullscreen","0"},{"r_aaSamples","1"}})
        {
            var matches=Regex.Matches(text,@"(?im)^[ \t]*seta[ \t]+"+row.Key+"[ \\t]+\"([^\"\\r\\n]*)\"[ \\t]*\\r?$");
            if(matches.Count!=1 || matches[0].Groups[1].Value!=row.Value)
                throw new IOException("Select Install / Update to apply the selected VR resolution and display settings. Your saves and other controls will be kept.");
        }
    }
    void CheckOwner(string game,string source)
    {
        var owner=Path.Combine(game,"vr-owner.json");SafeLocal(owner);
        if (!File.Exists(owner)) throw new IOException("The VR setup ownership record is missing. Nothing was changed.");
        var prior=JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(owner),Json);
        if (!string.Equals(prior?.GetValueOrDefault("game"),source,StringComparison.OrdinalIgnoreCase)) throw new IOException("The VR setup belongs to a different game folder.");
    }
    void CheckInstalledDll(string game)
    {
        var dll=Path.Combine(game,"d3d9.dll");var marker=Path.Combine(game,"installed.json");SafeLocal(dll);SafeLocal(marker);
        if(!File.Exists(dll)) return;
        if(!File.Exists(marker)) throw new IOException("An unrecognized VR DLL is present. Nothing was overwritten.");
        using var doc=JsonDocument.Parse(File.ReadAllText(marker));
        if(Sha(dll)!=doc.RootElement.GetProperty("dllSha256").GetString()) throw new IOException("The VR DLL was changed outside this launcher. Nothing was overwritten.");
    }
    static void ValidatePrivateRuntime(string game,string source)
    {
        var marker=Path.Combine(game,"installed.json");
        using var document=JsonDocument.Parse(File.ReadAllText(marker));var root=document.RootElement;
        if(!root.TryGetProperty("sourceExeSha256",out var sourceHashValue)||
            !root.TryGetProperty("steamBuildId",out var buildValue)||
            !root.TryGetProperty("steamSpDepotManifest",out var depotValue))
            throw new IOException("This VR setup was created by an older launcher. Close Black Ops and select Install / Update; your separate VR saves will be kept.");
        var expected=sourceHashValue.GetString()??"";var identity=InspectGame(game);
        if(!identity.Accepted||identity.Sha256!=expected)
            throw new IOException("The separate VR copy of BlackOps.exe no longer matches the selected Steam installation. Close Black Ops and select Install / Update to refresh it; your separate VR saves will be kept.");
        ValidateGame(game);
        var current=ValidateSteamSource(source);
        if(current.Executable.Sha256!=expected||buildValue.GetString()!=current.BuildId||depotValue.GetString()!=current.SpDepotManifest)
            throw new IOException("Steam has refreshed the selected Black Ops installation. Close Black Ops and select Install / Update to refresh the separate VR copy; your VR saves will be kept.");
    }
    void Prepare(LauncherSettings s)
    {
        Closed(); var source=GamePath(s); var sourceIdentity=ValidateSteamSource(source); var payload=VerifyPayload();
        var game=RuntimeDirectory(s); SafeLocal(game); Directory.CreateDirectory(game);
        var owner=Path.Combine(game,"vr-owner.json");SafeLocal(owner);
        if (File.Exists(owner))
        {
            CheckOwner(game,source);
        }
        else
        {
            if (Directory.EnumerateFileSystemEntries(game).Any()) throw new IOException("The VR setup folder contains unrecognized files. Nothing was overwritten.");
            WriteJson(owner,new Dictionary<string,string>{{"game",source}});
        }
        CheckInstalledDll(game);
        foreach(var name in new[]{"steam_appid.txt","installed.json","config-before-vr.cfg","profile-imported.json"}) SafeLocal(Path.Combine(game,name));
        foreach(var name in GameFiles)
        {
            var src=Path.Combine(source,name); if (!File.Exists(src)) continue;
            var dest=Path.Combine(game,name); SafeLocal(dest); Atomic(dest,File.ReadAllBytes(src));
        }
        // The writable main folder is private, so native console logs cannot reach Steam.
        var main=Path.Combine(game,"main"); SafeLocal(main); Directory.CreateDirectory(main);
        foreach(var src in Directory.EnumerateFiles(Path.Combine(source,"main"),"*.iwd"))
        {
            var name=Path.GetFileName(src);
            if (!Regex.IsMatch(name,@"^(iw_\d+|localized_[A-Za-z]+_iw\d+)\.iwd$",RegexOptions.IgnoreCase)) continue;
            var dest=Path.Combine(main,name); SafeLocal(dest);
            if (File.Exists(dest))
            {
                var a=new FileInfo(src); var b=new FileInfo(dest);
                if(a.Length==b.Length && a.LastWriteTimeUtc==b.LastWriteTimeUtc) continue;
                // This owned destination is an asset link/copy, never the selected game path.
                File.Delete(dest);
            }
            if (!CreateHardLink(dest,src,IntPtr.Zero)) File.Copy(src,dest,false);
        }
        Junction.Ensure(Path.Combine(game,"zone"),Path.Combine(source,"zone"));
        if (Directory.Exists(Path.Combine(source,"main","video"))) Junction.Ensure(Path.Combine(main,"video"),Path.Combine(source,"main","video"));
        var players=Path.Combine(game,"players"); SafeLocal(players);
        var imported=Path.Combine(game,"profile-imported.json");
        if (!File.Exists(imported))
        {
            if (Directory.Exists(players)) throw new IOException("A previous save import was interrupted. Your Steam saves are unchanged. Preserve the players folder in the separate VR setup before retrying setup; do not delete your Steam players folder.");
            var staging=Path.Combine(game,"players-import-"+Guid.NewGuid().ToString("N"));SafeLocal(staging);
            CopyProfile(Path.Combine(source,"players"),staging);
            // Both resolved endpoints are owned local paths, and this move never replaces a profile.
            SafeLocal(staging);SafeLocal(players);Directory.Move(staging,players);
            WriteJson(imported,new { utc=DateTimeOffset.UtcNow,source=Path.Combine(source,"players") });
        }
        Directory.CreateDirectory(players);
        var config=Path.Combine(players,"config.cfg"); SafeLocal(config);
        var original=File.Exists(config)?File.ReadAllBytes(config):Encoding.ASCII.GetBytes("// Black Ops VR local profile\r\n");
        if (File.Exists(config) && !File.Exists(Path.Combine(game,"config-before-vr.cfg"))) Atomic(Path.Combine(game,"config-before-vr.cfg"),File.ReadAllBytes(config));
        var defaults=ApplyConfig(File.ReadAllText(Path.Combine(package,"payload","config-default.cfg")),
            "seta r_customMode \""+s.RenderResolution+"\"\n");
        Atomic(config,OverlayConfig(original,defaults));
        Atomic(Path.Combine(game,"steam_appid.txt"),Encoding.ASCII.GetBytes("42700\n"));
        Atomic(Path.Combine(game,"d3d9.dll"),File.ReadAllBytes(Path.Combine(package,"payload","d3d9.dll")));
        WriteJson(Path.Combine(game,"installed.json"),new { version=BuildIdentity.Version,game=source,dllSha256=payload["d3d9.dll"],sourceExeSha256=sourceIdentity.Executable.Sha256,steamBuildId=sourceIdentity.BuildId,steamSpDepotManifest=sourceIdentity.SpDepotManifest,utc=DateTimeOffset.UtcNow });
    }
    public async Task<string> InstallAsync(LauncherSettings s)
    {
        if(!await operation.WaitAsync(0)) throw new IOException("A VR operation is already running.");
        try { await Task.Run(()=>Prepare(s)); SaveSettings(s); return "VR setup is ready. Connect your headset and select Launch VR. Steam game files are unchanged; VR saves are kept separately."; }
        finally { operation.Release(); }
    }
    public static string ValidateRuntime(string path)
    {
        path=Path.GetFullPath(path);
        using var document=JsonDocument.Parse(File.ReadAllText(path));
        var lib=document.RootElement.GetProperty("runtime").GetProperty("library_path").GetString() ?? "";
        if(!Path.IsPathRooted(lib)) lib=Path.Combine(Path.GetDirectoryName(path)!,lib);
        using var stream=File.OpenRead(lib); using var reader=new BinaryReader(stream);
        if(stream.Length<64 || reader.ReadUInt16()!=0x5A4D) throw new IOException("The selected OpenXR runtime is not a Windows runtime.");
        stream.Position=0x3c; var offset=reader.ReadInt32();
        if(offset<0 || offset>stream.Length-6) throw new IOException("The OpenXR runtime has invalid headers.");
        stream.Position=offset;
        if(reader.ReadUInt32()!=0x4550 || reader.ReadUInt16()!=0x14c) throw new IOException("Black Ops requires a 32-bit (x86) OpenXR runtime. Choose its 32-bit runtime JSON or configure your VR software accordingly.");
        return path;
    }
    static string ResolveRuntime(LauncherSettings s)
    {
        if(!string.IsNullOrWhiteSpace(s.RuntimePath)) return ValidateRuntime(s.RuntimePath);
        foreach(var hive in new[]{RegistryHive.LocalMachine,RegistryHive.CurrentUser})
        {
            using var root=RegistryKey.OpenBaseKey(hive,RegistryView.Registry32);
            using var key=root.OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1");
            if(key?.GetValue("ActiveRuntime") is string path && !string.IsNullOrWhiteSpace(path)) return ValidateRuntime(path);
        }
        throw new IOException("No 32-bit OpenXR runtime was found. Start your headset connection software and enable its OpenXR runtime, or select its x86 runtime JSON in Advanced.");
    }
    public Dictionary<string,string> LaunchEnvironment(LauncherSettings s,string runtime,string log)
    {
        s.Validate(); VerifyPayload();
        var env=JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(Path.Combine(package,"payload","runtime-env.json")),Json) ?? throw new IOException("Missing VR launch settings.");
        env["BOVR_SMOOTH_TURN"]=s.TurnMode=="smooth"?"1":"0";
        env["BOVR_MOVEMENT_ORIENTATION"]=s.MovementOrientation;
        env["BOVR_MANUAL_RELOAD"]=s.ReloadMode=="manual"?"1":"0";
        env["BOVR_BUTTON_GRENADES"]=s.GrenadeMode=="buttons"?"1":"0";
        env["BOVR_NATIVE_RENDER_SIZE"]=s.RenderResolution;
        env["BOVR_TRACE_FILE"]=log; env["XR_RUNTIME_JSON"]=runtime;
        env["SteamAppId"]="42700"; env["SteamGameId"]="42700";
        return env;
    }
    public static Dictionary<string,string> NativeLaunchSettings(string game)
    {
        game=Path.GetFullPath(game);
        var profile=Path.Combine(game,"players","config.cfg");
        var mission=File.Exists(profile)?CampaignMissionUnlock(DecodeConfig(File.ReadAllBytes(profile)).Text):20;
        return new Dictionary<string,string>
        {
            ["fs_basepath"]=game,
            ["fs_homepath"]=Path.Combine(game,"home"),
            ["fs_userDocuments"]=Path.Combine(game,"home","documents"),
            ["r_fullscreen"]="0",
            ["r_vsync"]="0",
            ["r_multiGpu"]="0",
            ["monkeytoy"]="0",
            ["con_restricted_access"]="0",
            ["logfile"]="2",
            ["developer"]="1",
            ["com_maxfps"]="0",
            ["mis_01"]=mission.ToString(System.Globalization.CultureInfo.InvariantCulture),
            ["onlinegame"]="0"
        };
    }
    public async Task<string> LaunchAsync(LauncherSettings s)
    {
        if(!await operation.WaitAsync(0)) throw new IOException("A VR operation is already running.");
        try
        {
            Closed(); var runtime=ResolveRuntime(s); VerifyPayload();
            foreach(var name in new[]{"d3dx9_43.dll","XINPUT1_3.dll"})
                if(!File.Exists(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.SystemX86),name)))
                    throw new IOException("The game's 32-bit DirectX prerequisite "+name+" is missing. Run Black Ops single player through Steam once and let its DirectX setup complete.");
            var steam=Process.GetProcessesByName("steam");
            foreach(var steamProcess in steam) steamProcess.Dispose();
            if(steam.Length==0) throw new IOException("Start Steam and sign in to the account that owns Black Ops before launching VR.");
            var game=RuntimeDirectory(s); SafeLocal(game);
            if(!File.Exists(Path.Combine(game,"installed.json"))) throw new IOException("Select Install / Update first.");
            CheckOwner(game,GamePath(s));
            ValidatePrivateRuntime(game,GamePath(s));
            VerifyRenderProfile(Path.Combine(game,"players","config.cfg"),s.RenderResolution);
            if(Sha(Path.Combine(game,"d3d9.dll"))!=Sha(Path.Combine(package,"payload","d3d9.dll"))) throw new IOException("The installed VR build differs from this release. Select Install / Update first.");
            var logs=Path.Combine(data,"logs",DateTime.UtcNow.ToString("yyyyMMddTHHmmssfffZ")); SafeLocal(logs); Directory.CreateDirectory(logs);
            var env=LaunchEnvironment(s,runtime,Path.Combine(logs,"adapter.log"));
            var start=new ProcessStartInfo(Path.Combine(game,"BlackOps.exe")){WorkingDirectory=game,UseShellExecute=false};
            foreach(var key in start.Environment.Keys.ToArray())
                if(key.StartsWith("BOVR_",StringComparison.OrdinalIgnoreCase)||key.StartsWith("WAWVR_",StringComparison.OrdinalIgnoreCase)||key.StartsWith("SIMXR_",StringComparison.OrdinalIgnoreCase)) start.Environment.Remove(key);
            foreach(var row in env) start.Environment[row.Key]=row.Value;
            var native=NativeLaunchSettings(game);
            foreach(var row in native) { start.ArgumentList.Add("+set");start.ArgumentList.Add(row.Key);start.ArgumentList.Add(row.Value); }
            SaveSettings(s); Closed();
            using var process=Process.Start(start) ?? throw new IOException("Windows could not start Black Ops.");
            WriteJson(Path.Combine(logs,"session.json"),new{version=BuildIdentity.Version,started=DateTimeOffset.UtcNow,pid=process.Id,game,dllSha256=Sha(Path.Combine(game,"d3d9.dll")),options=s,environment=env,nativeSettings=native});
            await process.WaitForExitAsync();
            WriteJson(Path.Combine(logs,"exit.json"),new{exitCode=process.ExitCode,finished=DateTimeOffset.UtcNow});
            return process.ExitCode==0?"Black Ops has closed. Your VR progress is saved in the separate VR setup.":"Black Ops closed with code "+process.ExitCode+". Use Open logs to include the session folder in your report.";
        }
        finally { operation.Release(); }
    }
    public async Task<string> RestoreAsync(LauncherSettings s)
    {
        if(!await operation.WaitAsync(0)) throw new IOException("A VR operation is already running.");
        try
        {
            return await Task.Run(()=>{
                Closed(); var game=RuntimeDirectory(s);SafeLocal(game);var marker=Path.Combine(game,"installed.json");
                if(!File.Exists(marker)) return "No active VR setup was found. Existing saves are retained.";
                CheckOwner(game,GamePath(s));
                using var doc=JsonDocument.Parse(File.ReadAllText(marker));
                var dll=Path.Combine(game,"d3d9.dll");SafeLocal(dll);SafeLocal(marker);
                if(File.Exists(dll) && Sha(dll)!=doc.RootElement.GetProperty("dllSha256").GetString()) throw new IOException("The VR DLL was changed outside this launcher. Nothing was removed.");
                if(File.Exists(dll)) File.Delete(dll);
                File.Delete(marker);
                return "VR setup removed. All VR saves are retained for reinstalling. Steam game files were never changed.";
            });
        }
        finally { operation.Release(); }
    }
    public void OpenControls()=>Process.Start(new ProcessStartInfo(Path.Combine(package,"CONTROLS.txt")){UseShellExecute=true});
    public void OpenLogs() { var logs=Path.Combine(data,"logs");Directory.CreateDirectory(logs);Process.Start(new ProcessStartInfo("explorer.exe"){ArgumentList={logs},UseShellExecute=true}); }
}
