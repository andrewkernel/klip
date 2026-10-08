param([ValidateSet('Debug','Release')][string]$Configuration='Release', [string]$SourceRoot='', [bool]$EnableTestHooks=$true)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $SourceRoot) { $SourceRoot=$repo }
$deps=Join-Path $repo 'work\validation-tools'
$llvm=(Get-ChildItem "$deps\llvm" -Directory | Select-Object -First 1).FullName
$cmakeRoot=(Get-ChildItem "$deps\cmake" -Directory | Select-Object -First 1).FullName
$ffmpeg=(Get-ChildItem "$deps\ffmpeg-8.1.2" -Directory | Select-Object -First 1).FullName
$imgui=(Get-ChildItem "$deps\imgui-1.92.8" -Directory | Select-Object -First 1).FullName
$sdk=(Get-ChildItem "$deps\winsdk\c\Include" -Directory | Select-Object -First 1).FullName
$llvm=$llvm.Replace('\','/')
$env:PATH="$llvm\bin;$cmakeRoot\bin;$deps\ninja;$env:PATH"
$build=Join-Path $SourceRoot "build-validation-$Configuration"
$winrt=($sdk+'\cppwinrt').Replace('\','/')
$testHooks=if($EnableTestHooks){'ON'}else{'OFF'}
& cmake -S $SourceRoot -B $build -G Ninja `
  "-DCMAKE_BUILD_TYPE=$Configuration" `
  "-DCMAKE_CXX_COMPILER=$llvm/bin/clang++.exe" `
  "-DCMAKE_RC_COMPILER=$llvm/bin/llvm-windres.exe" `
  "-DCMAKE_CXX_FLAGS=-isystem $winrt" `
  "-DFFMPEG_ROOT=$ffmpeg" '-DKLIP_USE_LIBOBS=OFF' "-Dimgui_DIR=$repo/cmake/validation-imgui" `
  "-DKLIP_IMGUI_SOURCE=$imgui" "-DKLIP_ENABLE_TEST_HOOKS=$testHooks"
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& cmake --build $build --parallel 4
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
Copy-Item "$llvm\bin\libc++.dll" $build -Force
Copy-Item "$llvm\bin\libunwind.dll" $build -Force
& ctest --test-dir $build --output-on-failure
exit $LASTEXITCODE
