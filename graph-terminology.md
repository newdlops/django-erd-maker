# Django ERD Graph — 용어 및 구조 정의

이 문서는 Django ERD를 layout 처리할 때 사용하는 그래프 용어를 정의합니다. 이후 모든 알고리즘과 코드는 이 정의를 따릅니다.

---

## 1. Edge (간선)

### 1.1 Edge Dedup (중복 간선 처리)
서로 다른 방향이거나 다른 FK 이름이라도, **같은 두 노드 쌍을 잇는 모든 간선은 하나의 간선으로 취급**합니다.

- `a → b` 와 `b → a` 가 동시 존재 → **1개 간선**
- `Order.user → User` 와 `Order.last_modifier → User` 도 동시 존재 → **1개 간선** (소스/타겟이 같은 쌍이므로)

이 규칙은 모든 계산 (degree, modularity, planarity, crossing count 등) 에 일관 적용됩니다. 이를 **Edge Dedup**이라 부릅니다.

### 1.1.1 Cluster Super-graph Edge Dedup (root-to-root chain dedup)
Cluster 수준의 super-graph에서는, 두 cluster root 사이를 잇는 임의 길이의 chain을 모두 **하나의 root-to-root edge**로 취급합니다.

- `root_A - root_B` (직접) → **1개 super-edge**
- `root_A - connector - root_B` → **1개 super-edge** (위와 동일)
- `root_A - connector - connector - root_B` → **1개 super-edge** (위와 동일)
- `root_A - member - member - root_B` (cluster member 포함 임의 chain) → **1개 super-edge**

**규칙**: 두 cluster root rA, rB 사이에 (rA, rB가 아닌 non-router 중간 노드들로만 이루어진) **path가 하나라도 존재**하면, super-graph에 rA↔rB super-edge를 1개 추가한다. Router는 자기 super-node를 갖는 별도의 hub이므로 BFS expansion에서 제외.

**같은 root pair에 대한 dedup**: 여러 chain path가 존재해도 (parallel chains) super-edge는 1개. (set-based dedup으로 자동 처리)

**시각적 효과**: 서로 chain으로만 연결되어 있던 cluster들 (직접 root↔root edge가 없어서 이전엔 super-graph에서 멀리 떨어졌음) 이 super-graph cross-min에서 가까이 배치되어, chain edge가 짧고 자연스럽게 그려진다.

### 1.1.1.1 Small-bus 직선화 (broken-wing 패턴)

특수한 패턴 — **wing이 깨져서 connector로 변한 경우**:
- 본래 root A의 wing 후보: `A → B → C → A` (deg-2 ring)
- 그런데 wing 노드 C가 다른 root D와도 연결됨 → C의 reach = {A, D} = 2 → C는 **connector**로 분류
- 결과: C는 cluster A에서 분리되어 connector untangling으로 A-D 중점에 배치, B는 cluster A의 leaf로 배치 → B-C edge가 cluster A 내부에서 A-D 중점까지 길게 휘어짐

**해결: chain 직선화**
- 검출: connector C (reach=2)의 추가 deg-2 이웃 B가 cluster A 또는 D의 멤버이고, B가 A/D root까지 deg-2 chain으로 연결됨 → A-B-C-D 형태의 small bus
- 배치: 전체 chain (`[B_A_chain reversed, C, B_D_chain]`)을 root A와 root D를 잇는 **직선 위에 균등 분포**
- 결과: edge가 휘어짐 없이 일직선으로 그려져 visual 노이즈 제거

### 1.1.2 Wing — 자기 root로 회귀하는 ring (가지치기 대상)
`A → B → C → A` 처럼, root A에서 시작해 non-root non-router 중간 노드를 거쳐 **다시 자기 자신 A로 돌아오는 ring**을 root A의 **wing**이라고 부른다.

- Wing은 위상적으로 ring이지만, 의미상으로는 **root A에 매달린 leaf 무리**와 동급이다 (cluster A에 소속, **leaf edge와 같은 등급**).
- 일반 deg-1 가지치기는 wing의 중간 노드를 제거하지 못한다 (deg=2이므로). **Wing은 가지치기 단계에 명시적으로 포함**되어야 한다 — 그래야 main 구조 (2-core, backbone) 검출 시 wing이 노이즈로 작용하지 않는다.

**가지치기 알고리즘 확장 (§2.5.2 Step B)**:
1. Step A: deg ≤ 1 노드 모두 제거 (기존 leaf + alone-root)
2. **Step B (NEW): Wing 가지치기** — 현재 2-core에서, deg ≥ 3인 anchor 노드에 대해 deg-2 chain을 따라가다가 **같은 anchor로 회귀**하면 그 chain은 wing. 중간 노드들을 anchor의 leaf로 (parentIdx = anchor) 가지치기.
3. Wing 가지치기 후 anchor의 deg가 줄어들 수 있음 (wing 양 끝점 2개 edge 사라짐) → Step A로 다시 돌아감
4. 더 이상 Step A/B에 해당 없으면 종료 → 깨끗한 main 2-core 추출

