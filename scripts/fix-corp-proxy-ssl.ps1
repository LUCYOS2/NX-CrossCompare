<#
.SYNOPSIS
    회사 MITM 프록시 환경에서 vcpkg/git/cmake의 SSL 인증서 검증 실패를 해결한다.

.DESCRIPTION
    회사 프록시는 HTTPS 트래픽을 가로채 자체 루트 인증서로 재서명한다(MITM).
    Windows/브라우저는 IT가 그룹정책으로 그 루트 인증서를 미리 신뢰 저장소에
    넣어뒀기 때문에 문제가 없지만, 이 문제는 실제로는 서로 다른 원인이 겹쳐서
    나타난다(2026-09-14, 실제 원격 PC 에러 3차례로 확인):

    1) curl 60 (SSL peer certificate or SSH remote key was not OK)
       - curl이 CA 인증서 체인을 못 찾아서 나는 에러. 환경변수(CURL_CA_BUNDLE
         등)가 자식 프로세스까지 전달 안 되는 경우가 있어, curl이 항상 직접
         읽는 설정 파일(_curlrc)에도 같은 값을 박아둔다.
    2) curl 35 (SSL connect error) + schannel CRYPT_E_NO_REVOCATION_CHECK
       - CA 신뢰 문제가 아니라 "인증서 폐기(revocation) 여부 확인 실패"다.
         회사 MITM 프록시가 발급한 인증서는 진짜 CRL/OCSP 서버가 없거나 그
         서버 접근 자체가 프록시에 막혀 있어서, curl의 schannel 백엔드가
         "체인은 신뢰하는데 폐기 여부를 확인할 수 없다"며 실패한다. 처음엔
         HKCU 레지스트리(Internet Settings\CertificateRevocation)로 껐는데
         이건 WinINet/IE 전용 설정이라 curl의 schannel 백엔드에는 적용되지
         않았다 - curl+schannel은 `--ssl-no-revoke`를 별도로 줘야 한다(이
         옵션은 curlrc 설정 파일에 옵션 이름만 한 줄 넣어도 동일하게 적용됨).
    3) (1차 버전 버그) curlrc의 cacert 경로가 깨져서 나온 에러
       - _curlrc(curl 설정 파일)는 큰따옴표(") 안의 백슬래시를 이스케이프
         문자로 해석한다. `cacert = "C:\Users\...\corp-ca-bundle.pem"`처럼
         큰따옴표로 감싸면 curl이 백슬래시를 전부 삼켜버려서
         "C:Users...corp-ca-bundle.pem"처럼 깨진 경로가 되어(실제 원격 PC
         에러로 확인) "파일이 없다"는 에러가 났다. 백슬래시를 아예 안 쓰는
         슬래시(/) 경로로 바꿔서 이 문제를 피한다(Windows API는 슬래시 경로도
         그대로 받아들인다).

    이 스크립트는 다음을 처리한다:
      A) Windows가 이미 신뢰 중인 루트 인증서 저장소(LocalMachine + CurrentUser)를
         PEM 하나로 뽑아 CURL_CA_BUNDLE/SSL_CERT_FILE/GIT_SSL_CAINFO 환경변수와
         curl의 _curlrc 설정 파일(슬래시 경로로, 이스케이프 문제 없이) 양쪽에
         지정한다(1, 3번 문제).
      B) _curlrc에 `ssl-no-revoke`를 등록해 curl의 schannel 백엔드가 폐기
         확인 실패로 막히지 않게 한다(2번 문제 - curl 기준).
      C) (보완책, curl이 아닌 다른 WinINet 기반 도구용으로 유지) HKCU
         레지스트리의 "인증서 폐기 확인" 옵션도 꺼둔다.

