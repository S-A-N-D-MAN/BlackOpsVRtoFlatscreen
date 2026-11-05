# Build Black Ops VR v0.1.0-beta.4

The complete matching runtime source and vendored dependencies are in `source/runtime-source`; the launcher, Core, launcher checks and shared packaging helper are in `source/release`. Public packaging code is in `source/tools`. Native source is byte-identical to the accepted Foliage-Test2 snapshot. Only the launcher project version and Core BuildIdentity version literals changed from beta.3 to beta.4. Existing launcher behavior is unchanged; the accepted native runtime environment and mip-filter defaults are included.

Use Visual Studio 2022 C++ desktop tools and a Windows SDK, CMake 3.25+, Python 3 and .NET 8 SDK with Windows desktop support. The mod is x86; the self-contained launcher is x64. From `source`:

```powershell
cmake -S runtime-source/mod -B native-build -G "Visual Studio 17 2022" -A Win32
cmake --build native-build --config Release --parallel 4
ctest --test-dir native-build -C Release --output-on-failure
dotnet publish release/launcher/BlackOpsVR.Launcher.csproj -c Release -r win-x64 --self-contained true -o launcher-build
```

Use generator `Visual Studio 17 2022` and argument `--parallel 4` with the spaces shown by your CMake installation. The released native DLL is `6fd7edc61eca82276ff632eca6f21cc3bcca9db54af269ed5ebbe24f2f4b41d1`, with 178 configured tests. Fresh launcher validation passed 139 checks; one multiplayer executable identity fixture was skipped because the MP executable was absent. BUILD-INFO.json states the validation limits.

Retain every published runtime file next to BlackOpsVR.exe. Deployment uses ordinary adjacent .NET 8.0.31 runtime files, without extracting a single-file bundle. Compiler/SDK versions and paths may affect byte identity; another PC's rebuild is not claimed to reproduce the exact release hash.

Launcher setup checks require your legitimate Steam game path and a private fixture output inside the source workspace; they never launch the game or write retail files. Optional native fixture tests may require locally captured data from your own game. No proprietary captured assets, game files, saves, logs or machine settings are distributed.

Source inventories and SHA256 manifests bind this snapshot to the package. Original notices are retained; see LICENSE.txt, THIRD-PARTY-NOTICES.txt and licenses/.
