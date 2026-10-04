# Captain Git 이력 조사 — 2026-09-15

> 복구 진전: 연결되지 않은 Git blob에서 **모든 경로가 직선인 visual 586**의 실제 좌표·그래프를 찾았고, 과거 측정 함수로 정확히 재현했다. 전체 원래 관계의 현재 측정 및 공통 입력 좌표 대조도 완료했다. [복구와 재측정](captain-layout-recovered-history.md).

> 추가 분해: 같은 A–B 중복 제거 코드는 6월과 현재가 동일했다. 과거 허브·구성원 관계의 추가 통합과 공통 대상 누락 버그를 추적하고, 현재 좌표를 고정한 관계 제외 진단을 수행했다. [관계 집합 차이의 정량 검증](captain-layout-pair-bundle-audit.md).

> 후속 검증 완료: 옛 변환의 추가 visual 개선은 0건이었다. 대조군의 포트 보정에서만 1819→1818이 나왔다. [회전·교환·이동 대조 결과](captain-layout-transform-ablation.md).

**과거 visual 587 기록은 실제로 있다.** 2026-06-05의 `aef540a2`는 `visualCrossings 991 → 587`, 카드 겹침 0, bbox **3.00B**를 명시한다. 과거의 낮은 수치를 꺾인 선만으로 설명한 것은 불충분했다. 직선 기록에서도 표시 관계의 통합, 카드 표현, 충돌 집계 대상이 현재와 달랐다.

이번 조사에서 현재 조건의 새로운 배치는 만들지 않았다. 보존 최선은 **visual 1819 = 선 교차 1401 + 카드 관통 418**, 실제 카드 bbox **0.990361705B**, 실제 카드 1219장·독립 관계 1684개다. 목표는 visual ≤500, bbox ≤1.5B로 유지한다. 조사 결과로 500의 가능·불가능이나 1819의 최적성을 판정할 수 없다.

**확인한 원기록**

| 기록 | 선 교차 | 총 visual | bbox, B | 해석 |
|---|---:|---:|---:|---|
| 5월 24일 보존 지표 | 568 | 679 | 3.6454 | 직선, 원본 경로 1554개·표시 선분 876개 |
| 5월 25일 seed 43 지표 | 577 | 663 | 2.6513 | 직선, 원본 경로 1554개·표시 선분 952개 |
| 5월 26일 bbox 지표 | 587 | 651 | 7.5156 | 직선, 원본 경로 1565개·표시 선분 920개 |
| **6월 5일 `aef540a2` 커밋** | 내역 미확보 | **587** | **3.00** | 커밋 본문에 visual 991→587 명시 |
| 6월 8일 인계 문서 | 447 | 517 | 여기서는 확정하지 않음 | 2-bend·final retouch 및 묶음 집계 포함 |
| 7월 10일 저장 파일 | 292 | 446 | 약 1.48 | rawRouteCrossings 6167, 표시 선분 817개 |

5월의 세 행은 [Git에 들어 있는 기준 지표](data/erd-poc/baselines/captain-2026-05-24.json)와 [추출한 비교 데이터](.tmp/captain-improvement/git-history/committed-baseline-summaries.json)다. `edgeBend=0`, `routePoints=2×routedEdges`이므로 모든 낮은 기록을 꺾임의 결과로 취급하면 안 된다. 동시에 선 교차 587과 총 visual 651도 구분해야 한다.

6월 5일의 **visual 587은 별도의 실제 기록**이다. [커밋 원문](.tmp/captain-improvement/git-history/snapshots/aef540a2/commit.txt)을 보존했다. 해당 실행의 완전한 입력·좌표·최종 화면을 Git에서 확보하지 못했으므로 정확한 587 재현을 완료했다고 주장하지 않는다. 당시 면적은 현재 상한 1.5B의 두 배다. 커밋 본문의 “optimum”은 당시 탐색 결과에 대한 표현이며 수학적 최적성 증명이 아니다.

517은 [handover.md](handover.md)의 기록이고 446은 [보존된 7월 10일 JSON](erd-layout-final.json)의 메타데이터다. 이번에 조회한 Git 이력에서는 이 두 파일의 해당 스냅샷을 찾지 못했다. Git 증거와 작업 디렉터리의 인계 자료를 구분한다.

