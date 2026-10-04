# Captain: 큰 leaf 카드에 직선 하나 — 2026-09-15

큰 카드 외곽을 실제 연결점으로 사용한다. 내부 모델로 선을 분산하지 않는다. 현재 Captain의 1,244개 모델을 기준으로 **Visual Crossing 468 / 영역 1.14915248448B**다. 6월식 관계 개요와 큰 카드 표시를 켠 상태의 수치다.

## F5 실행

1. django-erd-maker의 Run and Debug에서 **Run Captain (Leaf cards 468)**을 선택한다.
2. 기존 개발 실행을 종료하고 F5로 새 개발 호스트를 연다.
3. **Django ERD: Open Diagram**을 실행한다.
4. **Leaf cards (49)**와 **June bundles**가 켜져 있는지 확인한다. **Find leaf card → MeetingDocument · 52 leaves**로 가장 큰 카드를 찾을 수 있다.

실행 구성은 [전용 Captain 워크스페이스](.vscode/captain-leaf-cards.code-workspace)와 [468 후보](data/erd-poc/candidates/captain-leaf-card-connections-468.layout.json)를 읽는다. 일반 FMMM 최적화에 연구용 축약 계산기를 자동 적용한 것은 아니다.

## 표시 동작

- 같은 큰 카드와 외부 모델 사이에는 방향에 관계없이 한 직선만 표시한다. 현재 개요에서는 큰 카드 49개가 각각 정확히 한 선을 갖는다.
- 선은 큰 카드 외곽에서 끝난다. 기존 직선을 외곽에서 잘라 연결하므로 꺾임이나 내부 분기가 없다.
- 원래 모델은 내부 카드로 선택할 수 있고, 관계는 상세 정보에 남는다. 그룹을 이동하거나 모델을 숨기면 현재 좌표와 가시성으로 연결을 다시 계산한다.
- 기존 June 관계 묶음을 유지한다. June bundles를 꺼도 Leaf cards가 켜져 있으면 큰 카드 연결은 공유한다. 두 버튼을 모두 끄면 개별 모델 관계를 표시한다.
- 교차 측정에서도 큰 카드 전체를 장애물로 센다. 카드 내부의 빈틈을 통과하는 다른 선도 관통으로 집계한다. 내부 모델 간격은 별도로 검사한다.

## 실제 F5와 저장 데이터의 차이

처음 검증에 사용한 저장 payload는 1,219개 모델이었다. 실제 F5 분석 결과는 1,244개 모델·3,299개 구조 관계였다. 이전 후보를 그대로 읽으면 비자기 연결 51개가 입력과 맞지 않고, 일부 모델이 없어 49개 중 47개 큰 카드만 유효했다. 완전한 관계 집합을 요구하는 June 개요도 해제됐다.

[현재 analyzer 결과](data/erd-poc/recovered/captain-2026-09-15-payload.json)를 다시 보존하고 기존 배치를 맞췄다. 현재 그래프가 모델 이름과 관계의 기준이다. 없어진 모델 네 개의 위치를 새 모델에 재사용하고, 추가 모델 26개를 빈 공간에 배치했다. 커진 InvestmentAgreementDocument 카드는 같은 큰 카드 안에서 20px 위로 이동해 아래 카드와 42px 간격을 확보했다. 크기 변경으로 안쪽에 남은 기존 끝점도 외곽에 다시 붙였다.

기존 1,219개 자료에서 한 선 표시를 적용한 결과는 [419 기록](data/erd-poc/candidates/captain-leaf-card-connections-419.audit.json)에 별도로 남긴다. 중간 461 후보는 끝점 검증을 통과하지 않아 F5에 사용하지 않는다.

## 최종 검증

[제품 로딩 검사](data/erd-poc/candidates/captain-leaf-card-connections-468.audit.json)는 실제 `runOgdfLayout` 파일 로딩·디코딩·렌더 모델을 통과한 결과다.

