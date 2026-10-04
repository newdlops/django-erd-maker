# Captain: 6월식 번들 개요 적용 — 2026-09-15

후속 확인: 이 473 후보는 **큰 leaf 카드를 배치 단위로 만드는 단계 없이 표시선만 묶은 결과**다. 빠진 계산을 추가한 423 후보와 비축약 탐색 대조 405, 펼침 상태의 회귀는 [큰 leaf 카드 실험](captain-layout-leaf-compounds.md)에 별도로 기록했다. 아래 내용은 기존 473 후보에 대한 기록이다.

**6월의 leaf/hub 관계 묶음을 현재 배치에 적용한 개요 화면은 visual 473, 면적 0.990361705B다.** 개별 연결 화면과 전환할 수 있도록 제품 코드와 F5 실행 설정을 추가했다.

| 표시 방식 | 카드 | 표시 직선 | 보존 관계 | 선 교차 | 카드 관통 | visual | 면적 B |
|---|---:|---:|---:|---:|---:|---:|---:|
| 모든 개별 연결 | 1219 | 1684 | 1684 | 1400 | 415 | 1815 | 0.990362 |
| 6월식 번들 개요 | 1219 | 1009 | 1684 | 313 | 160 | **473** | **0.990362** |

겹침·최소 간격 위반·비정상 접촉은 0이다. 모델별 카드의 위치·크기와 모든 원래 경로의 좌표는 같다. 자기 관계 12개는 기존처럼 모델 카드와 상세 정보에서 유지한다.

## 6월에서 가져온 것과 이번 구현

6월 5일 `aef540a2`의 leaf/hub 분류를 기준으로 했다. Leaf 구성원 목록은 당시 방식으로 생성되어 보존된 **7월 10일 스냅샷**에서 가져왔다. 이 입력의 출처와 해시는 [구성원 자료](data/erd-poc/recovered/captain-june-bundle-memberships.json)에 있다. 정확한 6월 587 실행을 재생한 결과는 아니다.

이번에는 다음과 같이 표시한다.

- Leaf 구성원에 연결된 관계는 해당 그룹에 모으고, 나머지 관계의 허브 분류에는 6월의 클러스터별 연결 수와 임계값 14를 사용한다. 서로 다른 모델 쌍도 같은 그룹에 들어간다.
- 62개 그룹에 포함되는 737개 관계를 62개의 대표 직선으로 표시한다. 나머지 947개 관계는 각각 표시한다.
- 대표선은 그룹에서 가장 짧은 **실제 관계의 기존 직선**이다. 옛 평균 끝점 대신 실제 카드 경계에 붙은 선을 사용한다. 카드들을 옛 축소 타일로 다시 배치하지 않는다.
- 과거에 생략됐던 leaf의 부수 관계도 그룹의 `memberEdgeIds`에 포함한다. 전체 1684개 관계가 정확히 한 번씩 포함되는지 검사한다. `routedEdges`에는 개별 경로 전체가 남아 있다.
- `June bundles`를 끄면 1684개 개별 직선으로 펼쳐진다. 관계 상세에서 상대 모델로 이동할 때도 개별 연결을 펼친다. `Reset View`는 번들 개요로 돌아간다.

따라서 **473은 개요 화면의 표시 복잡도**다. 개별 관계 배치 자체가 1815→473으로 좋아졌다는 뜻은 아니다. 개요의 대표선은 모든 구성원 모델을 물리적으로 잇는 선도 아니다. 자세한 연결은 펼친 화면과 모델 상세에서 확인한다.

## 점수는 실제 표시 기하에서 계산한다

과거 네이티브 점수를 복사하지 않았다. 현재 제품의 `measureRenderedVisualConflicts`로 화면에 전달할 1009개 직선과 1219개 실제 카드를 측정했다. 논리적 공통 모델을 이유로 교차를 생략하거나, 번들 구성원을 카드 충돌 검사에서 빼지 않았다.

개요의 점수 범위는 `rendered-june-bundled-overview-v1`이다. 기존 개별 연결의 `rendered-canonical-direct-node-bundles-v3`와 구분한다. 화면에서도 번들 개요와 개별 연결의 **초기** 점수를 전환해 표시한다. 드래그 후의 실시간 재측정값이라고 표시하지 않는다.

