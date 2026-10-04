---
target: Captain ERD UX review
total_score: 22
max_score: 36
na_heuristics: 9
p0_count: 0
p1_count: 2
timestamp: 2026-09-15T14-53-23Z
slug: interaction-runtime-browserrelateddiagramsource-ts
---
Method: dual-agent (A: /root/ux_design_review · B: /root/ux_detector_evidence), supplemented by parent-operated live Captain inspection.

# Captain 관계 탐색 UX 점검 — 2026-09-15

현재 가장 큰 개선 기회는 **화면과 목록이 같은 관계 범위를 보여주고, 그 안에서 한 관계를 바로 따라갈 수 있게 하는 것**이다. 전체 배치를 보존하는 overview와 직접 이웃만 읽는 Related diagram은 유지할 가치가 있다.

## 확인 범위

- 실제 VS Code Extension Development Host의 Captain에서 Company 검색·Enter, Related diagram 진입, Full diagram 복귀, References 필터, 관계 행 선택, All connections, Leaf 카드 선택, Referenced by 필터를 조작했다.
- 실제 창을 넓은 폭·중간 폭·좁은 폭으로 바꾸어 렌더링을 확인했다. Company의 113개 연결 카드는 넓은 창에서 8개씩 15페이지, 좁은 창에서 4개씩 29페이지였다. 특정 CSS viewport나 모바일·태블릿 에뮬레이션 검증은 아니다.
- 독립 평가 A는 현행 소스와 기존 실제 캡처 네 장으로 디자인을 검토했다. B는 소스·같은 기존 캡처와 detector를 검토했다. 두 평가는 서로의 결과를 보지 않았고 A 완료 후 B 결과를 종합했다.
- 이 문서는 조사 결과다. 제품 코드·저장 배치·관계 데이터는 수정하지 않았다. 기존 468 crossing / 1.149B와 Company 113개 연결은 이전 검증 결과이며 이번 감사에서 수치 검사를 재실행하지 않았다.

## 디자인 적합성과 잘된 점

ERD의 규모와 개발자의 조사 작업에 맞춘 화면이다. 기존의 짙은 배경, 관계 유형 색, 모델 중심 위계, 상세 정보 disclosure를 유지한다. 별도 시각 스타일을 도입할 필요는 없다.

1. 모델 쌍의 여러 필드와 큰 Leaf 카드는 선 하나를 공유하고 inspector에서 관계를 확인할 수 있다.
2. Related diagram은 1,244개 전체 모델 중 직접 관계만 읽을 수 있는 별도 공간을 제공한다. 실제 Company 화면에서도 중심 모델과 각 이웃을 식별할 수 있다.
3. Back과 Full diagram은 탐색 문맥 복원 기반을 갖추었다. 이번 실제 조작에서도 Full diagram으로 Company의 원래 확대 화면과 필터에 돌아왔다.

## 우선 개선 사항

### 1. [P1] 목록·캔버스·Leaf 그룹의 필터 범위를 일치시킨다

**실제 재현 A:** 전체 보기에서 Company의 `References 1`을 누르면 목록에는 CompanySsoConfiguration 한 관계만 남지만 캔버스에는 Company의 모든 직접 연결이 계속 강하게 표시된다. 압축 보기에서는 같은 필터가 다이어그램에도 적용된다.

**실제 재현 B:** Related diagram의 `Company leaves`를 선택해 7개 관계를 읽는 상태에서 `Referenced by 171`을 누르면 그룹 선택이 해제되고 목록이 171개로 넓어진다. 그룹 내부에서 필터링한다고 이해하기 쉽다.

**개선:** 선택한 모델 → 선택한 peer/Leaf → 검색어·방향이라는 범위를 명시하고, 같은 결과 집합을 목록과 그림 강조에 사용한다. 그룹은 필터를 바꿔도 유지하고 그룹 선택 해제와 전체 필터 초기화를 구분한다. 필터 수치는 현재 범위 기준으로 표시한다. `All connections`가 방향 필터까지 지우는지와 그룹 선택만 지우는지를 문구와 동작으로 구별한다.