**chain BFS에서 자기 자신 root에 회귀**: super-edge 추가하지 않음 (self-edge 무의미). BFS visited 체크로 처리됨.

**일반화**: 임의 길이의 wing (A-B-C-D-A, A-B-C-D-E-A 등) 도 같은 규칙. 가지치기 후 중간 노드들은 face-aware 재부착 (§2.5.5) 으로 anchor 주변에 leaf와 함께 배치됨.

**Multi-root ring과 구분**: 2개 이상의 cluster root를 거쳐가는 ring (A-x-B-y-A 등) 은 wing이 아니라 §3.10의 **main backbone 후보**. 이런 ring은 wing 가지치기 대상이 아니다 (anchor가 둘 이상이므로).

### 1.2 Edge weight
모든 dedup된 간선의 weight = 1. 가중 그래프가 필요하면 별도 명시합니다.

---

## 2. Node degree

`degree(v)` = dedup된 간선 기준으로 v에 인접한 distinct 노드 수.

예: `a-b, b-a` 두 EdgeRecord가 있으면 `degree(a) = 1, degree(b) = 1` (간선 2개가 아닌 1개로 카운트).

---

## 2.5 가지치기 (Graph Pruning) — 2-core 추출

그래프에서 **leaf**(deg=1)와 **alone-root**(deg=0)를 반복적으로 제거하여 그래프의 골격(=2-core)을 뽑아낸다. 이 골격이 그래프의 본질적인 구조 (ring / 다면체 등) 을 담고, 외부의 모든 노드는 골격에 트리 형태로 매달려 있다.

### 2.5.1 Alone Root
deg = 0 인 노드. 다른 어떤 노드와도 연결되지 않은 isolated node. cluster root라 하더라도 deg가 0이면 alone root이며, 가지치기 1차에 함께 제거된다.

### 2.5.2 가지치기 단계 (Pruning Levels)
- **Level 0**: 원본 dedup 그래프
- **Level 1**: Level 0 그래프에서 **deg ≤ 1**인 모든 노드를 동시 제거 (= leaf + alone root)
- **Level 2**: Level 1 결과 그래프에서 deg ≤ 1이 된 노드를 동시 제거. 이건 **원래 connector였지만 leaf의 다른쪽 이웃이 사라져서 leaf가 된 노드들**을 포함한다
- **Level k (k ≥ 2)**: Level k-1 결과에서 deg ≤ 1이 된 노드를 동시 제거
- **종료 조건**: 더 이상 deg ≤ 1인 노드가 없음 → 남은 그래프 = **2-core**

각 pruned 노드는 두 가지 정보를 기록한다:
- `pruningLevel`: 몇 차에 제거됐는지 (1, 2, 3, ...)
- `attachmentParent`: 제거 직전 자기와 연결된 (단 하나의) 이웃의 nodeIdx. alone-root이면 자기 자신.

### 2.5.3 2-core
가지치기를 끝까지 반복한 후 남는 그래프. 모든 노드가 deg ≥ 2이므로 **반드시 cycle을 포함**한다. 가능한 형태:
- 하나의 ring (단순 cycle)
- 여러 ring이 노드/edge를 공유한 구조
- tetrahedral (K4), octahedral 등의 완전 다면체
- 더 복잡한 dense subgraph

**핵심 성질**: 2-core는 가지치기로 절대 더 이상 줄어들지 않는다. 모든 트리 형태의 가지는 이미 잘려나간 상태.

### 2.5.4 Pruning-First Layout 방법론
1. **Edge dedup** 적용 (§1.1)
2. **가지치기**로 2-core 추출, 각 pruned 노드의 `(level, parent)` 기록
3. **2-core layout**: ring/다면체(mesh) 구조를 cross-min(꼬임 최소)으로 배치 (Sugiyama / PlanarizationLayout / 폴라 anchor 기반 등)
4. **역순 재부착 = 트리를 mesh의 face로 자라게 하기** (§2.5.5)
5. 결과: 2-core mesh가 그래프의 골격, 트리들이 mesh의 face(빈 영역)를 채우면서 매달린 모양.

**이 방법론의 장점**:
- 2-core는 작은 그래프이므로 cross-min을 강력하게 풀 수 있음
- 트리 부분은 재부착 시 cross가 거의 발생하지 않음 (tree-like 추가는 평면적)
- 트리는 트리 구조 자체가 인식 가능하므로 **간격을 좁게 가져가도 알아볼 수 있음** → 면적 최소
- Cluster/polar/connector 분류와 양립 가능 (분류는 metadata, 배치는 pruning이 주도하거나 보완)

### 2.5.5 Face-aware 재부착 (트리를 mesh face에 배치)

핵심 원리: **2-core의 face(빈 영역)에 트리들을 배치**한다. mesh의 가장자리(노드)에서 face 안쪽으로 트리가 자라게 함.