| 항목 | 결과 |
|---|---:|
| 실제 모델 | 1,244 |
| 큰 카드 / 내부 모델 | 49 / 258 |
| 비자기 모델 쌍 관계 / 표시 직선 | 1,727 / 1,039 |
| Visual Crossing | **468 = 선 교차 299 + 일반 카드 관통 167 + 큰 카드 관통 2** |
| 큰 카드 외곽까지 포함한 영역 | **1.14915248448B** |
| 카드 겹침 / 외부 간격 위반 / 내부 간격 위반 | 0 / 0 / 0 |
| 큰 카드 연결 수 | 각 1개, 합계 49개 |
| 연결점 검사 | 2,078개 정상 |
| 관계 중복·누락 / 제품 로딩 경고 | 0 / 0 |
| 개별 모델 연결 모드의 Visual Crossing | 4,732 |

TypeScript 빌드와 관련 통합 테스트 **45개**가 통과했다. 현재 데이터 전체에 대해 GPU 화면이 사용하는 경로 함수와 서버 측 측정 경로가 일치함을 VM에서 검사했다. VM 검사는 실제 앱 화면 검증과 구별한다. 빌드·테스트·생성·측정은 모두 256MiB 메모리 감시 아래 직렬로 실행했다.

실제 VS Code 개발 호스트에서도 새 F5 구성으로 실행했다. **Leaf cards (49)**와 **June bundles**가 켜지고, Diagram 패널에 **1,244개 모델 / 1,039개 선 / 1,727개 관계 / visual 468**이 표시되는 것을 확인했다. 찾기로 MeetingDocument의 52개 leaf 카드를 확대해 한 선이 큰 카드 외곽에서 끝나고 내부 분기가 없는 것을 확인했다. Leaf cards 끄기·켜기와 확대·화면 이동도 실행했다. [실제 앱 화면](data/erd-poc/candidates/captain-leaf-card-connections-468.ui.png)을 보존했다.

[F5 출력 로그에서 추출한 기록](data/erd-poc/candidates/captain-leaf-card-connections-468.f5-evidence.json)도 `unroutedInputEdges=0`, `visualCrossings=468`, 실제 WebGPU 초기 scene의 `edgeSegments=1039`, `leafBundleRecords=49`, 큰 카드 관통 2개를 확인한다.

확인은 macOS VS Code 데스크톱 창(1458×768)에서 진행했다. 실제 앱에서 그룹 드래그·숨김 조작을 다시 검사한 것은 아니며 해당 경로는 통합 테스트로 검증했다. In-app Browser 런타임에는 사용 가능한 브라우저가 없어 Computer Use를 사용했다.

재생성은 [refresh_captain_leaf_snapshot.cjs](scripts/erd-poc/refresh_captain_leaf_snapshot.cjs), 검사는 [audit_leaf_card_connections.cjs](scripts/erd-poc/audit_leaf_card_connections.cjs)를 사용한다. 두 도구는 각각 `원본 배치 / 현재 payload / 출력 stem`을 인자로 받으며 `run_memory_bounded.py` 안에서 실행한다. 재생성의 원본은 `captain-leaf-cards-423.layout.json`이다.

후보 SHA-256: `5300d93ab2f6412d555a3b4e359f43cb3f176e7cba324b2162e3c58f5aac7d36`.

## 저자원 후속 실험 — 2026-10-02

사용자의 약 20% 자원 예산에 맞춰 단일 스레드·256MiB RSS 감시 아래 한 작업씩 실행했다. 장비의 논리 CPU는 15개이며, 탐색 프로세스에는 낮은 실행 우선순위(`nice +10`)를 적용했다. CPU 사용률을 직접 제한하거나 전체 작업의 실측 비율을 확인한 것은 아니다.

[459 후보](data/erd-poc/candidates/captain-leaf-card-connections-459.layout.json)는 위의 468 배치와 같은 저장 payload를 사용한다. 카드 위치와 크기를 고정하고, 큰 Leaf 카드에 닿지 않는 단일 관계 중 충돌 관여도가 높은 80개를 대상으로 연결점만 탐색했다. 탐색은 0.43초, 최대 측정 RSS 3.0MiB였고 모든 단계의 최대 측정 RSS는 184.3MiB였다.

처음 제안된 개요 455 후보는 개별 관계가 4,732→4,750으로 악화돼 그대로 채택하지 않았다. 각 변경을 두 보기에서 검사해 악화되는 경로 9개를 제외하고 11개 경로를 보존했다.