**놓친 점 1: 이름이 같은 visual이 서로 다른 그림을 셌다**

6월 5일의 [네이티브 측정 코드](.tmp/captain-improvement/git-history/snapshots/aef540a2/native/ogdf-layout/src/main.cpp)는 다음과 같이 동작한다.

- 5603행 부근: 묶음의 부모와 모든 leaf를 `bundleAbsorbed`에 넣어 개별 카드의 관통·겹침 검사에서 제외한다.
- 9540행 부근: 원래 경로 교차와 축약한 표시 선의 교차를 따로 계산한 뒤, `quality.edgeCrossings`, `edgeNodeIntersections`, `routeSegments`를 표시 선 기준으로 덮어쓴다.
- [당시 렌더러](.tmp/captain-improvement/git-history/snapshots/aef540a2/src/webview/state/createDiagramRenderModel.ts)는 leaf 카드를 200×56 타일로 재배치하고 가상 묶음 테이블을 추가한다. hub 관계들도 대표 선으로 합쳐 표시한다.

즉 원본 관계가 입력에 존재한다는 사실만으로 그 관계가 화면에서 독립적인 한 선으로 보인다고 결론 낼 수 없다. 현재 요구사항인 실제 크기의 개별 카드·원래 양 끝점·관계마다 한 직선과 비교하려면 표시 결과까지 검증해야 한다.

**놓친 점 2: `pure/direct`라는 문서 이름도 독립 관계 보존을 보장하지 않았다**

[5월 20일 연구 문서](.tmp/captain-improvement/git-history/snapshots/aef540a2/context.md)는 pure/direct 895, 이후 879를 기록한다. 그런데 같은 커밋의 [ExactRelationEvaluator](.tmp/captain-improvement/git-history/snapshots/aef540a2/scripts/erd-poc/v35_exact_relation_search.py)는 378행 이후에서 leaf-root 관계를 `B{bundle}|{root}` 하나로 합치고, 그 외 leaf 관련 관계는 `continue`로 제외한다. 파일 머리말도 이를 명시한다. 포트 뷰어는 이 평가기를 사용한다.

따라서 이 기록을 현재의 전체 독립 직선 성과로 재분류할 수 없다. 문서에 적힌 `/tmp/...r35...` 결과 파일은 현재 없어서 정확한 당시 입력은 확인하지 못했다. 묶음이 없는 입력이었다면 해당 분기가 작동하지 않았을 수 있다는 한계도 남긴다. 확인된 사실은 **당시 평가기가 원본 관계 전체를 강제하는 구현이 아니었다**는 점이다.

**놓친 점 3: 7월에는 실제 연결 구조를 바꾸는 렌더러가 들어갔다가 수정됐다**

`595cc1d0`의 `createSemanticCarrierEdges`는 연관 관계의 연결 성분을 트리로 만들고, 원래 FK ID를 트리의 여러 구간에 붙인다. 이어서 `d3a210ef`가 원래 관계의 양 끝을 보존하는 방식으로 바꿨다. [수정 코드](.tmp/captain-improvement/git-history/snapshots/d3a210ef/src/webview/state/createDiagramRenderModel.ts) 1280행 부근의 설명에도 “FK가 무관한 모델을 통과했다”는 문제가 기록되어 있다.

저장된 7월 10일의 **동일한 입력**을 각 시점의 렌더 모델 코드에 넣어 비교했다. 아래는 과거 앱의 실제 화면 재실행이나 그날의 성과 기록이 아니다. 현재 보존된 카탈로그를 공통으로 사용한 소스 재현 실험이다.

| 렌더 모델 버전 | 생성된 선 수 | 공통 선 교차 측정 | 해당 버전 모듈의 visual 측정 |
|---|---:|---:|---:|
| 6월 5일 `aef540a2` | 892 | 770 | 측정 함수 없음 |
| 7월 12일 `075fbea7` | 928 | 858 | 측정 함수 없음 |
| 7월 21일 트리 방식 `595cc1d0` | 1029 | **91** | 106 |
| 7월 21일 관계 보존 `d3a210ef` | 1399 | **5083** | 6199 |
| 현재 작업 디렉터리 구현 | 1682 | 7185 | 8618 |