알고리즘 (각 pruned 노드 N, parent P 기준):
1. P의 **현재 이웃들**(이미 배치된)의 각도를 모두 수집 — 이 중 자기가 배치할 자식들은 제외
2. **가장 큰 빈 arc**(largest empty angular gap)를 찾음 — 이 방향이 face(빈 영역)
3. 자식들을 그 빈 arc의 중심 방향으로 chord-fit으로 배치
4. 자식 간 padding은 작게 (`maxChildW + 6` 정도) — 트리는 좁아도 인식 가능

이 알고리즘이 자동으로 처리하는 것들:
- **레벨 1 leaf** (parent = 2-core 노드): 2-core의 다른 이웃들 사이의 빈 arc → mesh face 방향
- **레벨 k+1 leaf** (parent = 레벨 k pruned): parent의 유일한 이웃은 grandparent → 빈 arc는 grandparent 반대 방향 → 트리가 outward로 자람
- **자기 cluster 내부 leaf**: cluster root의 inner radial로 이미 배치된 internal/bridge들이 이웃으로 작용 → leaf는 그 사이의 빈 inward arc로 자동 배치 (= 기존 outward-aware 원칙과 일치)

이렇게 하면 트리가 mesh의 face를 따라 자연스럽게 자라며, level 1, 2, 3, ... leaf들이 가까이 packed되어도 트리 형태로 인식 가능.

**arc 폭 cap**: 빈 arc가 매우 클 때(고립된 parent 등) 자식들이 너무 퍼지지 않게 max 120°로 제한 → 트리 spread를 압축.

## 2.6 Certified crossing lower bound

Crossing 하한은 §1.1의 edge dedup을 적용하고 self-loop를 제외한 **canonical simple undirected graph**에서만 계산한다.

인증기는 서로 edge-disjoint한 다음 subgraph certificate를 합산한다.

- degree-2 chain을 suppress한 kernel의 `K₃,n` subdivision:
  `⌊n/2⌋ × ⌊(n-1)/2⌋`을 기여
- 남은 graph의 `K₅` 또는 `K₃,₃` Kuratowski subdivision: 각각 1을 기여
- 한 certificate에서 사용한 원본 edge는 다른 certificate에서 재사용하지 않는다.
- certificate를 모두 제거한 잔여 graph가 planar인지 별도로 확인한다.

이 값은 classical crossing number와 pair-crossing number의 엄밀한 하한이다. 반면 leaf bundle, hub carrier, edge-node 충돌을 합성한 renderer의 `edgeCrossings`/`visualCrossings`는 topology가 다르므로 이 하한을 적용하지 않는다.

현재 route와 비교할 때도 canonical edge마다 대표 route 하나를 골라야 한다. 모든 route가 존재하고, non-incident node 관통·collinear overlap·T-junction·self-intersection이 없는 proper drawing일 때만 `canonical crossing pairs - lower bound`를 gap으로 보고한다. 조건을 만족하지 않으면 하한 자체는 유효하지만 gap은 diagnostic-only이다.

---

## 3. Cluster (Louvain Community)

Louvain modularity 알고리즘으로 묶인 노드 집합. 한 노드는 단 하나의 cluster에만 속할 수 있습니다 (배타적 소속).

### 3.1 Root
각 cluster의 중심축 노드. **cluster 내부에서 가장 degree가 높은 노드** (tie 시 modelId 사전순 등 결정적 규칙으로 선정).

- 한 노드는 **단 하나의 cluster의 root**일 수만 있음
- root는 항상 자기 cluster의 일원
- **root는 절대 다른 cluster에 속하지 않는다** — 자기 cluster의 root 자격으로만 존재. 다른 cluster의 (root 외) 멤버 자격으로 등장할 수 없으며, 다른 cluster의 connector 자격도 가질 수 없다. (Louvain의 배타적 소속과 root 1-cluster 제약으로 자동 보장되어야 하며, 위반 시 알고리즘 버그)

### 3.2 Cluster 멤버 카테고리 (root 제외)
cluster 내 root가 아닌 모든 노드는 다음 3가지 중 하나에 속합니다.

| 카테고리 | 정의 | 시각적 위치 (radial layout 기준) |
|---|---|---|
| **Leaf** | root와만 연결됨 (intra-degree = 1, neighbour = root) — cluster 외부와도 연결 없음 | root 중심 기준 **내측 hemisphere** (다른 cluster 반대 방향) |
| **Internal** | root + 같은 cluster의 다른 멤버와 연결 — 외부 cluster의 root와는 연결 없음 | 측면 (외측·내측 사이 gap arc) |
| **Bridge** | root + 정확히 하나의 다른 cluster의 root와도 연결 (= inter-cluster 연결을 가지는 노드) | root 중심 기준 **외측 hemisphere** (super-graph상 연결된 cluster들의 중심 방향) |

