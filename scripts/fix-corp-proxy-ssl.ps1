<#
.SYNOPSIS
    회사 MITM 프록시 환경에서 vcpkg/git/cmake의 SSL 인증서 검증 실패를 해결한다.

.DESCRIPTION
    회사 프록시는 HTTPS 트래픽을 가로채 자체 루트 인증서로 재서명한다(MITM).
    Windows/브라우저는 IT가 그룹정책으로 그 루트 인증서를 미리 신뢰 저장소에
    넣어뒀기 때문에 문제가 없지만, curl.exe(vcpkg가 내부적으로 씀)/git/cmake는
    자체 CA 번들(OpenSSL 계열)을 써서 이 회사 루트 인증서를 모른 채 검증에
    실패한다.

    이 스크립트는 Windows가 이미 신뢰 중인 루트 인증서 저장소(LocalMachine +
    CurrentUser의 Root)를 통째로 PEM 파일 하나로 뽑아내고, curl/git/cmake/vcpkg가
    공통으로 참조하는 환경변수(CURL_CA_BUNDLE, SSL_CERT_FILE, GIT_SSL_CAINFO)를
    그 파일로 지정한다. 인터넷에서 별도로 뭔가를 받아올 필요가 없어(프록시 문제를
    "닭과 달걀"처럼 다시 겪지 않음) 지금 같은 상황에 안전하다.

.NOTES
    - 관리자 권한 없이 실행 가능(사용자 환경변수만 건드림).
    - 실행 후 "새 터미널/IDE를 다시 열어야" 환경변수가 반영된다(이미 열려있는
      셸에는 즉시 반영 안 됨 - Windows 환경변수 특성).
#>

$ErrorActionPreference = "Stop"

$outDir = Join-Path $env:USERPROFILE ".corp-ca"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$bundlePath = Join-Path $outDir "corp-ca-bundle.pem"

Write-Host "Windows 루트 인증서 저장소(LocalMachine + CurrentUser)를 PEM으로 내보내는 중..."

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

Write-Host "루트 인증서 $count 개를 $bundlePath 로 내보냈습니다."

# curl(vcpkg가 내부적으로 씀)/git/cmake가 공통으로 참조하는 환경변수를 지정.
[System.Environment]::SetEnvironmentVariable("CURL_CA_BUNDLE", $bundlePath, "User")
[System.Environment]::SetEnvironmentVariable("SSL_CERT_FILE", $bundlePath, "User")
[System.Environment]::SetEnvironmentVariable("GIT_SSL_CAINFO", $bundlePath, "User")

# git이 자체 CA 파일 대신 Windows 인증서 저장소를 직접 쓰게 하는 보완책(schannel).
# 이미 Windows가 회사 루트 인증서를 신뢰하므로 git도 별도 PEM 없이 바로 통과된다.
git config --global http.sslBackend schannel

Write-Host ""
Write-Host "완료. 환경변수 설정:"
Write-Host "  CURL_CA_BUNDLE = $bundlePath"
Write-Host "  SSL_CERT_FILE  = $bundlePath"
Write-Host "  GIT_SSL_CAINFO = $bundlePath"
Write-Host "  git http.sslBackend = schannel"
Write-Host ""
Write-Host "*** 지금 열려있는 터미널/VS Code/CLion 등은 전부 닫고 새로 열어야 환경변수가 반영됩니다. ***"
Write-Host ""
Write-Host "새 터미널에서 아래로 확인하세요:"
Write-Host '  curl.exe -I https://github.com          (인증서 에러 없이 200/301 응답이면 성공)'
Write-Host '  git ls-remote https://github.com/microsoft/vcpkg.git   (에러 없이 목록 나오면 성공)'
