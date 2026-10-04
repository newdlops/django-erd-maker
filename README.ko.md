# Django ERD Maker

Django 모델의 구조와 연결 관계를 VS Code 안에서 탐색합니다. 모델을 검색하고,
필드·참조·상속을 확인하며 직접 연결된 모델만 별도 관계도로 모아 볼 수 있습니다.

[English](README.md) · [지원](SUPPORT.md) · [개인정보 및 로컬 데이터](PRIVACY.md)

## 지원 환경

현재 미리보기 배포본은 **Apple Silicon macOS 26+ (`darwin-arm64`)와 VS Code 1.100 이상**을
지원합니다. Rust 분석기와 OGDF 레이아웃 실행 파일이 포함되어 있어 사용자가 Python,
Django, Rust, CMake를 별도로 설치할 필요가 없습니다.

로컬 Django 프로젝트를 신뢰한 작업 영역에서 실행합니다. Remote SSH·컨테이너·WSL은
작업 영역 쪽 운영체제와 CPU를 기준으로 동작합니다. 이번 배포본은 Windows·Linux·Intel
Mac·브라우저용 VS Code를 지원하지 않습니다.

## 시작하기

1. [Releases](https://github.com/newdlops/django-erd-maker/releases)에서 VSIX를 받습니다.
   아직 공개된 배포본이 없으면 [배포 문서](docs/RELEASING.ko.md)로 직접 패키징할 수 있습니다.
2. VS Code의 확장 화면에서 **… → Install from VSIX…**로 설치합니다.
3. Django 프로젝트 폴더를 열고 **Django ERD: Open Diagram**을 실행합니다.
   여러 폴더를 연 작업 영역에서는 첫 번째 폴더를 기준으로 프로젝트를 찾습니다.
4. 상단에서 모델을 검색하고 Enter로 이동합니다. Shift+Enter는 이전 검색 결과입니다.
5. 모델을 선택해 연결 관계를 확인하고 **Related diagram**으로 직접 이웃을 모아 봅니다.

분석기는 Python 소스를 정적으로 파싱합니다. Django 앱을 import하거나 `manage.py`를
실행하지 않으며, 데이터베이스 연결과 개발 서버가 필요하지 않습니다.

## 관계 탐색

- **Connections**에서 관계 방향과 모델명·필드명으로 범위를 좁힙니다.
- **Related diagram**은 직접 관계만 별도 위치에 배치합니다. 연결 카드를 고르면 그 카드의
  관계를 읽고, **Explore model** 또는 **Explore a member**로 다음 모델을 탐색합니다.
- **Back**은 탐색을 되돌리고 **Full diagram**은 진입했던 전체 보기로 돌아갑니다.
- 여러 필드가 같은 두 카드를 연결하면 선 하나로 묶습니다. Leaf 그룹이 있는 배치는 큰
  카드가 외부 대상마다 선 하나를 받습니다. 필드별 관계는 모델 패널에서 확인합니다.
- 압축 보기의 임시 좌표는 전체 배치를 덮어쓰지 않습니다. 페이지 수는 물리적인 연결 카드
  기준이며, 한 카드에 여러 모델이나 필드가 포함될 수 있습니다.
- 전체 보기에서 빈 공간을 클릭하면 모델 선택을 해제합니다. **Layout & view**에서 배치와
  표시 옵션을 펼칩니다.

소스를 수정한 뒤 **Django ERD: Refresh Diagram**, 문제가 있으면 **Django ERD: Show Logs**를
실행합니다. 보고 방법은 [SUPPORT.md](SUPPORT.md)를 참고하세요.

## 미리보기의 제한

실행 중 만들어지는 모델이나 모든 사용자 정의 필드·메타프로그래밍을 해석하지는 못합니다.
해결하지 못한 참조와 문법 오류는 진단에 표시됩니다. 대형 프로젝트의 배치에는 시간이
걸릴 수 있으며 최적화 배치 모드는 실험적입니다.

현재 전체 보기의 연결 필터는 모델 패널에 적용되고, 압축 보기에서는 그림에도 적용됩니다.
Leaf 그룹을 선택한 상태에서 필터를 바꾸면 모델 전체 관계 범위로 돌아갑니다.
[CHANGELOG.md](CHANGELOG.md)에 이번 미리보기에서 알려진 탐색 제한을 기록했습니다.

## 데이터와 라이선스

분석은 로컬에서 수행하며 프로젝트 소스나 다이어그램을 분석 서버로 전송하지 않습니다.
자체 사용 통계·추적 클라이언트가 없습니다. 로그·캐시에는 프로젝트 경로와 모델명이 들어갈
수 있으므로 공유 전에 내용을 확인하세요. [PRIVACY.md](PRIVACY.md)에 기록 위치를 설명했습니다.

자체 코드는 **GPL-3.0-only**이며 외부 구성요소는 각각의 라이선스를 유지합니다.
VSIX의 `sources/`에 프로젝트와 네이티브 엔진의 대응 소스, 빌드 안내를 함께 제공합니다.
[LICENSE](LICENSE), [NOTICE.md](NOTICE.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)를
참고하세요. Django Software Foundation 및 Microsoft와 제휴한 제품이 아닙니다.