특히 트리 방식과 관계 보존 방식은 **카드 위치·크기·숨김 상태가 모두 동일함을 검증했다**. 입력 좌표를 최적화하지 않고도 표시 방식만으로 선 교차가 91에서 5083으로 바뀐다. 이 차이를 배치 엔진의 성능 퇴보로 계산하면 잘못된 비교다. 6월의 587은 이 트리 실험보다 먼저 나온 기록이므로, 트리 변경으로 587까지 설명하지 않는다.

구체적인 예는 `ArticlePublishHistory.article` FK다.

```text
원래 관계: ArticlePublishHistory → Article
트리 표시: ArticlePublishHistory → AgendaBonusIssueNewIssueMinutesFile → Article
```

중간에 생긴 두 연결은 원본 그래프에 모두 없다. 그럼에도 두 선의 `memberEdgeIds`에는 원래 FK ID가 들어 있다. [174개 사례 중 예시와 대응 선 ID](.tmp/captain-improvement/git-history/mst-fk-witnesses.json), [원본 관계 존재 여부 검증](.tmp/captain-improvement/git-history/verified-findings.json)을 보존했다. 이 사례 수는 중간 경로가 실제 모델 3~6개인 경우만 센 값이며 전체 왜곡 수가 아니다.

같은 7월 10일 입력에서 과거 버전들은 가상 묶음 테이블 49개를 만들고 실제 카드 258장의 위치·크기를 현재 렌더러와 다르게 표시한다. 현재 구현은 실제 카드 1218장과 독립 선 1682개를 생성한다. `HEAD` 이후의 미커밋 변경에도 이 차이가 있으므로 Git 커밋까지만 보면 현재 동작을 설명할 수 없다.

**놓친 점 4: 587을 만든 기능은 남아 있지만 실행 단계와 평가 대상이 다르다**

`aef540a2`가 추가한 핵심은 묶음 구성원 전체의 회전·반사, 묶음 위치 교환, 저차수 카드의 이웃 방향 이동이다. 현재 [clusterGraph.cpp](native/ogdf-layout/src/clusterGraph.cpp) 4208·4401·4510행 부근에 남아 있고 [runOgdfLayout.ts](src/extension/services/layout/runOgdfLayout.ts)에서도 기본으로 켠다. 기능이 삭제되어 1800대로 돌아왔다는 증거는 찾지 못했다.

그러나 `--positions-tsv`로 기존 좌표를 주면 `DJERD_CG_SKIP_POSITIONING=1`이 설정되고 회전·교환·node-pull이 생략된다. 그 위치 계산을 실행해도 뒤에서 TSV 좌표로 덮어쓰기 때문이다. 따라서 이 플래그를 억지로 켜는 것만으로 보존 배치에 옛 개선이 적용되는 것은 아니다.

또한 해당 회전 단계는 중복을 합친 인접 관계의 **카드 중심 사이 교차**로 평가한다. 현재 multistart의 carrier 점수는 이미 기본값 0으로 꺼져 있고 관통 가중치도 1로 설정되어 있다. 다만 중심 기반 seed 점수와 최종 카드 경계의 포트에서 측정하는 visual은 여전히 같은 값이 아니다. 이것이 실제로 더 나쁜 후보를 선택하는지는 별도의 대조가 필요하며 이번 조사에서 성능 손실량을 확정하지 않았다.

**입력과 검증 범위**