복원 탐색 중 첫 관계를 대표로 선택한 대조는 530이었다. 가장 짧은 기존 관계를 대표로 고르는 결정적 규칙을 적용하니 473이었다. 추가 좌표 탐색이나 포트 최적화는 수행하지 않았다.

## 직접 테스트

1. 이 저장소의 VS Code `Run and Debug`에서 **Run Captain (June bundles 473)**를 선택하고 F5를 누른다.
2. 열린 Captain Extension Development Host에서 **Django ERD: Open Diagram**을 실행한다.
3. 상단 **June bundles** 버튼으로 개요와 개별 연결을 비교한다. 점수는 사이드바의 Diagram 탭에서 확인한다.

설정은 [.vscode/launch.json](.vscode/launch.json)에 추가했다. [보존 후보](data/erd-poc/candidates/captain-june-473.layout.json)를 `DJERD_LAYOUT_FROM_FILE`로 읽고, 기존 `build:extension` 작업으로 확장을 빌드한다. 기존 Captain 입력에 맞춘 고정 후보이므로 분석 입력이 달라지면 관계 완전성 로그도 함께 확인해야 한다.

## 검증 결과와 남은 범위

- TypeScript 확장 빌드 통과. 변경 전 타입 검사도 통과했다.
- 새 번들/전환 테스트 5개를 포함하여 관련 통합 테스트 **56개 통과**. 누락·중복·알 수 없는 구성원, 비어 있는 그룹, ID 충돌은 전체 개별 연결로 복귀한다.
- 실제 `runOgdfLayout` 파일 로딩 → 디코딩 → 표시 기하 집계 경로에서 **473·0.990B**, `unroutedInputEdges=0`, `routeCompletenessStatus=pass`, `straightRouteStatus=pass` 확인.
- 로딩 과정에서 `RsuAdjustEvent–Event` 쌍의 대표 ID 하나가 현재 통합 규칙과 달랐다. FK 대표 ID를 상속 대표 ID로 맞췄다. 모델 쌍·좌표·관계 수는 같다.
- 제품 브라우저 경로 함수를 별도 실행해 개요 1009개·개별 1684개 경로의 끝점과 직선 여부가 서버 렌더 모델과 같은지 확인했다. 전환 이벤트도 VM에서 실행해 원래 관계 ID와 표시 상태가 복원됨을 검사했다. 이것은 실제 브라우저 조작 검사가 아니다.
- Impeccable의 상태·입력 점검과 변경된 두 마크업 파일의 기계 검사는 수행했다. 검사기 보고 항목 0개. 전체 디자인 비평이나 브라우저 접근성 감사로 해석하지 않는다.
- Browser 연결은 `No browser is available`, 사용 가능한 브라우저 목록은 빈 배열이었다. **VS Code 실제 화면, 모바일·태블릿·데스크톱 뷰포트, 포인터·키보드 조작, GPU 렌더링은 미검증**이다.
- 전체 배치와 Company 주변의 [분석 비교 그림](.tmp/captain-improvement/june-restore/comparison.png), [상세 그림](.tmp/captain-improvement/june-restore/company-detail.png)을 열어 확인했다. 앱 스크린샷이 아니다.

제품 파일 로딩은 약 1.2초였으며 연구 실행은 256MiB 감시 아래 직렬로 수행했다. 관측 최대 RSS는 확장 빌드 186.9MiB, 관련 테스트 135.9MiB, 제품 로딩 검사 160.2MiB, 비교 그림 120.6MiB였다. 기존 개별 연결 최선 파일의 SHA256은 `bc1111bd1deacfedb57e4eee49c63655a823abf099495e988faf39d3c33d151c`로 유지했다.

[후보 집계·입력 해시](data/erd-poc/candidates/captain-june-473.audit.json), [제품 로딩 감사](.tmp/captain-improvement/june-restore/product-load.audit.json), [제품 로그](.tmp/captain-improvement/june-restore/product-load.log), [후보 생성기](scripts/erd-poc/create_june_overview.cjs).