**완료 기준:** References 1이면 목록과 그림에서 같은 한 관계가 강조된다. Company leaves의 7개 안에서 방향·검색을 바꿔도 명시적 그룹 해제 전에는 범위가 171개로 확장되지 않는다. 필터를 모두 지우면 원래 연결이 복원된다.

근거: [필터 갱신](/Users/lky/project/django-erd-maker/src/webview/interaction/runtime/browserRelationshipSource.ts:234), `browserCanvasDrawSource.ts:2570,2720`, `relationshipExplorer.ts:87–98`. 권장 작업: `$impeccable harden`.

### 2. [P1] Company에서 한 관계를 따라가는 조작을 만든다

**실제 관찰:** Company 검색·Enter는 모델을 확대하지만 많은 상대 카드는 화면 밖으로 나간다. 113개 직접 연결이 한꺼번에 굵고 밝아져 관계 하나를 눈으로 따라가기 어렵다. 관계 행을 누르면 전체 보기에서 Related diagram으로 전환된다.

**개선:** 전체 보기에서 목록 행에 포인터를 올리거나 키보드 포커스를 주면 해당 선·양 끝 카드만 강하게 강조한다. 클릭으로 해당 관계를 고정하고 필드명·방향·유형·상대 모델 이동을 같은 위치에서 제공한다. 압축 보기 진입은 기존의 명시적인 Related diagram 동작으로 유지한다. 나머지 직접 연결은 얇게 유지하며 선택 자체로 연결이 누락되지는 않게 한다. 선택한 관계의 양 끝을 화면에 맞추는 동작도 제공한다.

**완료 기준:** Company에서 특정 필드 한 개를 골라 화면 전환 없이 해당 선과 두 끝점을 식별할 수 있다. 마우스와 키보드 모두 가능하고 미리보기 해제 시 원래 강조가 복원된다. 큰 Leaf 카드에는 계속 직선 하나만 연결한다. 전체 보기의 저장 좌표와 선 형태는 보존한다.

근거: `browserEventSource.ts:506–515`의 검색 이동, `browserCanvasDrawSource.ts:2720–2774`의 강조/선 굵기, `browserRelationshipSource.ts:246–250`의 관계 행 전환. 권장 작업: `$impeccable shape` 후 `$impeccable harden`.

### 3. [P2] 창 폭이 바뀌어도 선택한 카드가 현재 페이지에 남게 한다

**소스에서 찾은 결함 경로 — 실제 선택 카드로 재현하지 않음:** 넓은 창의 8개 카드 중 뒤쪽 카드를 선택한 뒤 좁혀 4개로 바꾸면 페이지 번호만 유지된다. 페이지 보정은 관계 edge 선택에 대해서만 수행되고 peer 선택에는 적용되지 않는다. 따라서 inspector는 선택한 peer를 보여주는데 현재 페이지에는 그 카드가 없을 수 있다.

**개선:** 페이지 번호 대신 선택한 peer ID를 기준으로 새 페이지를 계산한다. 선택이 없으면 기존 첫 표시 카드가 유지되도록 한다.

**완료 기준:** 넓은 1페이지의 8번째 카드 또는 2페이지의 10번째 카드를 선택한 뒤 창을 좁혀도 해당 카드·강조 선·inspector 범위가 함께 유지된다. 넓혔을 때도 같은 카드가 보인다.

근거: `browserRelatedDiagramSource.ts:66–73,181–184`, `relatedDiagram.ts:40`. 권장 작업: `$impeccable harden`.

### 4. [P2] 모델 검색 결과를 선택 전에 보여준다

**실제 관찰:** Company 검색에 `1/103`만 표시되어 다른 후보 이름은 보이지 않는다. 소스는 Enter/Shift+Enter를 누를 때마다 실제 모델을 방문하도록 구현되어 있다.

