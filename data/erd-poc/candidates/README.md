# Captain 보존 후보

현재 앱 확인용 [최신 학습 체크포인트 배치](captain-ml-latest-checkpoints.layout.json)는 **개요 287 / 개별보기 1,963**이다. 각 보기의 최신 학습 64회 체크포인트를 한 번 추론한 좌표와 연결선을 그대로 내보냈다. 학습 모델의 최선 저장 결과로 대체하지 않았으며 이번 앱 업데이트에서 추가 학습이나 좌표 탐색은 수행하지 않았다. F5에서 **Run Captain (ML latest checkpoints)**를 선택하고 **Django ERD: Open Diagram**을 실행한다. **June bundles와 Leaf cards를 모두 끄면** 개별보기가 표시된다. 비교용 **Run Captain (ML best 285-1963)**는 기존 최선 배치 **285 / 1,963**을 불러온다.

최신 체크포인트의 전체 Native 재측정, 앱 파일 로딩, 모든 좌표·연결선 보존과 개요→개별→개요 전환을 검증했다. 기존 보기 전환 테스트 3개도 통과했다. 현재 앱 소스를 경량 컴파일러로 새로 빌드했고 F5에서는 저장된 빌드와 체크포인트의 해시를 확인한다. 성공한 작업의 최대 측정 RSS는 **112.4MiB**였다. 전체 TypeScript 타입검사는 128MiB 감시 한도에서 완료하지 못했으며 실패 로그를 보존했다. 브라우저 연결 초기화 오류로 실제 화면·뷰포트 검사는 수행하지 못했다. [검증](captain-ml-latest-checkpoints.audit.json), [체크포인트·빌드 출처](captain-ml-latest-checkpoints.provenance.json). 현재 목표 **150 / 750은 미달**이다.

앱 확인용 배치는 고정해 두었다. 이후 별도 실험의 195회 학습과 47개 출력도 최선 결과를 개선하지 못했으며, 모든 출력과 가중치 갱신을 검증했다. 기존 연결을 보존하는 이동 제약이 실제 변위를 최대 0.05픽셀로 축소하는 병목을 확인했다. [실험 기록](../experiments/independent-views-common-star-fields-20261004/manifest.json).

추가한 [보정 모델·소형 신경망 실험](../experiments/independent-views-calibrated-geometry-fields-20261004/manifest.json)에서도 **285 / 1,963을 보존**한다. 이미 전수 측정한 이동으로 14개 가중치의 보정 모델을 학습하고, 공동 카드 이동을 생성하는 18개 공유 가중치 모델을 학습했다. 새 업데이트 2,430회와 연속 이동 순위 1,952개를 재현했다. 기존 끝점이 갈라지는 현상을 재현해 모든 모델 출력에서 공유 끝점을 함께 움직이도록 묶었지만, 개별보기의 인접선 교차 위반과 추가 감소는 해결하지 못했다. 전체 기하 검사 1,091회를 대조했으며 공유 끝점 모델의 실제 제안 23개는 모두 전수 재측정했다. 해당 모델의 학습 보상은 380개 중 16개만 별도 전수 재측정했고 나머지 364개는 저장된 측정치로 업데이트를 재현했다. 중복 제안으로 중단된 이전 실행의 8개 결과는 전수 재측정하지 않았다. 보정 검증은 같은 장면 안의 이동 분리이며 미사용 장면의 성능을 증명하지 않는다. 직렬·단일 스레드·128MiB 감시를 유지했고 기록 압축 전 최대 측정 RSS는 83.7MiB였다. 현재 목표 **150 / 750은 미달**이며 실제 화면 검사는 여전히 미완료다.

추가한 [기하 이벤트 모델 실험](../experiments/independent-views-geometry-world-20261004/manifest.json)에서 파라미터 1,538개의 신경망을 학습하고 업데이트 29,920회를 재현했다. 모델 순위 8,040개와 시도한 이동 736개의 전체 기하 검사를 대조했으나 감소는 없어 **285 / 1,963을 보존**한다. 현재 장면의 검증용 쌍 분류는 모두 맞혔지만 큰 이동의 교차 수 예측에는 오차가 있었다. 이 장면의 검증 결과가 다른 장면의 성능을 증명하지는 않는다. 다음 학습에는 전수 재측정한 결과를 사용하고 여러 카드의 공동 이동을 평가한다. 단일 스레드·128MiB 감시를 유지했으며 첫 자료 생성의 메모리 초과는 작은 묶음 처리로 수정했다. 실제 앱 화면 검사는 여전히 미완료다.

