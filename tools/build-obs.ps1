param([ValidateSet('Debug','Release')][string]$Configuration='Release',
  [ValidatePattern('^[a-zA-Z0-9_-]{0,50}$')][string]$BuildSuffix='',
  [switch]$EnableTestHooks)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& "$PSScriptRoot/prepare-obs.ps1"
if($LASTEXITCODE){exit $LASTEXITCODE}
$deps=Join-Path $repo 'work/validation-tools'
$llvm=(Get-ChildItem "$deps/llvm" -Directory | Select-Object -First 1).FullName.Replace('\','/')
$cmake=(Get-ChildItem "$deps/cmake" -Directory | Select-Object -First 1).FullName
$imgui=(Get-ChildItem "$deps/imgui-1.92.8" -Directory | Select-Object -First 1).FullName
$sdk=(Get-ChildItem "$deps/winsdk/c/Include" -Directory | Select-Object -First 1).FullName
$previousPath=$env:PATH
try {
  $env:PATH="$llvm/bin;$cmake/bin;$deps/ninja;$env:PATH"
  $build=Join-Path $repo ("build-obs-$Configuration" + $(if($BuildSuffix){"-$BuildSuffix"}else{''}))
  & cmake -S $repo -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_CXX_COMPILER=$llvm/bin/clang++.exe" "-DCMAKE_RC_COMPILER=$llvm/bin/llvm-windres.exe" "-DCMAKE_CXX_FLAGS=-isystem $($sdk.Replace('\','/'))/cppwinrt" "-Dimgui_DIR=$repo/cmake/validation-imgui" "-DKLIP_IMGUI_SOURCE=$imgui" '-DKLIP_USE_LIBOBS=ON' "-DKLIP_ENABLE_TEST_HOOKS=$($EnableTestHooks.IsPresent.ToString().ToUpperInvariant())" "-DOBS_ROOT=$repo/work/obs-sdk-32.1.2"
  if($LASTEXITCODE){exit $LASTEXITCODE}
  & cmake --build $build --parallel 4
  if($LASTEXITCODE){exit $LASTEXITCODE}
  Copy-Item "$llvm/bin/libc++.dll","$llvm/bin/libunwind.dll" "$build/bin/64bit/" -Force
  Copy-Item "$llvm/bin/libc++.dll","$llvm/bin/libunwind.dll" $build -Force
  & ctest --test-dir $build --output-on-failure
  exit $LASTEXITCODE
} finally {$env:PATH=$previousPath}