**개선:** 모델명·app·DB table을 표시하는 작은 후보 목록을 제공한다. 정확히 일치하는 이름은 우선하고, 위아래 키로 고른 뒤 Enter로 확정한다. 후보 탐색만으로 모델 방문 이력이나 화면 위치를 바꾸지 않는다.

**완료 기준:** 부분 이름을 입력해 후보를 비교하고 원하는 모델을 한 번 확정해 이동한다. Escape는 후보만 닫고 기존 다이어그램 맥락을 보존한다. 결과 없음과 긴 이름도 처리한다.

근거: `browserEventSource.ts:447–530`, `renderCanvasScene.ts:34`. 권장 작업: `$impeccable shape`.

### 5. [P2] 카드에서 관계 의미와 다음 조사 대상을 함께 읽게 한다

**실제 관찰 및 소스:** 이웃 카드는 방향·모델명·관계 수를 보여주지만 필드명과 관계 유형을 읽으려면 inspector로 시선을 옮겨야 한다. 현재 Company는 15/29페이지의 이웃을 순차 확인해야 한다. 큰 Leaf는 멤버 이름 두 개와 나머지 개수, 전체 native select를 제공한다. 기존 MeetingDocument 캡처에는 52개 모델 그룹이 있다.

**개선:** 단일 관계 카드에는 `company · Many → one`을, 복수 관계에는 필드 수와 유형 요약을 보여준다. 선택된 관계 행 근처에 상대 모델로 이동하는 버튼을 두어 40개 관계 행을 지나 Tab으로 이동할 부담을 줄인다. Leaf 멤버는 현재 범위를 유지하는 검색으로 찾고 같은 자리에서 탐색한다. 이후 필요하면 도메인·관계 방향별 그룹 탐색을 추가하되 한 페이지에 모든 모델을 축소해서 넣지는 않는다.

**완료 기준:** 단일 관계의 필드와 방향을 카드 선택 전에 알 수 있고, 긴 Leaf 멤버를 검색한 뒤 같은 위치에서 다음 모델로 이동할 수 있다. 혼합 상속/참조의 구성은 색 이외의 텍스트로도 알 수 있다.

근거: `browserRelatedDiagramSource.ts:93–105`, `browserRenderSource.ts:365`, `relationshipExplorer.ts:87`. 권장 작업: `$impeccable clarify`와 `$impeccable harden`.

## 후순위 메모

- `← Back`을 `Back to Company`처럼 목적지가 보이게 하고 최근 2–3단계 경로를 표시하면 긴 탐색의 기억 부담을 줄일 수 있다.
- sidebar의 All connections는 재렌더링으로 사라지지만 포커스 복원 대상에 포함되지 않는다. 검색 등 안정적인 컨트롤로 복원하도록 한다. 이는 소스에서 확인했으며 실제 키보드 재현은 하지 않았다.
- self-reference 선택 버튼에는 일반 peer 버튼과 같은 `aria-pressed`가 없다. 소스 기준 접근성 보완 후보다.
- `1 connections`의 단복수와 중복된 Related diagram / All connections 버튼의 의미를 정리한다.
- 좁은 창에서는 현재 모델·선택 그룹·필터 범위를 짧게 고정하고 inspector를 접을 수 있으면 반복 스크롤을 줄일 수 있다.

## 잠정 휴리스틱 평가

점수는 소스·캡처·관찰에 기반한 주관적인 개선 우선순위 참고값이다. 레이아웃 성능 수치나 사용자 조사 결과가 아니다. A의 독립 평가 23/36에서 실제 필터 피드백 불일치를 반영해 시스템 상태 표시를 1점 낮췄다.

