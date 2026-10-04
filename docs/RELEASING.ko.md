# 배포 절차

현재 후보는 **0.0.1068 미리보기, Apple Silicon macOS 26 이상**입니다.
VS Code는 1.100 이상이 필요합니다. 상세 설정은 [영문 배포 문서](RELEASING.md),
빌드 도구와 대응 소스 재빌드는 [빌드 문서](BUILDING.md)를 참조하세요.

## 공개 전에 확정할 사항

- 루트 라이선스는 **GPL-3.0-only 초안**입니다. 프로젝트 소유자가 라이선스와 기여 코드의
  배포 권리를 확인한 뒤 공개합니다. 포함된 OGDF는 GPL v2 또는 v3이며, 현재 초안은
  래퍼와 엔진 조합에 v3를 적용합니다. 외부 구성요소의 기존 고지는 유지합니다.
- publisher는 저장소 소유자를 기준으로 `newdlops`로 작성했습니다.
  [Marketplace 관리 화면](https://marketplace.visualstudio.com/manage)에서 해당 이름을
  실제로 소유하는지 확인해야 합니다. 등록이나 로그인 확인이 완료됐다는 의미는 아닙니다.
- 다른 운영체제·CPU와 macOS 26 미만은 이번 패키지의 지원 범위에 포함하지 않습니다.

## 패키지 만들기

빌드 도구를 설치한 뒤 프로젝트 루트에서 순서대로 실행합니다. 명령을 동시에 실행하지
않습니다. 준비 스크립트가 단계별 메모리를 감시하며, 큰 C++ 파일의 컴파일만 단일 작업·
최대 1 GiB를 허용합니다. 나머지 단계와 연구 가드의 기본값은 256 MiB입니다.
중첩 실행을 막는 잠금이 있으므로 준비 명령을 다른 가드로 한 번 더 감싸지 않습니다.

```sh
bash scripts/prepare-release.sh
python3 scripts/erd-poc/run_memory_bounded.py -- node --max-old-space-size=64 --test --test-concurrency=1 test/integration/release-packaging.test.mjs
bash scripts/package-release.sh
```

`dist/`에 VSIX, 프로젝트·Rust 의존성·OGDF 소스 압축 파일, `source-manifest.json`,
`SHA256SUMS`, `verification.json`이 생깁니다. 소스·문서·아이콘을 수정했다면 첫 단계부터
다시 실행하세요. 파일 해시가 달라진 상태로 배포하는 것은 검사에서 막습니다.

자동 검사는 설치 파일을 별도 폴더에 풀고 개발용 경로 없이 분석기와 엔진을 실행합니다.
이는 VS Code 화면에서 설치·조작한 테스트와 구분됩니다.

## 직접 확인하고 게시하기

1. 임시 VS Code 프로필에 VSIX를 설치하고 확장 이름·아이콘·README를 확인합니다.
2. 작은 예제 Django 프로젝트에서 열기, 검색, 선택, Related diagram, Back,
   Full diagram과 새로고침을 확인합니다.
3. Captain 등 큰 프로젝트에서는 Leaf 카드의 연결·선택·페이지 이동·창 크기 변경을
   확인합니다. 공개 설명에 사용할 캡처는 공개 가능한 예제 데이터로 따로 만듭니다.
4. `CHANGELOG.md`의 알려진 제한과 `dist/verification.json`을 검토합니다.
5. 라이선스와 publisher를 확정한 뒤 **검토한 VSIX 파일 그대로** Marketplace에
   업로드합니다. [공식 인증·게시 안내](https://code.visualstudio.com/api/working-with-extensions/publishing-extension)를
   따르고 토큰을 저장소나 문서에 넣지 않습니다.
6. 검토한 소스와 문서를 커밋하고 해당 리비전에 태그를 붙입니다. GitHub 미리보기 릴리스에
   VSIX와 대응 소스, 해시 파일을 함께 첨부합니다. README 링크가 공개 리비전에서 열리는지도
   확인합니다.
7. 게시된 파일을 다시 내려받아 해시와 설치를 확인합니다. 수정 배포는 버전을 올려 진행합니다.

로컬 준비 스크립트는 파일을 공개하거나 기존 VS Code 프로필에 설치하지 않습니다.