> **각도 분리 원칙**: Leaf와 Bridge는 root를 중심으로 **반대편 반구**에 배치한다. Connector는 cluster 외부에서 외측 방향으로 접근하므로, leaf와 connector가 시각적으로 섞이지 않음. Internal은 두 영역 사이의 gap arc에 배치 (양쪽 카테고리와 중첩 방지).

### 3.3 Connector Node
**정확히 두 개의 서로 다른 cluster root와 연결된 노드**.

- **어떤 cluster에도 소속되지 않음** (모든 cluster 멤버에서 제외)
- super-graph에서 cluster 간 edge 위의 별도 노드로 표현됨
- 시각적으로: 두 cluster 사이 공간에 자리잡고, 각 cluster의 root와 직선 edge로 연결

### 3.4 Router Node
**3개 이상의 서로 다른 cluster root와 연결되었으나 자기는 root가 아닌 노드** (Connector의 상위 변종).

- 어떤 cluster에도 소속되지 않음 (Connector와 동일)
- **상위 계층 (cluster 군집/constellation) 의 root** 가 될 수 있음 — Router를 중심으로 그 router에 연결된 cluster들이 하나의 군집(constellation)을 형성
- 시각적으로: 자기에게 연결된 cluster들의 중심 위치 (constellation의 root)

### 3.5 Polar Node (최상위 축 노드)
**자기 cluster의 root이면서 동시에 3개 이상의 다른 cluster root와 직접 연결된 노드**.

= cluster root + router의 성질을 동시에 가진 hybrid 노드.

- Polar는 그래프 전체에서 **최상위 구조적 축** (structural skeleton의 vertex)
- 최우선 layout 순위: **polar들 사이의 간선이 먼저 cross-min 처리**되어야 함 — 이게 결정되면 그 골격을 기준으로 나머지 노드들이 배치됨
- Polar들끼리 **ring이나 line의 시퀀스**를 형성할 수 있고, 이 ring/line 구조 자체가 그래프의 거대 축을 정의

> 카테고리 통합 정리:
> - **Bridge**: 자기 cluster의 멤버, root 외에 정확히 1개 외부 root와 연결
> - **Connector**: 비-root, 정확히 2개의 외부 root 연결, 어떤 cluster에도 비소속
> - **Router**: 비-root, 3+ 개의 외부 root 연결, 어떤 cluster에도 비소속
> - **Polar**: 자기 cluster의 root이면서 3+ 개의 외부 cluster root에 직접 연결, 최상위 축 노드

### 3.6 Polar 우선 layout 원칙

1. 모든 노드 카테고리 분류 후 **polar 집합부터 추출**
2. polar들로만 이루어진 sub-graph (= polar skeleton) 를 별도 그래프로 분리
3. polar skeleton의 ring / line 구조 파악 (cycle 감지 + path 분석)
4. polar skeleton에 **cross-min 최우선 layout** 적용 (작은 그래프 — Sugiyama/PlanarizationLayout 가능)
5. polar 위치를 anchor (= 변경 불가 또는 강력 고정) 로 잡고 나머지 (root, router, connector, leaf 등) 를 그 주변에 배치
6. 결과: 그래프의 큰 축은 polar skeleton이 정의하고, 나머지 모든 visualization은 그 골격 위에 attach

### 3.7 Leaf-weight-aware perimeter 배치

각 cluster root의 위치는 super-graph 토폴로지뿐 아니라 **그 root에 매달린 leaf 트리 크기 (leaf weight)** 에 따라 추가 조정한다.

**원칙**:
- **Leaf-heavy root** (많은 leaf를 가진 root): 외곽 perimeter 쪽으로 push
  - leaf bundle이 크기 때문에 한쪽 호로 모아도 큰 angular span을 차지함
  - 외곽에 있으면 leaf bundle이 outward 방향(다른 cluster 영역 밖)으로 펼쳐져 다른 cluster를 침범하지 않음
  - 큰 leaf fan이 중앙부를 거치지 않으므로 **edge cross 자체가 줄어듦**
- **Leaf-light root** (적은 leaf): 내부 mesh 면 안에 배치 (제자리 유지)
  - leaf 호의 중심각이 작으므로 mesh face 내부에 cross 없이 잘 fit
  - 빈 face 공간을 효과적으로 활용

**"외곽" 정의**: layout centroid로부터 max radial extent의 1.35배 반지름의 원. 이 원이 perimeter ring.

**구현 (target-based, NOT factor-based)**:
- weight = leafW + dedup degree로 정렬, 상위 ⌈√N⌉개 = top tier
- Top tier는 **perimeter ring 위에 좌표 강제 배치** (factor 곱셈 아님!):
  - 각 root의 centroid 기준 angle을 계산 (FMMM 결과)
  - radius=0인 root (= FMMM force-balance로 centroid에 끌려간 high-deg 노드)는 angle 미정
  - angle 미정 root들은 **other top-tier 사이의 가장 큰 빈 gap에 angle 할당**
  - 새 좌표: `(cx + perimeterR·cosθ, cy + perimeterR·sinθ)`