.NOTES
    - 관리자 권한 없이 실행 가능(HKCU 사용자 레지스트리 + 사용자 환경변수만 건드림).
    - 실행 후 "새 터미널/IDE를 다시 열어야" 환경변수가 반영된다(이미 열려있는
      셸에는 즉시 반영 안 됨 - Windows 환경변수 특성).
#>

$ErrorActionPreference = "Stop"

# --- A) CA 번들: Windows 루트 저장소를 PEM으로 내보내기 ---
$outDir = Join-Path $env:USERPROFILE ".corp-ca"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$bundlePath = Join-Path $outDir "corp-ca-bundle.pem"

Write-Host "[1/5] Windows 루트 인증서 저장소(LocalMachine + CurrentUser)를 PEM으로 내보내는 중..."

if (Test-Path $bundlePath) { Remove-Item $bundlePath -Force }

$stores = @("Cert:\LocalMachine\Root", "Cert:\CurrentUser\Root")
$count = 0
foreach ($store in $stores) {
    if (-not (Test-Path $store)) { continue }
    foreach ($cert in Get-ChildItem -Path $store) {
        $b64 = [Convert]::ToBase64String($cert.RawData, [System.Base64FormattingOptions]::InsertLineBreaks)
        $pem = "-----BEGIN CERTIFICATE-----`r`n$b64`r`n-----END CERTIFICATE-----`r`n"
        Add-Content -Path $bundlePath -Value $pem -Encoding ascii
        $count++
    }
}
Write-Host "      루트 인증서 $count 개를 $bundlePath 로 내보냈습니다."

# curl(vcpkg가 내부적으로 씀)/git/cmake가 공통으로 참조하는 환경변수를 지정.
[System.Environment]::SetEnvironmentVariable("CURL_CA_BUNDLE", $bundlePath, "User")
[System.Environment]::SetEnvironmentVariable("SSL_CERT_FILE", $bundlePath, "User")
[System.Environment]::SetEnvironmentVariable("GIT_SSL_CAINFO", $bundlePath, "User")

# git이 자체 CA 파일 대신 Windows 인증서 저장소를 직접 쓰게 하는 보완책(schannel).
git config --global http.sslBackend schannel

