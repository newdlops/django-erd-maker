# 연결 관계 탐색 UX — 2026-09-15

## 사용 흐름

F5의 `Run Captain (Leaf cards 468)` 구성으로 실행한 뒤 `Django ERD: Open Diagram`을 연다.

1. 상단에서 모델을 검색하고 Enter로 이동한다. Shift+Enter는 이전 검색 결과다.
2. 모델 패널의 **Connections**에서 참조 방향이나 모델명·필드명으로 관계를 좁힌다.
3. **Related diagram**을 누르면 선택 모델과 직접 연결된 모델만 별도 다이어그램에 가깝게 배치한다. 관계 행을 누르거나 키보드 Enter로 선택해도 이 화면이 열리고, 해당 카드가 있는 페이지로 이동한다.
4. 연결 카드를 누르면 모델 패널에 그 카드의 관계만 표시한다. 관계 행을 선택하면 출발 모델·필드, 대상 모델, 관계 유형이 상단에 나타난다.
5. **Explore model →** 또는 Leaf 카드의 **Explore a member…**로 다른 모델을 새 중심으로 탐색한다. **← Back**은 이전 모델·필터·선택 묶음·페이지·목록 스크롤을 복원한다.
6. **Full diagram** 또는 입력창 밖의 Escape는 진입 시점의 전체 다이어그램과 선택 모델·화면 위치·필터를 복원한다.

모델 패널의 관계는 처음 40개를 표시하고 **Show next**로 더 볼 수 있다. 별도 다이어그램은 넓은 영역에서 페이지당 연결 카드 8개, 좁은 영역에서 4개를 표시하며 전체 개수와 페이지를 안내한다. 검색·방향 필터는 목록과 다이어그램에 함께 적용된다.

동일한 모델 쌍의 여러 필드와 기존 Leaf 묶음은 카드 경계에 닿는 직선 하나로 표시한다. 관련 없는 Leaf 멤버는 포함하지 않는다. 같은 이름의 Leaf 카드를 구분할 수 있도록 멤버 이름 일부를 보여 주며, 참조와 상속이 섞인 경우도 설명에 명시한다. 필드·속성·메서드는 모델 패널 아래에서 펼치고, 전체 배치 옵션은 전체 보기의 **Layout & view**에서 사용한다.

## 기능 검증

- 256MiB 제한 하에서 TypeScript 빌드 통과. 최종 빌드 최대 RSS 186.4MiB.
- 다음 6개 통합 테스트 파일을 단일 작업으로 실행: **61/61 통과**, 최대 RSS 132.6MiB.
  - `related-diagram.test.mjs`
  - `relationship-explorer.test.mjs`
  - `leaf-card-render.test.mjs`
  - `shared-root-bundle-render.test.mjs`
  - `june-relationship-overview.test.mjs`
  - `phase8-webview-render.test.mjs`
- 브라우저용 상태 변경 함수를 VM에서 실행하여 로컬 탐색/Back/전체 보기 복원과 저장 좌표 보존을 검사했다. 관계 구성 검사는 중복 필드, 반대 방향, 혼합 상속/참조, Leaf 소속 충돌, 자기 참조, 없는 끝점, 필터와 페이지 누락을 포함한다. HTML 배치 검증은 아래의 실제 UI 확인으로 구분한다.
- 현재 Captain의 **1,244개 모델 전체**를 순회하여 각 모델의 관계가 로컬 카드와 페이지에 빠짐없이 정확히 한 번 들어가는지 검사했다. 자기 참조를 포함해 모델별 관점에서 3,590개 관계 항목을 검사했으며, 이는 전역 고유 관계 수와 다른 값이다.
- MeetingDocument: 79개 모델, 78개 관계, 연결 카드 14개. 그중 하나는 직접 연결된 모델 52개를 담은 Leaf 카드다. Company: 170개 모델, 172개 관계, 연결 카드 113개.
- [테스트 로그](.tmp/captain-improvement/leaf-compounds/related-diagram-tests.log), [전체 모델 관계 검사](.tmp/captain-improvement/leaf-compounds/related-diagram-captain.audit.json).

## 전체 보기 보존

현재 Captain payload와 저장 배치를 제품의 파일 로드 경로로 다시 검사했다.

| 항목 | 결과 |
| --- | ---: |
| Visual Crossing | 468 |
| 영역 | 1.14915248448B |
| 실제 모델 | 1,244 |
| 표시 직선 | 1,039 |
| Canonical 관계 | 1,727 — 정확히 한 번씩 보존 |
| 큰 Leaf 카드 | 49 — 각 카드에 한 직선 |
| 유효한 경계 끝점 | 2,078 / 2,078 |
| 모델 좌표·크기·라우팅 변경 | 없음 |
| 배치/카드 내부 간격 위반 | 0 / 0 |

검사 결과: [related-diagram-overview.audit.json](.tmp/captain-improvement/leaf-compounds/related-diagram-overview.audit.json). 최대 RSS 127.6MiB. 이 파일의 `browserVerified: false`는 수치 검사 자체가 브라우저 캡처를 수행하지 않는다는 의미다. 실제 UI 검증은 아래에 별도로 기록한다.