| 항목 | 468 기준 | 459 후보 |
|---|---:|---:|
| 개요 Visual Crossing | 468 | **459** |
| 선 교차 / 일반 카드 관통 / 큰 카드 관통 | 299 / 167 / 2 | 295 / 162 / 2 |
| 개별 관계 Visual Crossing | 4,732 | **4,717** |
| 카드 외곽 포함 면적 | 1.14915248448B | 1.14915248448B |
| 외부 / 내부 간격 위반 | 0 / 0 | 0 / 0 |
| 모델 / 관계 / 표시 직선 | 1,244 / 1,727 / 1,039 | 1,244 / 1,727 / 1,039 |

[제품 파일 로딩 검사](data/erd-poc/candidates/captain-leaf-card-connections-459.audit.json)는 관계의 정확히 한 번 보존, 큰 카드마다 직선 하나, 경계 끝점 2,078개, 브라우저용 경로 함수와의 일치, 로딩 경고 0개를 확인했다. 별도 native 재검사에서도 visual 459를 확인했고 hard-condition 판정 합계는 기존과 같은 12였다. 탐색기의 단일 끝점 정확성 자체 검사는 1,920개 비교를 통과했다.

이 후보의 실제 앱 화면과 새로 분석한 Captain 소스는 아직 검증하지 않았다. 기본 F5 구성은 실제 화면을 검사한 468을 유지한다. [재현 도구](scripts/erd-poc/probe_overview_boundary.cjs)는 `export|apply 원본배치 payload 실험폴더 [native출력TSV]`를 받으며 `run_memory_bounded.py` 안에서 사용한다. Native 탐색은 기존 `probe_boundary_sweep.cpp`에 `--selection`, `--seconds 20`, `--rounds 2`, `--joint-probes 0`을 전달했다. [출처·변경 경로·기하 검사·자원 기록](data/erd-poc/candidates/captain-leaf-card-connections-459.provenance.json)에 상세 결과를 보존했다.

### 같은 자원 예산으로 양쪽 연결점 탐색

459 후보를 원본으로 같은 80개 관계 선정 방식과 `--seconds 20 --rounds 2`를 사용하고 `--joint-probes 1`을 켰다. 고정된 상대 끝점만 최적화하는 대신 상대 카드의 경계 후보도 함께 시도했다. 이 탐색은 전역 최적해를 보장하지 않는다.

탐색은 **4.97초 / 최대 측정 RSS 3.1MiB**였다. 제안된 개요 449는 개별 관계가 4,717→4,735로 악화돼, 변경 경로 6개를 제외하고 8개를 채택했다. 최종 [451 후보](data/erd-poc/candidates/captain-leaf-card-connections-451.layout.json)는 **개요 451 = 선 교차 296 + 일반 카드 관통 153 + 큰 카드 관통 2**, 개별 관계 **4,713**이다. 면적 1.14915248448B와 카드 위치·크기·묶음을 유지했다.

제품 파일 로딩·관계 보존·경계 끝점·브라우저용 경로 함수 일치 검사를 통과했다. 외부·내부 간격 위반과 로딩 경고는 0이며, 별도 native 재검사의 hard-condition 합계는 12→8로 줄었다. 이번 단계의 최대 측정 RSS는 **165.7MiB**였고 모든 계산을 단일 스레드·256MiB 제한으로 직렬 실행했다.

[제품 검사](data/erd-poc/candidates/captain-leaf-card-connections-451.audit.json)와 [출처·변경·자원 기록](data/erd-poc/candidates/captain-leaf-card-connections-451.provenance.json)을 보존했다. 이 역시 저장 payload 기준이며 실제 앱 화면과 최신 Captain 소스 분석은 미검증이다. 기본 F5 구성은 468을 사용한다.

### 선정 범위를 160개 관계로 확장

451 후보를 원본으로 선정 범위를 80개에서 160개로 늘렸다. [재현 도구](scripts/erd-poc/probe_overview_boundary.cjs)의 `export`에 `--max-routes 160`을 전달하며, 기본값 80과 최대값 256을 지원한다. Native 탐색의 단일 스레드·20초 상한·2회 반복·양쪽 연결점 탐색 설정은 같았다.