최선 [보기별 ML 배치](captain-ml-independent-views.layout.json)는 **개요 285 / 개별 보기 1,963**이다. 이전 300 이하 / 2,000 미만 목표는 달성했으며, 현재 목표 **150 / 750에는 미달**이다. 각 보기의 카드 위치와 직선 경로를 함께 저장하며 카드 크기, 1,244개 모델, 1,727개 관계, 개요의 49개 Leaf 카드와 간격을 보존한다. F5에서 **Run Captain (ML best 285-1963)**를 선택한다. 개별 보기는 **June bundles와 Leaf cards를 모두 끄면** 표시된다. 보기별 수동 이동과 확대 상태는 따로 유지하며 Reset View와 Refresh는 개요로 돌아온다.

실제 제품 파일 로딩과 개요→개별→개요 전환 함수의 전체 경로·충돌 수를 검증했다. 이번 통합 검증의 최대 측정 RSS는 **111.6MiB**이며, 수치 작업은 직렬·단일 스레드·128MiB 감시로 실행했다. 실제 Captain 이동 결과 2,512개를 기존 데이터에 추가해 학습 업데이트 23,488회를 실행하고 734회 시점의 가중치를 선택했다. 전체 데이터·검증용 조합 분리·업데이트·저장 가중치의 재현을 통과했다. 전역 이동 검증 손실은 줄었지만 기존 Captain 검증 손실은 늘었다. 같은 조건에서 이전 모델과 새 모델 모두 동일한 이동으로 개요 286→285를 얻었고 개별보기는 1,963을 보존했다. 학습 때문에 이번 개선이 발생했다고 단정하지 않는다. 새 제안 4,349개의 모델 선택·좌표와 모든 연속 이동 입력 상태를 대조했다. 새 연속 이동과 학습용 이전 보상은 전수 재측정했고 비교 실행은 표본·승리 좌표를 전수 기하 검사했다. 제품 코드의 이전 빌드·검사는 반복하지 않았다. 이전 브라우저 초기화 실패가 있어 실제 화면 검사는 미완료다. [제품 검증](captain-ml-independent-views.audit.json), [모델·단계 출처](captain-ml-independent-views.provenance.json), [최신 학습·대조·정책 및 자원 기록](../experiments/independent-views-global-owner-learning-20261004/manifest.json). 압축 기록 474개 매핑의 복원 해시를 독립 검증했고 이전 286 / 1,963 후보도 보존한다.

이전 [전역 이동·관측 캐시 실험](../experiments/independent-views-trained-owner-policies-20261004/manifest.json)은 추가 학습 없이 2,771개 제안으로 개요 288→286을 얻었다. 해당 실험의 2,259개 전역·연속 이동 결과를 이번에 별도로 전수 재측정해 학습 자료로 추가했다.

이전 [단일 카드 이동 학습·대조 실험](../experiments/independent-views-single-owner-learning-20261004/manifest.json)에서는 학습한 모델과 이전 모델 모두 개요 289→288을 얻었다. 합성 이동 4,096개의 기하 측정과 학습 업데이트 20,512회·저장 가중치의 정확한 재현을 별도로 검증했다. 이 학습 이력은 최신 추론 작업의 새 업데이트 수에 포함하지 않는다.

카드 위치와 연결점을 함께 예측하는 공유 신경망을 추가하고, 연결점·이동 모델을 이어 적용해 개별보기 1,996→1,990을 얻었다. 중간 1,991 분기는 모서리에서 안쪽으로 향하는 끝점 때문에 제품 로더가 거부했다. 이 실패 기록도 보존하고 연구용 손실·검증기를 제품의 바깥 방향 조건과 맞췄다. 최종 1,990은 강화한 검사와 실제 결합 파일 로딩을 통과한 별도 분기다. 이번 수치 작업은 직렬·단일 스레드로 실행했고 최대 측정 RSS는 컴파일 240.7MiB, 학습 219.6MiB였다.

