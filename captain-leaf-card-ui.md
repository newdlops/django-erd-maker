# Captain 큰 leaf 카드 표시 — 2026-09-15

> 아래는 외곽 표시를 처음 추가한 단계의 기록이다. 현재 F5 구성과 큰 카드에 한 직선만 연결하는 동작은 [최신 468 결과](captain-leaf-card-connections.md)를 따른다. 현재 Captain 데이터는 이 문서의 1,219개와 다른 1,244개 모델이다.

큰 카드는 연구 배치의 계산 단위로만 존재했고, 제품 화면에는 펼친 모델 카드만 표시했다. 또한 사용 중이던 F5 구성은 `Run Django ERD Extension`이었다. 이 구성은 저장한 423 후보를 읽지 않는다. 계산 결과가 준비됐다는 설명과 실제 확장 화면 사이에 누락이 있었다.

## 실행 방법

1. django-erd-maker의 Run and Debug에서 **Run Captain (Leaf compounds 423)**을 선택하고 F5로 실행한다. 이전 개발 호스트가 열려 있으면 해당 세션을 종료한 뒤 실행한다.
2. 새 개발 호스트에서 **Django ERD: Open Diagram**을 실행한다.
3. 도구 막대의 **Leaf cards (49)**와 **June bundles**를 확인한다.
4. **Find leaf card → MeetingDocument · 52 leaves**를 선택하면 가장 큰 묶음으로 확대한다.

F5는 [전용 워크스페이스](.vscode/captain-leaf-cards.code-workspace)와 [표시용 후보](data/erd-poc/candidates/captain-leaf-cards-423.layout.json)를 읽는다. 일반 Captain 폴더 창과 별도의 workspace 식별자를 사용한다. VS Code는 개발 호스트 시작 시 이미 다른 창에 열린 폴더 인자를 제외할 수 있다. [관련 VS Code 소스](https://github.com/microsoft/vscode/blob/main/src/vs/platform/windows/electron-main/windowsMainService.ts#L1323).

## 표시와 조작

- 258개 leaf를 담은 큰 카드 49개의 외곽과 부모 이름, leaf 개수를 표시한다. 실제 모델 카드 1219개와 기존 연결점은 유지한다.
- 내부 모델은 기존처럼 선택한다. 큰 카드의 제목이나 빈 여백을 드래그하면 구성원을 함께 이동한다. 찾기 목록에서 선택한 묶음은 Shift + 방향키로도 이동한다.
- Leaf cards 버튼으로 외곽 표시를 끄고 켠다. 찾기 목록은 해당 묶음으로 확대하며, 클러스터가 접혀 있으면 펼친다.
- 숨긴 모델을 제외한 현재 좌표로 외곽을 다시 계산한다. 레이아웃 갱신에서 묶음이 없는 입력을 받으면 찾기 목록을 숨기고 버튼을 비활성화한다.
- 일반 FMMM 실행에 연구 축약 계산기를 자동 적용한 것은 아니다. 이 구성은 검증한 저장 배치로 표시와 조작을 비교하는 용도다.

## 데이터와 검증

[attach_leaf_cards.cjs](scripts/erd-poc/attach_leaf_cards.cjs)가 기존 423 배치와 계산 사각형의 상대 좌표를 검사하고 기존 `leafBundles` 프로토콜에 구성원을 기록한다. 원래 후보 파일을 덮어쓰지 않았다.

제품 `runOgdfLayout` 파일 로딩 → 디코딩 → 렌더 모델 경로에서 확인한 값은 [검증 기록](data/erd-poc/candidates/captain-leaf-cards-423.audit.json)에 보존했다.

| 항목 | 결과 |
|---|---:|
| 큰 카드 / 구성 leaf | 49 / 258 |
| 실제 모델 카드 / 표시 직선 | 1219 / 1009 |
| 번들 개요 Visual Crossing | **423 = 선 교차 279 + 카드 관통 144** |
| 실제 모델 카드 영역 | **1.1483979936B** |
| 전체 개별 관계 Visual Crossing | **4000** |
| 카드 겹침 / 최소 간격 위반 / 끝점 오류 | 0 / 0 / 0 |

큰 카드 외곽은 모델을 대체하는 노드가 아닌 그룹 배경이다. 423은 실제 모델 카드와 번들 개요 직선을 기준으로 측정한 값이다. 개별 관계 1684개는 정확히 한 번씩 보존되며, June bundles를 끄면 모든 관계가 펼쳐진다. 이번 표시 변경으로 교차 수가 새로 감소한 것은 아니다.

확장 TypeScript 빌드와 관련 통합 테스트 **41개**가 통과했다. 구성원 보존, 잘못된 묶음 거부, 현재 좌표 반영, 공간 인덱스, 그룹 이동, 접힘 해제·확대, 키보드 이동 및 기존 June 토글을 확인했다. 빌드·테스트·제품 측정은 256MiB 메모리 감시 아래 직렬로 실행했다.

실제 VS Code에서 기존 일반 F5 선택과 큰 카드 구성으로의 변경, 빌드 완료까지 확인했다. 그러나 개발 호스트 창과 입력 대상이 검증 중 계속 바뀌어 **새 ERD의 GPU 화면·포인터 조작은 확인하지 못했다**. Browser 런타임도 사용 가능한 브라우저가 없었다. VM 테스트를 실제 앱 조작 검증으로 표시하지 않는다.

표시용 후보 SHA-256: `25bdd95b0074470f6cb90eb9422f95a535799cb3fa727b1caa36272b09a74921`.