- 하위 tier는 rank-linear factor (1.4 → 1.0) 로 약간 outward scaling

**왜 target-based가 필요한가**: factor 곱셈은 `pos × factor`이므로 centroid에 있는 노드(거리=0)는 곱해도 0. FMMM이 high-deg root를 barycenter로 끌어다 놓으면 거리=0이 되어 perimeter shaping이 못 움직였음. target 좌표로 강제 배치하면 centroid에 있던 노드도 외곽으로 이동 가능.

이 단계는 connector untangling **이전**에 적용.

### 3.7.5 Bus (Clustered Edge / 동질 edge bundle)

**Bus**는 동질성을 가진 edge들의 집합이 하나의 큰 conceptual edge로 행동하는 패턴.

**정의**:
- Cluster A의 여러 bridge들이 모두 **같은 external cluster B로 향한다**면, 이 bridge들은 cluster B 방향의 **하나의 bus**를 이룬다.
- Bus를 구성하는 edge들은 시각적으로 한 묶음(channel)으로 진행되어야 한다.
- 일반화: 다수의 connector가 같은 cluster pair (A, B) 사이에 존재할 때도 bus를 형성한다.

**문제점 (현재)**:
- 기존 inner radial은 bridge들을 outward 평균 방향(`thetaOutward = mean(외부 cluster centroid 방향)`) 의 180° arc에 모두 배치
- 결과: A→B 방향 bridge와 A→C 방향 bridge가 섞여서 같은 arc에 위치
- A→B bridge가 B 방향이 아닌 평균 방향에 있으면 edge가 꺾임이 심해지고 → 다른 edge와 cross 유발

**해결 (bus alignment)**:
- post-compose 단계에서 각 cluster의 bridge들을 **실제 external destination 별로 grouping**
- 각 group은 그 destination 방향을 중심으로 좁은 arc (≤ 60°) 에 chord-fit 배치
- 같은 destination의 bridge들이 한 묶음으로 모임 → bus 형태 → edge bundle 시각적 인지 + 꺾임 최소화 + cross 감소

### 3.10 Main Backbone — Ring 또는 한붓그리기 path

**점수 기반 main 구조 선택**: cluster super-graph의 multi-root structure (wing 아님) 중 다음 두 종류를 모두 후보로:

1. **Multi-root ring** (closed cycle): A → B → C → ... → A, 다시 시작 root로 돌아옴
2. **한붓그리기 path** (open Eulerian-like trail): A → B → C → ... → Z, 닫히지 않은 직선/곡선 path

각 후보의 점수:
```
score = Σ (deg of each cluster root on the structure)
```

dedup degree 기반. **둘 중 점수가 가장 높은 것 = main backbone**. closed든 open이든 같은 점수 체계로 비교.

**배치 원칙**:
- **Main backbone closed (= ring)**: layout perimeter **full circle** 위에 cycle 순서대로 배치
- **Main backbone open (= path)**: layout perimeter **상단 semicircle** (180° arc, left → right) 위에 path 순서대로 배치 → 열린 오목 hull (open concave hull) 형태로 전체 윤곽
- Main backbone에 속하지 않는 non-polar root: tier-based factor scaling으로 안쪽 배치
- Polar (3+root participants, §3.9): polar skeleton anchor 위치 유지 (interior)
- Wing (§1.1.2): 각 root cluster 내부 leaf arc

**왜 open path도 main backbone이 되나**: §3.9의 "2-root structure는 cross 0이 가능"은 ring에만 적용 안 됨. linear path도 marble한 2-root 위상이며, 평면 그래프상 cross 없이 그려질 수 있다. 한붓그리기 path가 그래프의 main 척추를 이루는 경우가 흔함 (특히 hub-spoke 구조 ERD에서).

**구현**:
- 2-root cluster super-graph (직접 root-root + connector-mediated)
- High-deg start nodes로부터 greedy DFS로 longest path 탐색
- 각 path가 시작 root로 closing edge가 있으면 ring (closed), 없으면 path (open) 으로 분류
- 둘 다 score 매김 → 최고 점수 = main backbone
- closed면 full circle, open이면 upper semicircle로 target placement

### 3.9 2-root vs 3+-root 분리 (planarity-based 분해)

**핵심 관찰** — 그래프의 inter-cluster 연결은 위상적으로 두 가지로 나뉜다:

- **2-root edge** (`root - cor - ... - cor - root`): 정확히 **2개의 cluster root만 관여**하는 chain
  - 단독으로 그릴 때 cross 0이 가능 (planar)
  - linear path (오목 hull) 또는 cycle (ring) 형성
  - **그래프의 outline(윤곽선) 을 정의**
- **3+root edge** (router 관여): 3개 이상의 cluster root에 동시 연결된 router의 path
  - 다수의 router 결합 시 cross 발생이 위상적으로 불가피
  - **interior 영역에 배치, cross-min 처리**