## 실제 UI 및 접근성

VS Code Extension Development Host에서 현재 Captain을 열고 Computer Use로 직접 조작했다. 웹용 Browser 런타임 목록은 비어 있어 독립 브라우저의 지정 CSS viewport 검증은 수행하지 않았다.

- Address: Related diagram 진입, 연결 카드 선택 시 두 관계만 필터링, 전체 보기로 원래 모델·필터 복원.
- MeetingDocument: 78개 관계가 14개 카드로 배치됨. 2페이지의 52개 모델 Leaf 카드에 직선 하나가 연결되고 선택 시 관계 목록도 52개로 좁혀짐.
- Leaf 멤버 MeetingDocumentCache로 이동한 뒤 Back으로 MeetingDocument의 2페이지·선택 묶음 복원.
- 일반 카드의 Explore model로 OptionExerciseClaimMeetingDocument를 새 중심으로 탐색한 뒤 Back으로 페이지·선택 관계 복원. Escape로 진입 모델의 전체 보기 복원.
- References 필터에서 다이어그램과 목록이 모두 3개 관계로 좁혀짐. 존재하지 않는 검색어 입력 시 0개 관계와 Clear filters 안내가 나타나며, 입력 포커스와 전체 검색어가 유지됨.
- Tab으로 관계 행까지 이동한 뒤 Enter로 압축 화면에 진입. 다른 페이지에 있는 Leaf 카드가 자동 선택되고 상단에는 정확한 상속 필드가 표시됨.
- 넓은 창, 중간 폭 창, 세로로 쌓이는 좁은 창을 실제 렌더링으로 확인했다. 좁은 창에서 4개 카드씩 두 열로 바뀌고 선택한 관계가 있는 페이지가 유지된다. 특정 모바일 기기나 태블릿을 에뮬레이션한 검증은 아니다.
- 긴 Leaf 카드의 제목이 좁은 창에서 가려지던 현상을 수정하고 실제 화면에서 재확인했다. 선택·페이지·열 배치가 바뀌면 선택 카드를 보이게 스크롤한다. 그 후 수동 스크롤로 마지막 카드까지 이동해도 직선 끝점이 카드 경계에 유지된다.
- [넓은 창 캡처](.tmp/captain-improvement/leaf-compounds/related-diagram-final-wide.jpg), [중간 폭 캡처](.tmp/captain-improvement/leaf-compounds/related-diagram-final-medium.jpg), [좁은 창 캡처](.tmp/captain-improvement/leaf-compounds/related-diagram-final-narrow.jpg).
- `ui-design-workflow`와 Impeccable의 제품 UI 원칙을 적용했다. 기계 검사에서 기존 canvas 격자 배경만 advisory로 나왔으며, 실제 다이어그램 좌표면이므로 유지했다.
- [Web Interface Guidelines](https://raw.githubusercontent.com/vercel-labs/web-interface-guidelines/main/command.md)에 따라 버튼/입력의 접근 가능한 이름, 키보드 작동, 선택 상태, 포커스 표시, 긴 식별자 처리, 검색 결과 안내, reduced motion을 점검했다. 스크린 리더 낭독과 브라우저 개발자 콘솔은 별도로 검증하지 않았다.

압축 화면은 선택 모델의 직접 관계를 새 위치에 표시하는 임시 탐색 상태다. 전체 보기의 교차 수가 추가로 감소했다고 해석하지 않는다. 기존 저장 배치와 묶음 의미를 유지한다. 간접 관계 전체를 재귀적으로 펼치거나 임의의 여러 중심 모델을 동시에 선택하는 기능은 이번 범위에 포함하지 않는다.

## 후속 수정: 큰 Leaf 카드의 선택 강조

전체 다이어그램에서 모델을 선택하면 일반 카드는 선택 영역 밖에서 흐려졌지만, 큰 Leaf 카드의 배경·테두리·제목에는 같은 규칙이 적용되지 않았다. 멤버들의 선택·클러스터 소속에 따라 큰 카드도 흐리게 표시하고, 멤버 중 하나라도 직접 연결되어 있으면 관계 유형에 따른 테두리 강조를 유지한다. 선택한 멤버가 들어 있는 카드는 선택 색을 사용하며, 선택 해제 시 원래 색으로 복원된다. 기존 WebGL2와 WebGPU의 공통 인스턴스 색 계산에 적용했다.

- TypeScript 빌드 통과: 최대 RSS 186.6MiB.
- 기존 6개 통합 테스트 파일과 새 회귀 사례 2개를 포함해 **63/63 통과**: 최대 RSS 128.7MiB. [테스트 로그](.tmp/captain-improvement/leaf-compounds/leaf-card-dimming-tests.log).
- VM에서 실제 렌더 함수의 카드 색·제목·GPU 인스턴스 값을 검사했다. 관련 없는 카드의 흐림, 호버 중 흐림 유지, 멤버 관계 강조, 같은 선택 클러스터, 선택 멤버, 관계 미리보기와 선택 해제 복원을 포함한다.
- 실제 Captain의 전체 보기에서 MeetingDocumentCache 선택 시 다른 큰 카드가 어두워지고, 해당 멤버를 담은 카드가 강조되는 것을 확인했다. 빈 공간 클릭으로 선택을 해제하면 큰 카드의 밝기도 복원된다.
- [수정 전](.tmp/captain-improvement/leaf-compounds/leaf-card-dimming-before.jpg), [수정 후](.tmp/captain-improvement/leaf-compounds/leaf-card-dimming-after-wide.jpg), [선택 해제](.tmp/captain-improvement/leaf-compounds/leaf-card-dimming-cleared.jpg). 이 후속 수정의 시각 검증은 실제 VS Code의 넓은 창에서 수행했으며, 별도 브라우저 viewport와 두 GPU 백엔드 각각의 실기 실행은 수행하지 않았다.

이 수정은 표시 색과 강조만 바꾸며, 저장 좌표·카드 크기·관계 라우팅은 변경하지 않는다.

## 후속 수정: 선택한 모델의 실제 연결 복원

Company 선택 시 관계 데이터를 포함한 번들 대표 선만 강조되어, 실제 선이 다른 모델에서 끝나는 문제가 있었다. 이전 검증은 관계 ID 보존을 확인했지만 각 선택 모델의 실제 끝점 연결은 검사하지 않았다. 현재 Captain의 Company는 큰 카드를 기준으로 연결 대상 113개 중 직접 연결이 4개였으며, 강조 선 8개는 Company에 닿지 않았다.

선택한 모델의 관계를 대표 선에서 분리하고 실제 모델 쌍으로 연결한다. 같은 두 모델의 여러 필드는 한 선으로 묶고, Leaf 그룹은 기존 큰 카드 경계에서 선을 받는다. 나머지 관계는 기존 전체 보기의 묶음을 유지하며, 대표 관계가 분리된 묶음은 남은 관계 중 실제 경로를 새 대표로 사용한다. 선택 해제 시 원래 선과 묶음을 복원한다. 화면의 통계는 전체 보기의 기준값임을 명시한다.

- Company: **직접 연결 113/113, 누락 0, 선택 모델에 닿지 않는 강조 선 0**.
- Captain **1,244개 모델 전체**에서 직접 연결 대상 3,166쌍과 실제 카드 경계 끝점 6,332개를 검사했다. Canonical 관계 1,727개가 각 선택 상태에서도 정확히 한 번 보존된다.
- 선택 해제 후 전체 보기의 모든 선·좌표·멤버 구성이 정확히 복원된다. 전체 보기 **468 crossing / 1.14915248448B / 1,039개 선**을 유지한다. 선택 중 직접 관계를 펼친 화면은 이 전체 보기 지표와 구분한다.
- 실제 브라우저용 메타데이터 선택·경로 생성·Leaf 투영 함수를 사용한 [전체 모델 검사](.tmp/captain-improvement/leaf-compounds/selected-connections.audit.json), [수정 전 Company 검사](.tmp/captain-improvement/leaf-compounds/company-connection-before.json).
- TypeScript 빌드 통과: 최대 RSS 183.3MiB. 새 `selected-relationship-edges.test.mjs`와 기존 6개 통합 테스트를 포함해 **67/67 통과**, 최대 RSS 136.9MiB. [테스트 로그](.tmp/captain-improvement/leaf-compounds/selected-connections-tests.log).
- 회귀 사례는 다른 모델에 붙은 대표 선, 대표 관계 분리, 반대 방향/중복 필드, 큰 카드의 단일 연결, 멤버 숨김, 현재 좌표 이동, Leaf 내부 관계, 선택 해제·재선택을 포함한다.
- 최종 빌드로 VS Code Extension Development Host를 다시 불러온 뒤 실제 Captain에서 Company 검색·Enter 선택과 Fit diagram을 실행했다. Company에서 연결 대상들로 직선이 이어지고 큰 Leaf 카드 경계에도 연결되는 것을 화면에서 확인했다.
- 빈 캔버스를 클릭해 선택을 해제하면 기존 전체 보기의 선과 밝기가 복원됐다. 이어서 캔버스의 Company 카드를 직접 클릭해 같은 직접 연결이 다시 나타나는 것을 확인했다.
- [수정 전 화면](.tmp/captain-improvement/leaf-compounds/company-connections-before.jpg), [수정 후 화면](.tmp/captain-improvement/leaf-compounds/company-connections-after.jpg), [선택 해제](.tmp/captain-improvement/leaf-compounds/company-connections-cleared.jpg), [Company 카드 재클릭](.tmp/captain-improvement/leaf-compounds/company-connections-card-click.jpg). 이번 후속 수정의 시각 검증은 실제 VS Code의 넓은 창에서 수행했다. 독립 브라우저의 모바일·태블릿 viewport와 GPU 백엔드별 실기 검증은 수행하지 않았다.