| Nielsen 기준 | 점수 | 관찰 |
| --- | ---: | --- |
| 시스템 상태 표시 | 2 | 목록과 캔버스의 필터 결과가 다름 |
| 사용자 언어와 일치 | 3 | 관계 방향은 개발자 작업에 적합 |
| 사용자 통제 | 3 | Back/Full diagram 복원 기반이 있음 |
| 일관성 | 2 | 그룹 선택과 필터의 적용 범위가 예측하기 어려움 |
| 오류 예방 | 3 | 불가능한 페이지와 모델 이동 비활성화 |
| 기억보다 인지 | 2 | 검색 후보와 탐색 경로가 보이지 않음 |
| 효율성 | 2 | 큰 그룹 조사와 관계→모델 이동에 반복 조작 필요 |
| 미니멀한 디자인 | 3 | 모델 위계와 상세 disclosure는 적절함 |
| 오류 복구 | n/a | 실패·새로고침 복구를 이번에 검증하지 않음 |
| 도움말 | 2 | 기본 안내가 있으나 조작의 범위를 충분히 설명하지 못함 |
| 합계 | **22/36** | **61% · 개선 필요** |

## 인지 부하와 사용자 관점

독립 A의 인지 부하 체크는 8개 중 3개 실패로 중간 수준이었다. 문제는 선택지를 작은 묶음으로 이해하기, 비교할 선택지 수, 이전 맥락을 기억해야 하는 부담이다. 8개 카드 자체를 줄이기보다 각 카드의 관계 의미와 검색 범위를 명확히 하는 편이 도움이 된다.

숙련 개발자는 검색 결과를 순서대로 방문하거나 Leaf 멤버를 긴 목록에서 고르는 과정에서 속도를 잃는다. 키보드 사용자는 inspector와 diagram 사이의 이동 거리, 재렌더링 뒤 포커스, 화면에 없는 선택 카드를 특히 점검해야 한다. 화면 낭독기를 실제 실행한 검증은 아니다.

전체 Company 화면은 규모와 많은 선으로 부담을 주고, Related diagram에서 중심과 이웃을 읽으면 부담이 줄어든다. 이후 Leaf 내부 조사의 범위가 바뀌는 지점에서 다시 혼란이 생긴다. 복귀 문맥 보존은 유지하고 조사 중 범위가 바뀌지 않는 데 우선 투자한다.

## 기계 검사

독립 B가 지정한 5개 관련 소스에 detector를 한 번 실행했다. 종료 코드 2, advisory 1개: `codex-grid-background`, `documentStyles.ts:620`의 `.erd-canvas` 격자 배경이다. 실제 ERD 좌표면이므로 규칙이 허용하는 용도이며 오탐으로 판단했다. 유효한 자동 검출 개선 항목은 0개다. 기계 검사는 위에서 재현한 상호작용 문제를 발견하지 못했다.

## 재현 자료

아래 파일은 프로젝트 루트의 `.tmp/captain-improvement/leaf-compounds/`에 저장했다.

- `ux-review-company-search.jpg`: Company 검색 후 확대·직접 연결.
- `ux-review-company-related.jpg`: Company 113개 연결 카드, 15페이지.
- `ux-review-filter-disagreement.jpg`: References 1인데 모든 선이 유지되는 전체 보기.
- `ux-review-relationship-click.jpg`: 관계 행 선택 시 압축 보기 전환.
- `ux-review-company-medium.jpg`, `ux-review-company-narrow.jpg`: 실제 창 폭 변경, 좁은 창 29페이지.
- `ux-review-leaf-selected.jpg`: Company leaves의 7개 관계 선택.
- `ux-review-leaf-filter-reset.jpg`: Referenced by 선택 후 전체 171개로 확대.
- `ux-review-detector.json`: 원본 기계 검사 결과.

## 다음 개선의 기준

기존 요청인 관계 추적 편의와 직선·큰 카드 단일 연결을 우선하므로 추가 디자인 취향 질문은 생략한다. **필터 범위 일치 → 한 관계 미리보기와 고정 → 검색 후보 및 관계 요약** 순서가 적절하다. 창 크기 변경에 따른 선택 유지와 포커스 복원은 관련 코드 수정 때 함께 닫는다. 최종 polish는 이 동작들이 실제 화면에서 일치하는지 확인한 뒤 수행한다.

후속 설계의 판단 질문: **선 하나를 고른 자리에서 어떤 모델의 어떤 필드인지와 다음 조사 대상을 함께 결정할 수 있는가?**