**Layout 우선순위**:
1. **Polar / Router (= 3+root participants)** → interior 배치 (polar skeleton structural cross-min 위치 유지)
2. **Non-polar cluster root (= 2-root participants)** → outline (perimeter ring)
3. 2-root subgraph가 ring 또는 linear hull을 형성 → 그것이 layout의 윤곽
4. 3+root는 그 윤곽 안쪽에서 polar skeleton이 결정한 상대 위치에 배치

이 분리는 **face minimization (§3.8) 과 정합**: outline이 명확할수록 face가 적게 분할되며, interior의 cross-min은 polar skeleton + 9b3 swap pass에서 이미 처리.

**§3.7 perimeter rank 수정**:
- weight 계산 시 polar는 **제외** (polar는 perimeter ring에 두지 않음)
- 비-polar cluster root만 leaf weight + degree 기반 정렬
- 상위 ⌈√N⌉ tier가 외곽 ring에 explicit target 배치
- 폴라들은 이전 단계 (polar anchor + similarity-fit + hub repulsion) 의 결과 위치 유지

### 3.8 Face minimization 우선 (이론적 토대)

가지치기를 끝까지 진행하면 2-core는 결국 **ring / 다면체 (mesh) 구조** 또는 deg-0 alone-root로 회귀한다 (이건 그래프 위상의 자연스러운 종착점). 이 mesh 구조의 layout에서:

- **mesh 노드들이 둘러싸는 면 (face)** 은 2-core의 cycle structure로 정의됨
- **edge crossing point는 가상 노드로 간주**할 수 있고, 이를 포함하면 새로운 면 경계가 만들어짐
- Euler 공식: 평면 그래프 `V - E + F = 2`. crossing X개를 가상 노드로 환산하면:
  `F = 2 + E - V + X`
- 따라서 **layout의 face 수는 crossing 수에 비례**한다 (E, V는 graph topology에 의해 고정)

**원칙**: 2-core layout을 결정할 때 **face 수를 최소화** (= crossing 최소화) 하는 배치를 우선시한다. 그렇게 만들어진 (가능한 큰) 면들 안에 leaf와 트리들을 packing한다.

**Rank-based perimeter (§3.7 강화)**: deg가 큰 noun-K (보통 √N개) 만 외곽에 두는 것이 아니라, **deg 순위 상위 √N개 모두** 를 외곽 ring에 배치해야 한다. 첫 번째 노드만 외곽에 가는 게 아니고, 상위 tier 전체가 윤곽선을 이루도록.
- 상위 √N tier: factor = max (outer ring 형성)
- 나머지: rank에 따라 linear 감소
- 결과: 외곽 ring + 안쪽 mesh + 내부 face들에 leaf를 packing

**고차 노드를 외곽에 두는 것이 face 최소화에 유리한 이유**:
- deg가 큰 노드는 주변에 많은 edge가 통과
- 중앙에 두면 → 면이 잘게 쪼개짐 (face 수 ↑)
- 외곽에 두면 → edge들이 외곽으로 빠져나가 중앙에 큰 면 형성 (face 수 ↓, leaf packing space ↑)

---

## 4. Cluster Node (Super-node)

한 cluster를 그래프상 **하나의 큰 노드**로 축약한 단위.

- cluster의 모든 멤버 (root, leaf, internal, bridge)는 super-node 내부에 들어감
- connector는 super-node 외부에 별도로 위치
- super-node 사이의 super-edge는 두 가지 형태로 발생:
  1. **Root↔Root direct**: 두 cluster의 root 끼리 직접 연결된 경우 (= Bridge 노드 없이 root 간 edge가 있는 경우)
  2. **Root↔Connector↔Root**: connector를 경유한 두 cluster 간 연결

> Bridge 노드의 inter-cluster edge도 super-graph 관점에서는 root↔root 연결의 한 형태로 본다 (Bridge는 자기 root에 강하게 묶여 있어 사실상 root의 "phantom" 으로 간주).

---

## 5. Cluster Constellation (클러스터 군집)
Router를 root로 하는 상위 계층 단위.

- **Constellation의 root = Router**
- **Constellation의 멤버 = 그 Router에 root edge로 연결된 cluster들의 집합**
- 한 cluster는 여러 router에 동시에 연결될 수 있지만 **하나의 constellation에만 소속** (배타적 — 가장 강하게 연결된 router 기준 결정적 규칙으로 선택)
- Constellation이 더 큰 군집을 형성할 가능성도 있음 (router 간 router 등 multi-level) — 현 구현은 1단계까지

### 5.1 Standalone Cluster
- 어떤 router에도 속하지 않은 cluster (router로부터의 직접 연결이 없는 cluster)
- top-level layout에서 단일 cluster로 등장

### 5.2 Independent Cluster
- 다른 어떤 cluster와도 super-edge로 연결되지 않은 cluster (connector/router/root↔root 모두 없음)
- super-graph에서 disconnected component