탐색은 **4.18초 / 최대 측정 RSS 3.0MiB**에 완료됐다. 제안된 개요 433은 개별 관계가 4,713→4,728로 악화돼, 경로 10개를 제외하고 변경 11개를 채택했다. 최종 [438 후보](data/erd-poc/candidates/captain-leaf-card-connections-438.layout.json)는 **개요 438 = 선 교차 290 + 일반 카드 관통 146 + 큰 카드 관통 2**, 개별 관계 **4,700**이다. 초기 468 배치 대비 개요 30건·개별 관계 32건 감소다.

면적 1.14915248448B와 카드 위치·크기·묶음은 동일하다. 제품 파일 로딩, 관계 1,727개의 정확히 한 번 보존, 경계 끝점 2,078개, 큰 카드마다 직선 하나, 브라우저용 경로 함수와의 일치를 검사했다. 간격 위반과 로딩 경고는 0이며 별도 native 재검사에서 개요 hard-condition 판정이 **8→0**으로 줄었다. 모든 계산은 256MiB 제한으로 직렬 실행했고, 이번 단계 최대 측정 RSS는 **186.9MiB**였다.

[제품 검사](data/erd-poc/candidates/captain-leaf-card-connections-438.audit.json)와 [출처·변경·자원 기록](data/erd-poc/candidates/captain-leaf-card-connections-438.provenance.json)을 보존했다. 실제 앱 화면과 새 Captain 분석 결과는 아직 미검증이며, 기본 F5 구성은 468이다.

### 충돌에 관여하는 단일 관계 전체 탐색

438 후보에서 `--max-routes 256`으로 충돌에 관여하는 단일 관계 238개를 모두 선정했다. 단일 스레드·256MiB 제한 아래 20초 상한으로 탐색했으며 **10.68초 / 최대 측정 RSS 3.1MiB**에 완료됐다. 제안된 개요 426은 개별 관계가 4,700→4,722로 악화돼, 경로 11개를 제외하고 변경 8개를 채택했다.

최종 [432 후보](data/erd-poc/candidates/captain-leaf-card-connections-432.layout.json)는 **개요 432 = 선 교차 289 + 일반 카드 관통 141 + 큰 카드 관통 2**, 개별 관계 **4,692**다. 초기 468 배치 대비 개요 36건·개별 관계 40건 감소다. 면적 1.14915248448B와 카드 위치·크기·묶음은 동일하다.

제품 파일 로딩, 관계 1,727개의 정확히 한 번 보존, 경계 끝점 2,078개, 큰 카드마다 직선 하나, 브라우저용 경로 함수 일치 검사를 통과했다. 외부·내부 간격 위반과 로딩 경고는 0이며, 별도 native 기하 재검사의 hard-condition 판정도 0이다.

후속으로 기존 묶음 대표 선 13개 중 충돌에 관여하는 1개를 0.11초 탐색했으나 경로 변경과 점수 개선이 없었다. 432에서 충돌에 관여하는 단일 관계 232개를 7.72초 재탐색한 제안도 개별 보기 악화 방지 조건을 적용하면 엄격한 개요 개선을 얻지 못해 채택하지 않았다. 이 탐색은 현재 방법의 한 차례 대조이며 전역 하한이나 최적해 증명이 아니다. 대표 선을 위한 임시 도구 변경은 제거했고 원래 검증한 도구의 해시를 확인했다.

이번 단계의 native 탐색 총 시간은 **18.51초**, 최대 측정 RSS는 **177.6MiB**였다. [제품 검사](data/erd-poc/candidates/captain-leaf-card-connections-432.audit.json)와 [출처·변경·후속 탐색·자원 기록](data/erd-poc/candidates/captain-leaf-card-connections-432.provenance.json)을 보존했다. 실제 앱 화면과 최신 Captain 분석 결과는 아직 미검증이며 기본 F5 구성은 468이다.

### 개요와 개별 보기의 경계를 함께 탐색

