<#
.SYNOPSIS
    회사 MITM 프록시 환경에서 vcpkg/git/cmake의 SSL 인증서 검증 실패를 해결한다.

.DESCRIPTION
    회사 프록시는 HTTPS 트래픽을 가로채 자체 루트 인증서로 재서명한다(MITM).
    Windows/브라우저는 IT가 그룹정책으로 그 루트 인증서를 미리 신뢰 저장소에
    넣어뒀기 때문에 문제가 없지만, 이 문제는 실제로는 서로 다른 두 원인이 겹쳐서
    나타난다(2026-09-14, 실제 원격 PC 에러로 확인):

    1) curl 60 (SSL peer certificate or SSH remote key was not OK)
       - vcpkg가 내부적으로 쓰는 curl.exe(OpenSSL 계열)가 CA 인증서 체인을
         못 찾아서 나는 에러. CURL_CA_BUNDLE 환경변수를 지정해도, 그 값이
         vcpkg의 자체 curl.exe 실행 시점까지 안 전달되는 경우가 있어(예: 이미
         열려있던 셸, 다른 프로세스 컨텍스트) 환경변수만으로는 안 잡힐 수 있다.
         이를 우회하려고 curl이 항상 직접 읽는 설정 파일(_curlrc)에도 같은 값을
         박아둔다 - 환경변수 전달 여부와 무관하게 항상 적용된다.
    2) schannel: next InitializeSecurityContext failed / CRYPT_E_NO_REVOCATION_CHECK
       - 이건 CA 신뢰 문제가 아니라 "인증서 폐기(revocation) 여부 확인 실패"다.
         회사 MITM 프록시가 발급한 인증서는 진짜 CRL/OCSP 서버가 없거나 그
         서버 접근 자체가 프록시에 막혀 있어서, Windows의 Schannel(WinHTTP
         기반 - vcpkg-tool 자체의 다운로더, cmake의 file(DOWNLOAD) 등이 씀)이
         "체인은 신뢰하는데 폐기 여부를 확인할 수 없다"며 실패한다. CA 번들과는
         완전히 별개 원인이라 레지스트리로 폐기 확인 자체를 꺼야 해결된다.

    이 스크립트는 두 가지를 모두 처리한다:
      A) Windows가 이미 신뢰 중인 루트 인증서 저장소(LocalMachine + CurrentUser)를
         PEM 하나로 뽑아 CURL_CA_BUNDLE/SSL_CERT_FILE/GIT_SSL_CAINFO 환경변수와
         curl의 _curlrc 설정 파일 양쪽에 지정한다(1번 문제).
      B) HKCU 레지스트리의 "인증서 폐기 확인" 옵션을 꺼서 schannel 기반 도구가
         CRYPT_E_NO_REVOCATION_CHECK로 막히지 않게 한다(2번 문제).

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

Write-Host "[1/4] Windows 루트 인증서 저장소(LocalMachine + CurrentUser)를 PEM으로 내보내는 중..."

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
Write-Host "[2/4] curl 설정 파일(_curlrc)에도 CA 번들을 등록하는 중..."
$curlrcPath = Join-Path $env:USERPROFILE "_curlrc"
$curlrcLine = "cacert = `"$bundlePath`""
if ((Test-Path $curlrcPath) -and (Get-Content $curlrcPath -Raw) -match [regex]::Escape($curlrcLine)) {
    Write-Host "      이미 등록되어 있습니다: $curlrcPath"
} else {
    Add-Content -Path $curlrcPath -Value $curlrcLine -Encoding ascii
    Write-Host "      등록했습니다: $curlrcPath"
}

# --- B) 인증서 폐기 확인(revocation check) 끄기 - CRYPT_E_NO_REVOCATION_CHECK 대응 ---
# Internet Explorer/WinHTTP 옵션("게시자 인증서 취소 확인")과 동일한 설정이다.
# vcpkg-tool 자체 다운로더/cmake의 file(DOWNLOAD) 등 WinHTTP·Schannel 기반 도구들이
# 공통으로 참조한다. 회사 MITM 인증서는 실제 CRL/OCSP 서버가 없거나 프록시에 막혀
# 있어서, 체인 자체는 신뢰해도 이 폐기 확인 단계에서 계속 실패했었다.
Write-Host "[3/4] 인증서 폐기(revocation) 확인을 끄는 중(HKCU 레지스트리)..."
$regPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings"
Set-ItemProperty -Path $regPath -Name "CertificateRevocation" -Value 0 -Type DWord
Write-Host "      완료: $regPath\CertificateRevocation = 0"

# --- 확인 ---
Write-Host "[4/4] 요약"
Write-Host ""
Write-Host "설정 내용:"
Write-Host "  CURL_CA_BUNDLE = $bundlePath"
Write-Host "  SSL_CERT_FILE  = $bundlePath"
Write-Host "  GIT_SSL_CAINFO = $bundlePath"
Write-Host "  git http.sslBackend = schannel"
Write-Host "  $curlrcPath 에 cacert 옵션 등록"
Write-Host "  $regPath\CertificateRevocation = 0 (폐기 확인 끔)"
Write-Host ""
Write-Host "*** 지금 열려있는 터미널/VS Code/CLion 등은 전부 닫고 새로 열어야 반영됩니다. ***"
Write-Host ""
Write-Host "새 터미널에서 아래로 확인하세요:"
Write-Host '  curl.exe -I https://github.com          (인증서 에러 없이 200/301 응답이면 성공)'
Write-Host '  git ls-remote https://github.com/microsoft/vcpkg.git   (에러 없이 목록 나오면 성공)'
Write-Host ""
Write-Host "그래도 vcpkg에서만 curl 60 에러가 계속 나면, vcpkg 전용 curl.exe 위치를 찾아 알려주세요:"
Write-Host '  Get-ChildItem -Recurse -Filter "curl.exe" -Path "external\vcpkg\downloads\tools" -ErrorAction SilentlyContinue'