# --- A-2) 환경변수가 안 먹힐 때를 대비한 curl 자체 설정 파일(_curlrc) ---
# curl은 실행될 때마다 %USERPROFILE%\_curlrc (Windows) 를 항상 읽는다 - 이 안에
# cacert 옵션을 넣어두면, vcpkg의 자체 curl.exe를 포함해 "어떤 방식으로 호출되든"
# 환경변수 전달 여부와 무관하게 항상 이 CA 번들을 쓴다. curl 60 에러의 근본 우회책.
#
# 주의: curlrc의 큰따옴표(") 안에서는 백슬래시가 이스케이프 문자로 해석되어
# Windows 경로의 백슬래시가 통째로 사라진다(실제 원격 PC에서 확인된 버그 -
# "C:\Users\..." 가 "C:Users..."로 깨짐). 슬래시(/)로 바꿔서 이 문제를 피한다.
Write-Host "[2/5] curl 설정 파일(_curlrc)을 다시 쓰는 중(CA 번들 + 폐기 확인 끄기)..."
$curlrcPath = Join-Path $env:USERPROFILE "_curlrc"
$bundlePathForward = $bundlePath -replace '\\', '/'
$cacertLine = "cacert = `"$bundlePathForward`""
$revokeLine = "ssl-no-revoke"

$existingLines = @()
if (Test-Path $curlrcPath) {
    $existingLines = Get-Content $curlrcPath | Where-Object {
        $_ -notmatch '^\s*cacert\s*=' -and $_ -notmatch '^\s*ssl-no-revoke\s*$'
    }
}
$newLines = $existingLines + @($cacertLine, $revokeLine)
Set-Content -Path $curlrcPath -Value $newLines -Encoding ascii
Write-Host "      다시 썼습니다: $curlrcPath"
Write-Host "        cacert = $bundlePathForward"
Write-Host "        ssl-no-revoke (schannel 폐기 확인 끔)"

# --- C) 인증서 폐기 확인(revocation check) 끄기 - WinINet 기반 도구용 보완책 ---
# Internet Explorer/WinINet 옵션("게시자 인증서 취소 확인")과 동일한 설정이다.
# curl의 schannel 백엔드는 이 설정을 안 보고 위 ssl-no-revoke를 따로 봐야 하지만,
# 다른 WinINet 기반 도구(있다면)를 위해 계속 꺼둔다. 해봐서 나쁠 거 없음.
Write-Host "[3/5] 인증서 폐기(revocation) 확인을 끄는 중(HKCU 레지스트리, WinINet 보완책)..."
$regPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings"
Set-ItemProperty -Path $regPath -Name "CertificateRevocation" -Value 0 -Type DWord
Write-Host "      완료: $regPath\CertificateRevocation = 0"

# --- D) 환경변수가 실제로 반영됐는지 이 스크립트 안에서 바로 재확인 ---
Write-Host "[4/5] 방금 설정한 환경변수를 레지스트리에서 다시 읽어 확인하는 중..."
foreach ($name in @("CURL_CA_BUNDLE", "SSL_CERT_FILE", "GIT_SSL_CAINFO")) {
    $readBack = [System.Environment]::GetEnvironmentVariable($name, "User")
    if ($readBack -eq $bundlePath) {
        Write-Host "      OK: $name = $readBack"
    } else {
        Write-Host "      !! 확인 필요: $name = '$readBack' (기대값과 다름)"
    }
}

# --- 확인 ---
Write-Host "[5/5] 요약"
Write-Host ""
Write-Host "설정 내용:"
Write-Host "  CURL_CA_BUNDLE = $bundlePath"
Write-Host "  SSL_CERT_FILE  = $bundlePath"
Write-Host "  GIT_SSL_CAINFO = $bundlePath"
Write-Host "  git http.sslBackend = schannel"
Write-Host "  $curlrcPath : cacert(슬래시 경로) + ssl-no-revoke"
Write-Host "  $regPath\CertificateRevocation = 0 (WinINet 보완책)"
Write-Host ""
Write-Host "*** 지금 열려있는 터미널/VS Code/CLion 등은 전부 닫고 새로 열어야 반영됩니다. ***"
Write-Host "*** 그래도 새 터미널에서 환경변수가 비어있으면(`$env:CURL_CA_BUNDLE 이 빈 값), ***"
Write-Host "*** 로그오프/로그인(또는 재부팅)까지 해야 완전히 반영되는 경우가 있습니다.   ***"
Write-Host ""
Write-Host "새 터미널에서 아래로 확인하세요:"
Write-Host '  curl.exe -v https://github.com 2>&1 | Select-String "CAfile|SSL certificate|schannel"'
Write-Host '  git ls-remote https://github.com/microsoft/vcpkg.git   (에러 없이 목록 나오면 성공)'
Write-Host ""
Write-Host "*** 참고: vcpkg-tool.exe 자체가 (curl이 아니라) 내부 다운로더로 cmake/ninja 같은 도구를"
Write-Host "*** 처음 내려받는 단계('CMake 다운로드 실패')는 curl 설정과 무관한 별도 경로일 수 있습니다."
Write-Host "*** 그 단계에서 여전히 막히면, 이미 설치된 시스템 CMake 4.4.0을 PATH에 먼저 두어 vcpkg가"
Write-Host "*** 자체적으로 새로 받지 않고 그걸 쓰도록 유도해보세요:"
Write-Host '  $env:PATH = "C:\Program Files\CMake\bin;" + $env:PATH   # 실제 CMake 설치 경로로 바꿔서'
Write-Host "*** 그래도 안 되면 이 단계의 정확한 에러 문구를 다시 알려주세요 - 원인이 또 다를 수 있습니다."