이전 탐색은 개요 비용이 가장 낮은 구간의 중간점을 먼저 고른 뒤, 개별 보기를 악화시키는 변경을 제외했다. 같은 개요 비용을 가진 구간 안에서도 개별 보기의 비용은 달라질 수 있어 허용되는 다른 연결점을 놓칠 수 있었다. [두 보기 탐색기](scripts/erd-poc/probe_dual_boundary_sweep.cpp)는 두 그래프의 비용이 바뀌는 경계를 합쳐, **두 보기의 Visual Crossing과 hard-condition 판정을 모두 악화시키지 않는 구간**에서 연결점을 고른다. 고정된 상대 끝점에 대한 0.01 좌표 경계 탐색이며, 양쪽 끝점 후보를 함께 시도해도 전역 최적해를 보장하지 않는다.

작은 그래프에서 경계 좌표를 전부 조사한 결과와 **961개 끝점 탐색 결과**를 비교했다. 개요만 최적화하면 숨겨진 관계의 교차가 늘지만 다른 구간은 두 보기를 함께 개선하는 사례도 포함했다. 지역 비용 변화가 두 그래프 전체의 Visual Crossing·hard-condition 변화와 같은지도 확인했다. 접촉·인접선 교차는 전체 hard-condition 계산에서 두 경로에 각각 집계되므로 지역 변화 계산에서도 이 중복을 반영했다.

432 후보의 충돌에 관여하는 단일 관계 232개를 `--seconds 20 --rounds 2 --joint-probes 1`로 탐색했다. **16.55초**에 끝났으며 경로 40개의 변경을 모두 제품 렌더 모델이 그대로 재현했다. 후처리에서 제외한 변경은 없었다.

| 항목 | 432 기준 | 429 후보 |
|---|---:|---:|
| 개요 Visual Crossing | 432 | **429** |
| 선 교차 / 일반 카드 관통 / 큰 카드 관통 | 289 / 141 / 2 | 286 / 141 / 2 |
| 개별 관계 Visual Crossing | 4,692 | **4,633** |
| 개요 / 개별 native hard-condition 합계 | 0 / 204 | 0 / 176 |
| 카드 외곽 포함 면적 | 1.14915248448B | 1.14915248448B |
| 외부 / 내부 간격 위반 | 0 / 0 | 0 / 0 |

[429 후보](data/erd-poc/candidates/captain-leaf-card-connections-429.layout.json)는 초기 468 대비 개요 **39건**, 개별 관계 **99건**을 줄였다. 카드 위치·크기·관계 묶음은 동일하다. [제품 파일 로딩 검사](data/erd-poc/candidates/captain-leaf-card-connections-429.audit.json)에서 관계 1,727개의 정확히 한 번 보존, 끝점 2,078개, 큰 카드 49개마다 직선 하나, 브라우저용 경로 함수 일치, 로딩 경고 0개를 확인했다. 기존 단일 보기 native 탐색기로도 탐색 없이 개요 429·hard-condition 0을 독립 재측정했다.

모든 계산을 단일 스레드·256MiB 감시 아래 직렬 실행했다. 탐색 프로세스 그룹(Python 캡처 포함)의 최대 측정 RSS는 **20.0MiB**, 제품 로딩 검사는 **173.4MiB**, 컴파일 포함 이번 단계 최대 측정 RSS는 **205.7MiB**였다. 전체 CPU 사용률의 20%를 직접 제한하거나 실측한 것은 아니다.

재현하려면 [내보내기 도구](scripts/erd-poc/probe_overview_boundary.cjs)의 `export`에 `--max-routes 256 --with-individual`을 전달하고, 새 native 탐색기에 `--directory 실험폴더 --out 출력TSV`와 위 탐색 옵션을 전달한다. C++17·`-O2 -ffp-contract=off`로 컴파일하며 모든 실행은 `run_memory_bounded.py`로 감싼다. 기존 `apply`와 제품 로딩 검사 도구로 결과를 검증한다. [출처·경로 변경·두 보기 판정·코드 해시·자원 기록](data/erd-poc/candidates/captain-leaf-card-connections-429.provenance.json)을 보존했다.

저장된 payload 기준의 연구 후보이며 실제 앱 화면과 최신 Captain 분석 결과는 아직 미검증이다. 기본 F5 구성은 468을 사용한다.

### 일반 카드의 제한적 이동