- 조회 가능한 전체 이력은 80개 커밋이다. 관련 13개 시점의 소스 134개를 추출했고, 추가로 옛 평가기와 현재 소스·실행 모듈을 보존했다. [Git 원본 manifest](.tmp/captain-improvement/git-history/source-manifest.json), [추가 manifest](.tmp/captain-improvement/git-history/additional-source-manifest.json).
- `d80fb5b8`의 Captain TSV를 실제로 세면 1304개 노드·3435개 원시 관계다. 커밋 본문의 1305·3443과 다르다. 현재 입력은 1219개 노드·3215개 원시 관계에서 1684개 정규 관계를 사용한다. 원시 관계 수와 최종 독립 선 수를 혼용하지 않는다.
- 6월 TSV는 모든 카드 높이가 76이다. 현재와 겹치는 1192개 모델의 카드 면적 합은 24.96M→27.25M으로 약 9.2% 달라졌다. 이것은 입력 차이의 증거이며 정확한 587 실행의 카드 크기나 성능 차이 전체의 원인이라고 주장하지 않는다. [치수 비교](.tmp/captain-improvement/git-history/card-dimension-comparison.json).
- 7월 10일 재현 입력은 1218장·1682경로다. 원래 analyzer 스냅샷을 확보하지 못해 보존된 7월 22일 카탈로그를 사용했고, 없는 `InvestmentAgreement`, `InvestmentAgreementAttachment`는 빈 필드 정보로 보완했다. 각 버전은 동일한 이 입력을 받지만 렌더러의 카드 재계산 규칙은 다르다.
- 공통 측정은 각 렌더 모델이 만든 경로를 보존한 채 현재 측정 함수로 계산했다. 과거 묶음 내부 카드까지 현재 규칙으로 세면 묶음 겹침이 추가되므로, 공통 total visual을 과거의 실적처럼 사용하지 않았다. 표의 마지막 열은 함수가 있는 버전 자신의 측정값이다. [전체 비교 데이터](.tmp/captain-improvement/git-history/renderer-comparison.json).
- 현재 TS 소스를 따로 변환한 결과와 기존 실행 모듈의 결과가 두 입력에서 완전히 같은지 확인했다. 현재 최선도 제품 측정에서 1819로 재확인했다. 전체 프로젝트 타입 검사, 제품 빌드, 옛 네이티브 실행, 브라우저·VS Code 화면 검증은 이번에 수행하지 않았다.
- 연구 재현은 256MiB 감시 아래 직렬로 실행했다. 감시 없이 시도한 보조 Python 조회 2건은 종료 코드 137로 실패하여 결과에 사용하지 않았고, 파일로 저장한 분석을 감시 아래 다시 실행해 완료했다.

**다음 개선에서 바꿀 우선순위**

새 솔버를 더 붙이기 전에 **동일한 원본 관계·실제 카드 크기·독립 직선·최종 포트 점수로 옛 배치 변환의 효과를 분리 검증**하는 것이 우선이다. 복구할 후보는 구성원 전체를 함께 움직이는 회전·교환·이웃 방향 이동이다. 과거의 대표 선, 제외된 카드, 트리 대체를 복구하면 현재 요구사항을 만족하지 않는다.

구체적인 대조는 초기 배치 생성과 보존 좌표 후처리를 분리해야 한다. 초기 배치에서는 동일한 seed와 입력으로 세 변환의 켜짐/꺼짐을 비교하고, 각 결과를 압축·직선 포트 처리까지 보낸 뒤 최종 visual과 bbox를 남긴다. 보존된 1819에 적용할 때는 TSV로 덮어쓰기 전의 코드를 재실행하는 대신, 불러온 실제 카드와 포트에 직접 변환을 적용해야 한다. 후보의 평가·채택은 전체 1684개 관계와 실제 카드에서 수행한다.

판정 자료에는 중심 교차, 최종 선 교차, 카드 관통, 실제 bbox, 카드·관계 수를 함께 남긴다. 면적 1.5B와 기존 기하 검증을 통과한 후보만 현재 최선과 비교한다. 이 대조는 아직 실행하지 않았으며, 587의 회복이나 500 달성을 약속하지 않는다.

**재현 파일**

[수집기](.tmp/captain-improvement/git-history/collect.py), [시점별 렌더러 실행기](.tmp/captain-improvement/git-history/replay.cjs), [원기록·FK 분석](.tmp/captain-improvement/git-history/analyze_history.py), [기하·관계·보존 해시 검증](.tmp/captain-improvement/git-history/verify_history.py)을 저장했다. 현재 최선의 SHA256은 `07b8252970975a204221fc9c4b4f54fca69f068914326eba369d82f77aeaca51`로 이전과 같다. 제품 코드·기본 캐시·배치 파일은 이번 조사에서 변경하지 않았다.