### 5.3 Independent Node
- 어떤 cluster에도 속하지 않음 (degree 0 또는 isolated)
- connector / router 도 아님
- super-graph에서 단일 노드로 처리

## 5.5 Ring (링)
**Top-level super-graph에서 root / connector / router 노드들이 형성하는 순환 구조**.

- 예: `cluster_root_A → cluster_root_B → connector → router → cluster_root_A` 같은 닫힌 cycle
- Ring을 형성하는 super-edge들은 모두 inter-cluster 연결 (intra-cluster edge는 ring에 포함되지 않음)
- Ring의 길이 (node 수): 3 (triangle) / 4 / 5 / ...
  - 현 구현은 길이 3 (triangle) 만 감지 — 가장 많이 등장하고 시각적으로 두드러짐. 더 큰 ring은 향후 확장
- 한 super-node가 여러 ring에 동시 참여할 수 있음 (배타적 소속 아님)

### Ring 시각 원칙: 내부는 가능한 비움
Ring 노드들은 평면상 (대략) 원형으로 배치되고, **그 원의 내부에는 ring과 무관한 super-node가 들어가지 않도록** 한다.

- Ring은 cluster 그룹의 "게이트웨이" — 내부를 비우면 ring 내부 공간이 cluster 군집의 명확한 시각 그루핑이 됨
- Layout 단계: top-level layout 끝난 후, 각 ring의 convex hull 내부에 있는 non-ring node들을 outward push (= ring의 centroid 반대 방향으로 hull 밖으로)
- 단, hard constraint 아님 — 다른 layout 우선순위(cross-min, hub repulsion)와 충돌 시 ring 빈 공간이 일부 양보될 수 있음

---

## 6. 그래프 단계 (Layout 시점 기준)

### Level 0: 원본 그래프
- 모든 노드, dedup된 간선

### Level 1: 카테고리 분류
- 모든 노드를 ROOT / LEAF / INTERNAL / BRIDGE / CONNECTOR / ROUTER / INDEPENDENT 중 하나로 분류
- 각 노드의 cluster 소속 결정
- 각 cluster의 constellation 소속 결정 (router 기반)

### Level 2: Top-level super-super-graph (constellation 수준)
- 노드 = {constellation, standalone cluster, standalone connector, independent node}
- 간선 = 위 노드들 사이의 inter-edges
- 이 단계에서 **constellation들의 위치를 먼저 결정** (cross-min 최우선)

### Level 3: Constellation 내부 layout (cluster 수준)
- 각 constellation 안에서: router를 중심으로 멤버 cluster들을 배치
- standalone cluster는 top-level 좌표를 그대로 사용

### Level 4: Cluster 내부 layout (member 수준)
- 각 cluster 안에서: root를 중심으로 leaf / internal / bridge 배치
- **edge 척력 원칙** (아래 섹션 7) 적용

### Level 5: 합성 (composition)
- 각 노드의 최종 좌표 = constellation 위치 + cluster offset + member local
- connector / router / independent는 자기 super-graph 좌표 그대로

## 7. Edge 척력 원칙 (Inner cluster layout)

cluster의 root를 기점으로 외부로 나가는 edges는 카테고리에 따라 시각적으로 군집해야 한다:

- **Leaf edge** (root → leaf): 같은 cluster 내부에서 끝남. 짧은 edge.
- **Bridge edge** (root → bridge → 다른 root): 외부로 향하는 edge.
- **Connector/Router 접근 edge** (root → connector or router): 외부로 향하는 edge.

**원칙**: leaf edge들끼리는 서로 끌어당기고 (= 한쪽에 모임), bridge/connector/router edge들끼리도 서로 끌어당긴다 (= 다른 쪽에 모임). 두 그룹 사이에는 척력이 작용해서 각도상 분리된다.

구현 방법 (현재):
- 각 cluster의 super-graph 인접 노드(다른 cluster root, connector, router)들의 centroid 방향 = "outward" 방향
- Leaves는 outward의 반대편 (180° 호) 에 배치 → 모든 leaf edge가 inward 반구에 모임
- Bridges는 outward 방향 (180° 호) 에 배치 → 모든 외부 향 edge가 outward 반구에 모임
- Internals는 측면 (좁은 호) 로 분리

이렇게 leaf edge bundle 과 connector/bridge edge bundle 이 root 반대편에 형성되어 시각적으로 깔끔.

## 8. 합성 좌표 (Layout composition)

최종 좌표는 다음과 같이 합성된다:

```
node_position = constellation_offset
              + cluster_offset_within_constellation
              + member_local_position_within_cluster
```

- `constellation_offset`: top-level layout 결과 (constellation의 중심 좌표)
- `cluster_offset_within_constellation`: constellation 내부 layout 결과 (router 중심 기준 cluster offset)
- `member_local_position_within_cluster`: cluster 내부 radial layout 결과 (root 중심 기준)

connector/router/independent는 자기 위치만 사용 (cluster offset 없음).

---

## 9. Layout 알고리즘 책임 분리