429 후보에서 연결점에 이어 카드 위치도 탐색했다. [두 보기 카드 탐색기](scripts/erd-poc/probe_dual_node_search.cpp)는 개요와 개별 보기에서 연결 관계가 일치하고 모든 연결이 단일 관계인 일반 카드만 이동한다. 큰 카드와 내부 모델의 위치·크기·묶음은 보존한다. 움직이는 카드에 붙은 직선의 끝점을 함께 옮기고 두 보기의 Visual Crossing·hard-condition·간격을 검사한다. 전체 영역을 늘리지 않으며, 각 카드는 원본 후보에서 최대 384px까지만 이동한다.

작은 그래프의 **4,096개 이동**에서 지역 교차·관통·겹침·간격·hard-condition 변화가 전체 재계산과 같은지 확인했다. 실제 데이터에서는 충돌에 관여하며 이동 조건을 만족하는 카드 **132개**를 모두 탐색했다. 2회 반복 탐색은 2.17초, 최대 8회로 확대한 후속 탐색은 세 번째 반복에서 추가 변경 없이 2.88초에 끝났다. 두 탐색은 같은 418 결과를 냈다. 이는 현재 후보 위치 집합에서의 결과이며 전역 최적해나 하한 증명은 아니다.

| 항목 | 429 기준 | 418 후보 |
|---|---:|---:|
| 개요 Visual Crossing | 429 | **418** |
| 선 교차 / 일반 카드 관통 / 큰 카드 관통 | 286 / 141 / 2 | 287 / 129 / 2 |
| 개별 관계 Visual Crossing | 4,633 | **4,591** |
| 개요 / 개별 native hard-condition 합계 | 0 / 176 | 0 / 174 |
| 카드 외곽 포함 면적 | 1.14915248448B | 1.14915248448B |
| 외부 / 내부 간격 위반 | 0 / 0 | 0 / 0 |

[418 후보](data/erd-poc/candidates/captain-leaf-card-connections-418.layout.json)는 일반 카드 **31개**, 연결선 **64개**가 바뀌었고 실제 최대 카드 이동 거리는 **362.04px**다. 선 교차는 하나 늘었지만 카드 관통이 12건 줄어 Visual Crossing 총량이 11건 감소했다. 초기 468과 비교하면 개요 **50건**, 개별 관계 **141건** 감소다.

[제품 적용 검사](scripts/erd-poc/apply_dual_node_proposal.cjs)는 두 보기의 모든 좌표·경로가 native 제안과 일치하는지 검사한다. [제품 파일 로딩 검사](data/erd-poc/candidates/captain-leaf-card-connections-418.audit.json)에서 관계 1,727개의 정확히 한 번 보존, 끝점 2,078개, 큰 카드 49개마다 직선 하나, 브라우저용 경로 함수 일치, 로딩 경고 0개를 확인했다. 기존 native 경계 탐색기로도 탐색 없이 두 보기 전체를 독립 재측정해 **418 / 4,591**, hard-condition **0 / 174**, 겹침·간격 위반 0을 확인했다.

모든 계산은 단일 스레드·256MiB 감시 아래 직렬 실행했다. 카드 탐색 총 시간은 **5.06초**, 탐색 프로세스 그룹 최대 측정 RSS는 **20.0MiB**, 컴파일 포함 이번 단계 최대 측정 RSS는 **171.6MiB**였다. 재현은 기존 `export --with-individual --max-routes 256`, 새 탐색기의 `--seconds 20 --rounds 8 --max-nodes 256 --max-displacement 384`, 새 적용 검사 순서로 실행한다. C++17·`-O2 -ffp-contract=off`로 컴파일하고 `run_memory_bounded.py`로 감싼다. [출처·이동 목록·자원 기록](data/erd-poc/candidates/captain-leaf-card-connections-418.provenance.json)을 보존했다.

후속으로 418 배치의 충돌에 관여하는 단일 관계 229개를 기존 두 보기 경계 탐색기로 17.86초 탐색했다. 제안은 개요 418·개별 관계 4,590이었지만 엄격한 개요 개선 조건을 만족하지 않아 채택하지 않았다. 제품 렌더 경로와 점수 일치 검사를 통과한 뒤 해당 조건에서 거절됐으며, 이 제안의 별도 제품 파일 로딩 검사는 수행하지 않았다. 이 후속 탐색까지 포함한 native 탐색 총 시간은 **22.92초**다.