이전 후속 7개 단계에서는 학습식에 실제 Leaf 대표 선과 연결점 유지 방식을 반영했으나 기존 최선을 넘는 유효 후보는 얻지 못했다. [정책 제안 15,933개·NN 배치 449개와 실패 원인 기록](../experiments/independent-views-attached-20261003/manifest.json)을 보존했다. 네이티브 기하 조건과 256MiB 감시는 유지했다.

그다음 7개 단계에서는 기하 조건을 학습에 추가하고 그래프 구조 입력·넓은 이동 범위·표본 손실을 검증했다. 유효 제안은 늘었지만 299 / 1,996을 넘지는 못했다. [NN 배치 497개와 메모리·검증 기록](../experiments/independent-views-hard-graph-20261003/manifest.json)을 보존했으며, 모든 후보의 채택에는 전수 기하 검증을 적용했다.

이전 공유 배치 ML 후보는 [captain-leaf-card-connections-ml-386.layout.json](captain-leaf-card-connections-ml-386.layout.json)이다. 두 보기의 카드 위치를 공유하면서 **개요 386 / 개별 보기 3,453**이다. 1,727개 관계·49개 Leaf 카드·실제 카드 크기와 묶음 내부 배치를 유지하며 겹침과 간격 위반은 0이다. 기존 387 후보에서 학습 정책 v6/v7이 추가로 생성한 32,000개 제안의 재현과 제품 로딩·좌표 검사를 통과했다. [제품 검사](captain-leaf-card-connections-ml-386.audit.json), [모델·단계별 기록](captain-leaf-card-connections-ml-386.provenance.json). 이 후보 자체는 당시 목표인 **개요 300 이하·개별 보기 2,000 미만에 미달**했다. 단일 스레드·256MiB 감시를 유지했으며 당시 실제 UI는 미검증이고 F5 기본값은 468이었다. 당시에는 보기별 위치를 따로 최적화하는 실험을 제품에 적용하지 않았다.

이전 [387 ML 후보](captain-leaf-card-connections-ml-387.layout.json)는 1,035개 물리 카드와 Leaf 묶음을 움직일 수 있는 학습 정책으로 **개요 417→387 / 개별 보기 4,585→3,492**까지 줄였다. 총 80,000개 제안을 저장된 모델로 재현했으며 제품 로딩과 두 보기 좌표 검사를 통과했다. 마지막 단계는 추론 14.45초, 전체 검증 포함 22.97초·최대 측정 RSS 186.4MiB였다. [제품 검사](captain-leaf-card-connections-ml-387.audit.json), [모델·단계별 기록](captain-leaf-card-connections-ml-387.provenance.json).

이전 **ML 생성 후보**는 [captain-leaf-card-connections-ml-417.layout.json](captain-leaf-card-connections-ml-417.layout.json)이다. 합성 그래프만으로 학습한 [모델](../checkpoints/leaf-card-policy-v1.npz)이 카드 이동량을 직접 생성해 **개요 418→417 / 개별 관계 4,591→4,585**로 줄였다. 같은 12,000회 제안에서 학습 전 가중치는 개요 418에 머물렀다. 추론에 native 제안 탐색이나 보정을 사용하지 않았으며, 모든 제안의 체크포인트 재현과 제품 로딩 검사를 통과했다. 단일 스레드·256MiB 제한 아래 전체 자동 실행은 19.26초, 최대 측정 RSS 241.3MiB였다. [제품 검사](captain-leaf-card-connections-ml-417.audit.json), [모델·이동·대조·자원 기록](captain-leaf-card-connections-ml-417.provenance.json), [학습 및 미사용 그래프 평가](../checkpoints/leaf-card-policy-v1.validation.json). 실제 앱 화면과 최신 Captain 분석은 아직 미검증이며 기본 F5 구성은 468이다.

이전 규칙 기반 후보는 [captain-leaf-card-connections-418.layout.json](captain-leaf-card-connections-418.layout.json)이다. 일반 카드 31개를 최대 362.04px 옮기고 연결선 64개의 끝점을 함께 이동해 **개요 429→418 / 개별 관계 4,633→4,591**로 줄였다. 카드 크기·관계 묶음·큰 카드 배치·전체 면적을 보존했으며 간격 위반은 0이다. 단일 스레드·256MiB 제한에서 카드 탐색 합계 5.06초, 연결점 후속 탐색 포함 총 22.92초, 이번 단계 최대 측정 RSS 171.6MiB였다. 후속 연결점 탐색에서는 개요 점수가 더 줄지 않았다. [제품 로딩 검사](captain-leaf-card-connections-418.audit.json)와 [실험 기록](captain-leaf-card-connections-418.provenance.json)을 보존했다.

