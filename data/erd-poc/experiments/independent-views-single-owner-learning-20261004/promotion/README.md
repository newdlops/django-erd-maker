# Captain 보존 후보

최신 [보기별 ML 배치](captain-ml-independent-views.layout.json)는 **개요 288 / 개별 보기 1,963**이다. 이전 300 이하 / 2,000 미만 목표는 달성했으며, 현재 목표 **150 / 750에는 미달**이다. 각 보기의 카드 위치와 직선 경로를 함께 저장하며 카드 크기, 1,244개 모델, 1,727개 관계, 개요의 49개 Leaf 카드와 간격을 보존한다. F5에서 **Run Captain (ML independent views)**를 선택한다. 개별 보기는 **June bundles와 Leaf cards를 모두 끄면** 표시된다. 보기별 수동 이동과 확대 상태는 따로 유지하며 Reset View와 Refresh는 개요로 돌아온다.

실제 제품 파일 로딩과 개요→개별→개요 전환 함수의 전체 경로·충돌 수를 검증했다. 이번 통합 검증의 최대 측정 RSS는 **116.1MiB**이며, 수치 작업은 직렬·단일 스레드·128MiB 감시로 실행했다. 단일 카드 이동 결과로 학습한 모델과 이전 모델 모두 개요 289→288을 얻었고 개별보기는 1,963을 유지했다. 학습 때문에 이번 개선이 발생했다고 단정하지 않는다. 합성 이동 4,096개를 전수 재측정하고 학습 업데이트 20,512회와 저장된 가중치를 정확히 재현했다. 이번 작업은 데이터와 연구 도구 갱신이며, 제품 코드의 이전 빌드·검사는 반복하지 않았다. 현재 실제 브라우저 도구가 없어 화면 검사는 수행하지 못했다. [제품 검증](captain-ml-independent-views.audit.json), [모델·단계 출처](captain-ml-independent-views.provenance.json), [최신 학습·정책 제안 8,192개·실패 및 자원 기록](../experiments/independent-views-single-owner-learning-20261004/manifest.json). 압축 기록 2,612개 매핑의 복원 해시를 독립 검증했고 이전 후보도 보존한다.

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