저장 payload 기준이며 실제 앱 화면과 최신 Captain 분석 결과는 미검증이다. 기본 F5 구성은 468이다.

## ML 모델이 이동량을 생성 — 2026-10-03

사용자가 “니가 줄이지 말고 ML모델이 줄이게끔 하라”고 방향을 바꿨다. 이후 경로는 [학습된 정책](scripts/erd-poc/learn_card_policy.py)이 이동량을 생성한다. [기하 환경](scripts/erd-poc/ml_card_environment.cpp)은 현재 상태를 관찰하고 주어진 이동을 검증·적용한다. 추론 중 native 후보 탐색·대체 이동 생성·좌표 보정은 호출하지 않는다.

모델은 58개 상대 기하 특징을 입력받는 9,301개 파라미터의 작은 신경망이다. 네 혼합 성분의 이동 평균·분산·확률을 출력하므로 사전에 정한 격자 이동 목록으로 제한되지 않는다. 여러 유효 이동 방향을 하나의 평균으로 합치지 않도록 [Bishop의 mixture density network](https://www.microsoft.com/en-us/research/publication/mixture-density-networks/) 형식을 사용했다. 모델 이름·절대 좌표·Captain의 정답 위치는 입력이나 학습 목표로 사용하지 않았다.

학습은 합성 그래프 **64개**, 무작위 연속 이동 **60,704개**의 기하 측정 결과로 진행했다. 그중 **7,565개**가 개요를 개선했다. 그래프 51개를 학습에, 나머지 13개를 검증에 사용했고 Captain은 포함하지 않았다. 검증 우도로 선택한 6번째 epoch·1,140회 갱신의 가중치를 저장했다. 총 학습은 60 epoch·4.98초였으며, 검증 음의 로그우도는 초기 -0.00524에서 최선 **-0.49195**로 낮아졌다. [모델 체크포인트](data/erd-poc/checkpoints/leaf-card-policy-v1.npz), [학습 기록](data/erd-poc/checkpoints/leaf-card-policy-v1.training.json), [압축 학습 자료](data/erd-poc/checkpoints/leaf-card-policy-v1.training-data.jsonl.gz)를 보존했다.

같은 418 입력·seed 123·제안 12,000회로 학습 전 가중치와 저장된 학습 가중치를 비교했다.

| 항목 | 시작 | 학습 전 가중치 | 학습된 모델 |
|---|---:|---:|---:|
| 개요 Visual Crossing | 418 | 418 | **417** |
| 개별 관계 Visual Crossing | 4,591 | 4,590 | **4,585** |
| 채택된 이동 | — | 1 | 3 |
| 개요 / 개별 hard-condition | 0 / 174 | 0 / 174 | 0 / 174 |
| 외부 / 내부 간격 위반 | 0 / 0 | 0 / 0 | 0 / 0 |

학습·검증에 쓰지 않은 추가 합성 그래프 4개에서는 각각 제안 512회를 허용했다. 합계 Visual Crossing은 초기 **559**, 학습 전 **465**, 학습 후 **452**였다. 학습 후가 3개 그래프에서 더 좋았고 1개에서는 더 나빴다. [대조 평가](data/erd-poc/checkpoints/leaf-card-policy-v1.validation.json)는 이 작은 평가 범위를 기록하며 모든 그래프에서 개선된다고 주장하지 않는다.

최종 [ML 417 후보](data/erd-poc/candidates/captain-leaf-card-connections-ml-417.layout.json)는 카드 **3개**와 연결선 **11개**가 바뀌었다. **417 = 선 교차 286 + 일반 카드 관통 129 + 큰 카드 관통 2**다. 모델·관계·크기·큰 카드 배치와 면적 **1.14915248448B**를 유지했다. 독립 추론 실행에서 같은 좌표·경로를 재현했고, 저장된 모델로 제안 **12,000개 전부**를 다시 계산해 일치함을 확인했다. [행동 기록](data/erd-poc/candidates/captain-leaf-card-connections-ml-417.actions.jsonl.gz)과 [입력 관찰 기록](data/erd-poc/candidates/captain-leaf-card-connections-ml-417.observations.jsonl.gz)을 보존했다.

[제품 로딩 검사](data/erd-poc/candidates/captain-leaf-card-connections-ml-417.audit.json)는 관계 1,727개의 정확히 한 번 보존, 경계 끝점 2,078개, 큰 카드 49개마다 직선 하나, 경로 함수 일치, 로딩 경고 0개를 확인했다. 기하 변화 4,096개, 특징의 평행이동 불변성, 신경망 미분 44개도 검사했다. 미분의 최대 절대 오차는 4.90e-10 미만이었다.

이제 아래 명령이 내보내기·모델 추론·동작 재현·제품 적용·제품 로딩 검사를 모두 수행한다. 새 출력 폴더를 사용한다. 모델은 고정된 가중치로 추론하며 개선 후보가 없으면 native 탐색으로 넘어가지 않는다.

```sh
python3 scripts/erd-poc/run_memory_bounded.py -- \
  python3 scripts/erd-poc/run_learned_leaf_layout.py \
  --source data/erd-poc/candidates/captain-leaf-card-connections-418.layout.json \
  --payload data/erd-poc/recovered/captain-2026-09-15-payload.json \
  --out .tmp/leaf-ml-reproduction --budget 12000 --seed 123
```

단일 스레드·256MiB 감시 아래 전체 자동 실행은 **19.26초**, 최대 측정 RSS는 **241.3MiB**였다. 별도 추론 실행은 약 **5.09초 / 41.3MiB**, 학습 프로세스 그룹은 최대 **114.2MiB**였다. 전체 CPU의 20%를 직접 실측하거나 강제 제한한 것은 아니다. [모델·대조·코드 해시·자원·재현 기록](data/erd-poc/candidates/captain-leaf-card-connections-ml-417.provenance.json)을 보존했다.

저장 payload 기준의 ML 실행 경로다. 실제 앱 화면과 최신 Captain 분석은 미검증이며 F5 기본값은 468이다.

## 2026-10-03: 학습 모델의 이동 범위 확대

이후 목표는 **개요 300 이하·개별 보기 2,000 미만**이다. 초기 정책이 움직이지 못하던 일반 카드와 Leaf 묶음까지 학습 모델이 이동량을 제안하도록 확장했다. [공유 배치 ML 386 후보](data/erd-poc/candidates/captain-leaf-card-connections-ml-386.layout.json)는 **개요 386 / 개별 보기 3,453**이며 목표에는 아직 미달한다. 1,244개 실제 모델·1,727개 관계·49개 Leaf 카드·카드 크기와 묶음 내부 배치를 유지하고, 겹침과 간격 위반은 0이다.

모델 v2~v5는 합성 그래프로 학습했다. v6는 Captain에서 좌표를 확정하지 않고 수집한 보상 관찰도 학습에 사용했으므로 Captain을 학습에 사용하지 않은 평가로 해석하면 안 된다. v7은 부모 카드와 Leaf를 함께 움직이는 행동을 학습했다. 모든 실제 이동량은 저장된 모델에서 나오며 기하 환경은 관계 투영·간격·교차를 측정하고 승인된 이동을 적용한다. 7단계의 총 112,000개 제안을 저장된 모델로 재현했다. [제품 검사](data/erd-poc/candidates/captain-leaf-card-connections-ml-386.audit.json), [이전 단계](data/erd-poc/candidates/captain-leaf-card-connections-ml-387.provenance.json), [추가 단계 및 모델](data/erd-poc/candidates/captain-leaf-card-connections-ml-386.provenance.json)을 보존했다.

보기별 좌표를 따로 쓰는 실험도 진행 중이다. 이 실험의 개요 점수와 개별 보기 점수는 서로 다른 배치에서 나온다. 제품의 보기 전환에는 아직 적용하지 않았으며, 두 점수를 공유 배치의 결과로 합산해 보고하지 않는다. 최신 실험과 재개 지점은 [context.md](context.md)에 기록한다. 실행은 한 번에 하나, 수치 계산 스레드 1개, 추론 20초·20,000개 제안 이내, 전체 프로세스 그룹 RSS 감시 256MiB를 유지한다.