이전 저자원 연구 후보는 [captain-leaf-card-connections-429.layout.json](captain-leaf-card-connections-429.layout.json)이다. 개요와 개별 보기의 경계 비용을 함께 탐색하고 경로 40개의 연결점을 바꿔 **개요 432→429 / 개별 관계 4,692→4,633**으로 줄였다. 면적·카드 배치·관계 묶음을 유지했고 개요의 native hard-condition 판정은 0이다. 두 보기의 악화 방지와 전수 경계 비교 961개를 검사했다. 단일 스레드·256MiB 제한 아래 탐색 16.55초·탐색 프로세스 그룹 최대 측정 RSS 20.0MiB·컴파일 포함 이번 단계 최대 측정 RSS 205.7MiB였다. [제품 로딩 검사](captain-leaf-card-connections-429.audit.json)와 [실험 기록](captain-leaf-card-connections-429.provenance.json)을 보존했다. 실제 앱 화면과 최신 Captain 분석 결과는 아직 미검증이며 기본 F5 구성은 468이다.

이전 저자원 연구 후보는 [captain-leaf-card-connections-432.layout.json](captain-leaf-card-connections-432.layout.json)이다. 충돌에 관여하는 단일 관계 238개를 살펴보고 변경 8개를 채택해 **개요 438→432 / 개별 관계 4,700→4,692**로 줄였다. 면적·카드 배치를 유지했고 native hard-condition 판정은 0이다. 첫 탐색 10.68초·후속 탐색 포함 총 18.51초·이번 단계 최대 측정 RSS 177.6MiB였다. 대표 선 탐색과 432에서의 재탐색에서는 추가 채택 결과가 없었다. [제품 로딩 검사](captain-leaf-card-connections-432.audit.json)와 [실험 기록](captain-leaf-card-connections-432.provenance.json)을 보존했다. 실제 앱 화면과 최신 Captain 분석 결과는 아직 미검증이며 기본 F5 구성은 468이다.

이전 저자원 연구 후보는 [captain-leaf-card-connections-438.layout.json](captain-leaf-card-connections-438.layout.json)이다. 선정 범위를 80개에서 160개로 넓히고 연결점 변경 11개를 채택해 **개요 451→438 / 개별 관계 4,713→4,700**으로 줄였다. 면적·카드 배치를 유지했으며, 개요의 native hard-condition 판정도 8→0으로 줄었다. 탐색 4.18초·이번 단계 최대 측정 RSS 186.9MiB였다. [제품 로딩 검사](captain-leaf-card-connections-438.audit.json)와 [실험 기록](captain-leaf-card-connections-438.provenance.json)을 보존했다. 실제 앱 화면과 최신 Captain 분석 결과는 아직 미검증이며 기본 F5 구성은 468이다.

이전 [captain-leaf-card-connections-451.layout.json](captain-leaf-card-connections-451.layout.json)은 같은 자원 제한으로 탐색한 후보다. 459에서 양쪽 연결점을 함께 탐색하고 변경 8개를 채택해 **개요 459→451 / 개별 관계 4,717→4,713**으로 개선했다. 초기 468 대비 개요 17건·개별 관계 19건 감소다. 면적과 카드 배치는 동일하며, 탐색 4.97초·이번 단계 최대 측정 RSS 165.7MiB였다. [제품 로딩 검사](captain-leaf-card-connections-451.audit.json)와 [실험 기록](captain-leaf-card-connections-451.provenance.json)을 보존했다. 저장 payload 기준의 후보이며 실제 앱 화면과 새 Captain 분석 결과는 미검증이다.

2026-10-02 저자원 후속 후보는 [captain-leaf-card-connections-459.layout.json](captain-leaf-card-connections-459.layout.json)이다. 저장된 1,244개 모델 자료에서 연결 11개의 경계 끝점만 조정해 **개요 468→459 / 개별 관계 4,732→4,717**로 줄였다. 면적은 **1.14915248448B**, 카드 위치·크기·묶음은 동일하다. 단일 스레드·256MiB 제한으로 실행했으며 최대 측정 RSS는 184.3MiB다. [제품 로딩 검사](captain-leaf-card-connections-459.audit.json)와 [실험·자원 기록](captain-leaf-card-connections-459.provenance.json)을 보존했다. 실제 앱 화면은 아직 검사하지 않았고 기본 F5 구성은 아래의 468을 사용한다.