| 단계 | 책임 | 알고리즘 후보 |
|---|---|---|
| **Cluster detection** | Louvain modularity (leaf-attachment 포함) | `assignLouvainClusterLabels` |
| **Root 선정** | cluster별 최대 dedup-degree 멤버 | helper |
| **Connector / Router 식별** | 외부 root 연결 수에 따라 분리 (=2 → connector, ≥3 → router) | helper |
| **Constellation 결정** | 각 router에 어떤 cluster들이 속할지 (배타적, 최강 연결 우선) | helper |
| **카테고리 분류** | leaf / internal / bridge | helper |
| **Top-level layout** | constellation/standalone/connector/independent 배치, cross-min 최우선 | Sugiyama 또는 FMMM |
| **Constellation inner** | router 중심으로 멤버 cluster 배치 | radial 또는 force-directed |
| **Cluster inner** | root 중심으로 멤버 노드 배치 (edge 척력 원칙) | outward-aware radial |
| **합성** | constellation + cluster + member 좌표 누적 | 순수 좌표 변환 |

---

## 8. 시각적 결과 모델

```
        +-----------+              +-----------+
        |           |              |           |
        | Cluster A |   connector  | Cluster B |
        |   ROOT    |------(C)----- |   ROOT    |
        |  / | \    |              |    / \    |
        |leaf int   |              | leaf  int |
        +-----|-----+              +-----------+
              |
              | (bridge → root of C)
              |
        +-----v-----+
        | Cluster C |
        |   ROOT    |
        +-----------+
```

- Cluster super-node는 사각형 (또는 큰 원)
- 내부 멤버는 root 중심 radial
- Connector는 cluster 간 직선 edge 중간에 별도 노드
- 모든 inter-cluster 연결은 super-graph cross-min을 거침

---

## 9. 메트릭 (이 모델에서 의미 있는 것)

- **Super-edge crossings**: super-graph 단계에서 cross-min 대상. 이게 0에 가까워지면 cluster 단위 시각이 깔끔
- **Connector count**: 많을수록 cluster 분리도가 약함 (다중 root에 걸친 노드가 많음)
- **Bridge count**: cluster 간 1:1 연결 강도. 적당히 있어야 cluster 관계가 보임
- **Independent count**: 너무 많으면 layout이 빈약함 — clustering 품질 신호
- **Largest cluster size**: 너무 크면 hub-dominant. cluster decomposition 문제 신호

---

## 11. Bubble Layout (대안 view)

**Bubble**은 §3 cluster-graph의 분류/구조를 그대로 이용하면서, **per-cluster inner placement만 다르게** 하는 view 모드.

### 11.1 정의
- **Bubble = 하나의 root가 만들어내는 ring**
- Root가 중심에 있고, 그 cluster의 모든 member (leaf, wing, internal, bridge) 가 root 주위에 **concentric ring (full 360°)** 으로 packing됨
- 각 cluster = 원형 bubble (= circle)
- Bubble 내부에 leaf/wing이 들어감 (cluster 분류 그대로 사용, 단지 각도 분산이 다름)
- Bubble 사이에는 빈 공간이 있을 수도 있고 (= 클러스터들이 떨어져 있음), 겹칠 수도 있음 (= 강하게 연결된 클러스터들이 가까이 배치됨)

### 11.2 Cluster-graph와의 차이
| 측면 | Cluster-graph | Bubble |
|---|---|---|
| Inner radial | bridges outward (180°) + leaves inward (180°) + internals side gap | 모든 member full 360° concentric ring |
| Outward bias | super-graph 이웃 centroid 방향 | 없음 (균등 분포) |
| Bus alignment (§3.7.5) | bridges destination별 grouping | 적용 안함 (균등 분포 우선) |
| Cluster shape | half-moon (방향성 있음) | full circle (등방성) |

### 11.3 Inner bubble fill 알고리즘
```
root at (0, 0)
collect non-root members M
maxW = max member width, maxH = max height
slotChord = maxW + 6   (each ring slot)
radialStep = max(maxH, maxW × 0.4) + 6
R = root_size/2 + maxW/2 + 12  (start radius)

while members remain:
  arcLen = 2π × R
  cap = floor(arcLen / slotChord)        # 이 ring에 들어갈 수 있는 멤버 수
  cap = min(cap, remaining)
  place 'cap' members at angles step = 2π/cap
  R += radialStep
```
바깥쪽 ring일수록 더 많은 member 수용 → cluster 사이즈에 따라 bubble 직경 결정.

### 11.4 사용 시점
- **분석 목적별 view 전환**:
  - Cluster-graph: 도메인 backbone, bus, polar 분석 (방향성 있음)
  - Bubble: 클러스터 자체를 닫힌 단위로 보고 클러스터 간 관계만 살펴보고 싶을 때 (= bubble들이 어떻게 위치하고 겹치는지)
- 둘은 같은 cluster 분류를 공유하므로 토글 시 클러스터 멤버는 그대로, 배치만 변경됨