이전 F5 구성 **Run Captain (Leaf cards 468)**은 [captain-leaf-card-connections-468.layout.json](captain-leaf-card-connections-468.layout.json)을 읽는다. 현재 Captain 1,244개 모델·1,727개 모델 쌍 관계를 포함하며, 큰 카드 49개마다 외곽에 직선 하나만 연결한다. **Visual Crossing 468 / 영역 1.14915248448B**, 끝점·관계 보존·카드 간격·제품 로딩 검사가 통과했다. [실행 방법과 검증 범위](../../../captain-leaf-card-connections.md), [검사 결과](captain-leaf-card-connections-468.audit.json).

이전 [captain-leaf-cards-423.layout.json](captain-leaf-cards-423.layout.json)은 1,219개 모델 기준의 외곽 표시 단계다. 같은 옛 자료에 한 선 연결을 적용한 419 기록과 현재 그래프에 맞추던 중간 461 후보도 보존했다. 461은 끝점 검증을 통과하지 않아 실행 구성에서 제외했다.

큰 leaf 카드를 계산에 넣은 [423 후보](captain-leaf-compound-423.layout.json)를 추가했다. Leaf 258개를 49개 큰 사각형으로 배치한 뒤 원래 카드로 펼쳤으며 **번들 개요 423 / 1.148398B**, 개별 연결 **4000**이다. 같은 탐색기의 [비축약 대조 405](captain-june-405.layout.json)는 **개요 405 / 0.990362B**, 개별 연결 **1925**다. 두 결과 모두 직선·간격·관계 보존과 제품 파일 로딩을 검사했다. 개별 연결 최선 1815를 대체하지 않는다. [비교·검증·F5 실행 방법](../../../captain-layout-leaf-compounds.md).

[captain-june-473.layout.json](captain-june-473.layout.json)은 6월식 관계 묶음을 적용한 **번들 개요 후보**다. 표시 직선 1009개·visual **473**·면적 **0.990361705B**이며, 1684개 원래 관계를 전부 펼칠 수 있다. 개별 연결 화면은 1815다. [.vscode의 Run Captain (June bundles 473)](../../../.vscode/launch.json)으로 테스트할 수 있다. [검증·표시 기준·사용법](../../../captain-layout-june-overview.md), [후보 집계](captain-june-473.audit.json).

[captain-straight-1815.layout.json](captain-straight-1815.layout.json)은 2026-09-15의 연구 후보다.
실제 모델 카드 1219장, 중복 모델 쌍을 정리한 관계 1684개를 각각 직선으로 표시한다.

- 제품 렌더 모델 재측정: **visual 1815 = 선 교차 1400 + 카드 관통 415**.
- 실제 카드 bbox: **0.9903617051328B**. 카드 크기·관계·끝점을 유지한다.
- 겹침, 최소 간격 위반, 자기 카드 재진입, 인접선 교차, 비정상 접촉은 0이다.
- 기존 1818과 카드 위치는 같고, 네 관계의 연결점만 다르다. 개선은 3건이다.
- 목표 visual ≤500은 미달이다. 제품 기본 배치에는 적용하지 않았고 앱 화면도 검증하지 않았다.

[출처·검증 정보](captain-straight-1815.provenance.json), [실험 보고서](../../../captain-layout-boundary-sweep.md).
과거 번들 집계의 586 자료는 `../recovered`에 따로 보존돼 있다.
# Current installed review: 0.0.1069

`captain-ml-latest-checkpoints.layout.json` now contains the actual final radial
NN outputs: overview update 18 and individual source-conflict update 21, yielding
**285 / 1,963**. Model inputs, checkpoints, inference code and validation evidence
are retained in `../checkpoints/radial-preview-20261004/`. The normal installed
extension carries the same audited geometry in `media/ml-preview/` and applies
it only to a matching graph and original card dimensions. The original best
`captain-ml-independent-views.layout.json` is unchanged. The previous 287 / 1,963
preview is retained in `../checkpoints/source-port-preview-20261004/previous-preview/`.
Targets **150 / 750 remain unmet**; browser visual verification remains pending.
